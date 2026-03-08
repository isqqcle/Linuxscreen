#pragma once

#include <cstddef>

namespace platform::x11 {

struct ImGuiInputDrainResult {
    std::size_t drained = 0;
    std::size_t applied = 0;
    bool hasImGuiSupport = false;
    bool hadCurrentContext = false;
};

bool HasImGuiInputBridgeSupport();
ImGuiInputDrainResult DrainImGuiInputEventsToCurrentContext(std::size_t maxEvents);

} // namespace platform::x11
