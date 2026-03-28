#include "key_state_tracker.h"

namespace platform::input {

void KeyStateTracker::ApplyEvent(const InputEvent& event) {
    if (event.type == InputEventType::Focus) {
        m_focused = event.focused;
        if (!m_focused) {
            Clear();
            m_focused = false;
        }
        return;
    }

    if (event.type != InputEventType::Key && event.type != InputEventType::MouseButton) { return; }

    const bool isPress = (event.action == InputAction::Press || event.action == InputAction::Repeat);
    const bool isRelease = (event.action == InputAction::Release);
    if (!isPress && !isRelease) { return; }

    const int bindingScanCode = event.bindingScanCode > 0 ? event.bindingScanCode : event.nativeScanCode;
    if (event.type == InputEventType::Key && bindingScanCode > 0) {
        if (isPress) {
            m_downScanCodes.insert(bindingScanCode);
            m_downKeyboardKeysByScanCode[bindingScanCode] = event.nativeKey;
        } else {
            m_downScanCodes.erase(bindingScanCode);
            m_downKeyboardKeysByScanCode.erase(bindingScanCode);
        }
    }
    if (event.type == InputEventType::MouseButton && event.nativeKey >= 0) {
        if (isPress) {
            m_downMouseButtons.insert(event.nativeKey);
        } else {
            m_downMouseButtons.erase(event.nativeKey);
        }
    }

}

void KeyStateTracker::Clear() {
    m_downKeyboardKeysByScanCode.clear();
    m_downScanCodes.clear();
    m_downMouseButtons.clear();
}

bool KeyStateTracker::IsScanCodeDown(int nativeScanCode) const {
    if (nativeScanCode <= 0) {
        return false;
    }
    return m_downScanCodes.find(nativeScanCode) != m_downScanCodes.end();
}

bool KeyStateTracker::IsAnyScanCodeDown(const std::initializer_list<int>& nativeScanCodes) const {
    for (int nativeScanCode : nativeScanCodes) {
        if (IsScanCodeDown(nativeScanCode)) {
            return true;
        }
    }
    return false;
}

bool KeyStateTracker::IsMouseButtonDown(int button) const {
    if (button < 0) {
        return false;
    }
    return m_downMouseButtons.find(button) != m_downMouseButtons.end();
}

bool KeyStateTracker::IsBindingDown(const BindingKey& key) const {
    if (IsKeyboardBindingKey(key)) {
        return IsScanCodeDown(key.code);
    }
    if (IsMouseBindingKey(key)) {
        return IsMouseButtonDown(key.code);
    }
    return false;
}

bool KeyStateTracker::IsFocused() const { return m_focused; }

std::vector<BindingKey> KeyStateTracker::GetDownBindings() const {
    std::vector<BindingKey> downBindings;
    downBindings.reserve(m_downScanCodes.size() + m_downMouseButtons.size());

    for (const auto& [scanCode, nativeKey] : m_downKeyboardKeysByScanCode) {
        (void)nativeKey;
        if (scanCode <= 0) {
            continue;
        }
        downBindings.push_back(MakeKeyboardBindingKey(scanCode));
    }
    for (int button : m_downMouseButtons) {
        if (button < 0) {
            continue;
        }
        downBindings.push_back(MakeMouseButtonBindingKey(button));
    }

    return downBindings;
}

} // namespace platform::input
