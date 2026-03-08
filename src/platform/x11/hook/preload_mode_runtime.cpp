void InitializeHotkeyDispatcherFromConfig() {
    if (g_hotkeyDispatcherInitialized.load(std::memory_order_acquire)) { return; }

    auto config = platform::config::GetConfigSnapshot();
    if (!config) { return; }

    std::vector<platform::config::HotkeyConfig> hotkeys;
    for (const auto& hk : config->hotkeys) {
        hotkeys.push_back(hk);
    }

    g_hotkeyDispatcher.SetHotkeys(std::move(hotkeys));
    ApplyGuiHotkeyFromConfig(*config);
    ApplyRebindToggleHotkeyFromConfig(*config);
    g_hotkeyDispatcherInitialized.store(true, std::memory_order_release);

    g_lastHotkeyConfigVersion.store(platform::config::GetConfigSnapshotVersion(), std::memory_order_relaxed);

    if (IsDebugEnabled()) {
        LogDebug("HotkeyDispatcher initialized with %zu hotkeys", config->hotkeys.size());
    }
}

void RefreshHotkeyDispatcherIfNeeded() {
    auto config = platform::config::GetConfigSnapshot();
    if (!config) return;

    uint64_t currentVersion = platform::config::GetConfigSnapshotVersion();
    uint64_t lastVersion = g_lastHotkeyConfigVersion.load(std::memory_order_relaxed);
    bool hotkeysOutOfSync = false;
    if (g_hotkeyDispatcherInitialized.load(std::memory_order_acquire)) {
        const auto runtimeHotkeys = g_hotkeyDispatcher.GetHotkeys();
        if (runtimeHotkeys.size() != config->hotkeys.size()) {
            hotkeysOutOfSync = true;
        } else {
            for (std::size_t i = 0; i < runtimeHotkeys.size(); ++i) {
                if (!HotkeyConfigMatches(runtimeHotkeys[i], config->hotkeys[i])) {
                    hotkeysOutOfSync = true;
                    break;
                }
            }
        }
    } else {
        hotkeysOutOfSync = true;
    }

    if (currentVersion > lastVersion || hotkeysOutOfSync) {
        std::vector<platform::config::HotkeyConfig> hotkeys;
        for (const auto& hk : config->hotkeys) {
            hotkeys.push_back(hk);
        }
        g_hotkeyDispatcher.SetHotkeys(std::move(hotkeys));
        ApplyGuiHotkeyFromConfig(*config);
        ApplyRebindToggleHotkeyFromConfig(*config);
        g_hotkeyDispatcherInitialized.store(true, std::memory_order_release);
        g_lastHotkeyConfigVersion.store(currentVersion, std::memory_order_relaxed);
        LogDebug("HotkeyDispatcher refreshed with %zu hotkeys", config->hotkeys.size());
    }
}

void ProcessModeSwitchHotkey(const std::string& targetMode, size_t hotkeyIndex) {
    if (targetMode.empty()) { return; }
    (void)hotkeyIndex;

    auto config = platform::config::GetConfigSnapshot();
    if (!config) {
        LogDebug("ProcessModeSwitchHotkey: no config snapshot available");
        return;
    }

    bool modeFound = false;
    for (const auto& mode : config->modes) {
        if (mode.name == targetMode) {
            modeFound = true;
            break;
        }
    }

    if (!modeFound) {
        LogDebug("ProcessModeSwitchHotkey: mode '%s' not found in config", targetMode.c_str());
        return;
    }

    if (platform::x11::GetMirrorModeState().GetActiveModeName() != targetMode) {
        platform::x11::UpdateSensitivityStateForModeSwitch(targetMode, *config);
    }

    int currentWidth = g_lastSwapViewportWidth.load(std::memory_order_relaxed);
    int currentHeight = g_lastSwapViewportHeight.load(std::memory_order_relaxed);
    if (currentWidth <= 0 || currentHeight <= 0) {
        if (!platform::x11::GetGameWindowSize(currentWidth, currentHeight)) {
            LogDebug("ProcessModeSwitchHotkey: unable to query current game window size; using transition fallback");
        }
    }

    platform::x11::GetMirrorModeState().ApplyModeSwitch(targetMode, *config);
    platform::x11::TriggerImmediateModeResizeEnforcement();

    LogAlways("Mode switched to: %s", targetMode.c_str());
}

int64_t NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
void TickModeResolutionTransition() {
    auto& modeState = platform::x11::GetMirrorModeState();
    const std::string activeModeName = modeState.GetActiveModeName();
    if (activeModeName.empty()) {
        return;
    }

    auto config = modeState.GetConfigSnapshot();
    if (!config) {
        return;
    }

    const platform::config::ModeConfig* activeModeConfig = nullptr;
    for (const auto& mode : config->modes) {
        if (mode.name == activeModeName) {
            activeModeConfig = &mode;
            break;
        }
    }
    if (!activeModeConfig) {
        return;
    }

    int currentWidth = g_lastSwapViewportWidth.load(std::memory_order_relaxed);
    int currentHeight = g_lastSwapViewportHeight.load(std::memory_order_relaxed);
    if (currentWidth <= 0 || currentHeight <= 0) {
        if (!GetCurrentContainerSizeForModeTarget(currentWidth, currentHeight)) {
            return;
        }
    }

    int basisWidth = 0;
    int basisHeight = 0;
    if (!GetCurrentContainerSizeForModeTarget(basisWidth, basisHeight)) {
        return;
    }

    int targetWidth = 0;
    int targetHeight = 0;
    if (!modeState.GetActiveModeTargetDimensions(basisWidth, basisHeight, targetWidth, targetHeight)) {
        return;
    }
    if (targetWidth <= 0 || targetHeight <= 0) {
        return;
    }

    if (currentWidth == targetWidth && currentHeight == targetHeight) {
        g_lastResizeRequestWidth.store(targetWidth, std::memory_order_relaxed);
        g_lastResizeRequestHeight.store(targetHeight, std::memory_order_relaxed);
        return;
    }

    if (!IsResizeEnabled()) {
        return;
    }

    bool resized = DispatchResizeEventToGame(targetWidth, targetHeight);
    if (!resized && IsPhysicalWindowResizeFallbackEnabled()) {
        resized = ResizeLatestGlfwWindow(targetWidth, targetHeight);
        if (!resized) {
            resized = platform::x11::ResizeGameWindow(targetWidth, targetHeight);
        }
    }

    if (resized) {
        g_lastResizeRequestWidth.store(targetWidth, std::memory_order_relaxed);
        g_lastResizeRequestHeight.store(targetHeight, std::memory_order_relaxed);
    } else if (IsDebugEnabled()) {
        LogDebug("Mode resize dispatch failed (%dx%d): no game callback and no physical resize fallback",
                 targetWidth,
                 targetHeight);
    }
}

bool IsWallTitleWaitingStateForReset(const std::string& gameState) {
    return gameState == "wall" || gameState == "title" || gameState == "waiting" || gameState.rfind("generating", 0) == 0;
}

bool IsInWorldStateForReset(const std::string& gameState) {
    return gameState.rfind("inworld", 0) == 0;
}

bool ModeExistsInConfig(const platform::config::LinuxscreenConfig& config, const std::string& modeName) {
    for (const auto& mode : config.modes) {
        if (mode.name == modeName) {
            return true;
        }
    }
    return false;
}

void SwitchToDefaultModeWithCutTransition(const platform::config::LinuxscreenConfig& configSnapshot,
                                          const char* reason) {
    if (configSnapshot.defaultMode.empty()) {
        return;
    }

    if (!ModeExistsInConfig(configSnapshot, configSnapshot.defaultMode)) {
        LogDebug("game-state reset skipped: default mode '%s' not found (%s)", configSnapshot.defaultMode.c_str(), reason);
        return;
    }

    if (platform::x11::GetMirrorModeState().GetActiveModeName() != configSnapshot.defaultMode) {
        platform::x11::UpdateSensitivityStateForModeSwitch(configSnapshot.defaultMode, configSnapshot);
    }

    int currentWidth = g_lastSwapViewportWidth.load(std::memory_order_relaxed);
    int currentHeight = g_lastSwapViewportHeight.load(std::memory_order_relaxed);
    if (currentWidth <= 0 || currentHeight <= 0) {
        if (!platform::x11::GetGameWindowSize(currentWidth, currentHeight)) {
            LogDebug("game-state reset: unable to query current game window size; using transition fallback");
        }
    }

    platform::x11::GetMirrorModeState().ApplyModeSwitch(configSnapshot.defaultMode, configSnapshot);
    platform::x11::TriggerImmediateModeResizeEnforcement();
    LogAlways("game-state reset switched to default mode '%s' (%s)", configSnapshot.defaultMode.c_str(), reason);
}

void MaybeApplyGameStateTransitionReset() {
    const int64_t nowMs = NowMs();
    const int64_t nextCheckMs = g_nextGameStateResetCheckMs.load(std::memory_order_acquire);
    if (nextCheckMs != 0 && nowMs < nextCheckMs) {
        return;
    }
    g_nextGameStateResetCheckMs.store(nowMs + 100, std::memory_order_release);

    const std::string gameState = platform::config::GetCurrentGameState();
    if (gameState.empty()) {
        return;
    }

    bool shouldResetSecondaryModes = false;
    bool shouldSwitchToDefault = false;
    {
        std::lock_guard<std::mutex> lock(g_gameStateResetMutex);
        if (!g_hasPreviousGameStateForReset) {
            g_previousGameStateForReset = gameState;
            g_hasPreviousGameStateForReset = true;
            return;
        }

        const bool currentIsWallTitleWaiting = IsWallTitleWaitingStateForReset(gameState);
        const bool previousIsWallTitleWaiting = IsWallTitleWaitingStateForReset(g_previousGameStateForReset);
        const bool currentIsInWorld = IsInWorldStateForReset(gameState);
        const bool previousIsInWorld = IsInWorldStateForReset(g_previousGameStateForReset);

        shouldSwitchToDefault = currentIsWallTitleWaiting && !previousIsWallTitleWaiting;
        shouldResetSecondaryModes = shouldSwitchToDefault || (previousIsInWorld && !currentIsInWorld);
        g_previousGameStateForReset = gameState;
    }

    if (!shouldResetSecondaryModes) {
        return;
    }

    g_hotkeyDispatcher.ResetSecondaryModes();

    if (!shouldSwitchToDefault) {
        return;
    }

    auto configSnapshot = platform::config::GetConfigSnapshot();
    if (!configSnapshot) {
        LogDebug("game-state reset skipped: no config snapshot available");
        return;
    }

    SwitchToDefaultModeWithCutTransition(*configSnapshot, "state transition to wall/title/waiting");
}
