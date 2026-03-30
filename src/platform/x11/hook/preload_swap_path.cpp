#include "../window_capture.h"

namespace {

std::mutex g_fpsLimitMutex;
std::chrono::steady_clock::time_point g_nextSwapDeadline{};
#ifdef __APPLE__
thread_local bool g_limitScheduledByAppleGlfwSwap = false;
#endif

void ApplyGlobalFpsLimitBeforeSwap() {
    const auto config = platform::config::GetConfigSnapshot();
    const int fpsLimit = config ? std::max(0, config->fpsLimit) : 0;
    if (fpsLimit <= 0) {
        std::lock_guard<std::mutex> lock(g_fpsLimitMutex);
        g_nextSwapDeadline = std::chrono::steady_clock::time_point{};
        return;
    }

    const auto frameDuration = std::chrono::microseconds(std::max<int64_t>(1, 1000000LL / fpsLimit));
    std::unique_lock<std::mutex> lock(g_fpsLimitMutex);
    const auto now = std::chrono::steady_clock::now();
    if (g_nextSwapDeadline == std::chrono::steady_clock::time_point{} || now > g_nextSwapDeadline + frameDuration) {
        g_nextSwapDeadline = now + frameDuration;
        return;
    }

    const auto targetTime = g_nextSwapDeadline;
    g_nextSwapDeadline += frameDuration;
    lock.unlock();

    while (true) {
        const auto currentTime = std::chrono::steady_clock::now();
        if (currentTime >= targetTime) {
            break;
        }

        const auto remaining = std::chrono::duration_cast<std::chrono::microseconds>(targetTime - currentTime).count();
        if (remaining > 2000) {
            usleep(static_cast<useconds_t>(remaining - 1000));
        }
    }
}

} // namespace

void RenderGuiOverlay(GLFWwindow* preferredWindow, const char* sourceLabel) {
    if (!platform::x11::IsImGuiRenderEnabled()) {
        DrainImGuiInputBridgeQueue(sourceLabel);
        RenderGuiPlaceholderOverlay();
        return;
    }

    const bool overlayCursorReleasedBeforeRender = IsOverlayCursorReleaseActive();
    const platform::x11::ImGuiOverlayRenderResult result = platform::x11::RenderImGuiOverlayFrame(preferredWindow, sourceLabel);
    const bool overlayCursorReleasedAfterRender = IsOverlayCursorReleaseActive();
    if (overlayCursorReleasedBeforeRender && !overlayCursorReleasedAfterRender) {
        // Handle overlay close transitions the same as a hotkey close so
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
                     "ImGui render enabled but no current OpenGL context is active during swap hook");
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
    platform::x11::UpdateMacMirrorRedirectState(containerW, containerH);

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

void BlitMacMirrorRedirectAndPrepareWindow() {
    if (!platform::x11::IsMacMirrorRedirectActive() ||
        !platform::x11::IsMacMirrorRedirectRendered()) {
        return;
    }

    int redirectWidth = 0;
    int redirectHeight = 0;
    if (!platform::x11::GetMacMirrorRedirectSize(redirectWidth, redirectHeight)) {
        return;
    }

    int dstX = 0;
    int dstY = 0;
    int surfaceWidth = redirectWidth;
    int surfaceHeight = redirectHeight;

    platform::x11::BlitMacMirrorRedirectToWindow(dstX,
                                                 dstY,
                                                 redirectWidth,
                                                 redirectHeight,
                                                 surfaceWidth,
                                                 surfaceHeight);
}

void PrepareDefaultFramebufferForSwap() {
    GlBindFramebufferFn bindFn = GetRealGlBindFramebuffer();
    if (!bindFn) { return; }

    // One bind is enough here.
    if (platform::x11::IsMacMirrorRedirectActive() &&
        platform::x11::IsMacMirrorRedirectRendered()) {
        return;
    }
    bindFn(GL_FRAMEBUFFER, 0);
}

namespace platform::x11 {

void TriggerImmediateModeResizeEnforcement() {
    g_lastResizeRequestWidth.store(0, std::memory_order_relaxed);
    g_lastResizeRequestHeight.store(0, std::memory_order_relaxed);
    TickModeResolutionTransition();
}

} // namespace platform::x11

#ifdef __APPLE__
extern "C" void glfwSwapBuffers(GLFWwindow* window) {
    GlfwSwapBuffersFn realFn = GetRealGlfwSwapBuffers();
    if (!realFn) {
        LogOnce(g_loggedNoGlfwSwap, "WARNING: glfwSwapBuffers called but real symbol could not be resolved");
        return;
    }

    if (window) {
        RefreshTrackedGlfwWindowMetrics(window);
        platform::x11::RegisterImGuiOverlayWindow(window);
    }

    const bool previousOuterSwapState = g_limitScheduledByAppleGlfwSwap;
    g_limitScheduledByAppleGlfwSwap = true;
    ApplyGlobalFpsLimitBeforeSwap();
    realFn(window);
    g_limitScheduledByAppleGlfwSwap = previousOuterSwapState;
}

CGLError my_CGLFlushDrawable(CGLContextObj ctx) {
    ReentryGuard guard;
    if (!guard.entered) { return CGLFlushDrawable(ctx); }

    GLFWwindow* window = FindTrackedGlfwWindowForContext(reinterpret_cast<void*>(ctx));
    if (window) {
        RefreshTrackedGlfwWindowMetrics(window);
        platform::x11::RegisterImGuiOverlayWindow(window);
    }

    if (!platform::x11::IsWindowCaptureRuntimeReady()) {
        platform::x11::SetWindowCaptureRuntimeReady(true);
    }

    MaybeInitSharedGlxContexts(nullptr, 0, reinterpret_cast<void*>(ctx), "CGLFlushDrawable");
    MaybeApplyGameStateTransitionReset();
    TickModeResolutionTransition();
    PumpManagedRepeatScheduler(window);
    ViewportPlacementBypassGuard bypassGuard(true);
    SubmitMirrorPipelineCapture();
    if (!platform::x11::IsMacMirrorRedirectActive()) {
        BlitOverscanAndPrepareWindow();
    }
    RenderMirrorPipelineOverlay();
    BlitMacMirrorRedirectAndPrepareWindow();
    PrepareDefaultFramebufferForSwap();
    RenderGuiOverlay(window, "CGLFlushDrawable");
    RenderRebindToggleIndicatorOverlay();

    if (!g_limitScheduledByAppleGlfwSwap) {
        ApplyGlobalFpsLimitBeforeSwap();
    }
    return CGLFlushDrawable(ctx);
}
#endif

#ifndef __APPLE__
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
    if (!platform::x11::IsWindowCaptureRuntimeReady()) {
        platform::x11::SetWindowCaptureRuntimeReady(true);
    }
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

    ApplyGlobalFpsLimitBeforeSwap();
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
    if (!platform::x11::IsWindowCaptureRuntimeReady()) {
        platform::x11::SetWindowCaptureRuntimeReady(true);
    }
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

    if (realFn) {
        ApplyGlobalFpsLimitBeforeSwap();
        return realFn(dpy, drawable, target_msc, divisor, remainder);
    }

    GlXSwapBuffersFn fallback = GetRealGlXSwapBuffers();
    if (fallback) {
        LogOnce(g_loggedNoMscFallback, "WARNING: glXSwapBuffersMscOML unresolved; falling back to glXSwapBuffers forwarding");
        ApplyGlobalFpsLimitBeforeSwap();
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
    void* currentGlContext = platform::x11::GetCurrentGlContextHandle();
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
    if (window && currentGlContext) {
        TrackGlfwWindowForCurrentContext(window, currentGlContext);
    }

    ViewportPlacementBypassGuard bypassGuard(true);
    PumpManagedRepeatScheduler(window);
    if (currentDisplay || currentDrawable || currentContext) {
        RecordAndLogSwap(SwapHookSource::GlfwSwapBuffers, currentDisplay, currentDrawable, currentContext);
        MaybeInitSharedGlxContexts(currentDisplay, currentDrawable, currentContext, "glfwSwapBuffers");
        if (!platform::x11::IsWindowCaptureRuntimeReady()) {
            platform::x11::SetWindowCaptureRuntimeReady(true);
        }
        MaybeApplyGameStateTransitionReset();
        TickModeResolutionTransition();
        SubmitMirrorPipelineCapture();
        BlitOverscanAndPrepareWindow();
        PrepareDefaultFramebufferForSwap();
        RenderMirrorPipelineOverlay();
        RenderGuiOverlay(window, "glfwSwapBuffers");
        RenderRebindToggleIndicatorOverlay();
    } else if (currentGlContext) {
        SwapHookSource expectedSource = SwapHookSource::Unknown;
        if (g_firstSwapSource.compare_exchange_strong(expectedSource, SwapHookSource::GlfwSwapBuffers, std::memory_order_acq_rel)) {
            LogAlways("first swap hook path selected: %s", SwapHookSourceToString(SwapHookSource::GlfwSwapBuffers));
        }

        bool expectedFirstSwapLog = false;
        if (g_loggedFirstSwap.compare_exchange_strong(expectedFirstSwapLog, true, std::memory_order_acq_rel)) {
            LogAlways("first glfwSwapBuffers call intercepted (non-GLX context=%p)", currentGlContext);
        }

        if (!platform::x11::IsWindowCaptureRuntimeReady()) {
            platform::x11::SetWindowCaptureRuntimeReady(true);
        }
        MaybeApplyGameStateTransitionReset();
        TickModeResolutionTransition();
        SubmitMirrorPipelineCapture();
        BlitOverscanAndPrepareWindow();
        PrepareDefaultFramebufferForSwap();
        RenderMirrorPipelineOverlay();
        RenderGuiOverlay(window, "glfwSwapBuffers");
        RenderRebindToggleIndicatorOverlay();
    } else {
        LogDebug("glfwSwapBuffers intercepted but no current OpenGL context was available");
    }

    ApplyGlobalFpsLimitBeforeSwap();
    realFn(window);
}
#endif // !__APPLE__

bool IsMainFramebufferDrawTarget() {
    GLint drawFramebuffer = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFramebuffer);

    if (drawFramebuffer == 0) {
        return true;
    }

    if (!platform::x11::IsOverscanActive()) {
        const GLuint redirectFbo = platform::x11::GetMacMirrorRedirectFboId();
        return redirectFbo != 0 && static_cast<GLuint>(drawFramebuffer) == redirectFbo;
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

    if (framebuffer == 0 && platform::x11::IsMacMirrorRedirectActive()) {
        const GLuint redirectFbo = platform::x11::GetMacMirrorRedirectFboId();
        if (redirectFbo != 0) {
            realFn(target, redirectFbo);
            if (target == GL_FRAMEBUFFER || target == GL_DRAW_FRAMEBUFFER) {
                platform::x11::MarkMacMirrorRedirectRendered();
            }
            return;
        }
    }

    realFn(target, framebuffer);
}
