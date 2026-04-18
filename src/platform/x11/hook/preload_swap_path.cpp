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

bool IsColorAttachmentEnum(GLint bufferEnum) {
    constexpr GLint kMaxColorAttachment = static_cast<GLint>(GL_COLOR_ATTACHMENT0) + 31;
    return bufferEnum >= static_cast<GLint>(GL_COLOR_ATTACHMENT0) &&
           bufferEnum <= kMaxColorAttachment;
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
#ifdef __APPLE__
    platform::x11::CaptureDefaultFramebufferToMacMirrorRedirectIfNeeded();
#endif

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
    g_lastResizeBasisWidth.store(0, std::memory_order_relaxed);
    g_lastResizeBasisHeight.store(0, std::memory_order_relaxed);
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

bool EnsureOffscreenModeTargetForDefaultFramebuffer() {
    if (platform::x11::IsOverscanActive()) {
        return true;
    }

    int containerW = 0;
    int containerH = 0;
    if (!GetCurrentPhysicalContainerSize(containerW, containerH) ||
        containerW <= 0 ||
        containerH <= 0) {
        return false;
    }

    platform::x11::UpdateOverscanState(containerW, containerH);
    if (platform::x11::IsOverscanActive()) {
        return true;
    }

#ifdef __APPLE__
    platform::x11::UpdateMacMirrorRedirectState(containerW, containerH);
    return platform::x11::IsMacMirrorRedirectActive();
#else
    return false;
#endif
}

void HookedGlViewport(GLint x, GLint y, GLsizei width, GLsizei height) {
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

    if (!IsMainContentCoordinateRect(x, y, width, height)) {
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

void HookedGlScissor(GLint x, GLint y, GLsizei width, GLsizei height) {
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

    if (!IsMainContentCoordinateRect(x, y, width, height)) {
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

void HookedGlBindFramebuffer(GLenum target, GLuint framebuffer) {
    GlBindFramebufferFn realFn = GetRealGlBindFramebuffer();
    if (!realFn) { return; }

    if (g_bypassViewportPlacement) {
        realFn(target, framebuffer);
        return;
    }

    if (framebuffer == 0) {
        (void)EnsureOffscreenModeTargetForDefaultFramebuffer();
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

bool GetTexture2DLevel0Size(GLuint texture, int& outWidth, int& outHeight) {
    outWidth = 0;
    outHeight = 0;
    if (texture == 0 || glIsTexture(texture) != GL_TRUE) {
        return false;
    }

    GLint previousActiveTexture = 0;
    GLint previousTexture = 0;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &previousActiveTexture);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);

    glBindTexture(GL_TEXTURE_2D, texture);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &outWidth);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &outHeight);

    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTexture));
    glActiveTexture(static_cast<GLenum>(previousActiveTexture));

    return outWidth > 0 && outHeight > 0;
}

void TrackCurrentReadFramebufferColorTexture() {
    GLint readFramebuffer = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFramebuffer);
    GLint readBuffer = 0;
    glGetIntegerv(GL_READ_BUFFER, &readBuffer);
    if (!IsColorAttachmentEnum(readBuffer)) {
        platform::x11::RecordPresentedGameTexture(0, 0, 0);
        return;
    }
    const GLenum attachment = static_cast<GLenum>(readBuffer);

    using GlGetFramebufferAttachmentParameterivFn = void (*)(GLenum, GLenum, GLenum, GLint*);
    static GlGetFramebufferAttachmentParameterivFn getFramebufferAttachmentParameteriv =
        reinterpret_cast<GlGetFramebufferAttachmentParameterivFn>(
            platform::x11::ResolveCurrentGlProcAddress("glGetFramebufferAttachmentParameteriv"));
    if (!getFramebufferAttachmentParameteriv) {
        platform::x11::RecordPresentedGameTexture(0, 0, 0);
        return;
    }

    GLint attachmentType = GL_NONE;
    getFramebufferAttachmentParameteriv(GL_READ_FRAMEBUFFER,
                                        attachment,
                                        GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE,
                                        &attachmentType);
    if (attachmentType != GL_TEXTURE) {
        platform::x11::RecordPresentedGameTexture(0, 0, 0);
        return;
    }

    GLint attachmentName = 0;
    getFramebufferAttachmentParameteriv(GL_READ_FRAMEBUFFER,
                                        attachment,
                                        GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME,
                                        &attachmentName);
    int textureWidth = 0;
    int textureHeight = 0;
    if (attachmentName <= 0 ||
        !GetTexture2DLevel0Size(static_cast<GLuint>(attachmentName), textureWidth, textureHeight)) {
        platform::x11::RecordPresentedGameTexture(0, 0, 0);
        return;
    }

    platform::x11::RecordPresentedGameTexture(static_cast<GLuint>(attachmentName), textureWidth, textureHeight);
}

void TrackExplicitFramebufferColorTexture(GLuint fbo) {
    if (fbo == 0) {
        platform::x11::RecordPresentedGameTexture(0, 0, 0);
        return;
    }

    using GlGetNamedFramebufferAttachmentParameterivFn = void (*)(GLuint, GLenum, GLenum, GLint*);
    static GlGetNamedFramebufferAttachmentParameterivFn getNamedFramebufferAttachmentParameteriv =
        reinterpret_cast<GlGetNamedFramebufferAttachmentParameterivFn>(
            platform::x11::ResolveCurrentGlProcAddress("glGetNamedFramebufferAttachmentParameteriv"));
    if (!getNamedFramebufferAttachmentParameteriv) {
        TrackCurrentReadFramebufferColorTexture();
        return;
    }

    GlBindFramebufferFn bindFramebufferFn = GetRealGlBindFramebuffer();
    if (!bindFramebufferFn) {
        platform::x11::RecordPresentedGameTexture(0, 0, 0);
        return;
    }

    GLint previousReadFramebuffer = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousReadFramebuffer);
    bindFramebufferFn(GL_READ_FRAMEBUFFER, fbo);
    GLint readBuffer = 0;
    glGetIntegerv(GL_READ_BUFFER, &readBuffer);
    bindFramebufferFn(GL_READ_FRAMEBUFFER, static_cast<GLuint>(previousReadFramebuffer));

    if (!IsColorAttachmentEnum(readBuffer)) {
        platform::x11::RecordPresentedGameTexture(0, 0, 0);
        return;
    }
    const GLenum attachment = static_cast<GLenum>(readBuffer);

    GLint attachmentType = GL_NONE;
    getNamedFramebufferAttachmentParameteriv(fbo,
                                             attachment,
                                             GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE,
                                             &attachmentType);
    if (attachmentType != GL_TEXTURE) {
        platform::x11::RecordPresentedGameTexture(0, 0, 0);
        return;
    }

    GLint attachmentName = 0;
    getNamedFramebufferAttachmentParameteriv(fbo,
                                             attachment,
                                             GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME,
                                             &attachmentName);
    int textureWidth = 0;
    int textureHeight = 0;
    if (attachmentName <= 0 ||
        !GetTexture2DLevel0Size(static_cast<GLuint>(attachmentName), textureWidth, textureHeight)) {
        platform::x11::RecordPresentedGameTexture(0, 0, 0);
        return;
    }

    platform::x11::RecordPresentedGameTexture(static_cast<GLuint>(attachmentName), textureWidth, textureHeight);
}

bool BlitRectEquals(GLint x0a,
                    GLint y0a,
                    GLint x1a,
                    GLint y1a,
                    GLint x0b,
                    GLint y0b,
                    GLint x1b,
                    GLint y1b) {
    const GLint aMinX = std::min(x0a, x1a);
    const GLint aMaxX = std::max(x0a, x1a);
    const GLint aMinY = std::min(y0a, y1a);
    const GLint aMaxY = std::max(y0a, y1a);
    const GLint bMinX = std::min(x0b, x1b);
    const GLint bMaxX = std::max(x0b, x1b);
    const GLint bMinY = std::min(y0b, y1b);
    const GLint bMaxY = std::max(y0b, y1b);
    return aMinX == bMinX && aMaxX == bMaxX && aMinY == bMinY && aMaxY == bMaxY;
}

bool ResolveTranslatedPresentedBlitDestination(GLint dstX0,
                                               GLint dstY0,
                                               GLint dstX1,
                                               GLint dstY1,
                                               GLint& outDstX0,
                                               GLint& outDstY0,
                                               GLint& outDstX1,
                                               GLint& outDstY1) {
    outDstX0 = 0;
    outDstY0 = 0;
    outDstX1 = 0;
    outDstY1 = 0;

    PlacementTransform transform;
    if (!ResolvePlacementTransform(transform) ||
        transform.physicalWidth <= 0 ||
        transform.physicalHeight <= 0) {
        return false;
    }

    if (!BlitRectEquals(dstX0,
                        dstY0,
                        dstX1,
                        dstY1,
                        0,
                        0,
                        static_cast<GLint>(transform.physicalWidth),
                        static_cast<GLint>(transform.physicalHeight))) {
        return false;
    }

    outDstX0 = static_cast<GLint>(transform.framebufferBottomLeftX);
    outDstY0 = static_cast<GLint>(transform.framebufferBottomLeftY);
    outDstX1 = static_cast<GLint>(transform.framebufferBottomLeftX + transform.physicalWidth);
    outDstY1 = static_cast<GLint>(transform.framebufferBottomLeftY + transform.physicalHeight);
    return true;
}

void HookedGlBlitFramebuffer(GLint srcX0,
                             GLint srcY0,
                             GLint srcX1,
                             GLint srcY1,
                             GLint dstX0,
                             GLint dstY0,
                             GLint dstX1,
                             GLint dstY1,
                             GLbitfield mask,
                             GLenum filter) {
    GlBlitFramebufferFn realFn = GetRealGlBlitFramebuffer();
    if (!realFn) {
        return;
    }

    if (g_bypassViewportPlacement) {
        realFn(srcX0, srcY0, srcX1, srcY1, dstX0, dstY0, dstX1, dstY1, mask, filter);
        return;
    }

    GLint readFramebuffer = 0;
    GLint drawFramebuffer = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFramebuffer);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFramebuffer);

    constexpr GLbitfield kPresentedBlitMask = GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT;
    if (drawFramebuffer == 0 && readFramebuffer != 0 && (mask & kPresentedBlitMask) != 0) {
        (void)EnsureOffscreenModeTargetForDefaultFramebuffer();

        if ((mask & GL_COLOR_BUFFER_BIT) != 0) {
            const int presentedWidth = srcX1 >= srcX0 ? (srcX1 - srcX0) : (srcX0 - srcX1);
            const int presentedHeight = srcY1 >= srcY0 ? (srcY1 - srcY0) : (srcY0 - srcY1);
            GLint readBuffer = GL_COLOR_ATTACHMENT0;
            glGetIntegerv(GL_READ_BUFFER, &readBuffer);
            platform::x11::RecordPresentedGameFramebuffer(static_cast<GLuint>(readFramebuffer),
                                                          presentedWidth,
                                                          presentedHeight,
                                                          static_cast<GLenum>(readBuffer));
            TrackCurrentReadFramebufferColorTexture();
        }

        if (platform::x11::IsOverscanActive()) {
            const GLuint overscanFbo = platform::x11::GetOverscanFboId();
            if (overscanFbo != 0) {
                const auto dims = platform::x11::GetOverscanDimensions();
                GlBindFramebufferFn bindFn = GetRealGlBindFramebuffer();
                if (bindFn) {
                    bindFn(GL_DRAW_FRAMEBUFFER, overscanFbo);
                    realFn(srcX0,
                           srcY0,
                           srcX1,
                           srcY1,
                           dstX0 + dims.marginLeft,
                           dstY0 + dims.marginBottom,
                           dstX1 + dims.marginLeft,
                           dstY1 + dims.marginBottom,
                           mask,
                           filter);
                    bindFn(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(drawFramebuffer));
                    platform::x11::MarkOverscanFboRendered();
                    return;
                }
            }
        }

        GLint resolvedDstX0 = 0;
        GLint resolvedDstY0 = 0;
        GLint resolvedDstX1 = 0;
        GLint resolvedDstY1 = 0;
        const bool translated = ResolveTranslatedPresentedBlitDestination(dstX0, dstY0, dstX1, dstY1,
                                                                          resolvedDstX0, resolvedDstY0,
                                                                          resolvedDstX1, resolvedDstY1);
        if (IsDebugEnabled()) {
            static int debugBlitFrame = 0;
            if ((++debugBlitFrame % 120) == 0) {
                PlacementTransform transform;
                const bool hasTransform = ResolvePlacementTransform(transform);
                LogDebug("glBlitFramebuffer present readFbo=%d mask=0x%x src=(%d,%d)-(%d,%d) dst=(%d,%d)-(%d,%d) "
                         "translated=%d translatedDst=(%d,%d)-(%d,%d) transform=%d phys=%dx%d fb=%dx%d placed=(%d,%d)",
                         readFramebuffer,
                         static_cast<unsigned int>(mask),
                         srcX0,
                         srcY0,
                         srcX1,
                         srcY1,
                         dstX0,
                         dstY0,
                         dstX1,
                         dstY1,
                         translated ? 1 : 0,
                         resolvedDstX0,
                         resolvedDstY0,
                         resolvedDstX1,
                         resolvedDstY1,
                         hasTransform ? 1 : 0,
                         hasTransform ? transform.physicalWidth : 0,
                         hasTransform ? transform.physicalHeight : 0,
                         hasTransform ? transform.framebufferWidth : 0,
                         hasTransform ? transform.framebufferHeight : 0,
                         hasTransform ? transform.framebufferBottomLeftX : 0,
                         hasTransform ? transform.framebufferBottomLeftY : 0);
            }
        }
        if (translated) {
            realFn(srcX0,
                   srcY0,
                   srcX1,
                   srcY1,
                   resolvedDstX0,
                   resolvedDstY0,
                   resolvedDstX1,
                   resolvedDstY1,
                   mask,
                   filter);
            return;
        }
    }

    realFn(srcX0, srcY0, srcX1, srcY1, dstX0, dstY0, dstX1, dstY1, mask, filter);
}

void HookedGlBlitNamedFramebuffer(GLuint readFramebuffer,
                                  GLuint drawFramebuffer,
                                  GLint srcX0,
                                  GLint srcY0,
                                  GLint srcX1,
                                  GLint srcY1,
                                  GLint dstX0,
                                  GLint dstY0,
                                  GLint dstX1,
                                  GLint dstY1,
                                  GLbitfield mask,
                                  GLenum filter) {
    GlBlitNamedFramebufferFn realFn = GetRealGlBlitNamedFramebuffer();
    if (!realFn) {
        return;
    }

    if (g_bypassViewportPlacement) {
        realFn(readFramebuffer, drawFramebuffer, srcX0, srcY0, srcX1, srcY1, dstX0, dstY0, dstX1, dstY1, mask, filter);
        return;
    }

    constexpr GLbitfield kPresentedBlitMask = GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT;
    if (drawFramebuffer == 0 && readFramebuffer != 0 && (mask & kPresentedBlitMask) != 0) {
        (void)EnsureOffscreenModeTargetForDefaultFramebuffer();

        if ((mask & GL_COLOR_BUFFER_BIT) != 0) {
            const int presentedWidth = srcX1 >= srcX0 ? (srcX1 - srcX0) : (srcX0 - srcX1);
            const int presentedHeight = srcY1 >= srcY0 ? (srcY1 - srcY0) : (srcY0 - srcY1);
            GLenum readBuffer = GL_COLOR_ATTACHMENT0;
            if (GlBindFramebufferFn bindFramebufferFn = GetRealGlBindFramebuffer()) {
                GLint previousReadFramebuffer = 0;
                GLint currentReadBuffer = GL_COLOR_ATTACHMENT0;
                glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousReadFramebuffer);
                bindFramebufferFn(GL_READ_FRAMEBUFFER, readFramebuffer);
                glGetIntegerv(GL_READ_BUFFER, &currentReadBuffer);
                bindFramebufferFn(GL_READ_FRAMEBUFFER, static_cast<GLuint>(previousReadFramebuffer));
                readBuffer = static_cast<GLenum>(currentReadBuffer);
            }
            platform::x11::RecordPresentedGameFramebuffer(readFramebuffer,
                                                          presentedWidth,
                                                          presentedHeight,
                                                          readBuffer);
            TrackExplicitFramebufferColorTexture(readFramebuffer);
        }

        if (platform::x11::IsOverscanActive()) {
            const GLuint overscanFbo = platform::x11::GetOverscanFboId();
            if (overscanFbo != 0) {
                const auto dims = platform::x11::GetOverscanDimensions();
                realFn(readFramebuffer,
                       overscanFbo,
                       srcX0,
                       srcY0,
                       srcX1,
                       srcY1,
                       dstX0 + dims.marginLeft,
                       dstY0 + dims.marginBottom,
                       dstX1 + dims.marginLeft,
                       dstY1 + dims.marginBottom,
                       mask,
                       filter);
                platform::x11::MarkOverscanFboRendered();
                return;
            }
        }

        GLint resolvedDstX0 = 0;
        GLint resolvedDstY0 = 0;
        GLint resolvedDstX1 = 0;
        GLint resolvedDstY1 = 0;
        const bool translated = ResolveTranslatedPresentedBlitDestination(dstX0, dstY0, dstX1, dstY1,
                                                                          resolvedDstX0, resolvedDstY0,
                                                                          resolvedDstX1, resolvedDstY1);
        if (IsDebugEnabled()) {
            static int debugNamedBlitFrame = 0;
            if ((++debugNamedBlitFrame % 120) == 0) {
                PlacementTransform transform;
                const bool hasTransform = ResolvePlacementTransform(transform);
                LogDebug("glBlitNamedFramebuffer present readFbo=%u mask=0x%x src=(%d,%d)-(%d,%d) dst=(%d,%d)-(%d,%d) "
                         "translated=%d translatedDst=(%d,%d)-(%d,%d) transform=%d phys=%dx%d fb=%dx%d placed=(%d,%d)",
                         readFramebuffer,
                         static_cast<unsigned int>(mask),
                         srcX0,
                         srcY0,
                         srcX1,
                         srcY1,
                         dstX0,
                         dstY0,
                         dstX1,
                         dstY1,
                         translated ? 1 : 0,
                         resolvedDstX0,
                         resolvedDstY0,
                         resolvedDstX1,
                         resolvedDstY1,
                         hasTransform ? 1 : 0,
                         hasTransform ? transform.physicalWidth : 0,
                         hasTransform ? transform.physicalHeight : 0,
                         hasTransform ? transform.framebufferWidth : 0,
                         hasTransform ? transform.framebufferHeight : 0,
                         hasTransform ? transform.framebufferBottomLeftX : 0,
                         hasTransform ? transform.framebufferBottomLeftY : 0);
            }
        }
        if (translated) {
            realFn(readFramebuffer,
                   drawFramebuffer,
                   srcX0,
                   srcY0,
                   srcX1,
                   srcY1,
                   resolvedDstX0,
                   resolvedDstY0,
                   resolvedDstX1,
                   resolvedDstY1,
                   mask,
                   filter);
            return;
        }
    }

    realFn(readFramebuffer, drawFramebuffer, srcX0, srcY0, srcX1, srcY1, dstX0, dstY0, dstX1, dstY1, mask, filter);
}

#ifndef __APPLE__
extern "C" void glViewport(GLint x, GLint y, GLsizei width, GLsizei height) {
    HookedGlViewport(x, y, width, height);
}

extern "C" void glScissor(GLint x, GLint y, GLsizei width, GLsizei height) {
    HookedGlScissor(x, y, width, height);
}

extern "C" void glBindFramebuffer(GLenum target, GLuint framebuffer) {
    HookedGlBindFramebuffer(target, framebuffer);
}

extern "C" void glBlitFramebuffer(GLint srcX0,
                                  GLint srcY0,
                                  GLint srcX1,
                                  GLint srcY1,
                                  GLint dstX0,
                                  GLint dstY0,
                                  GLint dstX1,
                                  GLint dstY1,
                                  GLbitfield mask,
                                  GLenum filter) {
    HookedGlBlitFramebuffer(srcX0, srcY0, srcX1, srcY1, dstX0, dstY0, dstX1, dstY1, mask, filter);
}

extern "C" void glBlitNamedFramebuffer(GLuint readFramebuffer,
                                       GLuint drawFramebuffer,
                                       GLint srcX0,
                                       GLint srcY0,
                                       GLint srcX1,
                                       GLint srcY1,
                                       GLint dstX0,
                                       GLint dstY0,
                                       GLint dstX1,
                                       GLint dstY1,
                                       GLbitfield mask,
                                       GLenum filter) {
    HookedGlBlitNamedFramebuffer(readFramebuffer,
                                 drawFramebuffer,
                                 srcX0,
                                 srcY0,
                                 srcX1,
                                 srcY1,
                                 dstX0,
                                 dstY0,
                                 dstX1,
                                 dstY1,
                                 mask,
                                 filter);
}
#endif
