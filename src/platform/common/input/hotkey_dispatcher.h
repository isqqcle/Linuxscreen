#pragma once

#include "../linuxscreen_config.h"
#include "hotkey_matcher.h"
#include "input_event.h"
#include "key_state_tracker.h"

#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace platform::input {

struct HotkeyEvaluationResult {
    std::string targetMode;
    size_t hotkeyIndex = 0;
    bool fired = false;
    bool blockKeyFromGame = false;
};

// Stateful hotkey dispatcher that handles debounce, trigger-on-release, and
// per-hotkey secondary-mode state.
class HotkeyDispatcher {
public:
    void SetHotkeys(std::vector<config::HotkeyConfig> hotkeys);

    HotkeyEvaluationResult Evaluate(const KeyStateTracker& tracker,
                                    const InputEvent& event,
                                    const std::string& gameState,
                                    const std::string& currentMode,
                                    const std::string& defaultMode,
                                    VkCode rebindTargetVk = VK_NONE);

    // Reset trigger-on-release pending state and per-hotkey secondary mode state.
    // Called after config reload/transitions.
    void ResetSecondaryModes();

    std::vector<config::HotkeyConfig> GetHotkeys() const;

    std::optional<config::HotkeyConfig> GetHotkey(size_t index) const;

private:
    bool IsGameStateAllowed(const config::HotkeyConfig& hotkey,
                            const std::string& gameState,
                            bool isExitTransition) const;
    bool CheckDebounce(size_t hotkeyIndex, int debounceMs, std::chrono::steady_clock::time_point now);
    std::string GetBaseSecondaryMode(const config::HotkeyConfig& hotkey) const;

    mutable std::mutex mutex_;
    std::vector<config::HotkeyConfig> hotkeys_;
    std::vector<std::chrono::steady_clock::time_point> lastTriggerTimes_;
    std::vector<int> pendingTriggerVariant_; // -2 = none, -1 = main, >=0 = altSecondaryModes index
    std::vector<bool> invalidatedTriggerOnRelease_;
    std::vector<bool> pendingTriggerViaRebind_;
    std::vector<std::string> currentSecondaryModes_;
};

} // namespace platform::input
