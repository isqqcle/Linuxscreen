void RenderGuiOverlay(GLFWwindow* preferredWindow, const char* sourceLabel) {
    if (!platform::x11::IsImGuiRenderEnabled()) {
        DrainImGuiInputBridgeQueue(sourceLabel);
        RenderGuiPlaceholderOverlay();
        return;
    }

    const bool guiVisibleBeforeRender = platform::x11::IsGuiVisible();
    const platform::x11::ImGuiOverlayRenderResult result = platform::x11::RenderImGuiOverlayFrame(preferredWindow, sourceLabel);
    const bool guiVisibleAfterRender = platform::x11::IsGuiVisible();
    if (guiVisibleBeforeRender && !guiVisibleAfterRender) {
        // Handle window-close button (X) the same as hotkey toggle close so
        // cursor mode restoration stays consistent.
        GLFWwindow* targetWindow = ResolveGuiToggleWindow(preferredWindow);
        RestoreCursorDisabledAfterGuiClose(targetWindow);
        DispatchCurrentFreeCursorPosition(targetWindow);
    }

    switch (result.status) {
    case platform::x11::ImGuiOverlayRenderStatus::Rendered:
        LogDebugOnce(g_loggedFirstImGuiOverlayFrame,
                     "first ImGui overlay frame rendered (enable LINUXSCREEN_X11_DEBUG=1 for ongoing frame diagnostics)");
        return;
    case platform::x11::ImGuiOverlayRenderStatus::Hidden:
        return;
    case platform::x11::ImGuiOverlayRenderStatus::MissingWindow:
        LogDebugOnce(g_loggedImGuiOverlayMissingWindow,
                     "ImGui render enabled but no GLFW window is known yet; waiting for GLFW callback registration or swap call");
        break;
    case platform::x11::ImGuiOverlayRenderStatus::MissingGlContext:
        LogDebugOnce(g_loggedImGuiOverlayMissingGlContext,
                     "ImGui render enabled but no current GLX context is active during swap hook");
        break;
    case platform::x11::ImGuiOverlayRenderStatus::InitFailed:
        LogOnce(g_loggedImGuiOverlayInitFailed,
                "WARNING: failed to initialize ImGui overlay runtime; falling back to placeholder panel");
        break;
    case platform::x11::ImGuiOverlayRenderStatus::Disabled:
        break;
    }

    RenderGuiPlaceholderOverlay();
}

void RenderMirrorPipelineOverlay() {
    if (!platform::x11::IsGlxMirrorPipelineEnabled()) { return; }

    int overlayWidth = 0;
    int overlayHeight = 0;
    if (!GetCurrentContainerSizeForModeTarget(overlayWidth, overlayHeight)) {
        GLint viewport[4] = { 0, 0, 0, 0 };
        glGetIntegerv(GL_VIEWPORT, viewport);
        overlayWidth = viewport[2];
        overlayHeight = viewport[3];
    }

    if (overlayWidth <= 0 || overlayHeight <= 0) {
        return;
    }

    platform::x11::RenderGlxMirrorOverlay(overlayWidth, overlayHeight);
    platform::x11::RenderGlxEyeZoomOverlay(overlayWidth, overlayHeight);
}

void SubmitMirrorPipelineCapture() {
    GLint viewport[4] = { 0, 0, 0, 0 };
    glGetIntegerv(GL_VIEWPORT, viewport);
    if (viewport[2] <= 0 || viewport[3] <= 0) { return; }
    const int renderedViewportW = viewport[2];
    const int renderedViewportH = viewport[3];

    // Recover the physical window/container size first. When overscan is active,
    // GL_VIEWPORT reflects the target-sized overscan FBO instead of the real
    // drawable, but the activation decision still needs the real container size.
    int containerW = 0;
    int containerH = 0;
    if (!GetCurrentPhysicalContainerSize(containerW, containerH) &&
        platform::x11::IsOverscanActive()) {
        const auto dims = platform::x11::GetOverscanDimensions();
        containerW = dims.windowWidth;
        containerH = dims.windowHeight;
    }
    if (containerW <= 0 || containerH <= 0) {
        containerW = viewport[2];
        containerH = viewport[3];
    }

    platform::x11::UpdateOverscanState(containerW, containerH);

    g_lastSwapViewportX.store(viewport[0], std::memory_order_relaxed);
    g_lastSwapViewportY.store(viewport[1], std::memory_order_relaxed);
    // Track the logical viewport size the game actually rendered this frame.
    // Using the post-update target here can mask the need to shrink back after
    // leaving an oversized mode.
    g_lastSwapViewportWidth.store(renderedViewportW,  std::memory_order_relaxed);
    g_lastSwapViewportHeight.store(renderedViewportH, std::memory_order_relaxed);

    if (!platform::x11::IsGlxMirrorPipelineEnabled()) { return; }
    platform::x11::SubmitGlxMirrorCapture(renderedViewportW, renderedViewportH);
}

void BlitOverscanAndPrepareWindow() {
    if (!platform::x11::IsOverscanActive()) { return; }

    const auto dims = platform::x11::GetOverscanDimensions();
    int dstX = 0;
    int dstY = 0;
    int surfaceWidth = dims.windowWidth;
    int surfaceHeight = dims.windowHeight;

    PlacementTransform transform;
    if (ResolvePlacementTransform(transform)) {
        dstX = transform.framebufferBottomLeftX;
        dstY = transform.framebufferBottomLeftY;
        surfaceWidth = transform.framebufferWidth;
        surfaceHeight = transform.framebufferHeight;
    }

    platform::x11::BlitOverscanToWindow(dstX,
                                        dstY,
                                        dims.windowWidth,
                                        dims.windowHeight,
                                        surfaceWidth,
                                        surfaceHeight);
}

void PrepareDefaultFramebufferForSwap() {
    GlBindFramebufferFn bindFn = GetRealGlBindFramebuffer();
    if (!bindFn) { return; }

    bindFn(GL_FRAMEBUFFER, 0);
    bindFn(GL_DRAW_FRAMEBUFFER, 0);
    bindFn(GL_READ_FRAMEBUFFER, 0);
}

namespace platform::x11 {

void TriggerImmediateModeResizeEnforcement() {
    g_lastResizeRequestWidth.store(0, std::memory_order_relaxed);
    g_lastResizeRequestHeight.store(0, std::memory_order_relaxed);
    TickModeResolutionTransition();
}

} // namespace platform::x11

extern "C" void glXSwapBuffers(Display* dpy, GLXDrawable drawable) {
    GlXSwapBuffersFn realFn = GetRealGlXSwapBuffers();
    if (!realFn) { return; }

    ReentryGuard guard;
    if (!guard.entered) {
        realFn(dpy, drawable);
        return;
    }

    GLXContext currentContext = glXGetCurrentContext();
    RecordAndLogSwap(SwapHookSource::GlXSwapBuffers, dpy, drawable, currentContext);
    MaybeInitSharedGlxContexts(dpy, drawable, currentContext, "glXSwapBuffers");
    MaybeApplyGameStateTransitionReset();
    TickModeResolutionTransition();
    PumpManagedRepeatScheduler(nullptr);
    ViewportPlacementBypassGuard bypassGuard(true);
    SubmitMirrorPipelineCapture();
    BlitOverscanAndPrepareWindow();
    PrepareDefaultFramebufferForSwap();
    RenderMirrorPipelineOverlay();
    RenderGuiOverlay(nullptr, "glXSwapBuffers");
    RenderRebindToggleIndicatorOverlay();

    realFn(dpy, drawable);
}

extern "C" Bool glXSwapBuffersMscOML(Display* dpy, GLXDrawable drawable, int64_t target_msc, int64_t divisor, int64_t remainder) {
    GlXSwapBuffersMscOMLFn realFn = GetRealGlXSwapBuffersMscOML();

    ReentryGuard guard;
    if (!guard.entered) {
        if (realFn) { return realFn(dpy, drawable, target_msc, divisor, remainder); }
        GlXSwapBuffersFn fallback = GetRealGlXSwapBuffers();
        if (fallback) {
            fallback(dpy, drawable);
            return True;
        }
        return False;
    }

    GLXContext currentContext = glXGetCurrentContext();
    RecordAndLogSwap(SwapHookSource::GlXSwapBuffersMscOML, dpy, drawable, currentContext);
    MaybeInitSharedGlxContexts(dpy, drawable, currentContext, "glXSwapBuffersMscOML");
    MaybeApplyGameStateTransitionReset();
    TickModeResolutionTransition();
    PumpManagedRepeatScheduler(nullptr);
    ViewportPlacementBypassGuard bypassGuard(true);
        SubmitMirrorPipelineCapture();
        BlitOverscanAndPrepareWindow();
        PrepareDefaultFramebufferForSwap();
        RenderMirrorPipelineOverlay();
        RenderGuiOverlay(nullptr, "glXSwapBuffersMscOML");
        RenderRebindToggleIndicatorOverlay();

    if (realFn) { return realFn(dpy, drawable, target_msc, divisor, remainder); }

    GlXSwapBuffersFn fallback = GetRealGlXSwapBuffers();
    if (fallback) {
        LogOnce(g_loggedNoMscFallback, "WARNING: glXSwapBuffersMscOML unresolved; falling back to glXSwapBuffers forwarding");
        fallback(dpy, drawable);
        return True;
    }

    LogOnce(g_loggedResolveFailure, "ERROR: glXSwapBuffersMscOML unresolved and no glXSwapBuffers fallback available");
    return False;
}

extern "C" void glfwSwapBuffers(GLFWwindow* window) {
    GlfwSwapBuffersFn realFn = GetRealGlfwSwapBuffers();
    if (!realFn) {
        LogOnce(g_loggedNoGlfwSwap, "WARNING: glfwSwapBuffers called but real symbol could not be resolved");
        return;
    }

    ReentryGuard guard;
    if (!guard.entered) {
        realFn(window);
        return;
    }

    Display* currentDisplay = glXGetCurrentDisplay();
    GLXDrawable currentDrawable = glXGetCurrentDrawable();
    GLXContext currentContext = glXGetCurrentContext();
    if (window) {
        g_lastSwapWindow.store(window, std::memory_order_release);

        int windowWidth = 0;
        int windowHeight = 0;
        int framebufferWidth = 0;
        int framebufferHeight = 0;
        (void)GetSizeFromLatestGlfwWindow(windowWidth, windowHeight);
        (void)GetFramebufferSizeFromLatestGlfwWindow(framebufferWidth, framebufferHeight);
        platform::x11::RecordGlfwWindowMetrics(windowWidth, windowHeight, framebufferWidth, framebufferHeight);
    }
    platform::x11::RegisterImGuiOverlayWindow(window);

    ViewportPlacementBypassGuard bypassGuard(true);
    PumpManagedRepeatScheduler(window);
    if (currentDisplay || currentDrawable || currentContext) {
        RecordAndLogSwap(SwapHookSource::GlfwSwapBuffers, currentDisplay, currentDrawable, currentContext);
        MaybeInitSharedGlxContexts(currentDisplay, currentDrawable, currentContext, "glfwSwapBuffers");
        MaybeApplyGameStateTransitionReset();
        TickModeResolutionTransition();
        SubmitMirrorPipelineCapture();
        BlitOverscanAndPrepareWindow();
        PrepareDefaultFramebufferForSwap();
        RenderMirrorPipelineOverlay();
        RenderGuiOverlay(window, "glfwSwapBuffers");
        RenderRebindToggleIndicatorOverlay();
    } else {
        LogDebug("glfwSwapBuffers intercepted but no current GLX handles were available");
    }

    realFn(window);
}

bool IsMainFramebufferDrawTarget() {
    GLint drawFramebuffer = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFramebuffer);

    if (drawFramebuffer == 0) {
        return true;
    }

    if (!platform::x11::IsOverscanActive()) {
        return false;
    }

    const GLuint overscanFbo = platform::x11::GetOverscanFboId();
    return overscanFbo != 0 && static_cast<GLuint>(drawFramebuffer) == overscanFbo;
}

extern "C" void glViewport(GLint x, GLint y, GLsizei width, GLsizei height) {
    GlViewportFn realFn = GetRealGlViewport();
    if (!realFn) {
        return;
    }

    if (g_bypassViewportPlacement || width <= 0 || height <= 0) {
        realFn(x, y, width, height);
        return;
    }

    if (!IsMainFramebufferDrawTarget()) {
        realFn(x, y, width, height);
        return;
    }

    if (!IsCanonicalMainContentRect(x, y, width, height)) {
        realFn(x, y, width, height);
        return;
    }

    GLint translatedX = x;
    GLint translatedY = y;
    if (TranslateMainViewport(x, y, width, height, translatedX, translatedY)) {
        realFn(translatedX, translatedY, width, height);
        return;
    }

    realFn(x, y, width, height);
}

extern "C" void glScissor(GLint x, GLint y, GLsizei width, GLsizei height) {
    GlScissorFn realFn = GetRealGlScissor();
    if (!realFn) {
        return;
    }

    if (g_bypassViewportPlacement || width <= 0 || height <= 0) {
        realFn(x, y, width, height);
        return;
    }

    if (!IsMainFramebufferDrawTarget()) {
        realFn(x, y, width, height);
        return;
    }

    if (!IsCanonicalMainContentRect(x, y, width, height)) {
        realFn(x, y, width, height);
        return;
    }

    GLint translatedX = x;
    GLint translatedY = y;
    if (TranslateMainScissor(x, y, width, height, translatedX, translatedY)) {
        realFn(translatedX, translatedY, width, height);
        return;
    }

    realFn(x, y, width, height);
}

extern "C" void glBindFramebuffer(GLenum target, GLuint framebuffer) {
    GlBindFramebufferFn realFn = GetRealGlBindFramebuffer();
    if (!realFn) { return; }

    if (g_bypassViewportPlacement) {
        realFn(target, framebuffer);
        return;
    }

    if (framebuffer == 0 && platform::x11::IsOverscanActive()) {
        GLuint overscanFbo = platform::x11::GetOverscanFboId();
        if (overscanFbo != 0) {
            realFn(target, overscanFbo);
            if (target == GL_FRAMEBUFFER || target == GL_DRAW_FRAMEBUFFER) {
                platform::x11::MarkOverscanFboRendered();
            }
            return;
        }
    }

    realFn(target, framebuffer);
}
