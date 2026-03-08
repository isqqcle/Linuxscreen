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

void ReleaseAllHeldInputsForGuiOpen(GLFWwindow* window) {
    std::vector<platform::input::VkCode> downKeys;
    {
        std::lock_guard<std::mutex> lock(g_inputStateMutex);
        downKeys = g_keyStateTracker.GetDownKeys();
        g_keyStateTracker.Clear();
    }

    std::stable_sort(downKeys.begin(), downKeys.end(), [](platform::input::VkCode a, platform::input::VkCode b) {
        const bool aModifier = platform::input::IsModifierVk(a);
        const bool bModifier = platform::input::IsModifierVk(b);
        if (aModifier != bModifier) {
            return !aModifier;
        }
        return static_cast<unsigned>(a) < static_cast<unsigned>(b);
    });
    downKeys.erase(std::unique(downKeys.begin(), downKeys.end()), downKeys.end());

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
        GlfwGetKeyScancodeFn getKeyScancode = GetRealGlfwGetKeyScancode();
        for (const auto vk : downKeys) {
            const int mouseButton = platform::input::VkToGlfwMouseButton(vk);
            if (mouseButton >= 0) {
                if (userMouseCallback) {
                    userMouseCallback(window, mouseButton, releaseAction, 0);
                }
                continue;
            }

            const int glfwKey = platform::input::VkToGlfwKey(vk);
            if (glfwKey < 0) {
                continue;
            }

            if (userKeyCallback) {
                const int scanCode = getKeyScancode ? getKeyScancode(glfwKey) : 0;
                userKeyCallback(window, glfwKey, scanCode, releaseAction, 0);
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
        const std::vector<platform::input::VkCode> guiHotkey = platform::x11::GetGuiHotkey();
        shouldToggleGui = platform::input::MatchesHotkey(g_keyStateTracker, guiHotkey, event);
        const std::vector<platform::input::VkCode> rebindToggleHotkey = platform::x11::GetRebindToggleHotkey();
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
    static const int kGlfwF3Key = platform::input::VkToGlfwKey(platform::input::VK_F1 + 2);
    return targetKey >= 0 && targetKey == kGlfwF3Key;
}

bool IsSyntheticRebindStateEmpty(const SyntheticRebindKeyState& state) {
    return state.sourceToTarget.empty() && state.targetPressCount.empty() && state.physicalKeyDown.empty() &&
           state.targetSuppressedKeys.empty() && state.suppressedKeyRefCount.empty();
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

        const platform::input::VkCode heldVk = platform::input::GlfwKeyToVk(heldKey, 0, 0);
        if (platform::input::IsModifierVk(heldVk)) {
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
    return rebind.enabled && rebind.fromKey != 0 && (rebind.consumeSourceInput || rebind.toKey != 0);
}

bool IsValidUnicodeScalar(std::uint32_t codepoint) {
    if (codepoint == 0 || codepoint > 0x10FFFFu) {
        return false;
    }
    return !(codepoint >= 0xD800u && codepoint <= 0xDFFFu);
}

platform::input::VkCode ResolveRebindTriggerVk(const platform::config::KeyRebind& rebind) {
    const int preferredScan = (rebind.useCustomOutput && rebind.customOutputScanCode != 0) ? static_cast<int>(rebind.customOutputScanCode) : 0;
    return platform::input::NormalizeModifierVkFromConfig(rebind.toKey, preferredScan);
}

platform::input::VkCode ResolveRebindTextVk(const platform::config::KeyRebind& rebind,
                                            platform::input::VkCode triggerVk,
                                            int outputScanCode) {
    if (platform::input::IsModifierVk(triggerVk)) {
        return triggerVk;
    }

    const platform::input::VkCode textBase =
        (rebind.useCustomOutput && rebind.customOutputVK != 0) ? rebind.customOutputVK : triggerVk;
    return platform::input::NormalizeModifierVkFromConfig(textBase, outputScanCode);
}

int GetFallbackX11ScanCodeForVk(platform::input::VkCode vk) {
    static const platform::input::VkCode kDigitRow[] = {
        static_cast<platform::input::VkCode>('1'), static_cast<platform::input::VkCode>('2'),
        static_cast<platform::input::VkCode>('3'), static_cast<platform::input::VkCode>('4'),
        static_cast<platform::input::VkCode>('5'), static_cast<platform::input::VkCode>('6'),
        static_cast<platform::input::VkCode>('7'), static_cast<platform::input::VkCode>('8'),
        static_cast<platform::input::VkCode>('9'), static_cast<platform::input::VkCode>('0'),
    };
    for (int i = 0; i < 10; ++i) {
        if (vk == kDigitRow[i]) {
            return 10 + i;
        }
    }

    static const platform::input::VkCode kTopAlphaRow[] = {
        static_cast<platform::input::VkCode>('Q'), static_cast<platform::input::VkCode>('W'),
        static_cast<platform::input::VkCode>('E'), static_cast<platform::input::VkCode>('R'),
        static_cast<platform::input::VkCode>('T'), static_cast<platform::input::VkCode>('Y'),
        static_cast<platform::input::VkCode>('U'), static_cast<platform::input::VkCode>('I'),
        static_cast<platform::input::VkCode>('O'), static_cast<platform::input::VkCode>('P'),
    };
    for (int i = 0; i < 10; ++i) {
        if (vk == kTopAlphaRow[i]) {
            return 24 + i;
        }
    }

    static const platform::input::VkCode kHomeAlphaRow[] = {
        static_cast<platform::input::VkCode>('A'), static_cast<platform::input::VkCode>('S'),
        static_cast<platform::input::VkCode>('D'), static_cast<platform::input::VkCode>('F'),
        static_cast<platform::input::VkCode>('G'), static_cast<platform::input::VkCode>('H'),
        static_cast<platform::input::VkCode>('J'), static_cast<platform::input::VkCode>('K'),
        static_cast<platform::input::VkCode>('L'),
    };
    for (int i = 0; i < 9; ++i) {
        if (vk == kHomeAlphaRow[i]) {
            return 38 + i;
        }
    }

    static const platform::input::VkCode kBottomAlphaRow[] = {
        static_cast<platform::input::VkCode>('Z'), static_cast<platform::input::VkCode>('X'),
        static_cast<platform::input::VkCode>('C'), static_cast<platform::input::VkCode>('V'),
        static_cast<platform::input::VkCode>('B'), static_cast<platform::input::VkCode>('N'),
        static_cast<platform::input::VkCode>('M'),
    };
    for (int i = 0; i < 7; ++i) {
        if (vk == kBottomAlphaRow[i]) {
            return 52 + i;
        }
    }

    if (vk >= platform::input::VK_F1 && vk <= (platform::input::VK_F1 + 9)) {
        return 67 + static_cast<int>(vk - platform::input::VK_F1);
    }

    switch (vk) {
    case platform::input::VK_SHIFT:
        return 50;
    case platform::input::VK_CONTROL:
        return 37;
    case platform::input::VK_MENU:
        return 64;
    case platform::input::VK_ESCAPE:
        return 9;
    case platform::input::VK_OEM_MINUS:
        return 20;
    case platform::input::VK_OEM_PLUS:
        return 21;
    case platform::input::VK_BACK:
        return 22;
    case platform::input::VK_TAB:
        return 23;
    case platform::input::VK_OEM_4:
        return 34;
    case platform::input::VK_OEM_6:
        return 35;
    case platform::input::VK_RETURN:
        return 36;
    case platform::input::VK_LCONTROL:
        return 37;
    case platform::input::VK_OEM_1:
        return 47;
    case platform::input::VK_OEM_7:
        return 48;
    case platform::input::VK_OEM_3:
        return 49;
    case platform::input::VK_LSHIFT:
        return 50;
    case platform::input::VK_OEM_5:
        return 51;
    case platform::input::VK_OEM_COMMA:
        return 59;
    case platform::input::VK_OEM_PERIOD:
        return 60;
    case platform::input::VK_OEM_2:
        return 61;
    case platform::input::VK_RSHIFT:
        return 62;
    case platform::input::VK_MULTIPLY:
        return 63;
    case platform::input::VK_LMENU:
        return 64;
    case platform::input::VK_SPACE:
        return 65;
    case platform::input::VK_CAPITAL:
        return 66;
    case platform::input::VK_NUMLOCK:
        return 77;
    case platform::input::VK_SCROLL:
        return 78;
    case platform::input::VK_SNAPSHOT:
        return 107;
    case platform::input::VK_PAUSE:
        return 127;
    case platform::input::VK_SUBTRACT:
        return 82;
    case platform::input::VK_ADD:
        return 86;
    case platform::input::VK_DECIMAL:
        return 91;
    case platform::input::VK_F1 + 10:
        return 95;
    case platform::input::VK_F1 + 11:
        return 96;
    case platform::input::VK_RCONTROL:
        return 105;
    case platform::input::VK_DIVIDE:
        return 106;
    case platform::input::VK_RMENU:
        return 108;
    case platform::input::VK_HOME:
        return 110;
    case platform::input::VK_UP:
        return 111;
    case platform::input::VK_PRIOR:
        return 112;
    case platform::input::VK_LEFT:
        return 113;
    case platform::input::VK_RIGHT:
        return 114;
    case platform::input::VK_END:
        return 115;
    case platform::input::VK_DOWN:
        return 116;
    case platform::input::VK_NEXT:
        return 117;
    case platform::input::VK_INSERT:
        return 118;
    case platform::input::VK_DELETE:
        return 119;
    case platform::input::VK_LWIN:
        return 133;
    case platform::input::VK_RWIN:
        return 134;
    case platform::input::VK_APPS:
        return 135;
    default:
        break;
    }

    if (vk >= (platform::input::VK_F1 + 12) && vk <= platform::input::VK_F24) {
        return 191 + static_cast<int>(vk - (platform::input::VK_F1 + 12));
    }

    if (vk >= platform::input::VK_NUMPAD0 && vk <= platform::input::VK_NUMPAD9) {
        static const int kNumPadScans[] = { 90, 87, 88, 89, 83, 84, 85, 79, 80, 81 };
        return kNumPadScans[vk - platform::input::VK_NUMPAD0];
    }

    return 0;
}

int GetDerivedOutputScanCodeForVk(platform::input::VkCode triggerVk) {
    if (!platform::input::IsKeyboardVk(triggerVk)) {
        return 0;
    }

    const int triggerGlfwKey = platform::input::VkToGlfwKey(triggerVk);
    if (triggerGlfwKey >= 0) {
        if (GlfwGetKeyScancodeFn getKeyScancode = GetRealGlfwGetKeyScancode()) {
            const int derived = getKeyScancode(triggerGlfwKey);
            if (derived > 0) {
                return derived;
            }
        }
    }

    return GetFallbackX11ScanCodeForVk(triggerVk);
}

int ResolveOutputScanCode(platform::input::VkCode triggerVk, std::uint32_t configuredScanCode) {
    if (configuredScanCode != 0) {
        return static_cast<int>(configuredScanCode);
    }
    return GetDerivedOutputScanCodeForVk(triggerVk);
}

struct ResolvedRebindOutput {
    bool matched = false;
    bool consumeSourceInput = false;
    platform::input::VkCode sourceVk = platform::input::VK_NONE;
    platform::input::VkCode triggerVk = platform::input::VK_NONE;
    platform::input::VkCode textVk = platform::input::VK_NONE;
    int outputScanCode = 0;
    bool targetIsMouse = false;
    std::uint32_t customUnicode = 0;
    std::uint32_t customShiftUnicode = 0;
};

std::optional<ResolvedRebindOutput> ResolveRebindOutput(const platform::config::LinuxscreenConfig& config,
                                                        const platform::input::InputEvent& event,
                                                        bool guiVisible) {
    if (!config.keyRebinds.enabled || event.vk == platform::input::VK_NONE) {
        return std::nullopt;
    }

    if (event.type == platform::input::InputEventType::MouseButton && guiVisible) {
        return std::nullopt;
    }

    for (const auto& rebind : config.keyRebinds.rebinds) {
        if (!IsRebindEntryConfigured(rebind)) {
            continue;
        }
        if (!platform::input::MatchesRebindSourceVk(event.vk, rebind.fromKey)) {
            continue;
        }

        ResolvedRebindOutput resolved;
        resolved.matched = true;
        resolved.sourceVk = event.vk;
        if (rebind.consumeSourceInput) {
            resolved.consumeSourceInput = true;
            return resolved;
        }
        resolved.triggerVk = ResolveRebindTriggerVk(rebind);
        resolved.outputScanCode = ResolveOutputScanCode(
            resolved.triggerVk,
            (rebind.useCustomOutput && rebind.customOutputScanCode != 0) ? rebind.customOutputScanCode : 0);
        resolved.textVk = ResolveRebindTextVk(rebind, resolved.triggerVk, resolved.outputScanCode);
        resolved.targetIsMouse = platform::input::IsMouseVk(resolved.triggerVk);
        resolved.customUnicode = rebind.useCustomOutput ? rebind.customOutputUnicode : 0;
        resolved.customShiftUnicode = rebind.useCustomOutput ? rebind.customOutputShiftUnicode : 0;
        return resolved;
    }

    return std::nullopt;
}

bool TryResolveRebindOutputCodepoint(const ResolvedRebindOutput& rebindOutput, int nativeMods, std::uint32_t& outCodepoint);

void QueuePendingCharRemap(const ResolvedRebindOutput& rebindOutput, const platform::input::InputEvent& event) {
    if (event.type != platform::input::InputEventType::Key) {
        return;
    }
    if (event.action != platform::input::InputAction::Press && event.action != platform::input::InputAction::Repeat) {
        return;
    }

    std::lock_guard<std::mutex> lock(g_pendingCharRemapMutex);
    constexpr std::size_t kMaxPendingCharRemaps = 32;
    g_pendingCharRemaps.emplace_back();
    PendingCharRemap& pending = g_pendingCharRemaps.back();
    pending.sequence = g_pendingCharRemapSequence.fetch_add(1, std::memory_order_acq_rel) + 1;
    pending.consume = !TryResolveRebindOutputCodepoint(rebindOutput, event.nativeMods, pending.outputCodepoint);
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

bool TryResolveRebindOutputCodepoint(const ResolvedRebindOutput& rebindOutput, int nativeMods, std::uint32_t& outCodepoint) {
    outCodepoint = 0;
    const bool shiftDown = (nativeMods & static_cast<int>(platform::input::GlfwMod::Shift)) != 0;

    if (shiftDown &&
        IsValidUnicodeScalar(rebindOutput.customShiftUnicode) &&
        !platform::input::IsNonTextVk(rebindOutput.textVk)) {
        outCodepoint = rebindOutput.customShiftUnicode;
        return true;
    }

    if (IsValidUnicodeScalar(rebindOutput.customUnicode) && !platform::input::IsNonTextVk(rebindOutput.textVk)) {
        outCodepoint = rebindOutput.customUnicode;
        return true;
    }

    std::uint32_t translated = 0;
    if (!platform::input::IsNonTextVk(rebindOutput.textVk) &&
        platform::input::TryMapVkToCodepoint(rebindOutput.textVk, shiftDown, translated) && translated != 0) {
        outCodepoint = translated;
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

bool IsConfiguredRebind(const platform::config::KeyRebind& rebind) {
    if (rebind.fromKey == 0) {
        return false;
    }
    if (rebind.consumeSourceInput || rebind.toKey != 0) {
        return true;
    }
    if (!IsRepeatBlacklistedSourceVk(rebind.fromKey) && rebind.keyRepeatDisabled) {
        return true;
    }
    if (!IsRepeatBlacklistedSourceVk(rebind.fromKey) &&
        (rebind.keyRepeatStartDelay > 0 || rebind.keyRepeatDelay > 0)) {
        return true;
    }
    return false;
}

int FindBestRebindIndexForSource(const platform::config::LinuxscreenConfig& config, platform::input::VkCode sourceVk) {
    if (sourceVk == platform::input::VK_NONE) {
        return -1;
    }

    int first = -1;
    int enabledAny = -1;
    int enabledConfigured = -1;
    int configuredAny = -1;
    for (int i = 0; i < static_cast<int>(config.keyRebinds.rebinds.size()); ++i) {
        const auto& rebind = config.keyRebinds.rebinds[static_cast<std::size_t>(i)];
        if (rebind.fromKey != sourceVk) {
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
    if (event.vk == platform::input::VK_NONE ||
        (event.type != platform::input::InputEventType::Key && event.type != platform::input::InputEventType::MouseButton)) {
        return resolved;
    }
    if (event.type == platform::input::InputEventType::Key &&
        platform::input::IsModifierVk(event.vk)) {
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
        const int bestRebindIndex = FindBestRebindIndexForSource(config, event.vk);
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
        event.vk == platform::input::VK_NONE ||
        platform::input::IsModifierVk(event.vk)) {
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

    if (state.sourceVk == platform::input::VK_NONE || state.key.sourceIsMouseButton) {
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
        if (TryResolveRebindOutputCodepoint(*rebindOutput, state.nativeMods, mappedCodepoint) && mappedCodepoint != 0) {
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

    const bool shiftDown = (state.nativeMods & static_cast<int>(platform::input::GlfwMod::Shift)) != 0;
    if (platform::input::TryMapVkToCodepoint(state.sourceVk, shiftDown, outCodepoint) && outCodepoint != 0) {
        return ManagedRepeatCharMode::InjectSynthetic;
    }
    return ManagedRepeatCharMode::NoCharacter;
}

void DispatchManagedSyntheticCharacter(GLFWwindow* window, std::uint32_t codepoint, int mods) {
    if (!window || codepoint == 0) {
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
    if (!window || event.vk == platform::input::VK_NONE || event.nativeKey < 0) {
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
        event.vk != platform::input::VK_NONE &&
        (event.action == platform::input::InputAction::Press || event.action == platform::input::InputAction::Release ||
         event.action == platform::input::InputAction::Repeat)) {
        platform::config::RegisterBindingInputEvent(event.vk, event.nativeScanCode, event.nativeMods, false, event.action);
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
    if (!syntheticManagedRepeatEvent && event.vk != platform::input::VK_NONE) {
        toggledGui = ProcessInputEventForGuiToggle(window, event, "glfwKeyCallback");
    }

    auto configSnapshot = platform::config::GetConfigSnapshot();
    if (configSnapshot) {
        MaybeClearPendingCharRemapsForRebindDisable(*configSnapshot);
    }
    const bool guiVisibleNow = platform::x11::IsGuiVisible();

    std::string gameState;
    if (event.vk != platform::input::VK_NONE && !toggledGui && !guiVisibleNow) {
        gameState = platform::config::GetCurrentGameState();
    }

    std::optional<ResolvedRebindOutput> rebindOutput;
    platform::input::VkCode rebindTargetVk = platform::input::VK_NONE;
    if (configSnapshot && event.vk != platform::input::VK_NONE && !toggledGui && !guiVisibleNow) {
        rebindOutput = ResolveRebindOutput(*configSnapshot, event, false);
        if (rebindOutput && !rebindOutput->consumeSourceInput) {
            rebindTargetVk = rebindOutput->triggerVk;
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
            event.vk != platform::input::VK_NONE &&
            !toggledGui &&
            !guiVisibleNow) {
            hotkeyResult = g_hotkeyDispatcher.Evaluate(g_keyStateTracker,
                                                       event,
                                                       gameState,
                                                       platform::x11::GetMirrorModeState().GetActiveModeName(),
                                                       configSnapshot->defaultMode);
            if (!hotkeyResult.fired) {
                sensitivityMatchedViaRebind = EvaluateSensitivityHotkeys(*configSnapshot,
                                                                         g_keyStateTracker,
                                                                         event,
                                                                         gameState,
                                                                         rebindTargetVk);
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

    if (configSnapshot && event.vk != platform::input::VK_NONE) {
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
            QueuePendingCharRemap(*rebindOutput, event);
        }

        if (rebindOutput->targetIsMouse) {
            if (event.action == platform::input::InputAction::Release) {
                ClearSyntheticRebindSourceKey(window, key);
            }
            const int mappedButton = platform::input::VkToGlfwMouseButton(rebindOutput->triggerVk);
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
            const int mappedKey = platform::input::VkToGlfwKey(rebindOutput->triggerVk);
            if (mappedKey < 0) {
                LogDebugOnce(g_loggedUnsupportedKeyRebindMapping,
                             "key rebind matched but keyboard target VK has no GLFW key mapping; forwarding original key event");
            } else if (userCallback) {
                const int mappedScanCode = rebindOutput->outputScanCode;
                UpdateSyntheticRebindKeyState(window, key, mappedKey, action);
                userCallback(window, mappedKey, mappedScanCode, action, mods);
                if (sourceIsNonText &&
                    (event.action == platform::input::InputAction::Press ||
                     event.action == platform::input::InputAction::Repeat)) {
                    std::uint32_t remappedCodepoint = 0;
                    if (TryResolveRebindOutputCodepoint(*rebindOutput, mods, remappedCodepoint) && remappedCodepoint != 0) {
                        if (userCharCallback) {
                            userCharCallback(window, remappedCodepoint);
                        } else if (userCharModsCallback) {
                            userCharModsCallback(window, remappedCodepoint, mods);
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
        platform::config::RegisterBindingInputEvent(event.vk, 0, event.nativeMods, true, event.action);
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
    if (event.vk != platform::input::VK_NONE && !toggledGui && !guiVisibleNow) {
        gameState = platform::config::GetCurrentGameState();
    }

    std::optional<ResolvedRebindOutput> rebindOutput;
    platform::input::VkCode rebindTargetVk = platform::input::VK_NONE;
    if (configSnapshot && event.vk != platform::input::VK_NONE && !toggledGui && !guiVisibleNow) {
        rebindOutput = ResolveRebindOutput(*configSnapshot, event, false);
        if (rebindOutput && !rebindOutput->consumeSourceInput) {
            rebindTargetVk = rebindOutput->triggerVk;
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
            event.vk != platform::input::VK_NONE &&
            !toggledGui &&
            !guiVisibleNow) {
            hotkeyResult = g_hotkeyDispatcher.Evaluate(g_keyStateTracker,
                                                       event,
                                                       gameState,
                                                       platform::x11::GetMirrorModeState().GetActiveModeName(),
                                                       configSnapshot->defaultMode);
            if (!hotkeyResult.fired) {
                sensitivityMatchedViaRebind = EvaluateSensitivityHotkeys(*configSnapshot,
                                                                         g_keyStateTracker,
                                                                         event,
                                                                         gameState,
                                                                         rebindTargetVk);
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

    if (configSnapshot && event.vk != platform::input::VK_NONE) {
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
            const int mappedButton = platform::input::VkToGlfwMouseButton(rebindOutput->triggerVk);
            if (mappedButton >= 0 && userCallback) {
                int forwardedAction = action;
                if (forwardedAction == static_cast<int>(platform::input::GlfwAction::Repeat)) {
                    forwardedAction = static_cast<int>(platform::input::GlfwAction::Press);
                }
                userCallback(window, mappedButton, forwardedAction, mods);
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
            const int mappedKey = platform::input::VkToGlfwKey(rebindOutput->triggerVk);
            if (mappedKey >= 0 && userKeyCallback) {
                UpdateSyntheticRebindKeyState(window, syntheticMouseSourceKey, mappedKey, action);
                int mappedAction = action;
                if (mappedAction == static_cast<int>(platform::input::GlfwAction::Repeat)) {
                    mappedAction = static_cast<int>(platform::input::GlfwAction::Press);
                }
                userKeyCallback(window, mappedKey, rebindOutput->outputScanCode, mappedAction, mods);
                if (event.action == platform::input::InputAction::Press ||
                    event.action == platform::input::InputAction::Repeat) {
                    std::uint32_t remappedCodepoint = 0;
                    if (TryResolveRebindOutputCodepoint(*rebindOutput, mods, remappedCodepoint) && remappedCodepoint != 0) {
                        if (userCharCallback) {
                            userCharCallback(window, remappedCodepoint);
                        } else if (userCharModsCallback) {
                            userCharModsCallback(window, remappedCodepoint, mods);
                        }
                    }
                }
                return;
            }
            if (mappedKey < 0) {
                LogDebugOnce(g_loggedUnsupportedKeyRebindMapping,
                             "mouse rebind matched but keyboard target VK has no GLFW key mapping; forwarding original mouse event");
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
        ClearSyntheticRebindWindow(window);
        ClearPendingCharRemaps();
        ClearManagedRepeatStatesForWindow(window);
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
        (void)ResolveResizeDispatchDimensionsForActiveMode(width, height, dispatchWidth, dispatchHeight);
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
        (void)ResolveResizeDispatchDimensionsForActiveMode(width, height, dispatchWidth, dispatchHeight);
        userCallback(window, dispatchWidth, dispatchHeight);
    } else {
        LogDebugOnce(g_loggedMissingGlfwFramebufferSizeUserCallback,
                     "hooked GLFW framebuffer-size callback had no user callback for this window");
    }

    TickModeResolutionTransition();
}
