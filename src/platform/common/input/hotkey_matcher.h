#pragma once

#include "input_event.h"
#include "key_state_tracker.h"

#include <vector>

namespace platform::input {

bool MatchesHotkey(const KeyStateTracker& tracker, const std::vector<VkCode>& keys, const InputEvent& triggerEvent,
                  const std::vector<VkCode>& exclusionKeys = {}, bool triggerOnRelease = false);

} // namespace platform::input
