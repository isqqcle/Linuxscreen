#pragma once

#include "input_event.h"

#include <map>
#include <unordered_set>
#include <vector>

namespace platform::input {

struct DownBindingState {
    BindingKey binding;
    int nativeKey = 0;
};

class KeyStateTracker {
  public:
    void ApplyEvent(const InputEvent& event);
    void Clear();

    bool IsScanCodeDown(int nativeScanCode) const;
    bool IsAnyScanCodeDown(const std::initializer_list<int>& nativeScanCodes) const;
    bool IsMouseButtonDown(int button) const;
    bool IsBindingDown(const BindingKey& key) const;
    bool IsFocused() const;
    std::vector<BindingKey> GetDownBindings() const;
    std::vector<DownBindingState> GetDownBindingStates() const;

  private:
    std::map<int, int> m_downKeyboardKeysByScanCode;
    std::unordered_set<int> m_downScanCodes;
    std::unordered_set<int> m_downMouseButtons;
    bool m_focused = true;
};

} // namespace platform::input
