#include "hotkey_matcher.h"

namespace platform::input {

namespace {

bool MainKeyMatches(VkCode expectedMain, VkCode actualVk) {
    if (expectedMain == actualVk) { return true; }

    if (expectedMain == VK_CONTROL && (actualVk == VK_LCONTROL || actualVk == VK_RCONTROL)) { return true; }
    if (expectedMain == VK_SHIFT && (actualVk == VK_LSHIFT || actualVk == VK_RSHIFT)) { return true; }
    if (expectedMain == VK_MENU && (actualVk == VK_LMENU || actualVk == VK_RMENU)) { return true; }

    if (actualVk == VK_CONTROL && (expectedMain == VK_LCONTROL || expectedMain == VK_RCONTROL)) { return true; }
    if (actualVk == VK_SHIFT && (expectedMain == VK_LSHIFT || expectedMain == VK_RSHIFT)) { return true; }
    if (actualVk == VK_MENU && (expectedMain == VK_LMENU || expectedMain == VK_RMENU)) { return true; }

    return false;
}

bool IsKeyEvent(const InputEvent& e) {
    return e.type == InputEventType::Key || e.type == InputEventType::MouseButton;
}

bool IsTriggerAction(const InputEvent& event, bool triggerOnRelease) {
    if (!IsKeyEvent(event)) { return false; }

    if (triggerOnRelease) { return event.action == InputAction::Release; }

    return event.action == InputAction::Press;
}

bool RequiredKeyDown(const KeyStateTracker& tracker, VkCode requiredKey) {
    if (requiredKey == VK_CONTROL) { return tracker.IsAnyCtrlDown(); }
    if (requiredKey == VK_SHIFT) { return tracker.IsAnyShiftDown(); }
    if (requiredKey == VK_MENU) { return tracker.IsAnyAltDown(); }
    return tracker.IsDown(requiredKey);
}

} // namespace

bool MatchesHotkey(const KeyStateTracker& tracker, const std::vector<VkCode>& keys, const InputEvent& triggerEvent,
                  const std::vector<VkCode>& exclusionKeys, bool triggerOnRelease) {
    if (keys.empty()) { return false; }
    if (!IsTriggerAction(triggerEvent, triggerOnRelease)) { return false; }

    const VkCode mainKey = keys.back();
    if (!MainKeyMatches(mainKey, triggerEvent.vk)) { return false; }

    if (!triggerOnRelease) {
        for (VkCode excluded : exclusionKeys) {
            if (excluded == VK_NONE) { continue; }
            if (RequiredKeyDown(tracker, excluded)) { return false; }
        }
    }

    if (triggerOnRelease) { return true; }

    for (std::size_t i = 0; i + 1 < keys.size(); ++i) {
        const VkCode requiredKey = keys[i];
        if (requiredKey == VK_NONE) { continue; }
        if (!RequiredKeyDown(tracker, requiredKey)) { return false; }
    }

    return true;
}

} // namespace platform::input
