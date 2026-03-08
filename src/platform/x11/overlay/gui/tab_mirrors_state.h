#pragma once

#include "../overlay_internal.h"

namespace platform::x11 {

struct MirrorEditorState {
    int selectedMirrorIndex = -1;
    char nameBuffer[256] = {};
    std::string mirrorNameError;
    int selectedGroupIndex = -1;
    char groupNameBuffer[256] = {};
    std::string groupNameError;
};
extern MirrorEditorState g_mirrorEditorState;

} // namespace platform::x11
