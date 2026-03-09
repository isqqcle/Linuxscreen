bool RemoveWindowCallbacksIfEmpty(GLFWwindow* window) {
    if (!window) { return false; }

    auto it = g_glfwCallbackMap.find(window);
    if (it == g_glfwCallbackMap.end()) { return false; }

    const bool hasAny = it->second.key || it->second.character || it->second.characterMods || it->second.mouseButton ||
                        it->second.cursorPos || it->second.scroll || it->second.focus || it->second.windowSize ||
                        it->second.framebufferSize;
    if (!hasAny) {
        g_glfwCallbackMap.erase(it);
        return true;
    }
    return false;
}

void MaybeShutdownMirrorPipelineAfterCallbackClear(bool callbackSet, bool removedWindowCallbacks) {
    if (callbackSet) { return; }
    if (!removedWindowCallbacks) { return; }
    if (!platform::x11::IsGlxMirrorPipelineEnabled()) { return; }
    platform::x11::ShutdownGlxMirrorPipeline();
}

struct ReentryGuard {
    bool entered = false;

    ReentryGuard() {
        if (!g_inSwapHook) {
            g_inSwapHook = true;
            entered = true;
        }
    }

    ~ReentryGuard() {
        if (entered) { g_inSwapHook = false; }
    }
};

struct DlsymReentryGuard {
    bool entered = false;

    DlsymReentryGuard() {
        if (!g_inDlsymHook) {
            g_inDlsymHook = true;
            entered = true;
        }
    }

    ~DlsymReentryGuard() {
        if (entered) { g_inDlsymHook = false; }
    }
};

void LogResolveFailureDetail(const char* source, const char* errorText) {
    if (!IsDebugEnabled()) { return; }
    if (errorText && errorText[0] != '\0') {
        LogDebug("symbol resolve failure at %s: %s", source, errorText);
    } else {
        LogDebug("symbol resolve failure at %s: unknown reason", source);
    }
}

void EnsureLibGlHandle() {
#ifndef __APPLE__
    std::call_once(g_libGlOpenOnce, []() {
        void* handle = dlopen("libGL.so.1", RTLD_NOW | RTLD_LOCAL);
        g_libGlHandle.store(handle, std::memory_order_release);
        if (!handle) {
            const char* openErr = dlerror();
            LogResolveFailureDetail("dlopen(libGL.so.1)", openErr);
        }
    });
#endif
}

void EnsureLibGlfwHandle() {
#ifndef __APPLE__
    std::call_once(g_libGlfwOpenOnce, []() {
        void* handle = dlopen("libglfw.so.3", RTLD_NOW | RTLD_LOCAL);
        if (!handle) { handle = dlopen("libglfw.so", RTLD_NOW | RTLD_LOCAL); }
        g_libGlfwHandle.store(handle, std::memory_order_release);

        if (!handle) {
            const char* openErr = dlerror();
            LogResolveFailureDetail("dlopen(libglfw.so.3/libglfw.so)", openErr);
        }
    });
#endif
}

void* GetGlfwFallbackHandle(const char*& label) {
    void* hinted = g_glfwResolverHandle.load(std::memory_order_acquire);
    if (hinted != nullptr) {
        label = "captured glfw handle";
        return hinted;
    }

    EnsureLibGlfwHandle();
    label = "libglfw.so.3/libglfw.so";
    return g_libGlfwHandle.load(std::memory_order_acquire);
}

void* GetGlfwCapturedHandle(const char*& label) {
    void* hinted = g_glfwResolverHandle.load(std::memory_order_acquire);
    if (hinted != nullptr) {
        label = "captured glfw handle";
        return hinted;
    }

    label = "no captured glfw handle";
    return nullptr;
}

void EnsureLibDlHandle() {
#ifndef __APPLE__
    std::call_once(g_libDlOpenOnce, []() {
        void* handle = dlopen("libdl.so.2", RTLD_NOW | RTLD_LOCAL);
        if (!handle) { handle = dlopen("libdl.so", RTLD_NOW | RTLD_LOCAL); }
        g_libDlHandle.store(handle, std::memory_order_release);

        if (!handle) {
            const char* openErr = dlerror();
            LogResolveFailureDetail("dlopen(libdl.so.2/libdl.so)", openErr);
        }
    });
#endif
}

void ResolveRealDlSym() {
    if (g_realDlSym.load(std::memory_order_acquire) != nullptr) { return; }

    DlSymFn resolved = nullptr;
#if defined(__APPLE__)
    g_bypassDlsymInterpose = true;
    resolved = reinterpret_cast<DlSymFn>(dlsym(RTLD_NEXT, "dlsym"));
    g_bypassDlsymInterpose = false;
#elif defined(__GLIBC__)
    static const char* kVersions[] = { "GLIBC_2.34", "GLIBC_2.33", "GLIBC_2.32", "GLIBC_2.2.5" };
    for (const char* version : kVersions) {
        resolved = reinterpret_cast<DlSymFn>(dlvsym(RTLD_NEXT, "dlsym", version));
        if (resolved) { break; }
    }
#endif

    if (!resolved) {
        EnsureLibDlHandle();
#if defined(__GLIBC__)
        void* libdl = g_libDlHandle.load(std::memory_order_acquire);
        if (libdl) {
            static const char* kVersions[] = { "GLIBC_2.34", "GLIBC_2.33", "GLIBC_2.32", "GLIBC_2.2.5" };
            for (const char* version : kVersions) {
                resolved = reinterpret_cast<DlSymFn>(dlvsym(libdl, "dlsym", version));
                if (resolved) { break; }
            }
        }
#endif
    }

    g_realDlSym.store(resolved, std::memory_order_release);

    if (resolved) {
        LogDebug("resolved real dlsym symbol at %p", reinterpret_cast<void*>(resolved));
    } else {
        LogOnce(g_loggedDlSymResolveFailure, "ERROR: failed to resolve real dlsym; dlsym interposition disabled");
    }
}

DlSymFn GetRealDlSym() {
    if (!g_realDlSym.load(std::memory_order_acquire)) { ResolveRealDlSym(); }
    return g_realDlSym.load(std::memory_order_acquire);
}

void* CallRealDlSym(void* handle, const char* symbolName) {
#ifdef __APPLE__
    g_bypassDlsymInterpose = true;
    void* symbol = dlsym(handle, symbolName);
    g_bypassDlsymInterpose = false;
    return symbol;
#else
    DlSymFn realFn = GetRealDlSym();
    if (!realFn) { return nullptr; }
    return realFn(handle, symbolName);
#endif
}

void* ResolveSymbol(const char* symbolName, void* fallbackHandle, const char* fallbackLabel) {
    dlerror();
    void* symbol = CallRealDlSym(RTLD_NEXT, symbolName);
    const char* nextErr = dlerror();
    if (symbol) { return symbol; }

    if (!fallbackHandle) {
        LogResolveFailureDetail("RTLD_NEXT", nextErr);
        return nullptr;
    }

    dlerror();
    symbol = CallRealDlSym(fallbackHandle, symbolName);
    const char* fallbackErr = dlerror();
    if (!symbol) {
        LogResolveFailureDetail("RTLD_NEXT", nextErr);
        LogResolveFailureDetail(fallbackLabel, fallbackErr);
    }
    return symbol;
}

bool IsRequestedProcName(const GLubyte* procName, const char* expectedName) {
    if (!procName || !expectedName) { return false; }
    return std::strcmp(reinterpret_cast<const char*>(procName), expectedName) == 0;
}

const char* RuntimeStateToString(platform::RuntimeState state) {
    switch (state) {
    case platform::RuntimeState::Uninitialized:
        return "Uninitialized";
    case platform::RuntimeState::Initialized:
        return "Initialized";
    case platform::RuntimeState::HooksInstalled:
        return "HooksInstalled";
    case platform::RuntimeState::ShuttingDown:
        return "ShuttingDown";
    case platform::RuntimeState::Failed:
        return "Failed";
    }
    return "Unknown";
}

#ifndef __APPLE__
void ResolveRealGlXSwapBuffers() {
    EnsureLibGlHandle();
    void* symbol = ResolveSymbol("glXSwapBuffers", g_libGlHandle.load(std::memory_order_acquire), "libGL.so.1");

    g_realGlXSwapBuffers.store(reinterpret_cast<GlXSwapBuffersFn>(symbol), std::memory_order_release);
    if (symbol) {
        LogOnce(g_loggedBootstrap, "preload bootstrap active (glXSwapBuffers interposed)");
        LogDebug("resolved real glXSwapBuffers symbol at %p", symbol);
    } else {
        LogOnce(g_loggedResolveFailure, "ERROR: failed to resolve real glXSwapBuffers");
    }
}

void ResolveRealGlXSwapBuffersMscOML() {
    EnsureLibGlHandle();
    void* symbol = ResolveSymbol("glXSwapBuffersMscOML", g_libGlHandle.load(std::memory_order_acquire), "libGL.so.1");
    g_realGlXSwapBuffersMscOML.store(reinterpret_cast<GlXSwapBuffersMscOMLFn>(symbol), std::memory_order_release);

    if (symbol) {
        LogDebug("resolved real glXSwapBuffersMscOML symbol at %p", symbol);
    } else {
        LogDebug("glXSwapBuffersMscOML symbol unavailable in current process");
    }
}
#endif

void ResolveRealGlfwSwapBuffers() {
    const char* fallbackLabel = nullptr;
    void* fallbackHandle = GetGlfwFallbackHandle(fallbackLabel);
    void* symbol = ResolveSymbol("glfwSwapBuffers", fallbackHandle, fallbackLabel);
    g_realGlfwSwapBuffers.store(reinterpret_cast<GlfwSwapBuffersFn>(symbol), std::memory_order_release);

    if (symbol) {
        LogDebug("resolved real glfwSwapBuffers symbol at %p", symbol);
    } else {
        LogDebug("glfwSwapBuffers symbol unavailable in current process");
    }
}

void ResolveRealGlfwSetKeyCallback() {
    const char* fallbackLabel = nullptr;
    void* fallbackHandle = GetGlfwCapturedHandle(fallbackLabel);
    void* symbol = ResolveSymbol("glfwSetKeyCallback", fallbackHandle, fallbackLabel);
    g_realGlfwSetKeyCallback.store(reinterpret_cast<GlfwSetKeyCallbackFn>(symbol), std::memory_order_release);
    if (symbol) {
        LogDebug("resolved real glfwSetKeyCallback symbol at %p", symbol);
    } else {
        LogDebug("glfwSetKeyCallback symbol unavailable in current process");
    }
}

void ResolveRealGlfwSetCharCallback() {
    const char* fallbackLabel = nullptr;
    void* fallbackHandle = GetGlfwCapturedHandle(fallbackLabel);
    void* symbol = ResolveSymbol("glfwSetCharCallback", fallbackHandle, fallbackLabel);
    g_realGlfwSetCharCallback.store(reinterpret_cast<GlfwSetCharCallbackFn>(symbol), std::memory_order_release);
    if (symbol) {
        LogDebug("resolved real glfwSetCharCallback symbol at %p", symbol);
    } else {
        LogDebug("glfwSetCharCallback symbol unavailable in current process");
    }
}

void ResolveRealGlfwSetCharModsCallback() {
    const char* fallbackLabel = nullptr;
    void* fallbackHandle = GetGlfwCapturedHandle(fallbackLabel);
    void* symbol = ResolveSymbol("glfwSetCharModsCallback", fallbackHandle, fallbackLabel);
    g_realGlfwSetCharModsCallback.store(reinterpret_cast<GlfwSetCharModsCallbackFn>(symbol), std::memory_order_release);
    if (symbol) {
        LogDebug("resolved real glfwSetCharModsCallback symbol at %p", symbol);
    } else {
        LogDebugOnce(g_loggedMissingGlfwCharModsSymbol,
                     "glfwSetCharModsCallback symbol unavailable in current process (char-mods remap disabled)");
    }
}

void ResolveRealGlfwSetMouseButtonCallback() {
    const char* fallbackLabel = nullptr;
    void* fallbackHandle = GetGlfwCapturedHandle(fallbackLabel);
    void* symbol = ResolveSymbol("glfwSetMouseButtonCallback", fallbackHandle, fallbackLabel);
    g_realGlfwSetMouseButtonCallback.store(reinterpret_cast<GlfwSetMouseButtonCallbackFn>(symbol), std::memory_order_release);
    if (symbol) {
        LogDebug("resolved real glfwSetMouseButtonCallback symbol at %p", symbol);
    } else {
        LogDebug("glfwSetMouseButtonCallback symbol unavailable in current process");
    }
}

void ResolveRealGlfwSetCursorPosCallback() {
    const char* fallbackLabel = nullptr;
    void* fallbackHandle = GetGlfwCapturedHandle(fallbackLabel);
    void* symbol = ResolveSymbol("glfwSetCursorPosCallback", fallbackHandle, fallbackLabel);
    g_realGlfwSetCursorPosCallback.store(reinterpret_cast<GlfwSetCursorPosCallbackFn>(symbol), std::memory_order_release);
    if (symbol) {
        LogDebug("resolved real glfwSetCursorPosCallback symbol at %p", symbol);
    } else {
        LogDebug("glfwSetCursorPosCallback symbol unavailable in current process");
    }
}

void ResolveRealGlfwSetScrollCallback() {
    const char* fallbackLabel = nullptr;
    void* fallbackHandle = GetGlfwCapturedHandle(fallbackLabel);
    void* symbol = ResolveSymbol("glfwSetScrollCallback", fallbackHandle, fallbackLabel);
    g_realGlfwSetScrollCallback.store(reinterpret_cast<GlfwSetScrollCallbackFn>(symbol), std::memory_order_release);
    if (symbol) {
        LogDebug("resolved real glfwSetScrollCallback symbol at %p", symbol);
    } else {
        LogDebug("glfwSetScrollCallback symbol unavailable in current process");
    }
}

void ResolveRealGlfwSetWindowFocusCallback() {
    const char* fallbackLabel = nullptr;
    void* fallbackHandle = GetGlfwCapturedHandle(fallbackLabel);
    void* symbol = ResolveSymbol("glfwSetWindowFocusCallback", fallbackHandle, fallbackLabel);
    g_realGlfwSetWindowFocusCallback.store(reinterpret_cast<GlfwSetWindowFocusCallbackFn>(symbol), std::memory_order_release);
    if (symbol) {
        LogDebug("resolved real glfwSetWindowFocusCallback symbol at %p", symbol);
    } else {
        LogDebug("glfwSetWindowFocusCallback symbol unavailable in current process");
    }
}

void ResolveRealGlfwSetWindowSizeCallback() {
    const char* fallbackLabel = nullptr;
    void* fallbackHandle = GetGlfwCapturedHandle(fallbackLabel);
    void* symbol = ResolveSymbol("glfwSetWindowSizeCallback", fallbackHandle, fallbackLabel);
    g_realGlfwSetWindowSizeCallback.store(reinterpret_cast<GlfwSetWindowSizeCallbackFn>(symbol), std::memory_order_release);
    if (symbol) {
        LogDebug("resolved real glfwSetWindowSizeCallback symbol at %p", symbol);
    } else {
        LogDebug("glfwSetWindowSizeCallback symbol unavailable in current process");
    }
}

void ResolveRealGlfwSetFramebufferSizeCallback() {
    const char* fallbackLabel = nullptr;
    void* fallbackHandle = GetGlfwCapturedHandle(fallbackLabel);
    void* symbol = ResolveSymbol("glfwSetFramebufferSizeCallback", fallbackHandle, fallbackLabel);
    g_realGlfwSetFramebufferSizeCallback.store(reinterpret_cast<GlfwSetFramebufferSizeCallbackFn>(symbol), std::memory_order_release);
    if (symbol) {
        LogDebug("resolved real glfwSetFramebufferSizeCallback symbol at %p", symbol);
    } else {
        LogDebug("glfwSetFramebufferSizeCallback symbol unavailable in current process");
    }
}

void ResolveRealGlfwSetWindowSize() {
    const char* fallbackLabel = nullptr;
    void* fallbackHandle = GetGlfwCapturedHandle(fallbackLabel);
    void* symbol = ResolveSymbol("glfwSetWindowSize", fallbackHandle, fallbackLabel);
    g_realGlfwSetWindowSize.store(reinterpret_cast<GlfwSetWindowSizeFn>(symbol), std::memory_order_release);
    if (symbol) {
        LogDebug("resolved real glfwSetWindowSize symbol at %p", symbol);
    } else {
        LogDebug("glfwSetWindowSize symbol unavailable in current process");
    }
}

void ResolveRealGlfwSetWindowIcon() {
    const char* fallbackLabel = nullptr;
    void* fallbackHandle = GetGlfwCapturedHandle(fallbackLabel);
    void* symbol = ResolveSymbol("glfwSetWindowIcon", fallbackHandle, fallbackLabel);
    g_realGlfwSetWindowIcon.store(reinterpret_cast<GlfwSetWindowIconFn>(symbol), std::memory_order_release);
    if (symbol) {
        LogDebug("resolved real glfwSetWindowIcon symbol at %p", symbol);
    } else {
        LogDebug("glfwSetWindowIcon symbol unavailable in current process");
    }
}

#ifdef __APPLE__
void ResolveRealGlfwMakeContextCurrent() {
    const char* fallbackLabel = nullptr;
    void* fallbackHandle = GetGlfwCapturedHandle(fallbackLabel);
    void* symbol = ResolveSymbol("glfwMakeContextCurrent", fallbackHandle, fallbackLabel);
    g_realGlfwMakeContextCurrent.store(reinterpret_cast<GlfwMakeContextCurrentFn>(symbol), std::memory_order_release);
    if (symbol) {
        LogDebug("resolved real glfwMakeContextCurrent symbol at %p", symbol);
    } else {
        LogDebug("glfwMakeContextCurrent symbol unavailable in current process");
    }
}

void ResolveRealGlfwDestroyWindow() {
    const char* fallbackLabel = nullptr;
    void* fallbackHandle = GetGlfwCapturedHandle(fallbackLabel);
    void* symbol = ResolveSymbol("glfwDestroyWindow", fallbackHandle, fallbackLabel);
    g_realGlfwDestroyWindow.store(reinterpret_cast<GlfwDestroyWindowFn>(symbol), std::memory_order_release);
    if (symbol) {
        LogDebug("resolved real glfwDestroyWindow symbol at %p", symbol);
    } else {
        LogDebug("glfwDestroyWindow symbol unavailable in current process");
    }
}
#endif

void ResolveRealGlfwGetWindowSize() {
    const char* fallbackLabel = nullptr;
    void* fallbackHandle = GetGlfwCapturedHandle(fallbackLabel);
    void* symbol = ResolveSymbol("glfwGetWindowSize", fallbackHandle, fallbackLabel);
    g_realGlfwGetWindowSize.store(reinterpret_cast<GlfwGetWindowSizeFn>(symbol), std::memory_order_release);
    if (symbol) {
        LogDebug("resolved real glfwGetWindowSize symbol at %p", symbol);
    } else {
        LogDebug("glfwGetWindowSize symbol unavailable in current process");
    }
}

void ResolveRealGlfwGetFramebufferSize() {
    const char* fallbackLabel = nullptr;
    void* fallbackHandle = GetGlfwCapturedHandle(fallbackLabel);
    void* symbol = ResolveSymbol("glfwGetFramebufferSize", fallbackHandle, fallbackLabel);
    g_realGlfwGetFramebufferSize.store(reinterpret_cast<GlfwGetFramebufferSizeFn>(symbol), std::memory_order_release);
    if (symbol) {
        LogDebug("resolved real glfwGetFramebufferSize symbol at %p", symbol);
    } else {
        LogDebug("glfwGetFramebufferSize symbol unavailable in current process");
    }
}

void ResolveRealGlfwGetKey() {
    const char* fallbackLabel = nullptr;
    void* fallbackHandle = GetGlfwCapturedHandle(fallbackLabel);
    void* symbol = ResolveSymbol("glfwGetKey", fallbackHandle, fallbackLabel);
    g_realGlfwGetKey.store(reinterpret_cast<GlfwGetKeyFn>(symbol), std::memory_order_release);
    if (symbol) {
        LogDebug("resolved real glfwGetKey symbol at %p", symbol);
    } else {
        LogDebug("glfwGetKey symbol unavailable in current process");
    }
}

void ResolveRealGlfwGetKeyScancode() {
    const char* fallbackLabel = nullptr;
    void* fallbackHandle = GetGlfwCapturedHandle(fallbackLabel);
    void* symbol = ResolveSymbol("glfwGetKeyScancode", fallbackHandle, fallbackLabel);
    g_realGlfwGetKeyScancode.store(reinterpret_cast<GlfwGetKeyScancodeFn>(symbol), std::memory_order_release);
    if (symbol) {
        LogDebug("resolved real glfwGetKeyScancode symbol at %p", symbol);
    } else {
        LogDebug("glfwGetKeyScancode symbol unavailable in current process");
    }
}

void ResolveRealGlfwGetCursorPos() {
    const char* fallbackLabel = nullptr;
    void* fallbackHandle = GetGlfwCapturedHandle(fallbackLabel);
    void* symbol = ResolveSymbol("glfwGetCursorPos", fallbackHandle, fallbackLabel);
    g_realGlfwGetCursorPos.store(reinterpret_cast<GlfwGetCursorPosProc>(symbol), std::memory_order_release);
    if (symbol) {
        LogDebug("resolved real glfwGetCursorPos symbol at %p", symbol);
    } else {
        LogDebug("glfwGetCursorPos symbol unavailable in current process");
    }
}

void ResolveRealGlfwSetCursorPos() {
    const char* fallbackLabel = nullptr;
    void* fallbackHandle = GetGlfwCapturedHandle(fallbackLabel);
    void* symbol = ResolveSymbol("glfwSetCursorPos", fallbackHandle, fallbackLabel);
    g_realGlfwSetCursorPos.store(reinterpret_cast<GlfwSetCursorPosProc>(symbol), std::memory_order_release);
    if (symbol) {
        LogDebug("resolved real glfwSetCursorPos symbol at %p", symbol);
    } else {
        LogDebug("glfwSetCursorPos symbol unavailable in current process");
    }
}

void ResolveRealGlfwSetInputMode() {
    const char* fallbackLabel = nullptr;
    void* fallbackHandle = GetGlfwCapturedHandle(fallbackLabel);
    void* symbol = ResolveSymbol("glfwSetInputMode", fallbackHandle, fallbackLabel);
    g_realGlfwSetInputMode.store(reinterpret_cast<GlfwSetInputModeFn>(symbol), std::memory_order_release);
    if (symbol) {
        LogDebug("resolved real glfwSetInputMode symbol at %p", symbol);
    } else {
        LogDebug("glfwSetInputMode symbol unavailable in current process");
    }
}

#ifndef __APPLE__
void ResolveRealGlXGetProcAddress() {
    EnsureLibGlHandle();
    void* symbol = ResolveSymbol("glXGetProcAddress", g_libGlHandle.load(std::memory_order_acquire), "libGL.so.1");
    g_realGlXGetProcAddress.store(reinterpret_cast<GlXGetProcAddressFn>(symbol), std::memory_order_release);
    if (symbol) { LogDebug("resolved real glXGetProcAddress symbol at %p", symbol); }
}

void ResolveRealGlXGetProcAddressARB() {
    EnsureLibGlHandle();
    void* symbol = ResolveSymbol("glXGetProcAddressARB", g_libGlHandle.load(std::memory_order_acquire), "libGL.so.1");
    g_realGlXGetProcAddressARB.store(reinterpret_cast<GlXGetProcAddressFn>(symbol), std::memory_order_release);
    if (symbol) { LogDebug("resolved real glXGetProcAddressARB symbol at %p", symbol); }
}
#endif

void ResolveRealGlViewport() {
#ifdef __APPLE__
    void* symbol = ResolveSymbol("glViewport", nullptr, nullptr);
#else
    EnsureLibGlHandle();
    void* symbol = ResolveSymbol("glViewport", g_libGlHandle.load(std::memory_order_acquire), "libGL.so.1");
#endif
    g_realGlViewport.store(reinterpret_cast<GlViewportFn>(symbol), std::memory_order_release);
    if (symbol) { LogDebug("resolved real glViewport symbol at %p", symbol); }
}

void ResolveRealGlScissor() {
#ifdef __APPLE__
    void* symbol = ResolveSymbol("glScissor", nullptr, nullptr);
#else
    EnsureLibGlHandle();
    void* symbol = ResolveSymbol("glScissor", g_libGlHandle.load(std::memory_order_acquire), "libGL.so.1");
#endif
    g_realGlScissor.store(reinterpret_cast<GlScissorFn>(symbol), std::memory_order_release);
    if (symbol) { LogDebug("resolved real glScissor symbol at %p", symbol); }
}

void ResolveRealGlBindFramebuffer() {
#ifdef __APPLE__
    void* symbol = ResolveSymbol("glBindFramebuffer", nullptr, nullptr);
#else
    EnsureLibGlHandle();
    void* symbol = ResolveSymbol("glBindFramebuffer", g_libGlHandle.load(std::memory_order_acquire), "libGL.so.1");
    if (!symbol) {
        auto getProc = g_realGlXGetProcAddress.load(std::memory_order_acquire);
        if (getProc) { symbol = reinterpret_cast<void*>(getProc(reinterpret_cast<const GLubyte*>("glBindFramebuffer"))); }
    }
#endif
    g_realGlBindFramebuffer.store(reinterpret_cast<GlBindFramebufferFn>(symbol), std::memory_order_release);
    if (symbol) { LogDebug("resolved real glBindFramebuffer symbol at %p", symbol); }
}

#ifndef __APPLE__
GlXSwapBuffersFn GetRealGlXSwapBuffers() {
    if (!g_realGlXSwapBuffers.load(std::memory_order_acquire)) { ResolveRealGlXSwapBuffers(); }
    return g_realGlXSwapBuffers.load(std::memory_order_acquire);
}
#endif

GlViewportFn GetRealGlViewport() {
    if (!g_realGlViewport.load(std::memory_order_acquire)) { ResolveRealGlViewport(); }
    return g_realGlViewport.load(std::memory_order_acquire);
}

GlScissorFn GetRealGlScissor() {
    if (!g_realGlScissor.load(std::memory_order_acquire)) { ResolveRealGlScissor(); }
    return g_realGlScissor.load(std::memory_order_acquire);
}

GlBindFramebufferFn GetRealGlBindFramebuffer() {
    if (!g_realGlBindFramebuffer.load(std::memory_order_acquire)) { ResolveRealGlBindFramebuffer(); }
    return g_realGlBindFramebuffer.load(std::memory_order_acquire);
}

#ifndef __APPLE__
GlXSwapBuffersMscOMLFn GetRealGlXSwapBuffersMscOML() {
    if (!g_realGlXSwapBuffersMscOML.load(std::memory_order_acquire)) { ResolveRealGlXSwapBuffersMscOML(); }
    return g_realGlXSwapBuffersMscOML.load(std::memory_order_acquire);
}
#endif

GlfwSwapBuffersFn GetRealGlfwSwapBuffers() {
    if (!g_realGlfwSwapBuffers.load(std::memory_order_acquire)) { ResolveRealGlfwSwapBuffers(); }
    return g_realGlfwSwapBuffers.load(std::memory_order_acquire);
}

GlfwSetKeyCallbackFn GetRealGlfwSetKeyCallback() {
    if (!g_realGlfwSetKeyCallback.load(std::memory_order_acquire)) { ResolveRealGlfwSetKeyCallback(); }
    return g_realGlfwSetKeyCallback.load(std::memory_order_acquire);
}

GlfwSetCharCallbackFn GetRealGlfwSetCharCallback() {
    if (!g_realGlfwSetCharCallback.load(std::memory_order_acquire)) { ResolveRealGlfwSetCharCallback(); }
    return g_realGlfwSetCharCallback.load(std::memory_order_acquire);
}

GlfwSetCharModsCallbackFn GetRealGlfwSetCharModsCallback() {
    if (!g_realGlfwSetCharModsCallback.load(std::memory_order_acquire)) { ResolveRealGlfwSetCharModsCallback(); }
    return g_realGlfwSetCharModsCallback.load(std::memory_order_acquire);
}

GlfwSetMouseButtonCallbackFn GetRealGlfwSetMouseButtonCallback() {
    if (!g_realGlfwSetMouseButtonCallback.load(std::memory_order_acquire)) { ResolveRealGlfwSetMouseButtonCallback(); }
    return g_realGlfwSetMouseButtonCallback.load(std::memory_order_acquire);
}

GlfwSetCursorPosCallbackFn GetRealGlfwSetCursorPosCallback() {
    if (!g_realGlfwSetCursorPosCallback.load(std::memory_order_acquire)) { ResolveRealGlfwSetCursorPosCallback(); }
    return g_realGlfwSetCursorPosCallback.load(std::memory_order_acquire);
}

GlfwSetScrollCallbackFn GetRealGlfwSetScrollCallback() {
    if (!g_realGlfwSetScrollCallback.load(std::memory_order_acquire)) { ResolveRealGlfwSetScrollCallback(); }
    return g_realGlfwSetScrollCallback.load(std::memory_order_acquire);
}

GlfwSetWindowFocusCallbackFn GetRealGlfwSetWindowFocusCallback() {
    if (!g_realGlfwSetWindowFocusCallback.load(std::memory_order_acquire)) { ResolveRealGlfwSetWindowFocusCallback(); }
    return g_realGlfwSetWindowFocusCallback.load(std::memory_order_acquire);
}

GlfwSetWindowSizeCallbackFn GetRealGlfwSetWindowSizeCallback() {
    if (!g_realGlfwSetWindowSizeCallback.load(std::memory_order_acquire)) { ResolveRealGlfwSetWindowSizeCallback(); }
    return g_realGlfwSetWindowSizeCallback.load(std::memory_order_acquire);
}

GlfwSetFramebufferSizeCallbackFn GetRealGlfwSetFramebufferSizeCallback() {
    if (!g_realGlfwSetFramebufferSizeCallback.load(std::memory_order_acquire)) { ResolveRealGlfwSetFramebufferSizeCallback(); }
    return g_realGlfwSetFramebufferSizeCallback.load(std::memory_order_acquire);
}

GlfwSetWindowSizeFn GetRealGlfwSetWindowSize() {
    if (!g_realGlfwSetWindowSize.load(std::memory_order_acquire)) { ResolveRealGlfwSetWindowSize(); }
    return g_realGlfwSetWindowSize.load(std::memory_order_acquire);
}

GlfwSetWindowIconFn GetRealGlfwSetWindowIcon() {
    if (!g_realGlfwSetWindowIcon.load(std::memory_order_acquire)) { ResolveRealGlfwSetWindowIcon(); }
    return g_realGlfwSetWindowIcon.load(std::memory_order_acquire);
}

#ifdef __APPLE__
GlfwMakeContextCurrentFn GetRealGlfwMakeContextCurrent() {
    if (!g_realGlfwMakeContextCurrent.load(std::memory_order_acquire)) { ResolveRealGlfwMakeContextCurrent(); }
    return g_realGlfwMakeContextCurrent.load(std::memory_order_acquire);
}

GlfwDestroyWindowFn GetRealGlfwDestroyWindow() {
    if (!g_realGlfwDestroyWindow.load(std::memory_order_acquire)) { ResolveRealGlfwDestroyWindow(); }
    return g_realGlfwDestroyWindow.load(std::memory_order_acquire);
}
#endif

GlfwGetWindowSizeFn GetRealGlfwGetWindowSize() {
    if (!g_realGlfwGetWindowSize.load(std::memory_order_acquire)) { ResolveRealGlfwGetWindowSize(); }
    return g_realGlfwGetWindowSize.load(std::memory_order_acquire);
}

GlfwGetFramebufferSizeFn GetRealGlfwGetFramebufferSize() {
    if (!g_realGlfwGetFramebufferSize.load(std::memory_order_acquire)) { ResolveRealGlfwGetFramebufferSize(); }
    return g_realGlfwGetFramebufferSize.load(std::memory_order_acquire);
}

GlfwGetKeyFn GetRealGlfwGetKey() {
    if (!g_realGlfwGetKey.load(std::memory_order_acquire)) { ResolveRealGlfwGetKey(); }
    return g_realGlfwGetKey.load(std::memory_order_acquire);
}

GlfwGetKeyScancodeFn GetRealGlfwGetKeyScancode() {
    if (!g_realGlfwGetKeyScancode.load(std::memory_order_acquire)) { ResolveRealGlfwGetKeyScancode(); }
    return g_realGlfwGetKeyScancode.load(std::memory_order_acquire);
}

GlfwGetCursorPosProc GetRealGlfwGetCursorPos() {
    if (!g_realGlfwGetCursorPos.load(std::memory_order_acquire)) { ResolveRealGlfwGetCursorPos(); }
    return g_realGlfwGetCursorPos.load(std::memory_order_acquire);
}

GlfwSetCursorPosProc GetRealGlfwSetCursorPos() {
    if (!g_realGlfwSetCursorPos.load(std::memory_order_acquire)) { ResolveRealGlfwSetCursorPos(); }
    return g_realGlfwSetCursorPos.load(std::memory_order_acquire);
}

GlfwSetInputModeFn GetRealGlfwSetInputMode() {
    if (!g_realGlfwSetInputMode.load(std::memory_order_acquire)) { ResolveRealGlfwSetInputMode(); }
    return g_realGlfwSetInputMode.load(std::memory_order_acquire);
}

#ifndef __APPLE__
GlXGetProcAddressFn GetRealGlXGetProcAddress() {
    if (!g_realGlXGetProcAddress.load(std::memory_order_acquire)) { ResolveRealGlXGetProcAddress(); }
    return g_realGlXGetProcAddress.load(std::memory_order_acquire);
}

GlXGetProcAddressFn GetRealGlXGetProcAddressARB() {
    if (!g_realGlXGetProcAddressARB.load(std::memory_order_acquire)) { ResolveRealGlXGetProcAddressARB(); }
    return g_realGlXGetProcAddressARB.load(std::memory_order_acquire);
}
#endif

#ifndef __APPLE__
void RecordAndLogSwap(SwapHookSource source, Display* dpy, GLXDrawable drawable, GLXContext ctx) {
    platform::x11::RecordSwapHandles(reinterpret_cast<void*>(dpy), static_cast<unsigned long>(drawable), reinterpret_cast<void*>(ctx));

    SwapHookSource expectedSource = SwapHookSource::Unknown;
    if (g_firstSwapSource.compare_exchange_strong(expectedSource, source, std::memory_order_acq_rel)) {
        LogAlways("first swap hook path selected: %s", SwapHookSourceToString(source));
    }

    const std::uint64_t swapCount = platform::x11::GetSwapObservationCount();

    bool expected = false;
    if (g_loggedFirstSwap.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        LogAlways("first %s call intercepted (display=%p drawable=0x%lx context=%p)", SwapHookSourceToString(source), static_cast<void*>(dpy),
                  static_cast<unsigned long>(drawable), reinterpret_cast<void*>(ctx));
    }

    const std::uintptr_t displayPtr = reinterpret_cast<std::uintptr_t>(dpy);
    const std::uintptr_t contextPtr = reinterpret_cast<std::uintptr_t>(ctx);
    const unsigned long previousDrawable = g_lastDrawable.exchange(static_cast<unsigned long>(drawable), std::memory_order_acq_rel);
    const std::uintptr_t previousDisplay = g_lastDisplay.exchange(displayPtr, std::memory_order_acq_rel);
    const std::uintptr_t previousContext = g_lastContext.exchange(contextPtr, std::memory_order_acq_rel);

    if (!IsDebugEnabled()) { return; }

    const bool handlesChanged = previousDisplay != displayPtr || previousDrawable != static_cast<unsigned long>(drawable) ||
                                previousContext != contextPtr;

    if (handlesChanged) {
        LogDebug("swap handles updated via %s (display=%p drawable=0x%lx context=%p)", SwapHookSourceToString(source), static_cast<void*>(dpy),
                 static_cast<unsigned long>(drawable), reinterpret_cast<void*>(ctx));
    }

    if (swapCount <= 5 || (swapCount % 600) == 0) {
        LogDebug("swap heartbeat source=%s count=%llu display=%p drawable=0x%lx context=%p", SwapHookSourceToString(source),
                 static_cast<unsigned long long>(swapCount), static_cast<void*>(dpy), static_cast<unsigned long>(drawable),
                 reinterpret_cast<void*>(ctx));
    }
}
#endif

constexpr int kGlfwCursorMode = 0x00033001;
constexpr int kGlfwCursorNormal = 0x00034001;
constexpr int kGlfwCursorDisabled = 0x00034003;

#ifndef __APPLE__
__GLXextFuncPtr ResolveProcAddressRequest(const GLubyte* procName, GlXGetProcAddressFn resolver) {
    if (!procName) { return resolver ? resolver(procName) : nullptr; }

    if (IsRequestedProcName(procName, "glXSwapBuffers")) {
        LogDebugOnce(g_loggedProcHookSwap, "glXGetProcAddress requested glXSwapBuffers; returning interposed function");
        return reinterpret_cast<__GLXextFuncPtr>(glXSwapBuffers);
    }

    if (IsRequestedProcName(procName, "glXSwapBuffersMscOML")) {
        if (GetRealGlXSwapBuffersMscOML()) {
            LogDebugOnce(g_loggedProcHookMsc, "glXGetProcAddress requested glXSwapBuffersMscOML; returning interposed function");
            return reinterpret_cast<__GLXextFuncPtr>(glXSwapBuffersMscOML);
        }
        LogDebug("glXGetProcAddress requested glXSwapBuffersMscOML but symbol is unavailable");
    }

    if (IsRequestedProcName(procName, "glViewport")) {
        return reinterpret_cast<__GLXextFuncPtr>(glViewport);
    }

    if (IsRequestedProcName(procName, "glScissor")) {
        return reinterpret_cast<__GLXextFuncPtr>(glScissor);
    }

    if (IsRequestedProcName(procName, "glBindFramebuffer")) {
        return reinterpret_cast<__GLXextFuncPtr>(glBindFramebuffer);
    }

    return resolver ? resolver(procName) : nullptr;
}
#endif

} // namespace
