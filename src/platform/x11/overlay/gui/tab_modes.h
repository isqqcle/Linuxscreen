#pragma once
#include "../overlay_internal.h"

namespace platform::x11 {

void RenderModesTab(platform::config::LinuxscreenConfig& config,
                    const std::string& activeMode,
                    platform::x11::MirrorModeState& modeState);

} // namespace platform::x11
