namespace {
std::atomic<bool> g_calcOverlayExitCleanupRan{ false };

void CleanupCalcOverlayAtExit() {
    if (g_calcOverlayExitCleanupRan.exchange(true, std::memory_order_acq_rel)) {
        return;
    }
    platform::x11::ShutdownCalcOverlayRuntimeForProcessExit();
}
}

__attribute__((constructor)) static void LinuxscreenX11PreloadInit() {
    LogProcessIdentityOnce();

    static bool registeredCalcOverlayExitCleanup = false;
    if (!registeredCalcOverlayExitCleanup) {
        std::atexit(CleanupCalcOverlayAtExit);
        registeredCalcOverlayExitCleanup = true;
    }

    if (IsDebugEnabled()) {
        LogOnce(g_loggedDebugEnabled, "debug logging enabled via LINUXSCREEN_X11_DEBUG=1");
    }

    platform::BootstrapConfig bootstrapConfig{};
    if (!platform::Initialize(bootstrapConfig)) {
        const std::string error = platform::GetLastErrorMessage();
        LogAlways("bootstrap initialize failed: %s", error.empty() ? "unknown error" : error.c_str());
        return;
    }

    {
        std::string profileInitError;
        if (!platform::config::InitializeActiveConfigProfile(profileInitError)) {
            LogAlways("WARNING: profile initialization failed: %s",
                      profileInitError.empty() ? "unknown" : profileInitError.c_str());
        }

        auto config = platform::config::LoadLinuxscreenConfig();
        platform::config::PublishConfigSnapshot(config);
        g_lastObservedRebindsEnabledState.store(config.keyRebinds.enabled ? 1 : 0, std::memory_order_release);
        ApplyGuiHotkeyFromConfig(config);
        ApplyRebindToggleHotkeyFromConfig(config);
        LogAlways("config loaded: %zu mirrors, %zu modes, %zu hotkeys from %s (profile=%s)",
                  config.mirrors.size(), config.modes.size(), config.hotkeys.size(),
                  platform::config::GetConfigPath().c_str(),
                  platform::config::GetActiveConfigProfileId().empty()
                      ? "unknown"
                      : platform::config::GetActiveConfigProfileId().c_str());

        std::string modeToActivate;

        if (!config.defaultMode.empty()) {
            bool defaultModeExists = false;
            for (const auto& mode : config.modes) {
                if (mode.name == config.defaultMode) {
                    defaultModeExists = true;
                    break;
                }
            }
            if (defaultModeExists) {
                modeToActivate = config.defaultMode;
            } else {
                LogAlways("WARNING: defaultMode '%s' not found in modes list", config.defaultMode.c_str());
            }
        }

        if (modeToActivate.empty()) {
            for (const auto& mode : config.modes) {
                if (!mode.name.empty()) {
                    modeToActivate = mode.name;
                    break;
                }
            }
        }

        if (!modeToActivate.empty()) {
            platform::x11::GetMirrorModeState().ApplyModeSwitch(modeToActivate, config);
            LogAlways("initialized mode: %s", modeToActivate.c_str());
        } else {
            LogAlways("WARNING: no valid modes found in config - mirrors will not render");
        }

        std::vector<platform::config::HotkeyConfig> hotkeys;
        for (const auto& hk : config.hotkeys) {
            hotkeys.push_back(hk);
        }
        g_hotkeyDispatcher().SetHotkeys(std::move(hotkeys));
        g_hotkeyDispatcherInitialized.store(true, std::memory_order_release);
        LogDebug("HotkeyDispatcher initialized with %zu hotkeys", config.hotkeys.size());

        platform::config::StartGameStateMonitor();
    }

    LogAlways("runtime state after initialize: %s", RuntimeStateToString(platform::GetRuntimeState()));
    LogAlways("x11 feature flags: glfwInputHook=%s imguiRender=%s", IsGlfwInputHookEnabled() ? "on" : "off",
              platform::x11::IsImGuiRenderEnabled() ? "on" : "off");

    if (!platform::InstallHooks()) {
        const std::string error = platform::GetLastErrorMessage();
        LogOnce(g_loggedInstallHookFailure, "WARNING: platform InstallHooks returned false during preload init");
        LogDebug("InstallHooks detail: %s", error.empty() ? "none" : error.c_str());
    } else {
        LogAlways("runtime state after InstallHooks: %s", RuntimeStateToString(platform::GetRuntimeState()));
    }

#ifdef __APPLE__
    LogOnce(g_loggedBootstrap, "preload bootstrap active (CGLFlushDrawable interposed)");
#else
    if (!GetRealGlXSwapBuffers()) {
        LogOnce(g_loggedLaunchContextFailure,
                "WARNING: preload initialized but glXSwapBuffers resolution failed; native GLFW/EGL swap interception may still be used");
    }

    (void)GetRealGlXSwapBuffersMscOML();
    (void)GetRealGlXGetProcAddress();
    (void)GetRealGlXGetProcAddressARB();
    (void)GetRealGlBlitFramebuffer();
    (void)GetRealGlBlitNamedFramebuffer();
#endif
    (void)GetRealDlSym();
}

__attribute__((destructor)) static void LinuxscreenX11PreloadShutdown() {
    LogAlways("preload shutdown starting (destructor context)");

#ifdef __APPLE__
    CleanupCalcOverlayAtExit();
    // Orderly cleanup seems to be unsafe on macOS? JVM's exit leaves global
    // mutexes in a destroyed state, and locking them crashes the process.
    // Need to look into this more :)
    return;
#endif

    platform::config::StopGameStateMonitor();
    platform::config::ShutdownConfigSaveThread();

    g_glfwResolverHandle.store(nullptr, std::memory_order_release);

    {
        std::lock_guard<std::mutex> lock(g_glfwCallbackMutex);
        g_glfwCallbackMap.clear();
    }

    {
        std::lock_guard<std::mutex> lock(g_inputStateMutex);
        g_keyStateTracker.Clear();
    }

    ClearPendingCharRemaps();

    {
        std::lock_guard<std::mutex> lock(g_syntheticRebindKeyMutex);
        g_syntheticRebindKeyStates.clear();
    }
    {
        std::lock_guard<std::mutex> lock(g_managedRepeatMutex);
        g_managedRepeatStates.clear();
        g_managedRepeatInvalidatedKeys.clear();
    }

    platform::x11::ShutdownImGuiOverlayForProcessExit();
    platform::x11::ShutdownGlxMirrorPipelineForProcessExit();
    platform::x11::ShutdownSharedGlxContextsForProcessExit();

    g_libGlHandle.store(nullptr, std::memory_order_release);
    g_libGlfwHandle.store(nullptr, std::memory_order_release);
    g_libDlHandle.store(nullptr, std::memory_order_release);

    platform::Shutdown();
    LogAlways("preload shutdown complete");
}
