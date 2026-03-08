ImGuiOverlayRenderResult RenderImGuiOverlayFrame(GLFWwindow* preferredWindow, const char* sourceLabel) {
    if (preferredWindow) { RegisterImGuiOverlayWindow(preferredWindow); }

    ImGuiOverlayRenderResult result{};
    if (!IsImGuiRenderEnabled()) {
        result.status = ImGuiOverlayRenderStatus::Disabled;
        return result;
    }

    auto& modeState = GetMirrorModeState();
    std::string activeMode = modeState.GetActiveModeName();
    bool needsEyeZoomText = (activeMode == "EyeZoom");

    bool guiVisible = IsGuiVisible();

    if (!guiVisible && !needsEyeZoomText) {
        // Keep the ImGui context alive so that tab selection and scroll positions are
        // preserved across open/close cycles within the same session.
        if (glXGetCurrentContext() == nullptr) {
            std::lock_guard<std::mutex> lock(g_imguiOverlayMutex);
            if (g_imguiOverlayState.context) { ShutdownImGuiOverlayLocked(false); }
        }
        ClearImGuiInputEvents();
        {
            std::lock_guard<std::mutex> lock(g_imguiOverlayMutex);
            if (g_imguiOverlayState.context) {
                ImGuiContext* previousContext = ImGui::GetCurrentContext();
                ImGui::SetCurrentContext(g_imguiOverlayState.context);
                ImGuiIO& io = ImGui::GetIO();
                io.ClearInputKeys();
                io.ClearInputMouse();
                ImGui::SetCurrentContext(previousContext);
            }
        }
        {
            std::lock_guard<std::mutex> captureLock(g_imguiOverlayCaptureMutex);
            ResetOverlayCaptureStateLocked();
        }
        result.status = ImGuiOverlayRenderStatus::Hidden;
        return result;
    }

    void* currentGlContext = reinterpret_cast<void*>(glXGetCurrentContext());
    if (!currentGlContext) {
        result.status = ImGuiOverlayRenderStatus::MissingGlContext;
        return result;
    }

    std::lock_guard<std::mutex> lock(g_imguiOverlayMutex);

    const std::uint64_t sharedGeneration = GetSharedGlxContextGeneration();
    bool resetRequested = g_resetOverlayRequested.exchange(false);
    if ((sharedGeneration != 0 && sharedGeneration != g_lastOverlaySharedGeneration) || resetRequested) {
        if (g_imguiOverlayState.context) {
            ShutdownImGuiOverlayLocked(false);
        }
        g_lastOverlaySharedGeneration = sharedGeneration;
    }

    if (!EnsureImGuiOverlayInitializedLocked(currentGlContext)) {
        {
            std::lock_guard<std::mutex> captureLock(g_imguiOverlayCaptureMutex);
            ResetOverlayCaptureStateLocked();
        }
        result.status = ImGuiOverlayRenderStatus::InitFailed;
        return result;
    }

    ImGuiContext* previousContext = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(g_imguiOverlayState.context);

    if (!EnsureImGuiDeviceObjectsHealthyLocked()) {
        ImGui::SetCurrentContext(previousContext);
        {
            std::lock_guard<std::mutex> captureLock(g_imguiOverlayCaptureMutex);
            ResetOverlayCaptureStateLocked();
        }
        result.status = ImGuiOverlayRenderStatus::InitFailed;
        return result;
    }

    ImGuiIO& io = ImGui::GetIO();
    float displayWidth = 1.0f;
    float displayHeight = 1.0f;
    float framebufferScaleX = 1.0f;
    float framebufferScaleY = 1.0f;
    (void)GetOverlayDisplayMetrics(displayWidth, displayHeight, framebufferScaleX, framebufferScaleY);

    io.DisplaySize = ImVec2(displayWidth, displayHeight);
    io.DisplayFramebufferScale = ImVec2(framebufferScaleX, framebufferScaleY);

    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    if (g_imguiOverlayState.hasLastFrameTime) {
        io.DeltaTime = std::chrono::duration<float>(now - g_imguiOverlayState.lastFrameTime).count();
        if (!(io.DeltaTime > 0.0f && io.DeltaTime < 1.0f)) { io.DeltaTime = 1.0f / 60.0f; }
    } else {
        io.DeltaTime = 1.0f / 60.0f;
    }
    g_imguiOverlayState.lastFrameTime = now;
    g_imguiOverlayState.hasLastFrameTime = true;

    const ImGuiInputDrainResult drain = DrainInputQueueToCurrentContext();
    result.drained = drain.drained;
    result.applied = drain.applied;

    // Check for completed input capture
    platform::config::CaptureResult captureResult;
    if (platform::config::IsCaptureDone(captureResult)) {
        std::vector<uint32_t> capturedKeys = platform::config::GetCapturedKeys();
        if (captureResult.completion != platform::config::CaptureCompletion::Canceled) {
            const bool confirmed = captureResult.completion == platform::config::CaptureCompletion::Confirmed;
            const bool cleared = captureResult.completion == platform::config::CaptureCompletion::Cleared;

            if (captureResult.target == platform::config::CaptureTarget::RebindDraftInput) {
                if (confirmed && !capturedKeys.empty()) {
                    g_rebindLayoutState.customDraftInputVk = capturedKeys.back();
                } else if (cleared) {
                    g_rebindLayoutState.customDraftInputVk = 0;
                }
            } else {
                auto config = platform::config::GetConfigSnapshot();
                if (config) {
                    auto mutableConfig = *config;
                    bool changed = false;

                    if (captureResult.target == platform::config::CaptureTarget::GuiHotkey) {
                        if (confirmed) {
                            mutableConfig.guiHotkey = capturedKeys;
                            changed = true;
                        } else if (cleared) {
                            mutableConfig.guiHotkey.clear();
                            changed = true;
                        }
                    } else if (captureResult.target == platform::config::CaptureTarget::RebindToggleHotkey) {
                        if (confirmed) {
                            mutableConfig.rebindToggleHotkey = capturedKeys;
                            changed = true;
                        } else if (cleared) {
                            mutableConfig.rebindToggleHotkey.clear();
                            changed = true;
                        }
                    } else if (captureResult.target == platform::config::CaptureTarget::Hotkey) {
                        if (captureResult.targetIndex >= 0 && captureResult.targetIndex < static_cast<int>(mutableConfig.hotkeys.size())) {
                            if (confirmed) {
                                mutableConfig.hotkeys[captureResult.targetIndex].keys = capturedKeys;
                                changed = true;
                            } else if (cleared) {
                                mutableConfig.hotkeys[captureResult.targetIndex].keys.clear();
                                changed = true;
                            }
                        }
                    } else if (captureResult.target == platform::config::CaptureTarget::SensitivityHotkey) {
                        if (captureResult.targetIndex >= 0 &&
                            captureResult.targetIndex < static_cast<int>(mutableConfig.sensitivityHotkeys.size())) {
                            if (confirmed) {
                                mutableConfig.sensitivityHotkeys[captureResult.targetIndex].keys = capturedKeys;
                                changed = true;
                            } else if (cleared) {
                                mutableConfig.sensitivityHotkeys[captureResult.targetIndex].keys.clear();
                                changed = true;
                            }
                        }
                    } else if (captureResult.target == platform::config::CaptureTarget::AltSecondary) {
                        if (captureResult.targetIndex >= 0 && captureResult.targetIndex < static_cast<int>(mutableConfig.hotkeys.size())) {
                            auto& alts = mutableConfig.hotkeys[captureResult.targetIndex].altSecondaryModes;
                            if (captureResult.targetSubIndex >= 0 &&
                                captureResult.targetSubIndex < static_cast<int>(alts.size())) {
                                if (confirmed) {
                                    alts[captureResult.targetSubIndex].keys = capturedKeys;
                                    changed = true;
                                } else if (cleared) {
                                    alts[captureResult.targetSubIndex].keys.clear();
                                    changed = true;
                                }
                            }
                        }
                    } else if (captureResult.target == platform::config::CaptureTarget::Exclusion) {
                        if (confirmed && !capturedKeys.empty() &&
                            captureResult.targetIndex >= 0 &&
                            captureResult.targetIndex < static_cast<int>(mutableConfig.hotkeys.size())) {
                            auto& exclusions = mutableConfig.hotkeys[captureResult.targetIndex].conditions.exclusions;
                            const uint32_t capturedVk = capturedKeys.back();
                            if (captureResult.targetSubIndex >= 0 &&
                                captureResult.targetSubIndex < static_cast<int>(exclusions.size())) {
                                exclusions[captureResult.targetSubIndex] = capturedVk;
                                changed = true;
                            } else if (captureResult.targetSubIndex == static_cast<int>(exclusions.size())) {
                                exclusions.push_back(capturedVk);
                                changed = true;
                            }
                        }
                    } else if (captureResult.target == platform::config::CaptureTarget::SensitivityExclusion) {
                        if (confirmed && !capturedKeys.empty() &&
                            captureResult.targetIndex >= 0 &&
                            captureResult.targetIndex < static_cast<int>(mutableConfig.sensitivityHotkeys.size())) {
                            auto& exclusions = mutableConfig.sensitivityHotkeys[captureResult.targetIndex].conditions.exclusions;
                            const uint32_t capturedVk = capturedKeys.back();
                            if (captureResult.targetSubIndex >= 0 &&
                                captureResult.targetSubIndex < static_cast<int>(exclusions.size())) {
                                exclusions[captureResult.targetSubIndex] = capturedVk;
                                changed = true;
                            } else if (captureResult.targetSubIndex == static_cast<int>(exclusions.size())) {
                                exclusions.push_back(capturedVk);
                                changed = true;
                            }
                        }
                    } else if (captureResult.target == platform::config::CaptureTarget::RebindFrom ||
                               captureResult.target == platform::config::CaptureTarget::RebindTo ||
                               captureResult.target == platform::config::CaptureTarget::RebindTypes) {
                        if (captureResult.targetIndex >= 0 &&
                            captureResult.targetIndex < static_cast<int>(mutableConfig.keyRebinds.rebinds.size())) {
                            auto& rebind = mutableConfig.keyRebinds.rebinds[captureResult.targetIndex];
                            if (confirmed && !capturedKeys.empty()) {
                                const uint32_t capturedVk = capturedKeys.back();
                                if (captureResult.target == platform::config::CaptureTarget::RebindFrom) {
                                    rebind.fromKey = capturedVk;
                                } else if (captureResult.target == platform::config::CaptureTarget::RebindTo) {
                                    const bool hadExplicitTextOverride = HasExplicitTextOverride(rebind);
                                    const uint32_t previousTriggerVk = rebind.toKey;
                                    rebind.toKey = capturedVk;
                                    if (!hadExplicitTextOverride &&
                                        previousTriggerVk != 0 &&
                                        previousTriggerVk != capturedVk) {
                                        rebind.useCustomOutput = true;
                                        rebind.customOutputVK = previousTriggerVk;
                                        rebind.customOutputUnicode = 0;
                                        rebind.customOutputShiftUnicode = 0;
                                    }
                                } else {
                                    rebind.useCustomOutput = true;
                                    rebind.customOutputVK = capturedVk;
                                    rebind.customOutputUnicode = 0;
                                    rebind.customOutputShiftUnicode = 0;
                                    if (rebind.customOutputVK == rebind.toKey) {
                                        rebind.customOutputVK = 0;
                                        if (rebind.customOutputScanCode == 0) {
                                            rebind.useCustomOutput = false;
                                        }
                                    }
                                }
                                changed = true;
                            } else if (cleared) {
                                if (captureResult.target == platform::config::CaptureTarget::RebindFrom) {
                                    rebind.fromKey = 0;
                                } else if (captureResult.target == platform::config::CaptureTarget::RebindTo) {
                                    rebind.toKey = 0;
                                } else {
                                    rebind.customOutputVK = 0;
                                    rebind.customOutputUnicode = 0;
                                    rebind.customOutputShiftUnicode = 0;
                                    if (rebind.customOutputScanCode == 0) {
                                        rebind.useCustomOutput = false;
                                    }
                                }
                                changed = true;
                            }

                            if (changed &&
                                captureResult.targetIndex >= 0 &&
                                captureResult.targetIndex < static_cast<int>(mutableConfig.keyRebinds.rebinds.size())) {
                                const auto& updatedRebind = mutableConfig.keyRebinds.rebinds[captureResult.targetIndex];
                                if (updatedRebind.fromKey == 0 ||
                                    IsNoOpRebindForKey(updatedRebind, updatedRebind.fromKey)) {
                                    EraseRebindAdjustingLayoutState(mutableConfig, captureResult.targetIndex);
                                }
                            }
                        }
                    }

                    if (changed) {
                        AutoSaveConfig(mutableConfig);
                    }
                }
            }
        }
        platform::config::ResetHotkeyCapture();
    }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();
    iam_update_begin_frame();
    iam_clip_update(io.DeltaTime);
    GarbageCollectButtonRippleStates();
    UpdateThemeTransitionForFrame();

    auto frameConfig = platform::config::GetConfigSnapshot();
    ImGui::GetIO().FontGlobalScale = frameConfig ? frameConfig->guiScale : 1.0f;
    if (frameConfig) {
        ImGui::GetStyle().Alpha = frameConfig->guiOpacity;
    }

    bool windowVisible = false;
    bool windowBegun = false;
    if (guiVisible) {
        if (frameConfig) {
            ImGui::SetNextWindowSize(ImVec2((float)frameConfig->guiWidth, (float)frameConfig->guiHeight), ImGuiCond_FirstUseEver);
        } else {
            ImGui::SetNextWindowSize(ImVec2(600.0f, 500.0f), ImGuiCond_FirstUseEver);
        }
        
        const ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoSavedSettings;
        windowVisible = ImGui::Begin("Linuxscreen Settings", &guiVisible, windowFlags);
        windowBegun = true;

        // If ImGui cleared guiVisible via the X button, propagate to global state.
        if (!guiVisible) {
            platform::x11::SetGuiVisible(false);
        }
        
        // Persistent window size handling
        ImVec2 currentSize = ImGui::GetWindowSize();
        if (frameConfig && (std::abs(currentSize.x - frameConfig->guiWidth) > 1.0f || std::abs(currentSize.y - frameConfig->guiHeight) > 1.0f)) {
            static auto lastResizeTime = std::chrono::steady_clock::now();
            auto now = std::chrono::steady_clock::now();
            
            if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastResizeTime).count() > 500) {
                auto mutableConfig = *frameConfig;
                mutableConfig.guiWidth = (int)currentSize.x;
                mutableConfig.guiHeight = (int)currentSize.y;
                AutoSaveConfig(mutableConfig);
                lastResizeTime = now;
            }
        }
    }

    const ImVec2 windowPos = windowBegun ? ImGui::GetWindowPos() : ImVec2(0.0f, 0.0f);
    const ImVec2 windowSize = windowBegun ? ImGui::GetWindowSize() : ImVec2(0.0f, 0.0f);
    const bool windowHovered = windowBegun && ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

    {
        std::lock_guard<std::mutex> captureLock(g_imguiOverlayCaptureMutex);
        g_imguiOverlayCaptureState.hasFrame = true;
        g_imguiOverlayCaptureState.wantCaptureMouse = guiVisible ? io.WantCaptureMouse : false;
        g_imguiOverlayCaptureState.forceConsumeMouse = guiVisible && g_rebindLayoutState.keyboardLayoutOpen;
        g_imguiOverlayCaptureState.wantCaptureKeyboard = guiVisible ? io.WantCaptureKeyboard : false;
        g_imguiOverlayCaptureState.wantTextInput = guiVisible ? io.WantTextInput : false;
        g_imguiOverlayCaptureState.hasWindowRect = windowSize.x > 0.0f && windowSize.y > 0.0f;
        g_imguiOverlayCaptureState.windowX = windowPos.x;
        g_imguiOverlayCaptureState.windowY = windowPos.y;
        g_imguiOverlayCaptureState.windowWidth = windowSize.x;
        g_imguiOverlayCaptureState.windowHeight = windowSize.y;

        if (!g_imguiOverlayCaptureState.hasPointerPosition) {
            g_imguiOverlayCaptureState.pointerX = io.MousePos.x;
            g_imguiOverlayCaptureState.pointerY = io.MousePos.y;
            g_imguiOverlayCaptureState.hasPointerPosition = io.MousePos.x >= 0.0f && io.MousePos.y >= 0.0f;
        }

        RefreshPointerOverWindowLocked();
        if (windowHovered) { g_imguiOverlayCaptureState.pointerOverWindow = true; }
    }

    if (windowBegun && windowVisible) {
        auto config = platform::config::GetConfigSnapshot();
        bool isCapturing = platform::config::IsHotkeyCapturing();
        const platform::config::CaptureTarget captureTarget = platform::config::GetCaptureTarget();
        const bool deferCaptureModalToKeyboardLayout = g_rebindLayoutState.keyboardLayoutOpen && IsRebindCaptureTarget(captureTarget);
        if (!deferCaptureModalToKeyboardLayout) {
            RenderHotkeyCaptureModal();
        }

        if (ImGui::IsKeyPressed(ImGuiKey_Escape) && !isCapturing && !g_rebindLayoutState.keyboardLayoutOpen) {
            guiVisible = false;
            platform::x11::SetGuiVisible(false);
        }

        if (ImGui::BeginTabBar("LinuxscreenTabs")) {
            if (ImGui::BeginTabItem("Modes")) {
                NotifyMainTabSelected(MainSettingsTab::Modes);
                PushMainTabContentAnimationStyle();
                if (config) {
                    auto mutableConfig = *config;
                    RenderModesTab(mutableConfig, activeMode, modeState);
                }
                ImGui::PopStyleVar();
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Mirrors")) {
                NotifyMainTabSelected(MainSettingsTab::Mirrors);
                PushMainTabContentAnimationStyle();
                if (config) {
                    auto mutableConfig = *config;
                    RenderMirrorsTab(mutableConfig);
                }
                ImGui::PopStyleVar();
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("EyeZoom")) {
                NotifyMainTabSelected(MainSettingsTab::EyeZoom);
                PushMainTabContentAnimationStyle();
                if (config) {
                    auto mutableConfig = *config;
                    RenderEyeZoomTab(mutableConfig, activeMode, modeState);
                }
                ImGui::PopStyleVar();
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Inputs")) {
                NotifyMainTabSelected(MainSettingsTab::Inputs);
                PushMainTabContentAnimationStyle();
                if (config) {
                    auto mutableConfig = *config;
                    RenderInputsTab(mutableConfig, isCapturing);
                }
                ImGui::PopStyleVar();
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Misc")) {
                NotifyMainTabSelected(MainSettingsTab::Misc);
                PushMainTabContentAnimationStyle();
                if (config) {
                    auto mutableConfig = *config;
                    RenderMiscTab(mutableConfig);
                }
                ImGui::PopStyleVar();
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }
    }
    
    if (windowBegun) {
        ImGui::End();
    }

    FlushPendingConfigSave();

    auto config = platform::config::GetConfigSnapshot();
    if (config) {
        RenderEyeZoomTextDrawList(*config, displayWidth, displayHeight);
    }

    ImGui::Render();
    FinalizeButtonRippleStatesForFrame();

    GLint lastUnpackAlignment = 4;
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &lastUnpackAlignment);
    GLint lastUnpackRowLength = 0;
    GLint lastUnpackSkipPixels = 0;
    GLint lastUnpackSkipRows = 0;
#ifdef GL_UNPACK_ROW_LENGTH
    glGetIntegerv(GL_UNPACK_ROW_LENGTH, &lastUnpackRowLength);
    glGetIntegerv(GL_UNPACK_SKIP_PIXELS, &lastUnpackSkipPixels);
    glGetIntegerv(GL_UNPACK_SKIP_ROWS, &lastUnpackSkipRows);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
#endif
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    static auto pfnBindBuffer = reinterpret_cast<PFNGLBINDBUFFERPROC>(
        glXGetProcAddressARB(reinterpret_cast<const GLubyte*>("glBindBuffer")));
    GLint lastPixelUnpackBuffer = 0;
    glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &lastPixelUnpackBuffer);
    if (lastPixelUnpackBuffer != 0 && pfnBindBuffer) {
        pfnBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    }

    GLboolean lastColorMask[4] = { GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE };
    glGetBooleanv(GL_COLOR_WRITEMASK, lastColorMask);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

    GLboolean lastFramebufferSrgb = glIsEnabled(GL_FRAMEBUFFER_SRGB);
    if (lastFramebufferSrgb) {
        glDisable(GL_FRAMEBUFFER_SRGB);
    }

    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    // --- Restore all state (reverse order) ---
    if (lastFramebufferSrgb) {
        glEnable(GL_FRAMEBUFFER_SRGB);
    }
    glColorMask(lastColorMask[0], lastColorMask[1], lastColorMask[2], lastColorMask[3]);
    if (lastPixelUnpackBuffer != 0 && pfnBindBuffer) {
        pfnBindBuffer(GL_PIXEL_UNPACK_BUFFER, lastPixelUnpackBuffer);
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, lastUnpackAlignment);
#ifdef GL_UNPACK_ROW_LENGTH
    glPixelStorei(GL_UNPACK_ROW_LENGTH, lastUnpackRowLength);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, lastUnpackSkipPixels);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, lastUnpackSkipRows);
#endif
    
    ++g_imguiOverlayState.frameCount;

    ImGui::SetCurrentContext(previousContext);
    result.status = ImGuiOverlayRenderStatus::Rendered;
    return result;
}

void ShutdownImGuiOverlay() {
    std::lock_guard<std::mutex> lock(g_imguiOverlayMutex);
    ShutdownImGuiOverlayLocked(false);
    g_registeredWindow.store(nullptr, std::memory_order_release);
}

void ShutdownImGuiOverlayForProcessExit() {
    std::lock_guard<std::mutex> lock(g_imguiOverlayMutex);
    ShutdownImGuiOverlayLocked(true);
    g_registeredWindow.store(nullptr, std::memory_order_release);
}

} // namespace platform::x11
