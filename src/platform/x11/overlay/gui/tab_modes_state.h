#pragma once

#include "../overlay_internal.h"

namespace platform::x11 {

struct ModeEditorState {
    int selectedModeIndex = -1;
    char newModeNameBuffer[256] = {};
    std::unordered_map<size_t, std::string> renameBuffers;
    bool showNewModeInput = false;
    bool requestFocusNewMode = false;
    size_t lastModeCountForBackgroundCopy = 0;
    int backgroundCopySourceIndex = -1;
    std::set<std::string> backgroundCopyTargets;
    int backgroundCopyModalSourceIndex = -1;
    std::vector<std::string> backgroundCopyModalTargets;
};
extern ModeEditorState g_modeEditorState;

struct ModeBackgroundPickerState {
    bool dialogOpen = false;
    int modeIndex = -1;
};
extern ModeBackgroundPickerState g_modeBackgroundPickerState;

} // namespace platform::x11
