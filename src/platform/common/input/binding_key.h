#pragma once

#include <cstdint>

namespace platform::input {

enum class BindingKeyKind : std::uint8_t {
    None = 0,
    Keyboard = 1,
    MouseButton = 2,
};

struct BindingKey {
    BindingKeyKind kind = BindingKeyKind::None;
    int code = 0;
};

inline bool operator==(const BindingKey& lhs, const BindingKey& rhs) {
    return lhs.kind == rhs.kind && lhs.code == rhs.code;
}

inline bool operator!=(const BindingKey& lhs, const BindingKey& rhs) {
    return !(lhs == rhs);
}

inline BindingKey MakeKeyboardBindingKey(int scanCode) {
    if (scanCode <= 0) {
        return {};
    }
    return BindingKey{ BindingKeyKind::Keyboard, scanCode };
}

inline BindingKey MakeMouseButtonBindingKey(int button) {
    if (button < 0) {
        return {};
    }
    return BindingKey{ BindingKeyKind::MouseButton, button };
}

inline bool IsKeyboardBindingKey(const BindingKey& key) {
    return key.kind == BindingKeyKind::Keyboard && key.code > 0;
}

inline bool IsMouseBindingKey(const BindingKey& key) {
    return key.kind == BindingKeyKind::MouseButton && key.code >= 0;
}

inline bool IsValidBindingKey(const BindingKey& key) {
    return IsKeyboardBindingKey(key) || IsMouseBindingKey(key);
}

} // namespace platform::input
