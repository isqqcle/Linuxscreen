#pragma once
#include "../overlay_internal.h"

namespace platform::x11 {

void RenderMirrorsTab(platform::config::LinuxscreenConfig& config);
bool IsMirrorDirectEditActive();
void SetMirrorDirectEditActive(bool active, bool hideMainWindow = true);
void RenderMirrorDirectEditOverlay(platform::config::LinuxscreenConfig& config,
                                   float displayWidth,
                                   float displayHeight,
                                   bool* inOutGuiVisible = nullptr);

} // namespace platform::x11
