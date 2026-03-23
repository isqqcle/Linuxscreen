#pragma once

#include "../overlay_internal.h"
#include "../../mirror_image_source.h"
#include "../../window_capture.h"

#include <mutex>
#include <vector>

namespace platform::x11 {

struct MirrorEditorState {
    struct WaylandSelectionTask {
        bool inFlight = false;
        bool completed = false;
        std::string mirrorName;
        platform::config::MirrorSourceConfig source;
        WindowCaptureSelectionResult result = WindowCaptureSelectionResult::Unsupported;
        std::string message;
    };

    int mirrorListSelectionIndex = 0;
    int selectedMirrorIndex = -1;
    char nameBuffer[256] = {};
    std::string mirrorNameError;
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
