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

    const std::size_t idx = ClampIndex(event.vk);
    if (idx > 0) { m_down[idx] = isPress; }

    RefreshAggregateModifiers();
}

void KeyStateTracker::Clear() {
    m_down.fill(false);
    RefreshAggregateModifiers();
}

bool KeyStateTracker::IsDown(VkCode vk) const {
    const std::size_t idx = ClampIndex(vk);
    if (idx == 0) { return false; }
    return m_down[idx];
}

bool KeyStateTracker::IsAnyCtrlDown() const { return IsDown(VK_CONTROL); }

bool KeyStateTracker::IsAnyShiftDown() const { return IsDown(VK_SHIFT); }

bool KeyStateTracker::IsAnyAltDown() const { return IsDown(VK_MENU); }

bool KeyStateTracker::IsFocused() const { return m_focused; }

std::vector<VkCode> KeyStateTracker::GetDownKeys() const {
    std::vector<VkCode> downKeys;
    downKeys.reserve(32);

    for (std::size_t i = 1; i < kMaxTrackedVk; ++i) {
        if (!m_down[i]) {
            continue;
        }

        const VkCode vk = static_cast<VkCode>(i);
        if (vk == VK_CONTROL || vk == VK_SHIFT || vk == VK_MENU) {
            continue;
        }
        downKeys.push_back(vk);
    }

    return downKeys;
}

void KeyStateTracker::RefreshAggregateModifiers() {
    m_down[ClampIndex(VK_CONTROL)] = m_down[ClampIndex(VK_LCONTROL)] || m_down[ClampIndex(VK_RCONTROL)];
    m_down[ClampIndex(VK_SHIFT)] = m_down[ClampIndex(VK_LSHIFT)] || m_down[ClampIndex(VK_RSHIFT)];
    m_down[ClampIndex(VK_MENU)] = m_down[ClampIndex(VK_LMENU)] || m_down[ClampIndex(VK_RMENU)];
}

std::size_t KeyStateTracker::ClampIndex(VkCode vk) {
    if (vk == VK_NONE || vk >= kMaxTrackedVk) { return 0; }
    return static_cast<std::size_t>(vk);
}

} // namespace platform::input
