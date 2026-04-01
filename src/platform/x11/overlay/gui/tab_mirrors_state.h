#pragma once

#include "../overlay_internal.h"
#include "../../mirror_image_source.h"
#include "../../window_capture.h"

#include <mutex>
#include <string>
#include <vector>

namespace platform::x11 {

enum class MirrorVisualEditorMode {
    Layout = 0,
    Crop = 1,
};

enum class MirrorDirectEditSelectionKind {
    None = 0,
    Mirror = 1,
    GroupItem = 2,
    Group = 3,
};

enum class MirrorsMainEditorTab {
    Mirrors = 0,
    Groups = 1,
};

struct MirrorEditorState {
    struct WaylandSelectionTask {
        bool inFlight = false;
        bool completed = false;
        std::string mirrorName;
        platform::config::MirrorSourceConfig source;
        WindowCaptureSelectionResult result = WindowCaptureSelectionResult::Unsupported;
        std::string message;
    };

    struct VisualDragState {
        bool active = false;
        bool crop = false;
        bool moved = false;
        bool dirty = false;
        int edgeMask = 0;
        int mirrorIndex = -1;
        int zoneIndex = -1;
        ImVec2 dragStartMouse{0.0f, 0.0f};
        ImVec4 startRect{0.0f, 0.0f, 0.0f, 0.0f};
        float startOutputWidth = 0.0f;
        float startOutputHeight = 0.0f;
        float startCaptureWidth = 0.0f;
        float startCaptureHeight = 0.0f;
        float startOutputScaleX = 1.0f;
        float startOutputScaleY = 1.0f;
        float startGroupItemWidthPercent = 1.0f;
        float startGroupItemHeightPercent = 1.0f;
        int startGroupItemOffsetX = 0;
        int startGroupItemOffsetY = 0;
        int startGroupOutputX = 0;
        int startGroupOutputY = 0;
        bool startGroupUseRelativeSize = false;
        float startGroupRelativeWidth = 1.0f;
        float startGroupRelativeHeight = 1.0f;
        float startGroupScale = 1.0f;
        float startGroupScaleX = 1.0f;
        float startGroupScaleY = 1.0f;
        std::vector<float> startGroupWidthsPercent;
        std::vector<float> startGroupHeightsPercent;
        std::vector<int> startGroupOffsetsX;
        std::vector<int> startGroupOffsetsY;
        std::vector<ImVec4> startGroupRects;
    };

    struct DirectEditSelection {
        MirrorDirectEditSelectionKind kind = MirrorDirectEditSelectionKind::None;
        std::string mirrorId;
        std::string groupId;
        int groupItemIndex = -1;
    };

    int mirrorListSelectionIndex = 0;
    int selectedMirrorIndex = -1;
    char nameBuffer[256] = {};
    std::string mirrorNameError;
    MirrorVisualEditorMode visualEditorMode = MirrorVisualEditorMode::Layout;
    int visualEditorCropZoneIndex = 0;
    VisualDragState visualDrag;
    bool directEditActive = false;
    bool directEditHideMainWindow = true;
    bool directEditFullscreenHovered = false;
    bool directEditFullscreenActive = false;
    DirectEditSelection directEditSelection;
    DirectEditSelection directEditPendingClickSelection;
    bool directEditHasPendingClickSelection = false;
    ImVec2 directEditLastCyclePos{0.0f, 0.0f};
    int directEditCycleIndex = 0;
    std::string directEditLastCycleStackKey;
    bool directEditShowCaptureGuides = true;
    int directEditSelectedCaptureZoneIndex = 0;
    bool directEditShowGroupInspector = false;
    MirrorsMainEditorTab mainEditorTab = MirrorsMainEditorTab::Mirrors;
    bool mainEditorTabSelectionPending = false;
    bool openMirrorPresetPopup = false;
    std::string mirrorPresetStatusMessage;
    int groupListSelectionIndex = 0;
    int selectedGroupIndex = -1;
    char groupNameBuffer[256] = {};
    std::string groupNameError;
    std::string windowSourceCacheKey;
    double windowSourceCacheExpiresAt = 0.0;
    WindowCaptureStatus windowSourceStatus;
    std::vector<AvailableWindow> availableWindows;
    std::string titlePatternBufferKey;
    char titlePatternBuffer[512] = {};
    bool imageSourcePickerOpen = false;
    int imageSourcePickerMirrorIndex = -1;
    MirrorImageSourceStatus imageSourceStatus;
    std::mutex waylandSelectionMutex;
    WaylandSelectionTask waylandSelectionTask;
};
extern MirrorEditorState g_mirrorEditorState;

} // namespace platform::x11
