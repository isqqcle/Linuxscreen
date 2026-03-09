#include "hotkey_dispatcher.h"

#include "../game_state_monitor.h"

#include <algorithm>
#include <cstdint>

namespace platform::input {

namespace {

constexpr int kNoPendingVariant = -2;
constexpr int kMainVariant = -1;

struct BindingEvaluation {
    bool matched = false;
    bool matchedViaFallback = false;
    int variantIndex = kNoPendingVariant;
    std::string targetMode;
    bool isExitTransition = false;
    bool updateSecondaryMode = false;
    std::string nextSecondaryMode;
};

bool MatchesHotkeyWithFallback(const KeyStateTracker& tracker,
                               const std::vector<VkCode>& keys,
                               const InputEvent& event,
                               const std::vector<VkCode>& exclusions,
                               bool triggerOnRelease,
                               VkCode fallbackVk,
                               bool& matchedViaFallback) {
    matchedViaFallback = false;
    if (MatchesHotkey(tracker, keys, event, exclusions, triggerOnRelease)) {
        return true;
    }

    if (fallbackVk == VK_NONE) {
        return false;
    }

    InputEvent fallbackEvent = event;
    fallbackEvent.vk = fallbackVk;
    if (MatchesHotkey(tracker, keys, fallbackEvent, exclusions, triggerOnRelease)) {
        matchedViaFallback = true;
        return true;
    }

    return false;
}

void ComputeMainBindingTarget(const config::HotkeyConfig& hotkey,
                              const std::string& selectedSecondaryMode,
                              const std::string& currentMode,
                              const std::string& returnMode,
                              bool allowReturnWhenActive,
                              std::string& outTargetMode,
                              bool& outExitTransition) {
    outTargetMode = selectedSecondaryMode;
    outExitTransition = false;

    if (!selectedSecondaryMode.empty() &&
        allowReturnWhenActive &&
        hotkey.returnToDefaultOnRepeat &&
        !returnMode.empty() &&
        currentMode == selectedSecondaryMode &&
        returnMode != currentMode) {
        outTargetMode = returnMode;
        outExitTransition = true;
    }
}

void ComputeAltBindingTarget(const config::HotkeyConfig& hotkey,
                             const std::string& selectedSecondaryMode,
                             const config::AltSecondaryModeConfig& alt,
                             const std::string& baseSecondaryMode,
                             const std::string& returnMode,
                             const std::string& currentMode,
                             bool allowReturnWhenActive,
                             std::string& outTargetMode,
                             bool& outExitTransition,
                             std::string& outNextSecondaryMode) {
    if (allowReturnWhenActive &&
        hotkey.returnToDefaultOnRepeat &&
        !returnMode.empty() &&
        !alt.mode.empty() &&
        currentMode == alt.mode &&
        returnMode != currentMode) {
        outNextSecondaryMode = baseSecondaryMode;
        outTargetMode = returnMode;
        outExitTransition = true;
        return;
    }

    outNextSecondaryMode = (selectedSecondaryMode == alt.mode) ? baseSecondaryMode : alt.mode;
    outTargetMode = outNextSecondaryMode;
    outExitTransition = !selectedSecondaryMode.empty() &&
                        !alt.mode.empty() &&
                        selectedSecondaryMode == alt.mode &&
                        currentMode == selectedSecondaryMode;
}

std::string ResolveReturnMode(const config::HotkeyConfig& hotkey, const std::string& defaultMode) {
    if (!hotkey.returnMode.empty()) {
        return hotkey.returnMode;
    }
    if (!hotkey.secondaryMode.empty() && !hotkey.mainMode.empty()) {
        return hotkey.mainMode;
    }
    return defaultMode;
}

} // namespace

void HotkeyDispatcher::SetHotkeys(std::vector<config::HotkeyConfig> hotkeys) {
    std::lock_guard<std::mutex> lock(mutex_);
    hotkeys_ = std::move(hotkeys);

    lastTriggerTimes_.clear();
    lastTriggerTimes_.resize(hotkeys_.size());

    pendingTriggerVariant_.clear();
    pendingTriggerVariant_.resize(hotkeys_.size(), kNoPendingVariant);

    invalidatedTriggerOnRelease_.clear();
    invalidatedTriggerOnRelease_.resize(hotkeys_.size(), false);

    pendingTriggerViaRebind_.clear();
    pendingTriggerViaRebind_.resize(hotkeys_.size(), false);

    activeHoldVariant_.clear();
    activeHoldVariant_.resize(hotkeys_.size(), kNoPendingVariant);

    activeHoldViaRebind_.clear();
    activeHoldViaRebind_.resize(hotkeys_.size(), false);

    activeHoldTargetModes_.clear();
    activeHoldTargetModes_.resize(hotkeys_.size());

    activeHoldReturnModes_.clear();
    activeHoldReturnModes_.resize(hotkeys_.size());

    currentSecondaryModes_.clear();
    currentSecondaryModes_.reserve(hotkeys_.size());
    for (const auto& hotkey : hotkeys_) {
        currentSecondaryModes_.push_back(GetBaseSecondaryMode(hotkey));
    }
}

HotkeyEvaluationResult HotkeyDispatcher::Evaluate(const KeyStateTracker& tracker,
                                                  const InputEvent& event,
                                                  const std::string& gameState,
                                                  const std::string& currentMode,
                                                  const std::string& defaultMode,
                                                  VkCode rebindTargetVk) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (hotkeys_.empty()) {
        return HotkeyEvaluationResult{};
    }

    auto evaluateBindingForHotkey = [&](std::size_t hotkeyIndex, bool triggerOnRelease, bool allowReturnWhenActive) {
        BindingEvaluation result;
        const config::HotkeyConfig& hotkey = hotkeys_[hotkeyIndex];
        const std::string baseSecondaryMode = GetBaseSecondaryMode(hotkey);
        const std::string returnMode = ResolveReturnMode(hotkey, defaultMode);
        std::string selectedSecondaryMode = baseSecondaryMode;
        if (hotkeyIndex < currentSecondaryModes_.size() && !currentSecondaryModes_[hotkeyIndex].empty()) {
            selectedSecondaryMode = currentSecondaryModes_[hotkeyIndex];
        }

        auto tryBinding = [&](const std::vector<VkCode>& keys,
                              int variantIndex,
                              const std::string& targetMode,
                              bool isExitTransition,
                              bool updateSecondaryMode,
                              const std::string& nextSecondaryMode) {
            if (!IsGameStateAllowed(hotkey, gameState, isExitTransition)) {
                return false;
            }

            bool matchedViaFallback = false;
            if (!MatchesHotkeyWithFallback(tracker,
                                           keys,
                                           event,
                                           hotkey.conditions.exclusions,
                                           triggerOnRelease,
                                           rebindTargetVk,
                                           matchedViaFallback)) {
                return false;
            }

            result.matched = true;
            result.matchedViaFallback = matchedViaFallback;
            result.variantIndex = variantIndex;
            result.targetMode = targetMode;
            result.isExitTransition = isExitTransition;
            result.updateSecondaryMode = updateSecondaryMode;
            result.nextSecondaryMode = nextSecondaryMode;
            return true;
        };

        for (std::size_t altIndex = 0; altIndex < hotkey.altSecondaryModes.size(); ++altIndex) {
            const auto& alt = hotkey.altSecondaryModes[altIndex];
            std::string targetMode;
            bool isExitTransition = false;
            std::string nextSecondaryMode;
            ComputeAltBindingTarget(hotkey,
                                    selectedSecondaryMode,
                                    alt,
                                    baseSecondaryMode,
                                    returnMode,
                                    currentMode,
                                    allowReturnWhenActive,
                                    targetMode,
                                    isExitTransition,
                                    nextSecondaryMode);
            if (tryBinding(alt.keys,
                           static_cast<int>(altIndex),
                           targetMode,
                           isExitTransition,
                           true,
                           nextSecondaryMode)) {
                return result;
            }
        }

        std::string targetMode;
        bool isExitTransition = false;
        ComputeMainBindingTarget(hotkey,
                                 selectedSecondaryMode,
                                 currentMode,
                                 returnMode,
                                 allowReturnWhenActive,
                                 targetMode,
                                 isExitTransition);
        (void)tryBinding(hotkey.keys, kMainVariant, targetMode, isExitTransition, false, selectedSecondaryMode);
        return result;
    };

    const auto now = std::chrono::steady_clock::now();

    std::vector<int> pressMatchedVariant(hotkeys_.size(), kNoPendingVariant);
    std::vector<bool> pressMatchedViaFallback(hotkeys_.size(), false);
    if (event.action == InputAction::Press) {
        for (std::size_t i = 0; i < hotkeys_.size(); ++i) {
            const auto& hotkey = hotkeys_[i];
            if (!hotkey.triggerOnRelease || hotkey.triggerOnHold) {
                continue;
            }

            const BindingEvaluation pressMatch = evaluateBindingForHotkey(i, false, true);
            if (pressMatch.matched) {
                pressMatchedVariant[i] = pressMatch.variantIndex;
                pressMatchedViaFallback[i] = pressMatch.matchedViaFallback;
            }
        }

        for (std::size_t i = 0; i < hotkeys_.size(); ++i) {
            if (pendingTriggerVariant_[i] != kNoPendingVariant && pressMatchedVariant[i] != pendingTriggerVariant_[i]) {
                invalidatedTriggerOnRelease_[i] = true;
            }

            if (pressMatchedVariant[i] != kNoPendingVariant) {
                pendingTriggerVariant_[i] = pressMatchedVariant[i];
                pendingTriggerViaRebind_[i] = pressMatchedViaFallback[i];
                invalidatedTriggerOnRelease_[i] = false;
            }
        }
    }

    for (std::size_t i = 0; i < hotkeys_.size(); ++i) {
        const auto& hotkey = hotkeys_[i];
        bool blockKeyFromGame = hotkey.blockKeyFromGame;

        if (hotkey.triggerOnHold) {
            if (event.action == InputAction::Press) {
                const BindingEvaluation holdPressMatch = evaluateBindingForHotkey(i, false, false);
                if (!holdPressMatch.matched) {
                    continue;
                }

                if (holdPressMatch.matchedViaFallback) {
                    blockKeyFromGame = true;
                }

                if (!CheckDebounce(i, hotkey.debounce, now)) {
                    return HotkeyEvaluationResult{"", i, true, false, blockKeyFromGame};
                }

                activeHoldVariant_[i] = holdPressMatch.variantIndex;
                activeHoldViaRebind_[i] = holdPressMatch.matchedViaFallback;
                activeHoldTargetModes_[i] = holdPressMatch.targetMode;
                activeHoldReturnModes_[i] = ResolveReturnMode(hotkey, defaultMode);
                lastTriggerTimes_[i] = now;

                if (holdPressMatch.targetMode.empty() || currentMode == holdPressMatch.targetMode) {
                    return HotkeyEvaluationResult{"", i, true, false, blockKeyFromGame};
                }

                return HotkeyEvaluationResult{holdPressMatch.targetMode, i, true, true, blockKeyFromGame};
            }

            if (event.action != InputAction::Release) {
                continue;
            }

            const BindingEvaluation holdReleaseMatch = evaluateBindingForHotkey(i, true, false);
            if (!holdReleaseMatch.matched) {
                continue;
            }

            if (holdReleaseMatch.matchedViaFallback || activeHoldViaRebind_[i]) {
                blockKeyFromGame = true;
            }

            const bool wasHolding = (activeHoldVariant_[i] == holdReleaseMatch.variantIndex);
            const std::string heldTargetMode = activeHoldTargetModes_[i];
            const std::string returnMode = activeHoldReturnModes_[i];
            activeHoldVariant_[i] = kNoPendingVariant;
            activeHoldViaRebind_[i] = false;
            activeHoldTargetModes_[i].clear();
            activeHoldReturnModes_[i].clear();

            if (!wasHolding) {
                return HotkeyEvaluationResult{"", i, true, false, blockKeyFromGame};
            }

            if (heldTargetMode.empty() || currentMode != heldTargetMode || returnMode.empty() || returnMode == currentMode) {
                return HotkeyEvaluationResult{"", i, true, false, blockKeyFromGame};
            }

            return HotkeyEvaluationResult{returnMode, i, true, true, blockKeyFromGame};
        }

        if (hotkey.triggerOnRelease) {
            if (event.action == InputAction::Press) {
                if (pressMatchedVariant[i] == kNoPendingVariant) {
                    continue;
                }

                if (pressMatchedViaFallback[i] || pendingTriggerViaRebind_[i]) {
                    blockKeyFromGame = true;
                }
                return HotkeyEvaluationResult{"", i, true, false, blockKeyFromGame};
            }

            if (event.action != InputAction::Release) {
                continue;
            }

            const BindingEvaluation releaseMatch = evaluateBindingForHotkey(i, true, true);
            if (!releaseMatch.matched) {
                continue;
            }

            if (releaseMatch.matchedViaFallback || pendingTriggerViaRebind_[i]) {
                blockKeyFromGame = true;
            }

            const bool wasPending = (pendingTriggerVariant_[i] == releaseMatch.variantIndex);
            const bool wasInvalidated = invalidatedTriggerOnRelease_[i];
            pendingTriggerVariant_[i] = kNoPendingVariant;
            invalidatedTriggerOnRelease_[i] = false;
            pendingTriggerViaRebind_[i] = false;

            if (!wasPending || wasInvalidated) {
                return HotkeyEvaluationResult{"", i, true, false, blockKeyFromGame};
            }

            if (!CheckDebounce(i, hotkey.debounce, now)) {
                return HotkeyEvaluationResult{"", i, true, false, blockKeyFromGame};
            }

            if (releaseMatch.updateSecondaryMode && i < currentSecondaryModes_.size()) {
                currentSecondaryModes_[i] = releaseMatch.nextSecondaryMode;
            }
            lastTriggerTimes_[i] = now;
            return HotkeyEvaluationResult{releaseMatch.targetMode, i, true, true, blockKeyFromGame};
        }

        const BindingEvaluation match = evaluateBindingForHotkey(i, false, true);
        if (!match.matched) {
            continue;
        }

        if (match.matchedViaFallback) {
            blockKeyFromGame = true;
        }

        if (!CheckDebounce(i, hotkey.debounce, now)) {
            return HotkeyEvaluationResult{"", i, true, false, blockKeyFromGame};
        }

        if (match.updateSecondaryMode && i < currentSecondaryModes_.size()) {
            currentSecondaryModes_[i] = match.nextSecondaryMode;
        }
        lastTriggerTimes_[i] = now;
        return HotkeyEvaluationResult{match.targetMode, i, true, true, blockKeyFromGame};
    }

    return HotkeyEvaluationResult{};
}

bool HotkeyDispatcher::IsGameStateAllowed(const config::HotkeyConfig& hotkey,
                                          const std::string& gameState,
                                          bool isExitTransition) const {
    if (hotkey.conditions.gameState.empty() || gameState.empty()) {
        return true;
    }

    if (config::HasMatchingGameStateCondition(hotkey.conditions.gameState, gameState)) {
        return true;
    }

    if (!hotkey.allowExitToFullscreenRegardlessOfGameState) {
        return false;
    }

    return isExitTransition;
}

bool HotkeyDispatcher::CheckDebounce(size_t hotkeyIndex,
                                     int debounceMs,
                                     std::chrono::steady_clock::time_point now) {
    if (debounceMs <= 0) {
        return true;
    }

    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastTriggerTimes_[hotkeyIndex]).count();
    return elapsed >= debounceMs;
}

std::string HotkeyDispatcher::GetBaseSecondaryMode(const config::HotkeyConfig& hotkey) const {
    if (!hotkey.secondaryMode.empty()) {
        return hotkey.secondaryMode;
    }
    return hotkey.mainMode;
}

void HotkeyDispatcher::ResetSecondaryModes() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (std::size_t i = 0; i < pendingTriggerVariant_.size(); ++i) {
        pendingTriggerVariant_[i] = kNoPendingVariant;
    }
    for (std::size_t i = 0; i < invalidatedTriggerOnRelease_.size(); ++i) {
        invalidatedTriggerOnRelease_[i] = false;
    }
    for (std::size_t i = 0; i < pendingTriggerViaRebind_.size(); ++i) {
        pendingTriggerViaRebind_[i] = false;
    }
    for (std::size_t i = 0; i < activeHoldVariant_.size(); ++i) {
        activeHoldVariant_[i] = kNoPendingVariant;
    }
    for (std::size_t i = 0; i < activeHoldViaRebind_.size(); ++i) {
        activeHoldViaRebind_[i] = false;
    }
    for (std::size_t i = 0; i < activeHoldTargetModes_.size(); ++i) {
        activeHoldTargetModes_[i].clear();
    }
    for (std::size_t i = 0; i < activeHoldReturnModes_.size(); ++i) {
        activeHoldReturnModes_[i].clear();
    }
    currentSecondaryModes_.clear();
    currentSecondaryModes_.reserve(hotkeys_.size());
    for (const auto& hotkey : hotkeys_) {
        currentSecondaryModes_.push_back(GetBaseSecondaryMode(hotkey));
    }
}

std::optional<std::string> HotkeyDispatcher::ReleaseHeldModeForInputReset(const std::string& currentMode) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::optional<std::string> returnMode;
    for (std::size_t i = 0; i < pendingTriggerVariant_.size(); ++i) {
        pendingTriggerVariant_[i] = kNoPendingVariant;
    }
    for (std::size_t i = 0; i < invalidatedTriggerOnRelease_.size(); ++i) {
        invalidatedTriggerOnRelease_[i] = false;
    }
    for (std::size_t i = 0; i < pendingTriggerViaRebind_.size(); ++i) {
        pendingTriggerViaRebind_[i] = false;
    }

    for (std::size_t i = 0; i < activeHoldVariant_.size(); ++i) {
        if (activeHoldVariant_[i] != kNoPendingVariant &&
            activeHoldTargetModes_[i] == currentMode &&
            !activeHoldReturnModes_[i].empty() &&
            activeHoldReturnModes_[i] != currentMode) {
            returnMode = activeHoldReturnModes_[i];
        }

        activeHoldVariant_[i] = kNoPendingVariant;
        activeHoldViaRebind_[i] = false;
        activeHoldTargetModes_[i].clear();
        activeHoldReturnModes_[i].clear();
    }

    return returnMode;
}

std::vector<config::HotkeyConfig> HotkeyDispatcher::GetHotkeys() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return hotkeys_;
}

std::optional<config::HotkeyConfig> HotkeyDispatcher::GetHotkey(size_t index) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (index < hotkeys_.size()) {
        return hotkeys_[index];
    }
    return std::nullopt;
}

} // namespace platform::input
