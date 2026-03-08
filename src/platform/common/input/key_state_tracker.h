#pragma once

#include "input_event.h"

#include <array>
#include <cstddef>
#include <vector>

namespace platform::input {

class KeyStateTracker {
  public:
    void ApplyEvent(const InputEvent& event);
    void Clear();

    bool IsDown(VkCode vk) const;
    bool IsAnyCtrlDown() const;
    bool IsAnyShiftDown() const;
    bool IsAnyAltDown() const;
    bool IsFocused() const;
    std::vector<VkCode> GetDownKeys() const;

  private:
    static constexpr std::size_t kMaxTrackedVk = 512;

    std::array<bool, kMaxTrackedVk> m_down{};
    bool m_focused = true;

    void RefreshAggregateModifiers();
    static std::size_t ClampIndex(VkCode vk);
};

} // namespace platform::input
