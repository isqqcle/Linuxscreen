#pragma once

#include "binding_key.h"
#include "vk_codes.h"

#include <cstdint>

namespace platform::input {

enum class InputEventType : std::uint8_t {
    Unknown = 0,
    Key,
    Character,
    MouseButton,
    Scroll,
    CursorPosition,
    Focus,
};

enum class InputAction : std::uint8_t {
    Unknown = 0,
    Press,
    Release,
    Repeat,
    Move,
    FocusChanged,
};

struct InputEvent {
    InputEventType type = InputEventType::Unknown;
    InputAction action = InputAction::Unknown;
    VkCode vk = VK_NONE;
    int nativeKey = 0;
    int nativeScanCode = 0;
    int bindingScanCode = 0;
    int nativeMods = 0;
    std::uint32_t charCodepoint = 0;
    double x = 0.0;
    double y = 0.0;
    double scrollX = 0.0;
    double scrollY = 0.0;
    bool focused = true;
};

inline BindingKey BindingKeyFromInputEvent(const InputEvent& event) {
    if (event.type == InputEventType::Key) {
        const int keyboardScanCode =
            IsValidKeyboardScanCode(event.bindingScanCode) ? event.bindingScanCode : event.nativeScanCode;
        return MakeKeyboardBindingKey(keyboardScanCode);
    }
    if (event.type == InputEventType::MouseButton) {
        return MakeMouseButtonBindingKey(event.nativeKey);
    }
    return {};
}

} // namespace platform::input
