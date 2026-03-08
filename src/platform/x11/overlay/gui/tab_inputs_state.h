#pragma once

#include "../overlay_internal.h"

namespace platform::x11 {

struct HotkeyEditorState {
    int selectedModeForNewHotkey = 0;
    bool showAddHotkeyDialog = false;
};
extern HotkeyEditorState g_hotkeyEditorState;

struct HotkeyCaptureModalState {
    bool initialized = false;
    std::uint64_t lastSequence = 0;
    bool hadKeysPressed = false;
    std::vector<uint32_t> bindingKeys;
    std::set<uint32_t> currentlyPressed;
};
extern HotkeyCaptureModalState g_hotkeyCaptureModalState;

enum class RebindLayoutBindTarget {
    Unset = 0,
    TypesVk,
    TriggersVk,
};

struct RebindLayoutState {
    bool keyboardLayoutOpen = false;
    bool keyboardLayoutCloseRequested = false;
    std::uint64_t keyboardLayoutOpenSequence = 0;
    float keyboardLayoutScale = 1.40f;
    uint32_t contextVk = 0;
    int contextPreferredIndex = -1;
    RebindLayoutBindTarget bindTarget = RebindLayoutBindTarget::Unset;
    int bindIndex = -1;
    std::uint64_t bindLastSequence = 0;
    int unicodeEditIndex = -1;
    std::string unicodeEditText;
    std::string unicodeShiftEditText;
    uint32_t customDraftInputVk = 0;
    std::string customDraftName;
    bool reopenContextPopupAfterCapture = false;
    bool hasContextPopupPos = false;
    ImVec2 contextPopupPos = ImVec2(0.0f, 0.0f);
    std::uint64_t contextPopupOpenSequence = 0;
    int customSourceCaptureIndex = -1;
    std::uint64_t customSourceCaptureSequence = 0;
};
extern RebindLayoutState g_rebindLayoutState;

} // namespace platform::x11
