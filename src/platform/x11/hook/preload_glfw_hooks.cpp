extern "C" __attribute__((visibility("default"))) int _glfwLinuxscreenGetCursorCenter(void* opaqueWindow,
                                                                                      double* xpos,
                                                                                      double* ypos) {
    GLFWwindow* lastSwapWindow = g_lastSwapWindow.load(std::memory_order_acquire);
    if (lastSwapWindow && opaqueWindow && opaqueWindow != lastSwapWindow) {
        return 0;
    }

    double centerX = 0.0;
    double centerY = 0.0;
    if (!ResolveCurrentWindowCursorCenter(centerX, centerY)) {
        return 0;
    }

    if (xpos) {
        *xpos = centerX;
    }
    if (ypos) {
        *ypos = centerY;
    }
    return 1;
}

extern "C" GlfwKeyCallback glfwSetKeyCallback(GLFWwindow* window, GlfwKeyCallback callback) {
    GlfwSetKeyCallbackFn realSetter = GetRealGlfwSetKeyCallback();
    if (!realSetter) {
        LogOnce(g_loggedGlfwCallbackResolveFailure, "ERROR: failed to resolve glfwSetKeyCallback");
        return nullptr;
    }

    platform::x11::RegisterImGuiOverlayWindow(window);

    if (!IsGlfwInputHookEnabled()) {
        LogDebugOnce(g_loggedGlfwInputHookDisabled,
                     "GLFW input callback interposition disabled");
        return realSetter(window, callback);
    }

    GlfwKeyCallback previous = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_glfwCallbackMutex);
        auto it = g_glfwCallbackMap.find(window);
        if (it != g_glfwCallbackMap.end()) { previous = it->second.key; }
    }

    const GlfwKeyCallback dispatch = callback ? HookedGlfwKeyCallback : nullptr;
    GlfwKeyCallback realPrevious = realSetter(window, dispatch);
    if (realPrevious && realPrevious != HookedGlfwKeyCallback) { previous = realPrevious; }

    bool removedWindowCallbacks = false;
    {
        std::lock_guard<std::mutex> lock(g_glfwCallbackMutex);
        GlfwCallbackState& state = g_glfwCallbackMap[window];
        state.key = callback;
        removedWindowCallbacks = RemoveWindowCallbacksIfEmpty(window);
    }

    MaybeShutdownMirrorPipelineAfterCallbackClear(callback != nullptr, removedWindowCallbacks);

    LogDebug("glfwSetKeyCallback interposed callback=%s window=%p", callback ? "set" : "null", static_cast<void*>(window));

    if (callback) { LogDebugOnce(g_loggedGlfwSetKeyHook, "installed glfw key callback interposition"); }

    return previous;
}

extern "C" GlfwCharCallback glfwSetCharCallback(GLFWwindow* window, GlfwCharCallback callback) {
    GlfwSetCharCallbackFn realSetter = GetRealGlfwSetCharCallback();
    if (!realSetter) {
        LogOnce(g_loggedGlfwCallbackResolveFailure, "ERROR: failed to resolve glfwSetCharCallback");
        return nullptr;
    }

    platform::x11::RegisterImGuiOverlayWindow(window);

    if (!IsGlfwInputHookEnabled()) { return realSetter(window, callback); }

    GlfwCharCallback previous = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_glfwCallbackMutex);
        auto it = g_glfwCallbackMap.find(window);
        if (it != g_glfwCallbackMap.end()) { previous = it->second.character; }
    }

    GlfwCharCallback realPrevious = realSetter(window, HookedGlfwCharCallback);
    g_charCallbackInstalled.store(true, std::memory_order_release);
    if (realPrevious && realPrevious != HookedGlfwCharCallback) { previous = realPrevious; }

    bool removedWindowCallbacks = false;
    {
        std::lock_guard<std::mutex> lock(g_glfwCallbackMutex);
        GlfwCallbackState& state = g_glfwCallbackMap[window];
        state.character = callback;
        removedWindowCallbacks = RemoveWindowCallbacksIfEmpty(window);
    }

    MaybeShutdownMirrorPipelineAfterCallbackClear(callback != nullptr, removedWindowCallbacks);

    LogDebug("glfwSetCharCallback interposed callback=%s window=%p", callback ? "set" : "null", static_cast<void*>(window));

    if (callback) { LogDebugOnce(g_loggedGlfwSetCharHook, "installed glfw char callback interposition"); }

    return previous;
}

extern "C" GlfwCharModsCallback glfwSetCharModsCallback(GLFWwindow* window, GlfwCharModsCallback callback) {
    GlfwSetCharModsCallbackFn realSetter = GetRealGlfwSetCharModsCallback();
    if (!realSetter) {
        LogDebugOnce(g_loggedMissingGlfwCharModsSymbol,
                     "glfwSetCharModsCallback unavailable; leaving char-mods callback path untouched");
        return nullptr;
    }

    platform::x11::RegisterImGuiOverlayWindow(window);

    if (!IsGlfwInputHookEnabled()) { return realSetter(window, callback); }

    GlfwCharModsCallback previous = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_glfwCallbackMutex);
        auto it = g_glfwCallbackMap.find(window);
        if (it != g_glfwCallbackMap.end()) { previous = it->second.characterMods; }
    }

    GlfwCharModsCallback realPrevious = realSetter(window, HookedGlfwCharModsCallback);
    if (realPrevious && realPrevious != HookedGlfwCharModsCallback) { previous = realPrevious; }

    bool removedWindowCallbacks = false;
    {
        std::lock_guard<std::mutex> lock(g_glfwCallbackMutex);
        GlfwCallbackState& state = g_glfwCallbackMap[window];
        state.characterMods = callback;
        removedWindowCallbacks = RemoveWindowCallbacksIfEmpty(window);
    }

    MaybeShutdownMirrorPipelineAfterCallbackClear(callback != nullptr, removedWindowCallbacks);

    LogDebug("glfwSetCharModsCallback interposed callback=%s window=%p", callback ? "set" : "null", static_cast<void*>(window));

    if (callback) { LogDebugOnce(g_loggedGlfwSetCharModsHook, "installed glfw char-mods callback interposition"); }

    return previous;
}

extern "C" GlfwMouseButtonCallback glfwSetMouseButtonCallback(GLFWwindow* window, GlfwMouseButtonCallback callback) {
    GlfwSetMouseButtonCallbackFn realSetter = GetRealGlfwSetMouseButtonCallback();
    if (!realSetter) {
        LogOnce(g_loggedGlfwCallbackResolveFailure, "ERROR: failed to resolve glfwSetMouseButtonCallback");
        return nullptr;
    }

    platform::x11::RegisterImGuiOverlayWindow(window);

    if (!IsGlfwInputHookEnabled()) { return realSetter(window, callback); }

    GlfwMouseButtonCallback previous = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_glfwCallbackMutex);
        auto it = g_glfwCallbackMap.find(window);
        if (it != g_glfwCallbackMap.end()) { previous = it->second.mouseButton; }
    }

    const GlfwMouseButtonCallback dispatch = callback ? HookedGlfwMouseButtonCallback : nullptr;
    GlfwMouseButtonCallback realPrevious = realSetter(window, dispatch);
    if (realPrevious && realPrevious != HookedGlfwMouseButtonCallback) { previous = realPrevious; }

    bool removedWindowCallbacks = false;
    {
        std::lock_guard<std::mutex> lock(g_glfwCallbackMutex);
        GlfwCallbackState& state = g_glfwCallbackMap[window];
        state.mouseButton = callback;
        removedWindowCallbacks = RemoveWindowCallbacksIfEmpty(window);
    }

    MaybeShutdownMirrorPipelineAfterCallbackClear(callback != nullptr, removedWindowCallbacks);

    LogDebug("glfwSetMouseButtonCallback interposed callback=%s window=%p", callback ? "set" : "null", static_cast<void*>(window));

    if (callback) { LogDebugOnce(g_loggedGlfwSetMouseHook, "installed glfw mouse-button callback interposition"); }

    return previous;
}

extern "C" GlfwCursorPosCallback glfwSetCursorPosCallback(GLFWwindow* window, GlfwCursorPosCallback callback) {
    GlfwSetCursorPosCallbackFn realSetter = GetRealGlfwSetCursorPosCallback();
    if (!realSetter) {
        LogOnce(g_loggedGlfwCallbackResolveFailure, "ERROR: failed to resolve glfwSetCursorPosCallback");
        return nullptr;
    }

    platform::x11::RegisterImGuiOverlayWindow(window);

    if (!IsGlfwInputHookEnabled()) { return realSetter(window, callback); }

    GlfwCursorPosCallback previous = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_glfwCallbackMutex);
        auto it = g_glfwCallbackMap.find(window);
        if (it != g_glfwCallbackMap.end()) { previous = it->second.cursorPos; }
    }

    const GlfwCursorPosCallback dispatch = callback ? HookedGlfwCursorPosCallback : nullptr;
    GlfwCursorPosCallback realPrevious = realSetter(window, dispatch);
    if (realPrevious && realPrevious != HookedGlfwCursorPosCallback) { previous = realPrevious; }

    bool removedWindowCallbacks = false;
    {
        std::lock_guard<std::mutex> lock(g_glfwCallbackMutex);
        GlfwCallbackState& state = g_glfwCallbackMap[window];
        state.cursorPos = callback;
        removedWindowCallbacks = RemoveWindowCallbacksIfEmpty(window);
    }

    MaybeShutdownMirrorPipelineAfterCallbackClear(callback != nullptr, removedWindowCallbacks);

    LogDebug("glfwSetCursorPosCallback interposed callback=%s window=%p", callback ? "set" : "null", static_cast<void*>(window));

    if (callback) { LogDebugOnce(g_loggedGlfwSetCursorPosHook, "installed glfw cursor-position callback interposition"); }

    return previous;
}

extern "C" void glfwSetCursorPos(GLFWwindow* window, double xpos, double ypos) {
    GlfwSetCursorPosProc realFn = GetRealGlfwSetCursorPos();
    if (!realFn) {
        return;
    }

    double windowX = xpos;
    double windowY = ypos;
    const bool freeCursorForGameInput = !IsCursorDisabledForGameInput();
    if (freeCursorForGameInput) {
        (void)GameToWindow(xpos, ypos, windowX, windowY);
    }

    StoreTrackedRawCursorPosition(windowX, windowY);
    realFn(window, windowX, windowY);
    if (freeCursorForGameInput) {
        DispatchSyntheticGlfwCursorPosCallback(window, windowX, windowY);
    } else {
        double logicalCenterX = 0.0;
        double logicalCenterY = 0.0;
        if (ResolveCurrentLogicalCursorCenter(logicalCenterX, logicalCenterY)) {
            StoreTrackedCapturedCursorPosition(logicalCenterX, logicalCenterY);
        }
        g_trackedCursorState.captureEnterPending.store(true, std::memory_order_release);
        ResetCursorSensitivityState();
    }
}

extern "C" void glfwSetWindowIcon(GLFWwindow* window, int count, const GLFWimage* images) {
    GlfwSetWindowIconFn realFn = GetRealGlfwSetWindowIcon();
    if (!realFn) {
        LogDebug("glfwSetWindowIcon symbol unavailable; skipping icon update");
        return;
    }

#ifdef __APPLE__
    if (ShouldBypassGlfwSetWindowIconOnMac()) {
        LogOnce(g_loggedGlfwSetWindowIconBypass,
                "bypassing glfwSetWindowIcon on macOS for Java/LWJGL compatibility; override with LINUXSCREEN_MACOS_BYPASS_GLFW_ICON=0");
        return;
    }
#endif

    realFn(window, count, images);
}

#ifdef __APPLE__
extern "C" void glfwMakeContextCurrent(GLFWwindow* window) {
    GlfwMakeContextCurrentFn realFn = GetRealGlfwMakeContextCurrent();
    if (!realFn) {
        LogDebug("glfwMakeContextCurrent symbol unavailable; skipping context tracking");
        return;
    }

    realFn(window);
    if (!window) {
        return;
    }

    platform::x11::RegisterImGuiOverlayWindow(window);
    RefreshTrackedGlfwWindowMetrics(window);
    TrackGlfwWindowForCurrentContext(window, reinterpret_cast<void*>(CGLGetCurrentContext()));
}

extern "C" void glfwDestroyWindow(GLFWwindow* window) {
    GlfwDestroyWindowFn realFn = GetRealGlfwDestroyWindow();
    if (!realFn) {
        LogDebug("glfwDestroyWindow symbol unavailable; skipping cleanup");
        return;
    }

    ForgetTrackedGlfwWindow(window);
    realFn(window);
}
#endif

extern "C" GlfwScrollCallback glfwSetScrollCallback(GLFWwindow* window, GlfwScrollCallback callback) {
    GlfwSetScrollCallbackFn realSetter = GetRealGlfwSetScrollCallback();
    if (!realSetter) {
        LogOnce(g_loggedGlfwCallbackResolveFailure, "ERROR: failed to resolve glfwSetScrollCallback");
        return nullptr;
    }

    platform::x11::RegisterImGuiOverlayWindow(window);

    if (!IsGlfwInputHookEnabled()) { return realSetter(window, callback); }

    GlfwScrollCallback previous = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_glfwCallbackMutex);
        auto it = g_glfwCallbackMap.find(window);
        if (it != g_glfwCallbackMap.end()) { previous = it->second.scroll; }
    }

    const GlfwScrollCallback dispatch = callback ? HookedGlfwScrollCallback : nullptr;
    GlfwScrollCallback realPrevious = realSetter(window, dispatch);
    if (realPrevious && realPrevious != HookedGlfwScrollCallback) { previous = realPrevious; }

    bool removedWindowCallbacks = false;
    {
        std::lock_guard<std::mutex> lock(g_glfwCallbackMutex);
        GlfwCallbackState& state = g_glfwCallbackMap[window];
        state.scroll = callback;
        removedWindowCallbacks = RemoveWindowCallbacksIfEmpty(window);
    }

    MaybeShutdownMirrorPipelineAfterCallbackClear(callback != nullptr, removedWindowCallbacks);

    LogDebug("glfwSetScrollCallback interposed callback=%s window=%p", callback ? "set" : "null", static_cast<void*>(window));

    if (callback) { LogDebugOnce(g_loggedGlfwSetScrollHook, "installed glfw scroll callback interposition"); }

    return previous;
}

extern "C" GlfwWindowFocusCallback glfwSetWindowFocusCallback(GLFWwindow* window, GlfwWindowFocusCallback callback) {
    GlfwSetWindowFocusCallbackFn realSetter = GetRealGlfwSetWindowFocusCallback();
    if (!realSetter) {
        LogOnce(g_loggedGlfwCallbackResolveFailure, "ERROR: failed to resolve glfwSetWindowFocusCallback");
        return nullptr;
    }

    platform::x11::RegisterImGuiOverlayWindow(window);

    if (!IsGlfwInputHookEnabled()) { return realSetter(window, callback); }

    GlfwWindowFocusCallback previous = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_glfwCallbackMutex);
        auto it = g_glfwCallbackMap.find(window);
        if (it != g_glfwCallbackMap.end()) { previous = it->second.focus; }
    }

    const GlfwWindowFocusCallback dispatch = callback ? HookedGlfwWindowFocusCallback : nullptr;
    GlfwWindowFocusCallback realPrevious = realSetter(window, dispatch);
    if (realPrevious && realPrevious != HookedGlfwWindowFocusCallback) { previous = realPrevious; }

    bool removedWindowCallbacks = false;
    {
        std::lock_guard<std::mutex> lock(g_glfwCallbackMutex);
        GlfwCallbackState& state = g_glfwCallbackMap[window];
        state.focus = callback;
        removedWindowCallbacks = RemoveWindowCallbacksIfEmpty(window);
    }

    MaybeShutdownMirrorPipelineAfterCallbackClear(callback != nullptr, removedWindowCallbacks);

    LogDebug("glfwSetWindowFocusCallback interposed callback=%s window=%p", callback ? "set" : "null", static_cast<void*>(window));

    if (callback) { LogDebugOnce(g_loggedGlfwSetFocusHook, "installed glfw window-focus callback interposition"); }

    return previous;
}

extern "C" GlfwWindowSizeCallback glfwSetWindowSizeCallback(GLFWwindow* window, GlfwWindowSizeCallback callback) {
    GlfwSetWindowSizeCallbackFn realSetter = GetRealGlfwSetWindowSizeCallback();
    if (!realSetter) {
        LogOnce(g_loggedGlfwCallbackResolveFailure, "ERROR: failed to resolve glfwSetWindowSizeCallback");
        return nullptr;
    }

    if (window) {
        g_lastSwapWindow.store(window, std::memory_order_release);
    }
    platform::x11::RegisterImGuiOverlayWindow(window);

    GlfwWindowSizeCallback previous = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_glfwCallbackMutex);
        auto it = g_glfwCallbackMap.find(window);
        if (it != g_glfwCallbackMap.end()) {
            previous = it->second.windowSize;
        }
    }

    const GlfwWindowSizeCallback dispatch = callback ? HookedGlfwWindowSizeCallback : nullptr;
    GlfwWindowSizeCallback realPrevious = realSetter(window, dispatch);
    if (realPrevious && realPrevious != HookedGlfwWindowSizeCallback) {
        previous = realPrevious;
    }

    bool removedWindowCallbacks = false;
    {
        std::lock_guard<std::mutex> lock(g_glfwCallbackMutex);
        GlfwCallbackState& state = g_glfwCallbackMap[window];
        state.windowSize = callback;
        removedWindowCallbacks = RemoveWindowCallbacksIfEmpty(window);
    }

    MaybeShutdownMirrorPipelineAfterCallbackClear(callback != nullptr, removedWindowCallbacks);

    LogDebug("glfwSetWindowSizeCallback interposed callback=%s window=%p", callback ? "set" : "null", static_cast<void*>(window));
    if (callback) {
        LogDebugOnce(g_loggedGlfwSetWindowSizeHook, "installed glfw window-size callback interposition");
    }

    return previous;
}

extern "C" GlfwFramebufferSizeCallback glfwSetFramebufferSizeCallback(GLFWwindow* window, GlfwFramebufferSizeCallback callback) {
    GlfwSetFramebufferSizeCallbackFn realSetter = GetRealGlfwSetFramebufferSizeCallback();
    if (!realSetter) {
        LogOnce(g_loggedGlfwCallbackResolveFailure, "ERROR: failed to resolve glfwSetFramebufferSizeCallback");
        return nullptr;
    }

    if (window) {
        g_lastSwapWindow.store(window, std::memory_order_release);
    }
    platform::x11::RegisterImGuiOverlayWindow(window);

    GlfwFramebufferSizeCallback previous = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_glfwCallbackMutex);
        auto it = g_glfwCallbackMap.find(window);
        if (it != g_glfwCallbackMap.end()) {
            previous = it->second.framebufferSize;
        }
    }

    const GlfwFramebufferSizeCallback dispatch = callback ? HookedGlfwFramebufferSizeCallback : nullptr;
    GlfwFramebufferSizeCallback realPrevious = realSetter(window, dispatch);
    if (realPrevious && realPrevious != HookedGlfwFramebufferSizeCallback) {
        previous = realPrevious;
    }

    bool removedWindowCallbacks = false;
    {
        std::lock_guard<std::mutex> lock(g_glfwCallbackMutex);
        GlfwCallbackState& state = g_glfwCallbackMap[window];
        state.framebufferSize = callback;
        removedWindowCallbacks = RemoveWindowCallbacksIfEmpty(window);
    }

    MaybeShutdownMirrorPipelineAfterCallbackClear(callback != nullptr, removedWindowCallbacks);

    LogDebug("glfwSetFramebufferSizeCallback interposed callback=%s window=%p",
             callback ? "set" : "null",
             static_cast<void*>(window));
    if (callback) {
        LogDebugOnce(g_loggedGlfwSetFramebufferSizeHook, "installed glfw framebuffer-size callback interposition");
    }

    return previous;
}

#define GLFW_CURSOR          0x00033001
#define GLFW_CURSOR_NORMAL   0x00034001
#define GLFW_CURSOR_HIDDEN   0x00034002
#define GLFW_CURSOR_DISABLED 0x00034003

extern "C" void glfwSetInputMode(GLFWwindow* window, int mode, int value) {
    GlfwSetInputModeFn realFn = GetRealGlfwSetInputMode();
    if (!realFn) { return; }

    if (mode != GLFW_CURSOR) {
        realFn(window, mode, value);
        return;
    }

    const bool cursorDisabledNext = value == GLFW_CURSOR_DISABLED;
    const bool cursorDisabledPrev = g_gameWantsCursorDisabled.exchange(cursorDisabledNext, std::memory_order_acq_rel);
    if (cursorDisabledPrev != cursorDisabledNext) {
        ResetCursorSensitivityState();
    }

    if (value == GLFW_CURSOR_DISABLED) {
        if (platform::x11::IsGuiVisible()) {
            g_cursorCaptureActive.store(false, std::memory_order_release);
            g_restoreCursorDisabledOnGuiClose.store(true, std::memory_order_release);
            ClearTrackedCursorCaptureState();
            LogDebug("glfwSetInputMode CURSOR_DISABLED suppressed (GUI visible)");
            return;
        }
        if (!g_cursorCaptureActive.load(std::memory_order_acquire)) {
            PrepareTrackedCursorForCapture(window);
        }
        double centerX = 0.0;
        double centerY = 0.0;
        if (ResolveCurrentWindowCursorCenter(centerX, centerY)) {
            ArmCaptureEntryCenterSuppression(centerX, centerY);
        } else {
            ClearCaptureEntryCenterSuppression();
        }
        ClearPendingSyntheticCursorPosCallbackState();
        g_cursorCaptureActive.store(true, std::memory_order_release);
        realFn(window, mode, value);
        return;
    }

    g_restoreCursorDisabledOnGuiClose.store(false, std::memory_order_release);
    g_cursorCaptureActive.store(false, std::memory_order_release);
    ClearTrackedCursorCaptureState();
    realFn(window, mode, value);
    if (cursorDisabledPrev) {
        double centerX = 0.0;
        double centerY = 0.0;
        if (ResolveCurrentWindowCursorCenter(centerX, centerY)) {
            GlfwSetCursorPosProc realSetCursorPos = GetRealGlfwSetCursorPos();
            if (realSetCursorPos) {
                StoreTrackedRawCursorPosition(centerX, centerY);
                realSetCursorPos(window, centerX, centerY);
                ArmPendingSyntheticCursorPosCallback(window, centerX, centerY);
                LogAlways("forced unlock cursor center window=(%.3f, %.3f)", centerX, centerY);
            }
        }
    }
}

extern "C" void glfwGetCursorPos(GLFWwindow* window, double* xpos, double* ypos) {
    if (IsCursorDisabledForGameInput()) {
        double logicalX = 0.0;
        double logicalY = 0.0;
        if (!ResolveCurrentLogicalCursorCenter(logicalX, logicalY)) {
            PrepareTrackedCursorForCapture(window);
            if (!ResolveCurrentLogicalCursorCenter(logicalX, logicalY)) {
                logicalX = 0.0;
                logicalY = 0.0;
            }
        }

        if (xpos) {
            *xpos = logicalX;
        }
        if (ypos) {
            *ypos = logicalY;
        }
        return;
    }

    GlfwGetCursorPosProc realFn = GetRealGlfwGetCursorPos();
    if (!realFn && !g_pendingSyntheticCursorPosCallback.valid.load(std::memory_order_acquire)) {
        if (xpos) {
            *xpos = 0.0;
        }
        if (ypos) {
            *ypos = 0.0;
        }
        return;
    }

    double rawX = 0.0;
    double rawY = 0.0;
    const bool havePendingSyntheticPosition = g_pendingSyntheticCursorPosCallback.valid.load(std::memory_order_acquire) &&
                                              g_pendingSyntheticCursorPosCallback.window.load(std::memory_order_relaxed) == window;
    if (havePendingSyntheticPosition) {
        rawX = g_pendingSyntheticCursorPosCallback.rawX.load(std::memory_order_relaxed);
        rawY = g_pendingSyntheticCursorPosCallback.rawY.load(std::memory_order_relaxed);
    } else {
        if (!realFn) {
            if (xpos) {
                *xpos = 0.0;
            }
            if (ypos) {
                *ypos = 0.0;
            }
            return;
        }

        double* rawXPtr = xpos ? xpos : &rawX;
        double* rawYPtr = ypos ? ypos : &rawY;
        realFn(window, rawXPtr, rawYPtr);

        rawX = *rawXPtr;
        rawY = *rawYPtr;
    }
    StoreTrackedRawCursorPosition(rawX, rawY);

    double mappedX = rawX;
    double mappedY = rawY;
    (void)WindowToGame(rawX, rawY, mappedX, mappedY);

    if (xpos) {
        *xpos = mappedX;
    }
    if (ypos) {
        *ypos = mappedY;
    }
}

extern "C" int glfwGetKey(GLFWwindow* window, int key) {
    GlfwGetKeyFn realFn = GetRealGlfwGetKey();
    if (!realFn) {
        return static_cast<int>(platform::input::GlfwAction::Release);
    }

    const int realState = realFn(window, key);
    if (!window || key < 0) {
        return realState;
    }

    std::lock_guard<std::mutex> lock(g_syntheticRebindKeyMutex);
    auto windowIt = g_syntheticRebindKeyStates.find(window);
    if (windowIt == g_syntheticRebindKeyStates.end()) {
        return realState;
    }

    const SyntheticRebindKeyState& synthetic = windowIt->second;
    if (synthetic.sourceToTarget.find(key) != synthetic.sourceToTarget.end()) {
        return static_cast<int>(platform::input::GlfwAction::Release);
    }

    auto suppressedIt = synthetic.suppressedKeyRefCount.find(key);
    if (suppressedIt != synthetic.suppressedKeyRefCount.end() && suppressedIt->second > 0) {
        return static_cast<int>(platform::input::GlfwAction::Release);
    }

    auto targetIt = synthetic.targetPressCount.find(key);
    if (targetIt != synthetic.targetPressCount.end() && targetIt->second > 0) {
        return static_cast<int>(platform::input::GlfwAction::Press);
    }

    return realState;
}

#ifndef __APPLE__
extern "C" __GLXextFuncPtr glXGetProcAddress(const GLubyte* procName) {
    return ResolveProcAddressRequest(procName, GetRealGlXGetProcAddress());
}

extern "C" __GLXextFuncPtr glXGetProcAddressARB(const GLubyte* procName) {
    return ResolveProcAddressRequest(procName, GetRealGlXGetProcAddressARB());
}
#endif

#ifdef __APPLE__
static void* my_dlsym(void* handle, const char* symbolName) {
    auto realFn = [](void* h, const char* s) -> void* { return dlsym(h, s); };
    if (g_bypassDlsymInterpose) { return realFn(handle, symbolName); }
    DlsymReentryGuard guard;
    if (!guard.entered) { return realFn(handle, symbolName); }

    if (std::strcmp(symbolName, "dlsym") == 0) { return reinterpret_cast<void*>(my_dlsym); }
#else
extern "C" void* dlsym(void* handle, const char* symbolName) {
    DlSymFn realFn = GetRealDlSym();
    if (!realFn) { return nullptr; }

    DlsymReentryGuard guard;
    if (!guard.entered) { return realFn(handle, symbolName); }

    if (std::strcmp(symbolName, "dlsym") == 0) { return reinterpret_cast<void*>(realFn); }
#endif

    if (std::strncmp(symbolName, "glfw", 4) == 0 && handle != nullptr && handle != RTLD_DEFAULT && handle != RTLD_NEXT) {
        void* previousHandle = g_glfwResolverHandle.exchange(handle, std::memory_order_acq_rel);
        if (previousHandle != handle) {
            g_realGlfwSwapBuffers.store(nullptr, std::memory_order_release);
            g_realGlfwSetKeyCallback.store(nullptr, std::memory_order_release);
            g_realGlfwSetCharCallback.store(nullptr, std::memory_order_release);
            g_realGlfwSetCharModsCallback.store(nullptr, std::memory_order_release);
            g_realGlfwSetMouseButtonCallback.store(nullptr, std::memory_order_release);
            g_realGlfwSetCursorPosCallback.store(nullptr, std::memory_order_release);
            g_realGlfwSetScrollCallback.store(nullptr, std::memory_order_release);
            g_realGlfwSetWindowFocusCallback.store(nullptr, std::memory_order_release);
            g_realGlfwSetWindowSizeCallback.store(nullptr, std::memory_order_release);
            g_realGlfwSetFramebufferSizeCallback.store(nullptr, std::memory_order_release);
            g_realGlfwSetWindowSize.store(nullptr, std::memory_order_release);
            g_realGlfwSetWindowIcon.store(nullptr, std::memory_order_release);
            g_realGlfwGetWindowSize.store(nullptr, std::memory_order_release);
            g_realGlfwGetFramebufferSize.store(nullptr, std::memory_order_release);
#ifdef __APPLE__
            g_realGlfwMakeContextCurrent.store(nullptr, std::memory_order_release);
            g_realGlfwDestroyWindow.store(nullptr, std::memory_order_release);
#endif
            g_realGlfwGetKey.store(nullptr, std::memory_order_release);
            g_realGlfwGetKeyScancode.store(nullptr, std::memory_order_release);
            g_realGlfwGetCursorPos.store(nullptr, std::memory_order_release);
            g_realGlfwSetCursorPos.store(nullptr, std::memory_order_release);
            g_realGlfwSetInputMode.store(nullptr, std::memory_order_release);
            LogDebug("captured glfw resolver handle=%p and reset cached glfw symbols", handle);
        }
    }

#ifndef __APPLE__
    if (std::strcmp(symbolName, "glXSwapBuffers") == 0) {
        LogDebugOnce(g_loggedDlSymHookSwap, "dlsym requested glXSwapBuffers; returning interposed function");
        return reinterpret_cast<void*>(glXSwapBuffers);
    }

    if (std::strcmp(symbolName, "glXSwapBuffersMscOML") == 0) {
        LogDebugOnce(g_loggedDlSymHookMsc, "dlsym requested glXSwapBuffersMscOML; returning interposed function");
        return reinterpret_cast<void*>(glXSwapBuffersMscOML);
    }

    if (std::strcmp(symbolName, "glfwSwapBuffers") == 0) {
        LogDebugOnce(g_loggedDlSymHookGlfw, "dlsym requested glfwSwapBuffers; returning interposed function");
        return reinterpret_cast<void*>(glfwSwapBuffers);
    }
#endif

    if (std::strcmp(symbolName, "glViewport") == 0) {
        return reinterpret_cast<void*>(glViewport);
    }

    if (std::strcmp(symbolName, "glScissor") == 0) {
        return reinterpret_cast<void*>(glScissor);
    }

    if (std::strcmp(symbolName, "glBindFramebuffer") == 0) {
        return reinterpret_cast<void*>(glBindFramebuffer);
    }

    if (std::strcmp(symbolName, "glfwSetWindowSizeCallback") == 0) {
        return reinterpret_cast<void*>(glfwSetWindowSizeCallback);
    }

    if (std::strcmp(symbolName, "glfwSetFramebufferSizeCallback") == 0) {
        return reinterpret_cast<void*>(glfwSetFramebufferSizeCallback);
    }

    if (std::strcmp(symbolName, "glfwGetCursorPos") == 0) {
        return reinterpret_cast<void*>(glfwGetCursorPos);
    }

    if (std::strcmp(symbolName, "glfwSetCursorPos") == 0) {
        return reinterpret_cast<void*>(glfwSetCursorPos);
    }

    if (std::strcmp(symbolName, "glfwSetWindowIcon") == 0) {
        return reinterpret_cast<void*>(glfwSetWindowIcon);
    }

#ifdef __APPLE__
    if (std::strcmp(symbolName, "glfwMakeContextCurrent") == 0) {
        return reinterpret_cast<void*>(glfwMakeContextCurrent);
    }

    if (std::strcmp(symbolName, "glfwDestroyWindow") == 0) {
        return reinterpret_cast<void*>(glfwDestroyWindow);
    }
#endif

    if (std::strcmp(symbolName, "glfwGetKey") == 0) {
        return reinterpret_cast<void*>(glfwGetKey);
    }

    if (std::strcmp(symbolName, "glfwSetInputMode") == 0) {
        return reinterpret_cast<void*>(glfwSetInputMode);
    }

#ifndef __APPLE__
    if (std::strcmp(symbolName, "glXGetProcAddress") == 0) {
        LogDebugOnce(g_loggedDlSymHookGetProc, "dlsym requested glXGetProcAddress; returning interposed function");
        return reinterpret_cast<void*>(glXGetProcAddress);
    }

    if (std::strcmp(symbolName, "glXGetProcAddressARB") == 0) {
        LogDebugOnce(g_loggedDlSymHookGetProcArb, "dlsym requested glXGetProcAddressARB; returning interposed function");
        return reinterpret_cast<void*>(glXGetProcAddressARB);
    }
#endif

    if (std::strcmp(symbolName, "glfwSetKeyCallback") == 0) {
        if (IsGlfwInputHookEnabled()) {
            LogDebugOnce(g_loggedGlfwSetKeyHook, "dlsym requested glfwSetKeyCallback; returning interposed function");
            return reinterpret_cast<void*>(glfwSetKeyCallback);
        }
        return realFn(handle, symbolName);
    }

    if (std::strcmp(symbolName, "glfwSetCharCallback") == 0) {
        if (IsGlfwInputHookEnabled()) {
            LogDebugOnce(g_loggedGlfwSetCharHook, "dlsym requested glfwSetCharCallback; returning interposed function");
            return reinterpret_cast<void*>(glfwSetCharCallback);
        }
        return realFn(handle, symbolName);
    }

    if (std::strcmp(symbolName, "glfwSetCharModsCallback") == 0) {
        if (IsGlfwInputHookEnabled()) {
            if (GetRealGlfwSetCharModsCallback()) {
                LogDebugOnce(g_loggedGlfwSetCharModsHook, "dlsym requested glfwSetCharModsCallback; returning interposed function");
                return reinterpret_cast<void*>(glfwSetCharModsCallback);
            }
        }
        return realFn(handle, symbolName);
    }

    if (std::strcmp(symbolName, "glfwSetMouseButtonCallback") == 0) {
        if (IsGlfwInputHookEnabled()) {
            LogDebugOnce(g_loggedGlfwSetMouseHook, "dlsym requested glfwSetMouseButtonCallback; returning interposed function");
            return reinterpret_cast<void*>(glfwSetMouseButtonCallback);
        }
        return realFn(handle, symbolName);
    }

    if (std::strcmp(symbolName, "glfwSetCursorPosCallback") == 0) {
        if (IsGlfwInputHookEnabled()) {
            LogDebugOnce(g_loggedGlfwSetCursorPosHook, "dlsym requested glfwSetCursorPosCallback; returning interposed function");
            return reinterpret_cast<void*>(glfwSetCursorPosCallback);
        }
        return realFn(handle, symbolName);
    }

    if (std::strcmp(symbolName, "glfwSetScrollCallback") == 0) {
        if (IsGlfwInputHookEnabled()) {
            LogDebugOnce(g_loggedGlfwSetScrollHook, "dlsym requested glfwSetScrollCallback; returning interposed function");
            return reinterpret_cast<void*>(glfwSetScrollCallback);
        }
        return realFn(handle, symbolName);
    }

    if (std::strcmp(symbolName, "glfwSetWindowFocusCallback") == 0) {
        if (IsGlfwInputHookEnabled()) {
            LogDebugOnce(g_loggedGlfwSetFocusHook, "dlsym requested glfwSetWindowFocusCallback; returning interposed function");
            return reinterpret_cast<void*>(glfwSetWindowFocusCallback);
        }
        return realFn(handle, symbolName);
    }

    return realFn(handle, symbolName);
}
