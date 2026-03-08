bool GetOverlayDisplayMetrics(float& outDisplayWidth,
                              float& outDisplayHeight,
                              float& outFramebufferScaleX,
                              float& outFramebufferScaleY) {
    return GetOverlayDisplayMetricsImpl(outDisplayWidth,
                                        outDisplayHeight,
                                        outFramebufferScaleX,
                                        outFramebufferScaleY);
}

bool IsImGuiRenderEnabled() {
    return true;
}

void RegisterImGuiOverlayWindow(GLFWwindow* window) {
    if (!window) { return; }
    g_registeredWindow.store(window, std::memory_order_release);
}

void UpdateImGuiOverlayPointerPosition(double x, double y) {
    std::lock_guard<std::mutex> lock(g_imguiOverlayCaptureMutex);
    g_imguiOverlayCaptureState.pointerX = x;
    g_imguiOverlayCaptureState.pointerY = y;
    g_imguiOverlayCaptureState.hasPointerPosition = true;
    RefreshPointerOverWindowLocked();
}

bool ShouldConsumeInputForOverlay(const input::InputEvent& event) {
    if (!IsImGuiRenderEnabled() || !IsGuiVisible()) { return false; }

    std::lock_guard<std::mutex> lock(g_imguiOverlayCaptureMutex);
    ImGuiOverlayCaptureState& state = g_imguiOverlayCaptureState;

    if (!state.hasFrame) { return false; }

    switch (event.type) {
    case input::InputEventType::CursorPosition:
        state.pointerX = event.x;
        state.pointerY = event.y;
        state.hasPointerPosition = true;
        RefreshPointerOverWindowLocked();
        if (state.forceConsumeMouse) {
            return true;
        }
        return state.wantCaptureMouse && (state.pointerOverWindow || state.mouseInteractionActive);

    case input::InputEventType::MouseButton: {
        if (state.forceConsumeMouse) {
            if (event.action == input::InputAction::Press || event.action == input::InputAction::Repeat) {
                state.mouseInteractionActive = true;
            } else if (event.action == input::InputAction::Release) {
                state.mouseInteractionActive = false;
            }
            return true;
        }

        bool consume = state.wantCaptureMouse && (state.pointerOverWindow || state.mouseInteractionActive);

        if (event.action == input::InputAction::Press || event.action == input::InputAction::Repeat) {
            if (state.pointerOverWindow) {
                state.mouseInteractionActive = true;
                consume = true;
            }
        }

        if (event.action == input::InputAction::Release) {
            if (state.mouseInteractionActive) { consume = true; }
            state.mouseInteractionActive = false;
        }

        return consume;
    }

    case input::InputEventType::Scroll:
        if (state.forceConsumeMouse) {
            return true;
        }
        return state.wantCaptureMouse && (state.pointerOverWindow || state.mouseInteractionActive);

    case input::InputEventType::Key:
        return state.wantCaptureKeyboard || state.wantTextInput;

    case input::InputEventType::Character:
        // Character is applied directly in HookedGlfwCharCallback; only decide forwarding here.
        return state.pointerOverWindow || state.wantTextInput;

    case input::InputEventType::Focus:
    case input::InputEventType::Unknown:
        break;
    }

    return false;
}

