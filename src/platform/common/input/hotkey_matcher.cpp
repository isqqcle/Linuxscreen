#include "hotkey_matcher.h"

namespace platform::input {

namespace {

bool MainKeyMatches(const BindingKey& expectedMain, const InputEvent& triggerEvent) {
    return expectedMain == BindingKeyFromInputEvent(triggerEvent);
}

bool IsKeyEvent(const InputEvent& e) {
    return e.type == InputEventType::Key || e.type == InputEventType::MouseButton;
}

bool IsTriggerAction(const InputEvent& event, bool triggerOnRelease) {
    if (!IsKeyEvent(event)) { return false; }

    if (triggerOnRelease) { return event.action == InputAction::Release; }

    return event.action == InputAction::Press;
}

bool RequiredKeyDown(const KeyStateTracker& tracker, const BindingKey& requiredKey) {
    return tracker.IsBindingDown(requiredKey);
}

} // namespace

bool MatchesHotkey(const KeyStateTracker& tracker,
                   const std::vector<BindingKey>& keys,
                   const InputEvent& triggerEvent,
                   const std::vector<BindingKey>& exclusionKeys,
                   bool triggerOnRelease) {
    if (keys.empty()) { return false; }
    if (!IsTriggerAction(triggerEvent, triggerOnRelease)) { return false; }

    const BindingKey& mainKey = keys.back();
    if (!MainKeyMatches(mainKey, triggerEvent)) { return false; }

    if (!triggerOnRelease) {
        for (const BindingKey& excluded : exclusionKeys) {
            if (!IsValidBindingKey(excluded)) { continue; }
            if (RequiredKeyDown(tracker, excluded)) { return false; }
        }
    }

    if (triggerOnRelease) { return true; }

    for (std::size_t i = 0; i + 1 < keys.size(); ++i) {
        const BindingKey& requiredKey = keys[i];
        if (!IsValidBindingKey(requiredKey)) { continue; }
        if (!RequiredKeyDown(tracker, requiredKey)) { return false; }
    }

    return true;
}

} // namespace platform::input
