#define GLFW_CURSOR          0x00033001
#define GLFW_CURSOR_NORMAL   0x00034001
#define GLFW_CURSOR_HIDDEN   0x00034002
#define GLFW_CURSOR_DISABLED 0x00034003
void ClearPendingSyntheticCursorPosCallbackState();
GLFWwindow* ResolveGuiToggleWindow(GLFWwindow* preferredWindow) {
    if (preferredWindow) {
        return preferredWindow;
    }
    return g_lastSwapWindow.load(std::memory_order_acquire);
}

void ForceCursorNormalForGuiOpen(GLFWwindow* window) {
    if (!window) {
        return;
    }
    GlfwSetInputModeFn realSetInputMode = GetRealGlfwSetInputMode();
    if (!realSetInputMode) {
        return;
    }
    realSetInputMode(window, kGlfwCursorMode, kGlfwCursorNormal);
    g_gameWantsCursorDisabled.store(false, std::memory_order_release);
    g_cursorCaptureActive.store(false, std::memory_order_release);
    ClearPendingSyntheticCursorPosCallbackState();
    ClearTrackedCursorCaptureState();
    ResetCursorSensitivityState();
}

void RestoreCursorDisabledAfterGuiClose(GLFWwindow* window) {
    if (!window) {
        return;
    }
    if (!g_restoreCursorDisabledOnGuiClose.exchange(false, std::memory_order_acq_rel)) {
        return;
    }

    if (!GetRealGlfwSetInputMode()) {
        return;
    }
    ::glfwSetInputMode(window, kGlfwCursorMode, kGlfwCursorDisabled);
}

void ClearPendingCharRemaps();
void MaybeClearPendingCharRemapsForRebindDisable(const platform::config::LinuxscreenConfig& config);
void ClearManagedRepeatStateForSource(GLFWwindow* window, const platform::input::InputEvent& event);
void ClearManagedRepeatStatesForWindow(GLFWwindow* window);
void ArmPendingSyntheticCursorPosCallback(GLFWwindow* window, double xpos, double ypos);
void ClearPendingSyntheticCursorPosCallbackState();
void DispatchCurrentFreeCursorPosition(GLFWwindow* window);
bool HasKeyboardBindingIdentity(const platform::input::InputEvent& event);

void ReleaseAllHeldInputsForGuiOpen(GLFWwindow* window) {
    std::vector<platform::input::BindingKey> downBindings;
    std::optional<std::string> modeToRestore;
    {
        std::lock_guard<std::mutex> lock(g_inputStateMutex);
        downBindings = g_keyStateTracker.GetDownBindings();
        g_keyStateTracker.Clear();
    }

    if (const auto configSnapshot = platform::config::GetConfigSnapshot()) {
        modeToRestore = g_hotkeyDispatcher().ReleaseHeldModeForInputReset(
            platform::x11::GetMirrorModeState().GetActiveModeName());
        (void)platform::x11::ReleaseHeldSensitivityOverrideForInputReset();

        if (modeToRestore && !modeToRestore->empty()) {
            bool modeExists = false;
            for (const auto& mode : configSnapshot->modes) {
                if (mode.name == *modeToRestore) {
                    modeExists = true;
                    break;
                }
            }

            if (modeExists) {
                if (platform::x11::GetMirrorModeState().GetActiveModeName() != *modeToRestore) {
                    platform::x11::UpdateSensitivityStateForModeSwitch(*modeToRestore, *configSnapshot);
                }
                platform::x11::GetMirrorModeState().ApplyModeSwitch(*modeToRestore, *configSnapshot);
                platform::x11::TriggerImmediateModeResizeEnforcement();
            }
        }
    }

    std::stable_sort(
        downBindings.begin(),
        downBindings.end(),
        [](const platform::input::BindingKey& a, const platform::input::BindingKey& b) {
        const bool aModifier = platform::input::IsKeyboardBindingKey(a) && platform::input::IsModifierScanCode(a.code);
        const bool bModifier = platform::input::IsKeyboardBindingKey(b) && platform::input::IsModifierScanCode(b.code);
        if (aModifier != bModifier) {
            return !aModifier;
        }
        if (a.kind != b.kind) {
            return static_cast<int>(a.kind) < static_cast<int>(b.kind);
        }
        return a.code < b.code;
    });
    downBindings.erase(std::unique(downBindings.begin(), downBindings.end()), downBindings.end());

    if (window) {
        GlfwKeyCallback userKeyCallback = nullptr;
        GlfwMouseButtonCallback userMouseCallback = nullptr;
        {
            std::lock_guard<std::mutex> lock(g_glfwCallbackMutex);
            auto it = g_glfwCallbackMap.find(window);
            if (it != g_glfwCallbackMap.end()) {
                userKeyCallback = it->second.key;
                userMouseCallback = it->second.mouseButton;
            }
        }

        const int releaseAction = static_cast<int>(platform::input::GlfwAction::Release);
        for (const auto& binding : downBindings) {
            if (platform::input::IsMouseBindingKey(binding)) {
                if (userMouseCallback) {
                    userMouseCallback(window, binding.code, releaseAction, 0);
                }
                continue;
            }

            if (!platform::input::IsKeyboardBindingKey(binding)) {
                continue;
            }

            if (userKeyCallback) {
                const int scanCode = binding.code;
                userKeyCallback(window, -1, scanCode, releaseAction, 0);
            }
        }
    }

    ClearPendingCharRemaps();
    {
        std::lock_guard<std::mutex> lock(g_syntheticRebindKeyMutex);
        g_syntheticRebindKeyStates.clear();
    }
    ClearManagedRepeatStatesForWindow(window);
}

bool ProcessInputEventForGuiToggle(GLFWwindow* sourceWindow, const platform::input::InputEvent& event, const char* sourceLabel) {
    const auto toggleConfigSnapshot = platform::config::GetConfigSnapshot();
    auto toggleKeyRebindsFromHotkey = [&](int64_t nowMs) {
        const int64_t lastMs = g_lastRebindToggleTimeMs.load(std::memory_order_relaxed);
        if (nowMs - lastMs < 200) {
            LogDebug("rebind toggle hotkey match debounced (%s)", sourceLabel);
            return;
        }
        g_lastRebindToggleTimeMs.store(nowMs, std::memory_order_relaxed);

        auto configSnapshot = platform::config::GetConfigSnapshot();
        if (!configSnapshot) {
            LogDebug("rebind toggle hotkey ignored (%s): no config snapshot", sourceLabel);
            return;
        }

        auto mutableConfig = *configSnapshot;
        mutableConfig.keyRebinds.enabled = !mutableConfig.keyRebinds.enabled;
        platform::config::PublishConfigSnapshot(mutableConfig);
        platform::config::SaveLinuxscreenConfig(mutableConfig);
        g_lastObservedRebindsEnabledState.store(mutableConfig.keyRebinds.enabled ? 1 : 0, std::memory_order_release);
        if (!mutableConfig.keyRebinds.enabled) {
            ClearPendingCharRemaps();
        }
        platform::x11::ShowRebindToggleIndicator(mutableConfig.keyRebinds.enabled);

        LogAlways("key rebinds toggled via %s (vk=%u) -> enabled=%s",
                  sourceLabel,
                  static_cast<unsigned>(event.vk),
                  mutableConfig.keyRebinds.enabled ? "true" : "false");
    };

    bool shouldToggleGui = false;
    bool shouldToggleRebinds = false;

    {
        std::lock_guard<std::mutex> lock(g_inputStateMutex);
        g_keyStateTracker.ApplyEvent(event);
        const std::vector<platform::input::BindingKey> guiHotkey =
            toggleConfigSnapshot ? toggleConfigSnapshot->guiHotkey : platform::x11::GetGuiHotkey();
        shouldToggleGui = platform::input::MatchesHotkey(g_keyStateTracker, guiHotkey, event);
        const std::vector<platform::input::BindingKey> rebindToggleHotkey =
            toggleConfigSnapshot ? toggleConfigSnapshot->rebindToggleHotkey : platform::x11::GetRebindToggleHotkey();
        shouldToggleRebinds = platform::input::MatchesHotkey(g_keyStateTracker, rebindToggleHotkey, event);
    }

    if (!shouldToggleGui && !shouldToggleRebinds) { return false; }

    const int64_t nowMs = NowMs();
    const bool consumedByRebindToggleHotkey = shouldToggleRebinds;

    if (shouldToggleRebinds && !shouldToggleGui) {
        toggleKeyRebindsFromHotkey(nowMs);
    }

    if (!shouldToggleGui) {
        return consumedByRebindToggleHotkey;
    }

    const int64_t lastMs = g_lastGuiToggleTimeMs.load(std::memory_order_relaxed);
    if (nowMs - lastMs < 200) {
        LogDebug("GUI toggle hotkey match debounced (%s)", sourceLabel);
        return consumedByRebindToggleHotkey;
    }
    g_lastGuiToggleTimeMs.store(nowMs, std::memory_order_relaxed);

    const bool newVisible = platform::x11::ToggleGuiVisible();
    GLFWwindow* targetWindow = ResolveGuiToggleWindow(sourceWindow);
    if (newVisible) {
        const bool shouldRestoreDisabled = g_gameWantsCursorDisabled.load(std::memory_order_acquire) ||
                                           g_cursorCaptureActive.load(std::memory_order_acquire);
        g_restoreCursorDisabledOnGuiClose.store(shouldRestoreDisabled, std::memory_order_release);
        ForceCursorNormalForGuiOpen(targetWindow);
        ReleaseAllHeldInputsForGuiOpen(targetWindow);
    } else {
        RestoreCursorDisabledAfterGuiClose(targetWindow);
        DispatchCurrentFreeCursorPosition(targetWindow);
    }

    const std::uint64_t toggleCount = platform::x11::GetGuiToggleCount();
    LogAlways("GUI toggle hotkey triggered via %s (vk=%u) -> guiVisible=%s toggleCount=%llu", sourceLabel,
              static_cast<unsigned>(event.vk), newVisible ? "true" : "false", static_cast<unsigned long long>(toggleCount));
    return true;
}

void PublishImGuiInputEvent(const platform::input::InputEvent& event, const char* sourceLabel) {
    if (!IsAnyImGuiInputConsumerEnabled()) { return; }
    if (platform::x11::EnqueueImGuiInputEvent(event)) { return; }

    bool expected = false;
    if (g_loggedImGuiInputQueueDrop.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        LogDebug("ImGui input queue overflow; dropping oldest event (source=%s dropped=%llu)", sourceLabel,
                 static_cast<unsigned long long>(platform::x11::GetImGuiInputDroppedCount()));
    }
}

void DrainImGuiInputBridgeQueue(const char* sourceLabel) {
    if (!IsImGuiInputBridgeEnabled()) { return; }
    if (platform::x11::GetImGuiInputQueuedCount() == 0) { return; }

    constexpr std::size_t kDrainBatchSize = 512;
    constexpr std::size_t kDrainPassLimit = 4;

    std::size_t totalDrained = 0;
    std::size_t totalApplied = 0;
    bool hasImGuiSupport = false;
    bool hadCurrentContext = false;

    for (std::size_t pass = 0; pass < kDrainPassLimit; ++pass) {
        const platform::x11::ImGuiInputDrainResult result = platform::x11::DrainImGuiInputEventsToCurrentContext(kDrainBatchSize);
        totalDrained += result.drained;
        totalApplied += result.applied;
        hasImGuiSupport = result.hasImGuiSupport;
        hadCurrentContext = hadCurrentContext || result.hadCurrentContext;
        if (result.drained < kDrainBatchSize) { break; }
    }

    if (totalDrained == 0) { return; }

    if (!hasImGuiSupport) {
        LogOnce(g_loggedImGuiInputBridgeNoImGui,
                "WARNING: ImGui input bridge enabled but this build has no ImGui bridge support; drained events are discarded");
        return;
    }

    if (!hadCurrentContext) {
        LogDebugOnce(g_loggedImGuiInputBridgeNoContext,
                     "ImGui input bridge drained events without active ImGui context; waiting for render-thread context");
        return;
    }

    static std::atomic<std::uint64_t> drainCount{ 0 };
    const std::uint64_t count = drainCount.fetch_add(1, std::memory_order_acq_rel) + 1;
    if (IsDebugEnabled() && (count <= 5 || (count % 600) == 0)) {
        LogDebug("ImGui input bridge drained via %s (drained=%zu applied=%zu queued=%llu)", sourceLabel, totalDrained, totalApplied,
                 static_cast<unsigned long long>(platform::x11::GetImGuiInputQueuedCount()));
    }
}

bool HasShortcutModifiersThatSuppressText(int nativeMods) {
    const int suppressMask = static_cast<int>(platform::input::GlfwMod::Control) |
                             static_cast<int>(platform::input::GlfwMod::Super);
    return (nativeMods & suppressMask) != 0;
}

bool AreShortcutModifiersCurrentlyDown() {
    std::lock_guard<std::mutex> lock(g_inputStateMutex);
    return g_keyStateTracker.IsAnyScanCodeDown({ 37, 105, 133, 134 });
}

void ClearScissoredRect(int x, int y, int width, int height, float r, float g, float b, float a) {
    if (width <= 0 || height <= 0) { return; }
    glScissor(x, y, width, height);
    glClearColor(r, g, b, a);
    glClear(GL_COLOR_BUFFER_BIT);
}

void RenderGuiPlaceholderOverlay() {
    if (!platform::x11::IsGuiVisible()) { return; }

    GLint viewport[4] = { 0, 0, 0, 0 };
    glGetIntegerv(GL_VIEWPORT, viewport);

    const int framebufferWidth = viewport[2];
    const int framebufferHeight = viewport[3];
    if (framebufferWidth <= 0 || framebufferHeight <= 0) { return; }

    int panelWidth = framebufferWidth / 3;
    if (panelWidth < 320) { panelWidth = 320; }
    if (panelWidth > framebufferWidth - 40) { panelWidth = framebufferWidth - 40; }

    int panelHeight = framebufferHeight / 3;
    if (panelHeight < 180) { panelHeight = 180; }
    if (panelHeight > framebufferHeight - 40) { panelHeight = framebufferHeight - 40; }

    if (panelWidth <= 0 || panelHeight <= 0) { return; }

    const int panelX = 20;
    const int panelY = framebufferHeight - panelHeight - 20;
    const int border = 2;

    GLboolean scissorEnabled = GL_FALSE;
    GLint previousScissor[4] = { 0, 0, 0, 0 };
    GLfloat previousClearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

    glGetBooleanv(GL_SCISSOR_TEST, &scissorEnabled);
    glGetIntegerv(GL_SCISSOR_BOX, previousScissor);
    glGetFloatv(GL_COLOR_CLEAR_VALUE, previousClearColor);

    glEnable(GL_SCISSOR_TEST);

    ClearScissoredRect(panelX, panelY, panelWidth, panelHeight, 0.07f, 0.08f, 0.10f, 1.0f);
    ClearScissoredRect(panelX, panelY + panelHeight - border, panelWidth, border, 0.18f, 0.65f, 0.90f, 1.0f);
    ClearScissoredRect(panelX, panelY, panelWidth, border, 0.18f, 0.65f, 0.90f, 1.0f);
    ClearScissoredRect(panelX, panelY, border, panelHeight, 0.18f, 0.65f, 0.90f, 1.0f);
    ClearScissoredRect(panelX + panelWidth - border, panelY, border, panelHeight, 0.18f, 0.65f, 0.90f, 1.0f);

    if (scissorEnabled) {
        glScissor(previousScissor[0], previousScissor[1], previousScissor[2], previousScissor[3]);
    } else {
        glDisable(GL_SCISSOR_TEST);
    }

    glClearColor(previousClearColor[0], previousClearColor[1], previousClearColor[2], previousClearColor[3]);
}

void RenderRebindToggleIndicatorOverlay() {
    bool rebindsEnabled = false;
    if (!platform::x11::GetRebindToggleIndicator(rebindsEnabled)) {
        return;
    }

    GLint viewport[4] = { 0, 0, 0, 0 };
    glGetIntegerv(GL_VIEWPORT, viewport);
    if (viewport[2] <= 0 || viewport[3] <= 0) {
        return;
    }

    GLboolean scissorEnabled = GL_FALSE;
    GLint previousScissor[4] = { 0, 0, 0, 0 };
    GLfloat previousClearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    GLboolean previousColorMask[4] = { GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE };

    glGetBooleanv(GL_SCISSOR_TEST, &scissorEnabled);
    glGetIntegerv(GL_SCISSOR_BOX, previousScissor);
    glGetFloatv(GL_COLOR_CLEAR_VALUE, previousClearColor);
    glGetBooleanv(GL_COLOR_WRITEMASK, previousColorMask);

    glEnable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    const float red = rebindsEnabled ? 0.0f : 1.0f;
    const float green = rebindsEnabled ? 1.0f : 0.0f;
    ClearScissoredRect(0, 0, 5, 5, red, green, 0.0f, 1.0f);

    if (scissorEnabled) {
        glScissor(previousScissor[0], previousScissor[1], previousScissor[2], previousScissor[3]);
    } else {
        glDisable(GL_SCISSOR_TEST);
    }

    glColorMask(previousColorMask[0], previousColorMask[1], previousColorMask[2], previousColorMask[3]);
    glClearColor(previousClearColor[0], previousClearColor[1], previousClearColor[2], previousClearColor[3]);
}

bool ShouldSuppressPreHeldKeysForSyntheticTarget(int targetKey) {
    static const int kGlfwF3Key = static_cast<int>(platform::input::VK_F1 + 2);
    return targetKey >= 0 && targetKey == kGlfwF3Key;
}

bool IsSyntheticRebindStateEmpty(const SyntheticRebindKeyState& state) {
    return state.sourceToTarget.empty() && state.targetPressCount.empty() && state.physicalKeyDown.empty() &&
           state.targetSuppressedKeys.empty() && state.suppressedKeyRefCount.empty();
}

int GetEffectiveNativeMods(GLFWwindow* window, int nativeMods) {
    if (!window) {
        return nativeMods;
    }

    std::lock_guard<std::mutex> lock(g_syntheticRebindKeyMutex);
    const auto windowIt = g_syntheticRebindKeyStates.find(window);
    if (windowIt == g_syntheticRebindKeyStates.end()) {
        return nativeMods;
    }

    const SyntheticRebindKeyState& state = windowIt->second;
    for (const auto& targetEntry : state.targetPressCount) {
        if (targetEntry.second <= 0) {
            continue;
        }

        if (platform::input::IsShiftGlfwKey(targetEntry.first)) {
            nativeMods |= static_cast<int>(platform::input::GlfwMod::Shift);
        }
    }

    return nativeMods;
}

void DecrementSyntheticSuppressedKeyRefCount(SyntheticRebindKeyState& state, int key) {
    auto it = state.suppressedKeyRefCount.find(key);
    if (it == state.suppressedKeyRefCount.end()) {
        return;
    }

    it->second -= 1;
    if (it->second <= 0) {
        state.suppressedKeyRefCount.erase(it);
    }
}

void ReleaseSyntheticSuppressedKeysForTarget(SyntheticRebindKeyState& state, int targetKey) {
    auto suppressedIt = state.targetSuppressedKeys.find(targetKey);
    if (suppressedIt == state.targetSuppressedKeys.end()) {
        return;
    }

    for (int key : suppressedIt->second) {
        DecrementSyntheticSuppressedKeyRefCount(state, key);
    }
    state.targetSuppressedKeys.erase(suppressedIt);
}

void CaptureSyntheticSuppressedKeysForTarget(SyntheticRebindKeyState& state, int targetKey) {
    ReleaseSyntheticSuppressedKeysForTarget(state, targetKey);
    if (!ShouldSuppressPreHeldKeysForSyntheticTarget(targetKey)) {
        return;
    }

    std::set<int>& suppressedKeys = state.targetSuppressedKeys[targetKey];
    for (const auto& physicalEntry : state.physicalKeyDown) {
        if (!physicalEntry.second) {
            continue;
        }

        const int heldKey = physicalEntry.first;
        if (heldKey < 0 || heldKey == targetKey) {
            continue;
        }
        if (state.sourceToTarget.find(heldKey) != state.sourceToTarget.end()) {
            continue;
        }

        if (platform::input::IsModifierGlfwKey(heldKey)) {
            continue;
        }

        if (suppressedKeys.insert(heldKey).second) {
            state.suppressedKeyRefCount[heldKey] += 1;
        }
    }

    if (suppressedKeys.empty()) {
        state.targetSuppressedKeys.erase(targetKey);
    }
}

void DecrementSyntheticTargetPressCount(SyntheticRebindKeyState& state, int targetKey) {
    auto targetIt = state.targetPressCount.find(targetKey);
    if (targetIt == state.targetPressCount.end()) {
        return;
    }

    targetIt->second -= 1;
    if (targetIt->second <= 0) {
        ReleaseSyntheticSuppressedKeysForTarget(state, targetKey);
        state.targetPressCount.erase(targetIt);
    }
}

void UpdateSyntheticPhysicalKeyState(GLFWwindow* window, int key, int action) {
    if (!window || key < 0) {
        return;
    }

    const bool isPressLike = action == static_cast<int>(platform::input::GlfwAction::Press) ||
                             action == static_cast<int>(platform::input::GlfwAction::Repeat);
    const bool isRelease = action == static_cast<int>(platform::input::GlfwAction::Release);
    if (!isPressLike && !isRelease) {
        return;
    }

    std::lock_guard<std::mutex> lock(g_syntheticRebindKeyMutex);
    SyntheticRebindKeyState& state = g_syntheticRebindKeyStates[window];

    if (isPressLike) {
        state.physicalKeyDown[key] = true;
        return;
    }

    state.physicalKeyDown.erase(key);

    for (auto targetIt = state.targetSuppressedKeys.begin(); targetIt != state.targetSuppressedKeys.end();) {
        if (targetIt->second.erase(key) > 0) {
            DecrementSyntheticSuppressedKeyRefCount(state, key);
        }

        if (targetIt->second.empty()) {
            targetIt = state.targetSuppressedKeys.erase(targetIt);
        } else {
            ++targetIt;
        }
    }

    if (IsSyntheticRebindStateEmpty(state)) {
        g_syntheticRebindKeyStates.erase(window);
    }
}

bool ShouldSuppressForwardedKeyEvent(GLFWwindow* window, int key, int action) {
    if (!window || key < 0) {
        return false;
    }

    const bool isPressLike = action == static_cast<int>(platform::input::GlfwAction::Press) ||
                             action == static_cast<int>(platform::input::GlfwAction::Repeat);
    if (!isPressLike) {
        return false;
    }

    std::lock_guard<std::mutex> lock(g_syntheticRebindKeyMutex);
    auto windowIt = g_syntheticRebindKeyStates.find(window);
    if (windowIt == g_syntheticRebindKeyStates.end()) {
        return false;
    }

    const SyntheticRebindKeyState& state = windowIt->second;
    auto suppressedIt = state.suppressedKeyRefCount.find(key);
    return suppressedIt != state.suppressedKeyRefCount.end() && suppressedIt->second > 0;
}

void ClearSyntheticRebindSourceKey(GLFWwindow* window, int sourceKey) {
    if (!window || sourceKey < 0) {
        return;
    }

    std::lock_guard<std::mutex> lock(g_syntheticRebindKeyMutex);
    auto windowIt = g_syntheticRebindKeyStates.find(window);
    if (windowIt == g_syntheticRebindKeyStates.end()) {
        return;
    }

    SyntheticRebindKeyState& state = windowIt->second;
    auto sourceIt = state.sourceToTarget.find(sourceKey);
    if (sourceIt != state.sourceToTarget.end()) {
        DecrementSyntheticTargetPressCount(state, sourceIt->second);
        state.sourceToTarget.erase(sourceIt);
    }

    if (IsSyntheticRebindStateEmpty(state)) {
        g_syntheticRebindKeyStates.erase(windowIt);
    }
}

void UpdateSyntheticRebindKeyState(GLFWwindow* window, int sourceKey, int targetKey, int action) {
    if (!window || sourceKey < 0) {
        return;
    }

    const bool isPressLike = action == static_cast<int>(platform::input::GlfwAction::Press) ||
                             action == static_cast<int>(platform::input::GlfwAction::Repeat);
    const bool isRelease = action == static_cast<int>(platform::input::GlfwAction::Release);

    if (!isPressLike && !isRelease) {
        return;
    }

    if (isRelease || targetKey < 0 || targetKey == sourceKey) {
        ClearSyntheticRebindSourceKey(window, sourceKey);
        return;
    }

    std::lock_guard<std::mutex> lock(g_syntheticRebindKeyMutex);
    SyntheticRebindKeyState& state = g_syntheticRebindKeyStates[window];

    auto sourceIt = state.sourceToTarget.find(sourceKey);
    if (sourceIt != state.sourceToTarget.end()) {
        if (sourceIt->second == targetKey) {
            return;
        }
        DecrementSyntheticTargetPressCount(state, sourceIt->second);
        sourceIt->second = targetKey;
    } else {
        state.sourceToTarget[sourceKey] = targetKey;
    }

    const bool targetWasInactive = state.targetPressCount.find(targetKey) == state.targetPressCount.end();
    state.targetPressCount[targetKey] += 1;
    if (targetWasInactive) {
        CaptureSyntheticSuppressedKeysForTarget(state, targetKey);
    }
}

void ClearSyntheticRebindWindow(GLFWwindow* window) {
    if (!window) {
        return;
    }

    std::lock_guard<std::mutex> lock(g_syntheticRebindKeyMutex);
    g_syntheticRebindKeyStates.erase(window);
}

int GetSyntheticRebindMouseSourceKey(int button) {
    if (button < 0) {
        return -1;
    }

    constexpr int kMouseSourceBase = 10000;
    return kMouseSourceBase + button;
}

bool IsRebindEntryConfigured(const platform::config::KeyRebind& rebind) {
    return rebind.enabled &&
           platform::input::IsValidBindingKey(rebind.fromInput) &&
           (rebind.consumeSourceInput || platform::input::IsValidBindingKey(rebind.toInput));
}

bool MatchesRebindSourceBinding(const platform::input::InputEvent& event, const platform::config::KeyRebind& rebind) {
    if ((!HasKeyboardBindingIdentity(event) && event.type != platform::input::InputEventType::MouseButton) ||
        !platform::input::IsValidBindingKey(rebind.fromInput)) {
        return false;
    }
    return platform::input::BindingKeyFromInputEvent(event) == rebind.fromInput;
}

bool IsValidUnicodeScalar(std::uint32_t codepoint) {
    if (codepoint == 0 || codepoint > 0x10FFFFu) {
        return false;
    }
    return !(codepoint >= 0xD800u && codepoint <= 0xDFFFu);
}

bool TryDecodeFirstUtf8Codepoint(const char* input, std::uint32_t& outCodepoint) {
    outCodepoint = 0;
    if (!input || input[0] == '\0') {
        return false;
    }

    const unsigned char* bytes = reinterpret_cast<const unsigned char*>(input);
    std::uint32_t cp = 0;
    std::size_t length = 0;

    const unsigned char b0 = bytes[0];
    if (b0 < 0x80) {
        cp = b0;
        length = 1;
    } else if ((b0 & 0xE0) == 0xC0) {
        cp = static_cast<std::uint32_t>(b0 & 0x1F);
        length = 2;
    } else if ((b0 & 0xF0) == 0xE0) {
        cp = static_cast<std::uint32_t>(b0 & 0x0F);
        length = 3;
    } else if ((b0 & 0xF8) == 0xF0) {
        cp = static_cast<std::uint32_t>(b0 & 0x07);
        length = 4;
    } else {
        return false;
    }

    for (std::size_t i = 1; i < length; ++i) {
        const unsigned char continuation = bytes[i];
        if ((continuation & 0xC0) != 0x80) {
            return false;
        }
        cp = (cp << 6) | static_cast<std::uint32_t>(continuation & 0x3F);
    }

    if (!IsValidUnicodeScalar(cp)) {
        return false;
    }

    outCodepoint = cp;
    return true;
}

bool TryResolveGlfwLayoutCodepoint(int nativeKey, int nativeScanCode, bool shiftDown, std::uint32_t& outCodepoint) {
    outCodepoint = 0;
    GlfwGetKeyNameFn getKeyName = GetRealGlfwGetKeyName();
    if (!getKeyName) {
        return false;
    }

    const char* keyName = getKeyName(nativeKey, nativeScanCode);
    if (!TryDecodeFirstUtf8Codepoint(keyName, outCodepoint)) {
        return false;
    }

    if (shiftDown && outCodepoint >= static_cast<std::uint32_t>('a') && outCodepoint <= static_cast<std::uint32_t>('z')) {
        outCodepoint = static_cast<std::uint32_t>('A' + (outCodepoint - static_cast<std::uint32_t>('a')));
    }
    return true;
}

bool HasKeyboardBindingIdentity(const platform::input::InputEvent& event) {
    return event.type == platform::input::InputEventType::Key && event.nativeScanCode > 0;
}

struct ResolvedRebindOutput {
    bool matched = false;
    bool consumeSourceInput = false;
    platform::input::VkCode sourceVk = platform::input::VK_NONE;
    platform::input::BindingKey triggerBinding;
    platform::input::BindingKey textBinding;
    int textScanCode = 0;
    int outputScanCode = 0;
    bool targetIsMouse = false;
    std::uint32_t customUnicode = 0;
    std::uint32_t customShiftUnicode = 0;
};

std::optional<ResolvedRebindOutput> ResolveRebindOutput(const platform::config::LinuxscreenConfig& config,
                                                        const platform::input::InputEvent& event,
                                                        bool guiVisible) {
    if (!config.keyRebinds.enabled ||
        (!HasKeyboardBindingIdentity(event) && event.type != platform::input::InputEventType::MouseButton)) {
        return std::nullopt;
    }

    if (event.type == platform::input::InputEventType::MouseButton && guiVisible) {
        return std::nullopt;
    }

    for (const auto& rebind : config.keyRebinds.rebinds) {
        if (!IsRebindEntryConfigured(rebind)) {
            continue;
        }
        if (!MatchesRebindSourceBinding(event, rebind)) {
            continue;
        }

        ResolvedRebindOutput resolved;
        resolved.matched = true;
        resolved.sourceVk = event.vk;
        if (rebind.consumeSourceInput) {
        resolved.consumeSourceInput = true;
        return resolved;
        }
        resolved.triggerBinding = rebind.toInput;
        resolved.outputScanCode =
            platform::input::IsKeyboardBindingKey(resolved.triggerBinding) ? resolved.triggerBinding.code : 0;
        resolved.textBinding =
            (rebind.useCustomOutput && platform::input::IsValidBindingKey(rebind.customOutputKey)) ? rebind.customOutputKey
                                                                                                    : resolved.triggerBinding;
        resolved.textScanCode =
            platform::input::IsKeyboardBindingKey(resolved.textBinding) ? resolved.textBinding.code : 0;
        resolved.targetIsMouse = platform::input::IsMouseBindingKey(resolved.triggerBinding);
        resolved.customUnicode = rebind.useCustomOutput ? rebind.customOutputUnicode : 0;
        resolved.customShiftUnicode = rebind.useCustomOutput ? rebind.customOutputShiftUnicode : 0;
        return resolved;
    }

    return std::nullopt;
}

bool TryResolveRebindOutputCodepoint(GLFWwindow* window,
                                     const ResolvedRebindOutput& rebindOutput,
                                     int nativeMods,
                                     std::uint32_t& outCodepoint);

void QueuePendingCharRemap(GLFWwindow* window,
                          const ResolvedRebindOutput& rebindOutput,
                          const platform::input::InputEvent& event) {
    if (event.type != platform::input::InputEventType::Key) {
        return;
    }
    if (event.action != platform::input::InputAction::Press && event.action != platform::input::InputAction::Repeat) {
        return;
    }
    if (HasShortcutModifiersThatSuppressText(event.nativeMods)) {
        return;
    }

    std::lock_guard<std::mutex> lock(g_pendingCharRemapMutex);
    constexpr std::size_t kMaxPendingCharRemaps = 32;
    g_pendingCharRemaps.emplace_back();
    PendingCharRemap& pending = g_pendingCharRemaps.back();
    pending.sequence = g_pendingCharRemapSequence.fetch_add(1, std::memory_order_acq_rel) + 1;
    pending.consume = !TryResolveRebindOutputCodepoint(window, rebindOutput, event.nativeMods, pending.outputCodepoint);
    while (g_pendingCharRemaps.size() > kMaxPendingCharRemaps) {
        g_pendingCharRemaps.pop_front();
    }
}

void QueuePendingCharConsume(const platform::input::InputEvent& event) {
    if (event.type != platform::input::InputEventType::Key) {
        return;
    }
    if (event.action != platform::input::InputAction::Press && event.action != platform::input::InputAction::Repeat) {
        return;
    }
    if (HasShortcutModifiersThatSuppressText(event.nativeMods)) {
        return;
    }

    std::lock_guard<std::mutex> lock(g_pendingCharRemapMutex);
    constexpr std::size_t kMaxPendingCharRemaps = 32;
    g_pendingCharRemaps.emplace_back();
    PendingCharRemap& pending = g_pendingCharRemaps.back();
    pending.sequence = g_pendingCharRemapSequence.fetch_add(1, std::memory_order_acq_rel) + 1;
    pending.outputCodepoint = 0;
    pending.consume = true;
    while (g_pendingCharRemaps.size() > kMaxPendingCharRemaps) {
        g_pendingCharRemaps.pop_front();
    }
}

bool ConsumePendingCharRemap(PendingCharRemap& outRemap) {
    std::lock_guard<std::mutex> lock(g_pendingCharRemapMutex);
    if (g_pendingCharRemaps.empty()) {
        return false;
    }

    outRemap = g_pendingCharRemaps.front();
    g_pendingCharRemaps.pop_front();
    return true;
}

void ClearPendingCharRemaps() {
    std::lock_guard<std::mutex> lock(g_pendingCharRemapMutex);
    g_pendingCharRemaps.clear();
}

void MaybeClearPendingCharRemapsForRebindDisable(const platform::config::LinuxscreenConfig& config) {
    const int newState = config.keyRebinds.enabled ? 1 : 0;
    const int oldState = g_lastObservedRebindsEnabledState.exchange(newState, std::memory_order_acq_rel);
    if (oldState == 1 && newState == 0) {
        ClearPendingCharRemaps();
    }
}

bool TryResolveRebindOutputCodepoint(GLFWwindow* window,
                                     const ResolvedRebindOutput& rebindOutput,
                                     int nativeMods,
                                     std::uint32_t& outCodepoint) {
    outCodepoint = 0;
    const int effectiveNativeMods = GetEffectiveNativeMods(window, nativeMods);
    const bool shiftDown = (effectiveNativeMods & static_cast<int>(platform::input::GlfwMod::Shift)) != 0;
    const bool textIsKeyboard = platform::input::IsKeyboardBindingKey(rebindOutput.textBinding);

    if (shiftDown &&
        IsValidUnicodeScalar(rebindOutput.customShiftUnicode) &&
        textIsKeyboard) {
        outCodepoint = rebindOutput.customShiftUnicode;
        return true;
    }

    if (IsValidUnicodeScalar(rebindOutput.customUnicode) && textIsKeyboard) {
        outCodepoint = rebindOutput.customUnicode;
        return true;
    }

    if (textIsKeyboard &&
        TryResolveGlfwLayoutCodepoint(-1, rebindOutput.textScanCode, shiftDown, outCodepoint) &&
        outCodepoint != 0) {
        return true;
    }

    return false;
}

void HookedGlfwKeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
void HookedGlfwMouseButtonCallback(GLFWwindow* window, int button, int action, int mods);

void EnsureNativeRepeatDefaultsInitialized() {
    std::lock_guard<std::mutex> lock(g_nativeRepeatDefaultsMutex);
    if (g_nativeRepeatDefaultsInitialized) {
        return;
    }

    int startDelayMs = 400;
    int repeatDelayMs = 33;

#ifndef __APPLE__
    Display* dpy = glXGetCurrentDisplay();
    if (dpy == nullptr) {
        dpy = reinterpret_cast<Display*>(g_lastDisplay.load(std::memory_order_acquire));
    }

    if (dpy != nullptr) {
        unsigned int delay = 0;
        unsigned int interval = 0;
        if (XkbGetAutoRepeatRate(dpy, XkbUseCoreKbd, &delay, &interval)) {
            if (delay > 0) {
                startDelayMs = static_cast<int>(delay);
            }
            if (interval > 0) {
                repeatDelayMs = static_cast<int>(interval);
            }
        }
    }
#endif

    g_nativeRepeatStartDelayMs = std::max(1, startDelayMs);
    g_nativeRepeatDelayMs = std::max(1, repeatDelayMs);
    g_nativeRepeatDefaultsInitialized = true;
}

void GetNativeRepeatDefaults(int& outStartDelayMs, int& outRepeatDelayMs) {
    EnsureNativeRepeatDefaultsInitialized();
    std::lock_guard<std::mutex> lock(g_nativeRepeatDefaultsMutex);
    outStartDelayMs = g_nativeRepeatStartDelayMs;
    outRepeatDelayMs = g_nativeRepeatDelayMs;
}

bool IsRepeatBlacklistedSourceVk(platform::input::VkCode vk) {
    return vk == platform::input::VK_LBUTTON || vk == platform::input::VK_RBUTTON;
}

bool IsRepeatBlacklistedSourceBinding(const platform::input::BindingKey& binding) {
    return platform::input::IsMouseBindingKey(binding) && (binding.code == 0 || binding.code == 1);
}

bool IsConfiguredRebind(const platform::config::KeyRebind& rebind) {
    if (!platform::input::IsValidBindingKey(rebind.fromInput)) {
        return false;
    }
    if (rebind.consumeSourceInput || platform::input::IsValidBindingKey(rebind.toInput)) {
        return true;
    }
    if (!IsRepeatBlacklistedSourceBinding(rebind.fromInput) && rebind.keyRepeatDisabled) {
        return true;
    }
    if (!IsRepeatBlacklistedSourceBinding(rebind.fromInput) &&
        (rebind.keyRepeatStartDelay > 0 || rebind.keyRepeatDelay > 0)) {
        return true;
    }
    return false;
}

int FindBestRebindIndexForSource(const platform::config::LinuxscreenConfig& config, const platform::input::InputEvent& sourceEvent) {
    if (!platform::input::IsValidBindingKey(platform::input::BindingKeyFromInputEvent(sourceEvent))) {
        return -1;
    }

    int first = -1;
    int enabledAny = -1;
    int enabledConfigured = -1;
    int configuredAny = -1;
    for (int i = 0; i < static_cast<int>(config.keyRebinds.rebinds.size()); ++i) {
        const auto& rebind = config.keyRebinds.rebinds[static_cast<std::size_t>(i)];
        if (!MatchesRebindSourceBinding(sourceEvent, rebind)) {
            continue;
        }

        if (first == -1) {
            first = i;
        }

        const bool configured = IsConfiguredRebind(rebind);
        if (configured && configuredAny == -1) {
            configuredAny = i;
        }
        if (rebind.enabled && enabledAny == -1) {
            enabledAny = i;
        }
        if (rebind.enabled && configured) {
            enabledConfigured = i;
            break;
        }
    }

    if (enabledConfigured != -1) {
        return enabledConfigured;
    }
    if (configuredAny != -1) {
        return configuredAny;
    }
    if (enabledAny != -1) {
        return enabledAny;
    }
    return first;
}

ManagedRepeatSettings ResolveManagedRepeatSettings(const platform::config::LinuxscreenConfig& config,
                                                   const platform::input::InputEvent& event) {
    ManagedRepeatSettings resolved;
    if ((!HasKeyboardBindingIdentity(event) && event.type != platform::input::InputEventType::MouseButton) ||
        (event.type != platform::input::InputEventType::Key && event.type != platform::input::InputEventType::MouseButton)) {
        return resolved;
    }
    if (event.type == platform::input::InputEventType::Key &&
        (platform::input::IsModifierGlfwKey(event.nativeKey) || platform::input::IsModifierScanCode(event.nativeScanCode))) {
        return resolved;
    }
    if (IsRepeatBlacklistedSourceVk(event.vk)) {
        return resolved;
    }

    const bool sourceIsMouseButton = event.type == platform::input::InputEventType::MouseButton;

    int nativeStartDelayMs = 400;
    int nativeRepeatDelayMs = 33;
    GetNativeRepeatDefaults(nativeStartDelayMs, nativeRepeatDelayMs);

    const int globalStartDelayMs = std::clamp(config.keyRepeatStartDelay, 0, 500);
    const int globalRepeatDelayMs = std::clamp(config.keyRepeatDelay, 0, 500);

    int perKeyStartDelayMs = 0;
    int perKeyRepeatDelayMs = 0;
    bool perKeyDisableRepeat = false;
    if (config.keyRebinds.enabled) {
        const int bestRebindIndex = FindBestRebindIndexForSource(config, event);
        if (bestRebindIndex >= 0 && bestRebindIndex < static_cast<int>(config.keyRebinds.rebinds.size())) {
            const auto& rebind = config.keyRebinds.rebinds[static_cast<std::size_t>(bestRebindIndex)];
            perKeyDisableRepeat = rebind.keyRepeatDisabled;
            perKeyStartDelayMs = std::clamp(rebind.keyRepeatStartDelay, 0, 500);
            perKeyRepeatDelayMs = std::clamp(rebind.keyRepeatDelay, 0, 500);
        }
    }

    const bool allowGlobalForSource = !sourceIsMouseButton || config.keyRepeatAffectsMouseButtons;
    const bool hasGlobalOverride = allowGlobalForSource && (globalStartDelayMs > 0 || globalRepeatDelayMs > 0);
    const bool hasPerKeyOverride = perKeyDisableRepeat || perKeyStartDelayMs > 0 || perKeyRepeatDelayMs > 0;
    resolved.enabled = hasPerKeyOverride || hasGlobalOverride;
    if (!resolved.enabled) {
        return resolved;
    }

    if (perKeyDisableRepeat) {
        resolved.disableRepeat = true;
        return resolved;
    }

    const int fallbackGlobalStartDelayMs = (allowGlobalForSource && globalStartDelayMs > 0) ? globalStartDelayMs : 0;
    const int fallbackGlobalRepeatDelayMs = (allowGlobalForSource && globalRepeatDelayMs > 0) ? globalRepeatDelayMs : 0;

    const int effectiveStartDelayMs = (perKeyStartDelayMs > 0)
        ? perKeyStartDelayMs
        : ((fallbackGlobalStartDelayMs > 0) ? fallbackGlobalStartDelayMs : nativeStartDelayMs);
    const int effectiveRepeatDelayMs = (perKeyRepeatDelayMs > 0)
        ? perKeyRepeatDelayMs
        : ((fallbackGlobalRepeatDelayMs > 0) ? fallbackGlobalRepeatDelayMs : nativeRepeatDelayMs);

    resolved.effectiveStartDelayMs = std::max(1, effectiveStartDelayMs);
    resolved.effectiveRepeatDelayMs = std::max(1, effectiveRepeatDelayMs);
    return resolved;
}

ManagedRepeatKey BuildManagedRepeatKey(GLFWwindow* window, const platform::input::InputEvent& event) {
    ManagedRepeatKey key;
    key.window = window;
    key.sourceCode = event.nativeKey;
    key.sourceIsMouseButton = event.type == platform::input::InputEventType::MouseButton;
    return key;
}

void UpdateManagedRepeatKeyboardModsForWindow(GLFWwindow* window, int nativeMods) {
    if (!window) {
        return;
    }

    std::lock_guard<std::mutex> lock(g_managedRepeatMutex);
    for (auto& entry : g_managedRepeatStates) {
        ManagedRepeatState& state = entry.second;
        if (state.key.window != window || state.key.sourceIsMouseButton) {
            continue;
        }
        state.nativeMods = nativeMods;
    }
}

void InvalidateManagedRepeatKeyboardStatesForAdditionalPress(GLFWwindow* window,
                                                             const platform::input::InputEvent& event) {
    if (!window ||
        event.type != platform::input::InputEventType::Key ||
        event.action != platform::input::InputAction::Press ||
        (!HasKeyboardBindingIdentity(event)) ||
        platform::input::IsModifierScanCode(event.nativeScanCode)) {
        return;
    }

    std::lock_guard<std::mutex> lock(g_managedRepeatMutex);
    for (auto it = g_managedRepeatStates.begin(); it != g_managedRepeatStates.end();) {
        if (it->first.window == window && !it->first.sourceIsMouseButton) {
            g_managedRepeatInvalidatedKeys.insert(it->first);
            it = g_managedRepeatStates.erase(it);
        } else {
            ++it;
        }
    }
}

void ClearManagedRepeatStateForSource(GLFWwindow* window, const platform::input::InputEvent& event) {
    if (!window || event.nativeKey < 0) {
        return;
    }
    const ManagedRepeatKey key = BuildManagedRepeatKey(window, event);
    std::lock_guard<std::mutex> lock(g_managedRepeatMutex);
    g_managedRepeatStates.erase(key);
    g_managedRepeatInvalidatedKeys.erase(key);
}

void ClearManagedRepeatStatesForWindow(GLFWwindow* window) {
    std::lock_guard<std::mutex> lock(g_managedRepeatMutex);
    if (window == nullptr) {
        g_managedRepeatStates.clear();
        g_managedRepeatInvalidatedKeys.clear();
        return;
    }

    for (auto it = g_managedRepeatStates.begin(); it != g_managedRepeatStates.end();) {
        if (it->first.window == window) {
            it = g_managedRepeatStates.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = g_managedRepeatInvalidatedKeys.begin(); it != g_managedRepeatInvalidatedKeys.end();) {
        if (it->window == window) {
            it = g_managedRepeatInvalidatedKeys.erase(it);
        } else {
            ++it;
        }
    }
}

ManagedRepeatCharMode ResolveManagedRepeatCharMode(const platform::config::LinuxscreenConfig& config,
                                                   const ManagedRepeatState& state,
                                                   std::uint32_t& outCodepoint) {
    outCodepoint = 0;

    if (state.key.sourceIsMouseButton) {
        return ManagedRepeatCharMode::NoCharacter;
    }
    if (state.key.sourceCode < 0 && state.nativeScanCode <= 0) {
        return ManagedRepeatCharMode::NoCharacter;
    }

    platform::input::InputEvent repeatEvent;
    repeatEvent.type = platform::input::InputEventType::Key;
    repeatEvent.action = platform::input::InputAction::Repeat;
    repeatEvent.vk = state.sourceVk;
    repeatEvent.nativeKey = state.key.sourceCode;
    repeatEvent.nativeScanCode = state.nativeScanCode;
    repeatEvent.nativeMods = state.nativeMods;

    if (const auto rebindOutput = ResolveRebindOutput(config, repeatEvent, false)) {
        if (rebindOutput->consumeSourceInput) {
            if (!platform::input::IsNonTextVk(state.sourceVk)) {
                return ManagedRepeatCharMode::ConsumeNativeOnly;
            }
            return ManagedRepeatCharMode::NoCharacter;
        }

        std::uint32_t mappedCodepoint = 0;
        if (TryResolveRebindOutputCodepoint(state.key.window, *rebindOutput, state.nativeMods, mappedCodepoint) &&
            mappedCodepoint != 0) {
            if (platform::input::IsNonTextVk(state.sourceVk)) {
                return ManagedRepeatCharMode::HandledByKeyCallback;
            }
            outCodepoint = mappedCodepoint;
            return ManagedRepeatCharMode::InjectSynthetic;
        }
    }

    if (platform::input::IsNonTextVk(state.sourceVk)) {
        return ManagedRepeatCharMode::NoCharacter;
    }

    const int effectiveNativeMods = GetEffectiveNativeMods(state.key.window, state.nativeMods);
    const bool shiftDown = (effectiveNativeMods & static_cast<int>(platform::input::GlfwMod::Shift)) != 0;
    if (TryResolveGlfwLayoutCodepoint(state.key.sourceCode, state.nativeScanCode, shiftDown, outCodepoint) &&
        outCodepoint != 0) {
        return ManagedRepeatCharMode::InjectSynthetic;
    }
    return ManagedRepeatCharMode::NoCharacter;
}

void DispatchManagedSyntheticCharacter(GLFWwindow* window, std::uint32_t codepoint, int mods) {
    if (!window || codepoint == 0) {
        return;
    }
    if (HasShortcutModifiersThatSuppressText(mods)) {
        return;
    }

    platform::input::InputEvent charEvent;
    charEvent.type = platform::input::InputEventType::Character;
    charEvent.action = platform::input::InputAction::Press;
    charEvent.charCodepoint = codepoint;
    charEvent.nativeMods = mods;

    if (platform::x11::IsGuiVisible()) {
        return;
    }
    if (platform::x11::ShouldConsumeInputForOverlay(charEvent)) {
        return;
    }

    GlfwCharCallback userCharCallback = nullptr;
    GlfwCharModsCallback userCharModsCallback = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_glfwCallbackMutex);
        auto it = g_glfwCallbackMap.find(window);
        if (it != g_glfwCallbackMap.end()) {
            userCharCallback = it->second.character;
            userCharModsCallback = it->second.characterMods;
        }
    }

    if (userCharCallback) {
        userCharCallback(window, codepoint);
        return;
    }
    if (userCharModsCallback) {
        userCharModsCallback(window, codepoint, mods);
    }
}

bool HandleManagedNativeRepeatEvent(GLFWwindow* window,
                                    const platform::config::LinuxscreenConfig& config,
                                    const platform::input::InputEvent& event) {
    if (g_dispatchingManagedSyntheticRepeat) {
        return false;
    }
    if (!window || (!HasKeyboardBindingIdentity(event) && event.type != platform::input::InputEventType::MouseButton) || event.nativeKey < 0) {
        return false;
    }

    const ManagedRepeatKey key = BuildManagedRepeatKey(window, event);

    if (event.action == platform::input::InputAction::Release) {
        std::lock_guard<std::mutex> lock(g_managedRepeatMutex);
        g_managedRepeatStates.erase(key);
        g_managedRepeatInvalidatedKeys.erase(key);
        return false;
    }

    bool suppressForInvalidatedSource = false;
    {
        std::lock_guard<std::mutex> lock(g_managedRepeatMutex);
        if (event.action == platform::input::InputAction::Press) {
            g_managedRepeatInvalidatedKeys.erase(key);
        } else if (event.action == platform::input::InputAction::Repeat &&
                   g_managedRepeatInvalidatedKeys.find(key) != g_managedRepeatInvalidatedKeys.end()) {
            suppressForInvalidatedSource = true;
        }
    }
    if (suppressForInvalidatedSource) {
        if (event.type == platform::input::InputEventType::Key &&
            !platform::input::IsNonTextVk(event.vk)) {
            QueuePendingCharConsume(event);
        }
        return true;
    }

    const ManagedRepeatSettings settings = ResolveManagedRepeatSettings(config, event);

    if (!settings.enabled) {
        std::lock_guard<std::mutex> lock(g_managedRepeatMutex);
        g_managedRepeatStates.erase(key);
        return false;
    }

    const auto now = std::chrono::steady_clock::now();
    if (event.action == platform::input::InputAction::Press) {
        if (settings.disableRepeat) {
            std::lock_guard<std::mutex> lock(g_managedRepeatMutex);
            g_managedRepeatStates.erase(key);
            return false;
        }

        ManagedRepeatState state;
        state.key = key;
        state.sourceVk = event.vk;
        state.nativeScanCode = event.nativeScanCode;
        state.nativeMods = event.nativeMods;
        if (event.type == platform::input::InputEventType::Key) {
            state.keyboardPressOrder = g_managedKeyboardPressSequence.fetch_add(1, std::memory_order_acq_rel) + 1;
        }
        state.effectiveRepeatDelayMs = settings.effectiveRepeatDelayMs;
        state.nextRepeatTime = now + std::chrono::milliseconds(settings.effectiveStartDelayMs);

        std::lock_guard<std::mutex> lock(g_managedRepeatMutex);
        g_managedRepeatStates[key] = state;
        return false;
    }

    if (event.action != platform::input::InputAction::Repeat) {
        return false;
    }

    if (settings.disableRepeat) {
        std::lock_guard<std::mutex> lock(g_managedRepeatMutex);
        g_managedRepeatStates.erase(key);

        if (event.type == platform::input::InputEventType::Key &&
            !platform::input::IsNonTextVk(event.vk)) {
            QueuePendingCharConsume(event);
        }
        return true;
    }

    ManagedRepeatState stateCopy;
    {
        std::lock_guard<std::mutex> lock(g_managedRepeatMutex);
        auto it = g_managedRepeatStates.find(key);
        if (it == g_managedRepeatStates.end()) {
            ManagedRepeatState state;
            state.key = key;
            state.sourceVk = event.vk;
            state.nativeScanCode = event.nativeScanCode;
            state.nativeMods = event.nativeMods;
            if (event.type == platform::input::InputEventType::Key) {
                state.keyboardPressOrder = g_managedKeyboardPressSequence.fetch_add(1, std::memory_order_acq_rel) + 1;
            }
            state.effectiveRepeatDelayMs = settings.effectiveRepeatDelayMs;
            state.nextRepeatTime = now + std::chrono::milliseconds(settings.effectiveRepeatDelayMs);
            it = g_managedRepeatStates.emplace(key, state).first;
        } else {
            it->second.sourceVk = event.vk;
            it->second.nativeScanCode = event.nativeScanCode;
            it->second.nativeMods = event.nativeMods;
            it->second.effectiveRepeatDelayMs = settings.effectiveRepeatDelayMs;
        }
        stateCopy = it->second;
    }

    std::uint32_t codepoint = 0;
    const ManagedRepeatCharMode charMode = ResolveManagedRepeatCharMode(config, stateCopy, codepoint);
    if (charMode == ManagedRepeatCharMode::InjectSynthetic ||
        charMode == ManagedRepeatCharMode::ConsumeNativeOnly) {
        QueuePendingCharConsume(event);
    }

    return true;
}

void PumpManagedRepeatScheduler(GLFWwindow* preferredWindow) {
    auto configSnapshot = platform::config::GetConfigSnapshot();
    if (!configSnapshot) {
        return;
    }

    if (platform::x11::IsGuiVisible()) {
        return;
    }

    struct DispatchItem {
        ManagedRepeatState state;
        bool sourceIsMouseButton = false;
    };

    std::vector<DispatchItem> dueItems;
    const auto now = std::chrono::steady_clock::now();
    {
        std::lock_guard<std::mutex> lock(g_managedRepeatMutex);
        if (g_managedRepeatStates.empty()) {
            return;
        }

        std::map<GLFWwindow*, std::pair<std::uint64_t, int>> keyboardRepeatOwnerByWindow;
        for (const auto& entry : g_managedRepeatStates) {
            const ManagedRepeatState& candidate = entry.second;
            if (candidate.key.sourceIsMouseButton || !candidate.key.window) {
                continue;
            }
            auto& owner = keyboardRepeatOwnerByWindow[candidate.key.window];
            if (candidate.keyboardPressOrder >= owner.first) {
                owner.first = candidate.keyboardPressOrder;
                owner.second = candidate.key.sourceCode;
            }
        }

        for (auto it = g_managedRepeatStates.begin(); it != g_managedRepeatStates.end();) {
            ManagedRepeatState& state = it->second;
            if (!state.key.window) {
                it = g_managedRepeatStates.erase(it);
                continue;
            }

            platform::input::InputEvent sourceEvent;
            sourceEvent.type = state.key.sourceIsMouseButton ? platform::input::InputEventType::MouseButton
                                                             : platform::input::InputEventType::Key;
            sourceEvent.vk = state.sourceVk;
            sourceEvent.nativeKey = state.key.sourceCode;
            sourceEvent.nativeScanCode = state.nativeScanCode;
            sourceEvent.nativeMods = state.nativeMods;

            const ManagedRepeatSettings settings = ResolveManagedRepeatSettings(*configSnapshot, sourceEvent);
            if (!settings.enabled) {
                it = g_managedRepeatStates.erase(it);
                continue;
            }
            if (settings.disableRepeat) {
                it = g_managedRepeatStates.erase(it);
                continue;
            }

            if (!state.key.sourceIsMouseButton) {
                const auto ownerIt = keyboardRepeatOwnerByWindow.find(state.key.window);
                const bool isRepeatOwner = ownerIt != keyboardRepeatOwnerByWindow.end() &&
                                           ownerIt->second.second == state.key.sourceCode;
                if (!isRepeatOwner) {
                    state.nextRepeatTime = now + std::chrono::milliseconds(settings.effectiveStartDelayMs);
                    ++it;
                    continue;
                }
            }

            state.effectiveRepeatDelayMs = settings.effectiveRepeatDelayMs;

            int emitted = 0;
            while (now >= state.nextRepeatTime && emitted < 8) {
                dueItems.push_back(DispatchItem{ state, state.key.sourceIsMouseButton });
                state.nextRepeatTime += std::chrono::milliseconds(settings.effectiveRepeatDelayMs);
                ++emitted;
            }

            if (emitted == 8 && now >= state.nextRepeatTime) {
                state.nextRepeatTime = now + std::chrono::milliseconds(settings.effectiveRepeatDelayMs);
            }

            ++it;
        }
    }

    for (const DispatchItem& item : dueItems) {
        GLFWwindow* targetWindow = item.state.key.window ? item.state.key.window : preferredWindow;
        if (!targetWindow) {
            targetWindow = g_lastSwapWindow.load(std::memory_order_acquire);
        }
        if (!targetWindow) {
            continue;
        }

        g_dispatchingManagedSyntheticRepeat = true;
        if (item.sourceIsMouseButton) {
            HookedGlfwMouseButtonCallback(targetWindow,
                                          item.state.key.sourceCode,
                                          static_cast<int>(platform::input::GlfwAction::Repeat),
                                          item.state.nativeMods);
            g_dispatchingManagedSyntheticRepeat = false;
            continue;
        }

        std::uint32_t codepoint = 0;
        const ManagedRepeatCharMode charMode = ResolveManagedRepeatCharMode(*configSnapshot, item.state, codepoint);
        HookedGlfwKeyCallback(targetWindow,
                              item.state.key.sourceCode,
                              item.state.nativeScanCode,
                              static_cast<int>(platform::input::GlfwAction::Repeat),
                              item.state.nativeMods);
        g_dispatchingManagedSyntheticRepeat = false;

        if (charMode == ManagedRepeatCharMode::InjectSynthetic && codepoint != 0) {
            DispatchManagedSyntheticCharacter(targetWindow, codepoint, item.state.nativeMods);
        }
    }
}

void HookedGlfwCharCallback(GLFWwindow* window, unsigned int codepoint);
void HookedGlfwCharModsCallback(GLFWwindow* window, unsigned int codepoint, int mods);
void HookedGlfwWindowSizeCallback(GLFWwindow* window, int width, int height);
void HookedGlfwFramebufferSizeCallback(GLFWwindow* window, int width, int height);

void EnsureCharCallbackInstalled(GLFWwindow* window) {
    if (g_charCallbackInstalled.load(std::memory_order_acquire)) { return; }

    GlfwSetCharCallbackFn realSetter = GetRealGlfwSetCharCallback();
    if (!realSetter) { return; }

    GlfwCharCallback prev = realSetter(window, HookedGlfwCharCallback);
    if (prev && prev != HookedGlfwCharCallback) {
        std::lock_guard<std::mutex> lock(g_glfwCallbackMutex);
        GlfwCallbackState& state = g_glfwCallbackMap[window];
        if (!state.character) { state.character = prev; }
    }

    g_charCallbackInstalled.store(true, std::memory_order_release);
    LogDebugOnce(g_loggedProactiveCharCallbackInstall,
                 "proactively installed glfw char callback on window (game never called glfwSetCharCallback)");
}

void HookedGlfwKeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (window) {
        g_lastSwapWindow.store(window, std::memory_order_release);
    }

    platform::x11::RegisterImGuiOverlayWindow(window);
    LogDebugOnce(g_loggedFirstGlfwKeyCallback, "first glfw key callback intercepted");
    EnsureCharCallbackInstalled(window);

    platform::input::InputEvent event;
    event.type = platform::input::InputEventType::Key;
    event.action = platform::input::GlfwActionToInputAction(action);
    event.vk = platform::input::GlfwKeyToVk(key, scancode, mods);
    event.nativeKey = key;
    event.nativeScanCode = scancode;
    event.nativeMods = mods;
    const bool syntheticManagedRepeatEvent =
        g_dispatchingManagedSyntheticRepeat &&
        event.action == platform::input::InputAction::Repeat;

    if (!syntheticManagedRepeatEvent &&
        (event.action == platform::input::InputAction::Press ||
         event.action == platform::input::InputAction::Release)) {
        UpdateManagedRepeatKeyboardModsForWindow(window, event.nativeMods);
    }

    UpdateSyntheticPhysicalKeyState(window, key, action);

    if (!syntheticManagedRepeatEvent &&
        (event.vk != platform::input::VK_NONE || event.nativeScanCode > 0) &&
        (event.action == platform::input::InputAction::Press || event.action == platform::input::InputAction::Release ||
         event.action == platform::input::InputAction::Repeat)) {
        platform::config::RegisterBindingInputEvent(platform::input::BindingKeyFromInputEvent(event),
                                                    event.vk,
                                                    event.nativeKey,
                                                    event.nativeMods,
                                                    platform::input::IsModifierGlfwKey(event.nativeKey) ||
                                                        platform::input::IsModifierScanCode(event.nativeScanCode),
                                                    event.action);
    }

    InitializeHotkeyDispatcherFromConfig();

    RefreshHotkeyDispatcherIfNeeded();

    if (!syntheticManagedRepeatEvent && platform::config::g_hotkeyCapturing.load(std::memory_order_acquire)) {
        const platform::config::CaptureTarget captureTarget = platform::config::GetCaptureTarget();
        const bool modalCapture = captureTarget == platform::config::CaptureTarget::Hotkey ||
                                  captureTarget == platform::config::CaptureTarget::GuiHotkey ||
                                  captureTarget == platform::config::CaptureTarget::RebindToggleHotkey ||
                                  captureTarget == platform::config::CaptureTarget::AltSecondary ||
                                  captureTarget == platform::config::CaptureTarget::Exclusion ||
                                  captureTarget == platform::config::CaptureTarget::SensitivityHotkey ||
                                  captureTarget == platform::config::CaptureTarget::SensitivityExclusion ||
                                  captureTarget == platform::config::CaptureTarget::RebindFrom ||
                                  captureTarget == platform::config::CaptureTarget::RebindTo ||
                                  captureTarget == platform::config::CaptureTarget::RebindTypes ||
                                  captureTarget == platform::config::CaptureTarget::RebindDraftInput;

        if (modalCapture) {
            PublishImGuiInputEvent(event, "glfwKeyCallback");
        }
        return;
    }

    bool toggledGui = false;
    if (!syntheticManagedRepeatEvent && (event.vk != platform::input::VK_NONE || HasKeyboardBindingIdentity(event))) {
        toggledGui = ProcessInputEventForGuiToggle(window, event, "glfwKeyCallback");
    }

    auto configSnapshot = platform::config::GetConfigSnapshot();
    if (configSnapshot) {
        MaybeClearPendingCharRemapsForRebindDisable(*configSnapshot);
    }
    const bool guiVisibleNow = platform::x11::IsGuiVisible();

    std::string gameState;
    if ((event.vk != platform::input::VK_NONE || HasKeyboardBindingIdentity(event)) && !toggledGui && !guiVisibleNow) {
        gameState = platform::config::GetCurrentGameState();
    }

    std::optional<ResolvedRebindOutput> rebindOutput;
    platform::input::BindingKey rebindTargetKey;
    if (configSnapshot && (event.vk != platform::input::VK_NONE || HasKeyboardBindingIdentity(event)) && !toggledGui && !guiVisibleNow) {
        rebindOutput = ResolveRebindOutput(*configSnapshot, event, false);
        if (rebindOutput && !rebindOutput->consumeSourceInput) {
            rebindTargetKey = rebindOutput->triggerBinding;
        }
    }

    platform::input::HotkeyEvaluationResult hotkeyResult;
    bool sensitivityMatchedViaRebind = false;
    {
        std::lock_guard<std::mutex> lock(g_inputStateMutex);
        if (!syntheticManagedRepeatEvent) {
            g_keyStateTracker.ApplyEvent(event);
        }

        if (!syntheticManagedRepeatEvent &&
            configSnapshot &&
            (event.vk != platform::input::VK_NONE || HasKeyboardBindingIdentity(event)) &&
            !toggledGui &&
            !guiVisibleNow) {
            hotkeyResult = g_hotkeyDispatcher().Evaluate(g_keyStateTracker,
                                                        event,
                                                        gameState,
                                                        platform::x11::GetMirrorModeState().GetActiveModeName(),
                                                        configSnapshot->defaultMode,
                                                        rebindTargetKey);
            if (!hotkeyResult.matched) {
                sensitivityMatchedViaRebind = EvaluateSensitivityHotkeys(*configSnapshot,
                                                                         g_keyStateTracker,
                                                                         event,
                                                                         gameState,
                                                                         rebindTargetKey);
            }
        }
    }

    if (hotkeyResult.fired) {
        ProcessModeSwitchHotkey(hotkeyResult.targetMode, hotkeyResult.hotkeyIndex);
    }

    PublishImGuiInputEvent(event, "glfwKeyCallback");

    if (guiVisibleNow) {
        if (event.action == platform::input::InputAction::Release) {
            ClearManagedRepeatStateForSource(window, event);
        }
        return;
    }

    if (hotkeyResult.blockKeyFromGame) {
        if (event.action == platform::input::InputAction::Release) {
            ClearManagedRepeatStateForSource(window, event);
        }
        return;
    }
    if (sensitivityMatchedViaRebind) {
        if (event.action == platform::input::InputAction::Release) {
            ClearManagedRepeatStateForSource(window, event);
        }
        return;
    }

    const bool consumeForOverlay = toggledGui || platform::x11::ShouldConsumeInputForOverlay(event);
    if (consumeForOverlay) {
        if (event.action == platform::input::InputAction::Release) {
            ClearManagedRepeatStateForSource(window, event);
        }
        return;
    }

    if (!syntheticManagedRepeatEvent) {
        InvalidateManagedRepeatKeyboardStatesForAdditionalPress(window, event);
    }

    if (configSnapshot && (event.vk != platform::input::VK_NONE || HasKeyboardBindingIdentity(event))) {
        const bool suppressNativeRepeat = HandleManagedNativeRepeatEvent(window, *configSnapshot, event);
        if (suppressNativeRepeat) {
            return;
        }
    }

    GlfwKeyCallback userCallback = nullptr;
    GlfwMouseButtonCallback userMouseCallback = nullptr;
    GlfwCharCallback userCharCallback = nullptr;
    GlfwCharModsCallback userCharModsCallback = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_glfwCallbackMutex);
        auto it = g_glfwCallbackMap.find(window);
        if (it != g_glfwCallbackMap.end()) {
            userCallback = it->second.key;
            userMouseCallback = it->second.mouseButton;
            userCharCallback = it->second.character;
            userCharModsCallback = it->second.characterMods;
        }
    }

    if (!rebindOutput && configSnapshot) {
        rebindOutput = ResolveRebindOutput(*configSnapshot, event, false);
    }

    if (rebindOutput) {
        const bool skipManagedSyntheticRepeatCharQueue =
            g_dispatchingManagedSyntheticRepeat &&
            event.action == platform::input::InputAction::Repeat;
        const bool sourceIsNonText = platform::input::IsNonTextVk(event.vk);
        if (rebindOutput->consumeSourceInput) {
            if (event.action == platform::input::InputAction::Release) {
                ClearSyntheticRebindSourceKey(window, key);
            } else if (!sourceIsNonText && !skipManagedSyntheticRepeatCharQueue) {
                QueuePendingCharConsume(event);
            }
            return;
        }

        if (!sourceIsNonText && !skipManagedSyntheticRepeatCharQueue) {
            QueuePendingCharRemap(window, *rebindOutput, event);
        }

        if (rebindOutput->targetIsMouse) {
            if (event.action == platform::input::InputAction::Release) {
                ClearSyntheticRebindSourceKey(window, key);
            }
            const int mappedButton = rebindOutput->triggerBinding.code;
            if (mappedButton < 0) {
                LogDebugOnce(g_loggedUnsupportedMouseRebindMapping,
                             "key rebind matched but mouse target VK has no GLFW button mapping; forwarding original key event");
            } else if (userMouseCallback) {
                int forwardedAction = action;
                if (forwardedAction == static_cast<int>(platform::input::GlfwAction::Repeat)) {
                    forwardedAction = static_cast<int>(platform::input::GlfwAction::Press);
                }
                userMouseCallback(window, mappedButton, forwardedAction, mods);
                return;
            } else {
                LogDebugOnce(g_loggedMissingRebindMouseDispatchCallback,
                             "key->mouse rebind matched but mouse callback is missing; forwarding original key event");
            }
        } else {
            const int mappedKey = -1;
            if (rebindOutput->outputScanCode <= 0) {
                LogDebugOnce(g_loggedUnsupportedKeyRebindMapping,
                             "key rebind matched but keyboard target binding has no scan code; forwarding original key event");
            } else if (userCallback) {
                const int mappedScanCode = rebindOutput->outputScanCode;
                const int effectiveMods = GetEffectiveNativeMods(window, mods);
                userCallback(window, mappedKey, mappedScanCode, action, effectiveMods);
                if (sourceIsNonText &&
                    (event.action == platform::input::InputAction::Press ||
                     event.action == platform::input::InputAction::Repeat)) {
                    std::uint32_t remappedCodepoint = 0;
                    if (!HasShortcutModifiersThatSuppressText(effectiveMods) &&
                        TryResolveRebindOutputCodepoint(window, *rebindOutput, effectiveMods, remappedCodepoint) &&
                        remappedCodepoint != 0) {
                        if (userCharCallback) {
                            userCharCallback(window, remappedCodepoint);
                        } else if (userCharModsCallback) {
                            userCharModsCallback(window, remappedCodepoint, effectiveMods);
                        }
                    }
                }
                return;
            } else {
                LogDebugOnce(g_loggedMissingGlfwKeyUserCallback,
                             "key rebind matched but key callback is missing; dropping remap and forwarding original event if possible");
            }
        }
    }

    if (!userCallback) {
        LogDebugOnce(g_loggedMissingGlfwKeyUserCallback, "hooked GLFW key callback had no user callback for this window");
        return;
    }
    if (ShouldSuppressForwardedKeyEvent(window, key, action)) {
        return;
    }
    if (event.action == platform::input::InputAction::Release) {
        ClearSyntheticRebindSourceKey(window, key);
    }
    userCallback(window, key, scancode, action, mods);
}

void HookedGlfwCharCallback(GLFWwindow* window, unsigned int codepoint) {
    platform::x11::RegisterImGuiOverlayWindow(window);
    LogDebugOnce(g_loggedFirstGlfwCharCallback, "first glfw char callback intercepted");

    platform::input::InputEvent event;
    event.type = platform::input::InputEventType::Character;
    event.action = platform::input::InputAction::Press;
    event.charCodepoint = codepoint;
    event.nativeKey = static_cast<int>(codepoint);
    PublishImGuiInputEvent(event, "glfwCharCallback");

    if (AreShortcutModifiersCurrentlyDown()) {
        return;
    }

    if (platform::x11::IsGuiVisible()) { return; }
    if (platform::x11::ShouldConsumeInputForOverlay(event)) { return; }

    GlfwCharCallback userCallback = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_glfwCallbackMutex);
        auto it = g_glfwCallbackMap.find(window);
        if (it != g_glfwCallbackMap.end()) { userCallback = it->second.character; }
    }

    if (!userCallback) {
        LogDebugOnce(g_loggedMissingGlfwCharUserCallback, "hooked GLFW char callback had no user callback for this window");
        return;
    }

    std::uint32_t forwardedCodepoint = codepoint;
    bool consumeChar = false;
    PendingCharRemap pending;
    if (ConsumePendingCharRemap(pending)) {
        consumeChar = pending.consume;
        if (!consumeChar && pending.outputCodepoint != 0) {
            forwardedCodepoint = pending.outputCodepoint;
        }
    }

    if (consumeChar) {
        return;
    }
    userCallback(window, forwardedCodepoint);
}

void HookedGlfwCharModsCallback(GLFWwindow* window, unsigned int codepoint, int mods) {
    platform::x11::RegisterImGuiOverlayWindow(window);
    LogDebugOnce(g_loggedFirstGlfwCharModsCallback, "first glfw char-mods callback intercepted");

    platform::input::InputEvent event;
    event.type = platform::input::InputEventType::Character;
    event.action = platform::input::InputAction::Press;
    event.charCodepoint = codepoint;
    event.nativeKey = static_cast<int>(codepoint);
    event.nativeMods = mods;
    if (!g_charCallbackInstalled.load(std::memory_order_acquire)) {
        PublishImGuiInputEvent(event, "glfwCharModsCallback");
    }

    if (HasShortcutModifiersThatSuppressText(mods)) {
        return;
    }

    if (platform::x11::IsGuiVisible()) { return; }
    if (platform::x11::ShouldConsumeInputForOverlay(event)) { return; }

    GlfwCharModsCallback userCallback = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_glfwCallbackMutex);
        auto it = g_glfwCallbackMap.find(window);
        if (it != g_glfwCallbackMap.end()) { userCallback = it->second.characterMods; }
    }

    if (!userCallback) {
        LogDebugOnce(g_loggedMissingGlfwCharModsUserCallback, "hooked GLFW char-mods callback had no user callback for this window");
        return;
    }

    std::uint32_t forwardedCodepoint = codepoint;
    bool consumeChar = false;
    PendingCharRemap pending;
    if (ConsumePendingCharRemap(pending)) {
        consumeChar = pending.consume;
        if (!consumeChar && pending.outputCodepoint != 0) {
            forwardedCodepoint = pending.outputCodepoint;
        }
    }

    if (consumeChar) {
        return;
    }
    userCallback(window, forwardedCodepoint, mods);
}

void HookedGlfwMouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
    if (window) {
        g_lastSwapWindow.store(window, std::memory_order_release);
    }
    platform::x11::RegisterImGuiOverlayWindow(window);
    LogDebugOnce(g_loggedFirstGlfwMouseCallback, "first glfw mouse-button callback intercepted");

    platform::input::InputEvent event;
    event.type = platform::input::InputEventType::MouseButton;
    event.action = platform::input::GlfwActionToInputAction(action);
    event.vk = platform::input::GlfwMouseButtonToVk(button);
    event.nativeKey = button;
    event.nativeMods = mods;
    const bool syntheticManagedRepeatEvent =
        g_dispatchingManagedSyntheticRepeat &&
        event.action == platform::input::InputAction::Repeat;

    if (!syntheticManagedRepeatEvent &&
        event.vk != platform::input::VK_NONE &&
        (event.action == platform::input::InputAction::Press || event.action == platform::input::InputAction::Release ||
         event.action == platform::input::InputAction::Repeat)) {
        platform::config::RegisterBindingInputEvent(platform::input::BindingKeyFromInputEvent(event),
                                                    event.vk,
                                                    event.nativeKey,
                                                    event.nativeMods,
                                                    false,
                                                    event.action);
    }

    InitializeHotkeyDispatcherFromConfig();

    RefreshHotkeyDispatcherIfNeeded();

    if (!syntheticManagedRepeatEvent && platform::config::g_hotkeyCapturing.load(std::memory_order_acquire)) {
        const platform::config::CaptureTarget captureTarget = platform::config::GetCaptureTarget();
        const bool modalCapture = captureTarget == platform::config::CaptureTarget::Hotkey ||
                                  captureTarget == platform::config::CaptureTarget::GuiHotkey ||
                                  captureTarget == platform::config::CaptureTarget::RebindToggleHotkey ||
                                  captureTarget == platform::config::CaptureTarget::AltSecondary ||
                                  captureTarget == platform::config::CaptureTarget::Exclusion ||
                                  captureTarget == platform::config::CaptureTarget::SensitivityHotkey ||
                                  captureTarget == platform::config::CaptureTarget::SensitivityExclusion ||
                                  captureTarget == platform::config::CaptureTarget::RebindFrom ||
                                  captureTarget == platform::config::CaptureTarget::RebindTo ||
                                  captureTarget == platform::config::CaptureTarget::RebindTypes ||
                                  captureTarget == platform::config::CaptureTarget::RebindDraftInput;

        if (modalCapture) {
            PublishImGuiInputEvent(event, "glfwMouseButtonCallback");
        }
        return;
    }

    bool toggledGui = false;
    if (!syntheticManagedRepeatEvent && event.vk != platform::input::VK_NONE) {
        toggledGui = ProcessInputEventForGuiToggle(window, event, "glfwMouseButtonCallback");
    }

    auto configSnapshot = platform::config::GetConfigSnapshot();
    if (configSnapshot) {
        MaybeClearPendingCharRemapsForRebindDisable(*configSnapshot);
    }
    const bool guiVisibleNow = platform::x11::IsGuiVisible();

    std::string gameState;
    if ((event.vk != platform::input::VK_NONE || HasKeyboardBindingIdentity(event)) && !toggledGui && !guiVisibleNow) {
        gameState = platform::config::GetCurrentGameState();
    }

    std::optional<ResolvedRebindOutput> rebindOutput;
    platform::input::BindingKey rebindTargetKey;
    if (configSnapshot && (event.vk != platform::input::VK_NONE || HasKeyboardBindingIdentity(event)) && !toggledGui && !guiVisibleNow) {
        rebindOutput = ResolveRebindOutput(*configSnapshot, event, false);
        if (rebindOutput && !rebindOutput->consumeSourceInput) {
            rebindTargetKey = rebindOutput->triggerBinding;
        }
    }

    platform::input::HotkeyEvaluationResult hotkeyResult;
    bool sensitivityMatchedViaRebind = false;
    {
        std::lock_guard<std::mutex> lock(g_inputStateMutex);
        if (!syntheticManagedRepeatEvent) {
            g_keyStateTracker.ApplyEvent(event);
        }

        if (!syntheticManagedRepeatEvent &&
            configSnapshot &&
            (event.vk != platform::input::VK_NONE || HasKeyboardBindingIdentity(event)) &&
            !toggledGui &&
            !guiVisibleNow) {
            hotkeyResult = g_hotkeyDispatcher().Evaluate(g_keyStateTracker,
                                                        event,
                                                        gameState,
                                                        platform::x11::GetMirrorModeState().GetActiveModeName(),
                                                        configSnapshot->defaultMode,
                                                        rebindTargetKey);
            if (!hotkeyResult.matched) {
                sensitivityMatchedViaRebind = EvaluateSensitivityHotkeys(*configSnapshot,
                                                                         g_keyStateTracker,
                                                                         event,
                                                                         gameState,
                                                                         rebindTargetKey);
            }
        }
    }

    if (hotkeyResult.fired) {
        ProcessModeSwitchHotkey(hotkeyResult.targetMode, hotkeyResult.hotkeyIndex);
    }

    PublishImGuiInputEvent(event, "glfwMouseButtonCallback");

    if (guiVisibleNow) {
        if (event.action == platform::input::InputAction::Release) {
            ClearManagedRepeatStateForSource(window, event);
        }
        return;
    }

    if (hotkeyResult.blockKeyFromGame) {
        if (event.action == platform::input::InputAction::Release) {
            ClearManagedRepeatStateForSource(window, event);
        }
        return;
    }
    if (sensitivityMatchedViaRebind) {
        if (event.action == platform::input::InputAction::Release) {
            ClearManagedRepeatStateForSource(window, event);
        }
        return;
    }

    if (toggledGui || platform::x11::ShouldConsumeInputForOverlay(event)) {
        if (event.action == platform::input::InputAction::Release) {
            ClearManagedRepeatStateForSource(window, event);
        }
        return;
    }

    if (configSnapshot && (event.vk != platform::input::VK_NONE || HasKeyboardBindingIdentity(event))) {
        const bool suppressNativeRepeat = HandleManagedNativeRepeatEvent(window, *configSnapshot, event);
        if (suppressNativeRepeat) {
            return;
        }
    }

    GlfwMouseButtonCallback userCallback = nullptr;
    GlfwKeyCallback userKeyCallback = nullptr;
    GlfwCharCallback userCharCallback = nullptr;
    GlfwCharModsCallback userCharModsCallback = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_glfwCallbackMutex);
        auto it = g_glfwCallbackMap.find(window);
        if (it != g_glfwCallbackMap.end()) {
            userCallback = it->second.mouseButton;
            userKeyCallback = it->second.key;
            userCharCallback = it->second.character;
            userCharModsCallback = it->second.characterMods;
        }
    }

    if (rebindOutput) {
        const int syntheticMouseSourceKey = GetSyntheticRebindMouseSourceKey(button);
        if (rebindOutput->consumeSourceInput) {
            if (event.action == platform::input::InputAction::Release) {
                ClearSyntheticRebindSourceKey(window, syntheticMouseSourceKey);
            }
            return;
        }

        if (rebindOutput->targetIsMouse) {
            if (event.action == platform::input::InputAction::Release) {
                ClearSyntheticRebindSourceKey(window, syntheticMouseSourceKey);
            }
            const int mappedButton = rebindOutput->triggerBinding.code;
            if (mappedButton >= 0 && userCallback) {
                int forwardedAction = action;
                if (forwardedAction == static_cast<int>(platform::input::GlfwAction::Repeat)) {
                    forwardedAction = static_cast<int>(platform::input::GlfwAction::Press);
                }
                const int effectiveMods = GetEffectiveNativeMods(window, mods);
                userCallback(window, mappedButton, forwardedAction, effectiveMods);
                return;
            }
            if (mappedButton < 0) {
                LogDebugOnce(g_loggedUnsupportedMouseRebindMapping,
                             "mouse rebind matched but mouse target VK has no GLFW button mapping; forwarding original mouse event");
            } else {
                LogDebugOnce(g_loggedMissingGlfwMouseUserCallback,
                             "mouse rebind matched but mouse callback is missing; forwarding original mouse event");
            }
        } else {
            const int mappedKey = -1;
            if (rebindOutput->outputScanCode > 0 && userKeyCallback) {
                const int effectiveMods = GetEffectiveNativeMods(window, mods);
                int mappedAction = action;
                if (mappedAction == static_cast<int>(platform::input::GlfwAction::Repeat)) {
                    mappedAction = static_cast<int>(platform::input::GlfwAction::Press);
                }
                userKeyCallback(window, mappedKey, rebindOutput->outputScanCode, mappedAction, effectiveMods);
                if (event.action == platform::input::InputAction::Press ||
                    event.action == platform::input::InputAction::Repeat) {
                    std::uint32_t remappedCodepoint = 0;
                    if (!HasShortcutModifiersThatSuppressText(effectiveMods) &&
                        TryResolveRebindOutputCodepoint(window, *rebindOutput, effectiveMods, remappedCodepoint) &&
                        remappedCodepoint != 0) {
                        if (userCharCallback) {
                            userCharCallback(window, remappedCodepoint);
                        } else if (userCharModsCallback) {
                            userCharModsCallback(window, remappedCodepoint, effectiveMods);
                        }
                    }
                }
                return;
            }
            if (rebindOutput->outputScanCode <= 0) {
                LogDebugOnce(g_loggedUnsupportedKeyRebindMapping,
                             "mouse rebind matched but keyboard target binding has no scan code; forwarding original mouse event");
            } else {
                LogDebugOnce(g_loggedMissingRebindKeyDispatchCallback,
                             "mouse->key rebind matched but key callback is missing; forwarding original mouse event");
            }
        }
    }

    if (!userCallback) {
        LogDebugOnce(g_loggedMissingGlfwMouseUserCallback, "hooked GLFW mouse callback had no user callback for this window");
        return;
    }
    if (event.action == platform::input::InputAction::Release) {
        ClearSyntheticRebindSourceKey(window, GetSyntheticRebindMouseSourceKey(button));
    }
    int forwardedAction = action;
    if (forwardedAction == static_cast<int>(platform::input::GlfwAction::Repeat)) {
        forwardedAction = static_cast<int>(platform::input::GlfwAction::Press);
    }
    userCallback(window, button, forwardedAction, mods);
}

bool ShouldSuppressPendingSyntheticCursorPosCallback(GLFWwindow* window,
                                                    double xpos,
                                                    double ypos,
                                                    const char*& outReason) {
    outReason = nullptr;
    if (!g_pendingSyntheticCursorPosCallback.valid.load(std::memory_order_acquire)) {
        return false;
    }

    GLFWwindow* pendingWindow = g_pendingSyntheticCursorPosCallback.window.load(std::memory_order_relaxed);
    if (pendingWindow != window) {
        return false;
    }

    const double pendingX = g_pendingSyntheticCursorPosCallback.rawX.load(std::memory_order_relaxed);
    const double pendingY = g_pendingSyntheticCursorPosCallback.rawY.load(std::memory_order_relaxed);
    const double deltaX = std::fabs(pendingX - xpos);
    const double deltaY = std::fabs(pendingY - ypos);

    if (deltaX < 0.01 && deltaY < 0.01) {
        g_pendingSyntheticCursorPosCallback.valid.store(false, std::memory_order_release);
        outReason = "duplicate";
        return true;
    }

    if (deltaX > 32.0 || deltaY > 32.0) {
        g_pendingSyntheticCursorPosCallback.valid.store(false, std::memory_order_release);
        outReason = "stale";
        return true;
    }

    g_pendingSyntheticCursorPosCallback.valid.store(false, std::memory_order_release);
    return false;
}

void ArmPendingSyntheticCursorPosCallback(GLFWwindow* window, double xpos, double ypos) {
    g_pendingSyntheticCursorPosCallback.window.store(window, std::memory_order_release);
    g_pendingSyntheticCursorPosCallback.rawX.store(xpos, std::memory_order_release);
    g_pendingSyntheticCursorPosCallback.rawY.store(ypos, std::memory_order_release);
    g_pendingSyntheticCursorPosCallback.valid.store(true, std::memory_order_release);
}

void ClearPendingSyntheticCursorPosCallbackState() {
    g_pendingSyntheticCursorPosCallback.valid.store(false, std::memory_order_release);
}

void DispatchGlfwCursorPosCallback(GLFWwindow* window,
                                   double xpos,
                                   double ypos,
                                   bool suppressPendingSyntheticDuplicate,
                                   const char* sourceLabel) {
    const char* suppressedReason = nullptr;
    if (suppressPendingSyntheticDuplicate &&
        ShouldSuppressPendingSyntheticCursorPosCallback(window, xpos, ypos, suppressedReason)) {
        return;
    }

    if (window) {
        g_lastSwapWindow.store(window, std::memory_order_release);
    }
    platform::x11::RegisterImGuiOverlayWindow(window);
    LogDebugOnce(g_loggedFirstGlfwCursorPosCallback, "first glfw cursor-position callback intercepted");

    int windowWidth = 0;
    int windowHeight = 0;
    int framebufferWidth = 0;
    int framebufferHeight = 0;
    (void)GetSizeFromLatestGlfwWindow(windowWidth, windowHeight);
    (void)GetFramebufferSizeFromLatestGlfwWindow(framebufferWidth, framebufferHeight);
    platform::x11::RecordGlfwWindowMetrics(windowWidth, windowHeight, framebufferWidth, framebufferHeight);

    platform::input::InputEvent event;
    event.type = platform::input::InputEventType::CursorPosition;
    event.action = platform::input::InputAction::Move;
    event.x = xpos;
    event.y = ypos;
    platform::x11::UpdateImGuiOverlayPointerPosition(xpos, ypos);
    PublishImGuiInputEvent(event, "glfwCursorPosCallback");
    StoreTrackedRawCursorPosition(xpos, ypos);

    const bool guiVisibleNow = platform::x11::IsGuiVisible();
    const bool consumedByOverlay = !guiVisibleNow && platform::x11::ShouldConsumeInputForOverlay(event);
    const bool cursorDisabledNow = IsCursorDisabledForGameInput();
    const bool shouldForwardToGame = !guiVisibleNow && !consumedByOverlay;

    if (cursorDisabledNow && ShouldSuppressCaptureEntryCursorEvent(xpos, ypos)) {
        return;
    }

    double forwardedX = xpos;
    double forwardedY = ypos;
    if (!cursorDisabledNow) {
        (void)WindowToGame(xpos, ypos, forwardedX, forwardedY);
    }

    const bool shouldApplySensitivityScaling = shouldForwardToGame && cursorDisabledNow;
    if (shouldApplySensitivityScaling) {
        float sensitivityX = 1.0f;
        float sensitivityY = 1.0f;
        auto configSnapshot = platform::config::GetConfigSnapshot();
        if (configSnapshot) {
            ResolveActiveSensitivity(*configSnapshot, sensitivityX, sensitivityY);
        }

        std::lock_guard<std::mutex> lock(g_cursorSensitivityStateMutex);
        double continuityX = xpos;
        double continuityY = ypos;
        if (!LoadTrackedCapturedCursorPosition(continuityX, continuityY)) {
            ResolveTrackedCapturedCursorCenter(continuityX, continuityY);
        }

        const bool captureEntered = g_trackedCursorState.captureEnterPending.exchange(false, std::memory_order_acq_rel);
        if (!g_cursorSensitivityBaselineValid || captureEntered) {
            g_cursorSensitivityBaselineValid = true;
            g_cursorSensitivityLastRawX = xpos;
            g_cursorSensitivityLastRawY = ypos;
            g_cursorSensitivityLastOutputX = continuityX;
            g_cursorSensitivityLastOutputY = continuityY;
            g_cursorSensitivityAccumX = 0.0;
            g_cursorSensitivityAccumY = 0.0;
        } else {
            const double rawDeltaX = xpos - g_cursorSensitivityLastRawX;
            const double rawDeltaY = ypos - g_cursorSensitivityLastRawY;
            g_cursorSensitivityLastRawX = xpos;
            g_cursorSensitivityLastRawY = ypos;

            g_cursorSensitivityAccumX += rawDeltaX * static_cast<double>(sensitivityX);
            g_cursorSensitivityAccumY += rawDeltaY * static_cast<double>(sensitivityY);

            const double quantizedDeltaX = std::trunc(g_cursorSensitivityAccumX);
            const double quantizedDeltaY = std::trunc(g_cursorSensitivityAccumY);
            g_cursorSensitivityAccumX -= quantizedDeltaX;
            g_cursorSensitivityAccumY -= quantizedDeltaY;

            g_cursorSensitivityLastOutputX += quantizedDeltaX;
            g_cursorSensitivityLastOutputY += quantizedDeltaY;
        }

        forwardedX = g_cursorSensitivityLastOutputX;
        forwardedY = g_cursorSensitivityLastOutputY;
        StoreTrackedCapturedCursorPosition(forwardedX, forwardedY);
    } else {
        ResetCursorSensitivityState();
    }

    if (guiVisibleNow) { return; }
    if (consumedByOverlay) { return; }

    GlfwCursorPosCallback userCallback = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_glfwCallbackMutex);
        auto it = g_glfwCallbackMap.find(window);
        if (it != g_glfwCallbackMap.end()) { userCallback = it->second.cursorPos; }
    }

    if (!userCallback) {
        LogDebugOnce(g_loggedMissingGlfwCursorPosUserCallback, "hooked GLFW cursor-position callback had no user callback for this window");
    }
    if (userCallback) {
        userCallback(window, forwardedX, forwardedY);
    }
}

void DispatchSyntheticGlfwCursorPosCallback(GLFWwindow* window, double xpos, double ypos) {
    if (!window) {
        return;
    }

    ArmPendingSyntheticCursorPosCallback(window, xpos, ypos);
    DispatchGlfwCursorPosCallback(window, xpos, ypos, false, "synthetic");
}

void RefreshTrackedCursorPositionAfterFocusGain(GLFWwindow* window) {
    ClearPendingSyntheticCursorPosCallbackState();
    if (!window) {
        return;
    }

    GlfwGetCursorPosProc realGetCursorPos = GetRealGlfwGetCursorPos();
    if (!realGetCursorPos) {
        return;
    }

    double rawX = 0.0;
    double rawY = 0.0;
    realGetCursorPos(window, &rawX, &rawY);
    StoreTrackedRawCursorPosition(rawX, rawY);

    if (platform::x11::IsGuiVisible()) {
        DispatchSyntheticGlfwCursorPosCallback(window, rawX, rawY);
    }
}

void DispatchCurrentFreeCursorPosition(GLFWwindow* window) {
    if (!window || IsCursorDisabledForGameInput()) {
        return;
    }

    GlfwGetCursorPosProc realGetCursorPos = GetRealGlfwGetCursorPos();
    if (!realGetCursorPos) {
        return;
    }

    double rawX = 0.0;
    double rawY = 0.0;
    realGetCursorPos(window, &rawX, &rawY);
    StoreTrackedRawCursorPosition(rawX, rawY);
    DispatchSyntheticGlfwCursorPosCallback(window, rawX, rawY);
}

struct CursorDispatchRegistrar {
    CursorDispatchRegistrar() { g_dispatchCursorAfterResize = &DispatchCurrentFreeCursorPosition; }
};
static CursorDispatchRegistrar g_cursorDispatchRegistrar;

void HookedGlfwCursorPosCallback(GLFWwindow* window, double xpos, double ypos) {
    DispatchGlfwCursorPosCallback(window, xpos, ypos, true, "real");
}

void HookedGlfwScrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
    platform::x11::RegisterImGuiOverlayWindow(window);
    platform::input::InputEvent event;
    event.type = platform::input::InputEventType::Scroll;
    event.action = platform::input::InputAction::Move;
    event.scrollX = xoffset;
    event.scrollY = yoffset;

    {
        std::lock_guard<std::mutex> lock(g_inputStateMutex);
        g_keyStateTracker.ApplyEvent(event);
    }

    PublishImGuiInputEvent(event, "glfwScrollCallback");

    if (platform::x11::IsGuiVisible()) { return; }
    if (platform::x11::ShouldConsumeInputForOverlay(event)) { return; }

    GlfwScrollCallback userCallback = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_glfwCallbackMutex);
        auto it = g_glfwCallbackMap.find(window);
        if (it != g_glfwCallbackMap.end()) { userCallback = it->second.scroll; }
    }

    if (!userCallback) { LogDebugOnce(g_loggedMissingGlfwScrollUserCallback, "hooked GLFW scroll callback had no user callback for this window"); }
    if (userCallback) { userCallback(window, xoffset, yoffset); }
}

void HookedGlfwWindowFocusCallback(GLFWwindow* window, int focused) {
    platform::x11::RegisterImGuiOverlayWindow(window);
    LogDebugOnce(g_loggedFirstGlfwFocusCallback, "first glfw window-focus callback intercepted");

    platform::input::InputEvent event;
    event.type = platform::input::InputEventType::Focus;
    event.action = platform::input::InputAction::FocusChanged;
    event.focused = focused != 0;

    {
        std::lock_guard<std::mutex> lock(g_inputStateMutex);
        g_keyStateTracker.ApplyEvent(event);
    }

    if (!event.focused) {
        ClearPendingSyntheticCursorPosCallbackState();
        ClearSyntheticRebindWindow(window);
        ClearPendingCharRemaps();
        ClearManagedRepeatStatesForWindow(window);
        ResetCursorSensitivityState();
    } else {
        RefreshTrackedCursorPositionAfterFocusGain(window);
        ResetCursorSensitivityState();
    }

    PublishImGuiInputEvent(event, "glfwWindowFocusCallback");

    GlfwWindowFocusCallback userCallback = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_glfwCallbackMutex);
        auto it = g_glfwCallbackMap.find(window);
        if (it != g_glfwCallbackMap.end()) { userCallback = it->second.focus; }
    }

    if (!userCallback) { LogDebugOnce(g_loggedMissingGlfwFocusUserCallback, "hooked GLFW focus callback had no user callback for this window"); }
    if (userCallback) { userCallback(window, focused); }
}

void HookedGlfwWindowSizeCallback(GLFWwindow* window, int width, int height) {
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

    int windowWidth = 0;
    int windowHeight = 0;
    int framebufferWidth = 0;
    int framebufferHeight = 0;
    (void)GetSizeFromLatestGlfwWindow(windowWidth, windowHeight);
    (void)GetFramebufferSizeFromLatestGlfwWindow(framebufferWidth, framebufferHeight);
    platform::x11::RecordGlfwWindowMetrics(windowWidth, windowHeight, framebufferWidth, framebufferHeight);
    g_lastResizeRequestWidth.store(0, std::memory_order_relaxed);
    g_lastResizeRequestHeight.store(0, std::memory_order_relaxed);

    GlfwWindowSizeCallback userCallback = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_glfwCallbackMutex);
        auto it = g_glfwCallbackMap.find(window);
        if (it != g_glfwCallbackMap.end()) {
            userCallback = it->second.windowSize;
        }
    }

    if (userCallback) {
        int dispatchWidth = width;
        int dispatchHeight = height;
        (void)ResolveResizeDispatchDimensionsForActiveMode(width,
                                                          height,
                                                          ManagedDimensionSpace::WindowLogical,
                                                          dispatchWidth,
                                                          dispatchHeight);
        userCallback(window, dispatchWidth, dispatchHeight);
    } else {
        LogDebugOnce(g_loggedMissingGlfwWindowSizeUserCallback,
                     "hooked GLFW window-size callback had no user callback for this window");
    }

    TickModeResolutionTransition();
}

void HookedGlfwFramebufferSizeCallback(GLFWwindow* window, int width, int height) {
    if (window) {
        g_lastSwapWindow.store(window, std::memory_order_release);
    }
    platform::x11::RegisterImGuiOverlayWindow(window);

    int windowWidth = 0;
    int windowHeight = 0;
    int framebufferWidth = 0;
    int framebufferHeight = 0;
    (void)GetSizeFromLatestGlfwWindow(windowWidth, windowHeight);
    (void)GetFramebufferSizeFromLatestGlfwWindow(framebufferWidth, framebufferHeight);
    platform::x11::RecordGlfwWindowMetrics(windowWidth, windowHeight, framebufferWidth, framebufferHeight);
    g_lastResizeRequestWidth.store(0, std::memory_order_relaxed);
    g_lastResizeRequestHeight.store(0, std::memory_order_relaxed);

    GlfwFramebufferSizeCallback userCallback = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_glfwCallbackMutex);
        auto it = g_glfwCallbackMap.find(window);
        if (it != g_glfwCallbackMap.end()) {
            userCallback = it->second.framebufferSize;
        }
    }

    if (userCallback) {
        int dispatchWidth = width;
        int dispatchHeight = height;
        (void)ResolveResizeDispatchDimensionsForActiveMode(width,
                                                          height,
                                                          ManagedDimensionSpace::FramebufferPhysical,
                                                          dispatchWidth,
                                                          dispatchHeight);
        userCallback(window, dispatchWidth, dispatchHeight);
    } else {
        LogDebugOnce(g_loggedMissingGlfwFramebufferSizeUserCallback,
                     "hooked GLFW framebuffer-size callback had no user callback for this window");
    }

    TickModeResolutionTransition();
}
