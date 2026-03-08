#pragma once

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
    int nativeMods = 0;
    std::uint32_t charCodepoint = 0;
    double x = 0.0;
    double y = 0.0;
    double scrollX = 0.0;
    double scrollY = 0.0;
    bool focused = true;
};

} // namespace platform::input
