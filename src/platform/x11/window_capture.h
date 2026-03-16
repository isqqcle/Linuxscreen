#pragma once

#ifdef None
#pragma push_macro("None")
#undef None
#define LINUXSCREEN_RESTORE_X11_NONE_MACRO
#endif

#include "../common/linuxscreen_config.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace platform::x11 {

enum class WindowCaptureBackend : std::uint8_t {
    Unknown = 0,
    MacOS = 1,
    X11 = 2,
    Wayland = 3,
};

enum class WindowCaptureAccessState : std::uint8_t {
    Unsupported = 0,
    Unknown = 1,
    Granted = 2,
    Denied = 3,
};

enum class WindowCaptureState : std::uint8_t {
    Idle = 0,
    Starting = 1,
    Streaming = 2,
    SelectionRequired = 3,
    NotFound = 4,
    NoAccess = 5,
    Unsupported = 6,
    Error = 7,
};

enum class WindowCaptureSelectionResult : std::uint8_t {
    Unsupported = 0,
    Cancelled = 1,
    Selected = 2,
    Failed = 3,
};

struct AvailableWindow {
    std::uint64_t windowId = 0;
    std::string appId;
    std::string appName;
    std::string windowTitle;
    std::string selectionToken;
    int width = 0;
    int height = 0;
    bool onScreen = false;
    bool active = false;
};

struct WindowCaptureStatus {
    WindowCaptureBackend backend = WindowCaptureBackend::Unknown;
    WindowCaptureAccessState access = WindowCaptureAccessState::Unsupported;
    WindowCaptureState state = WindowCaptureState::Unsupported;
    std::string message;
    std::uint64_t frameNumber = 0;
    int width = 0;
    int height = 0;
    bool canPersistSelection = false;
    bool selectionPersistent = false;
};

struct LatestFrameSnapshot {
    std::vector<std::uint8_t> pixels;
    int width = 0;
    int height = 0;
    int bytesPerRow = 0;
    int contentX = 0;
    int contentY = 0;
    int contentWidth = 0;
    int contentHeight = 0;
    std::uint64_t frameNumber = 0;
    bool fromCache = false;
};

struct WindowCaptureTextureSnapshot {
    std::uint32_t textureId = 0;
    int width = 0;
    int height = 0;
    std::uint64_t frameNumber = 0;
    bool yInverted = false;
};

struct WindowCaptureRequest {
    std::string appId;
    std::string windowTitle;
    platform::config::MirrorSourceTitleMatchMode titleMatchMode =
        platform::config::MirrorSourceTitleMatchMode::Exact;
    platform::config::MirrorSourceFallbackMode fallbackMode =
        platform::config::MirrorSourceFallbackMode::None;
    std::string selectionToken;
    int fps = 30;
    int preferredWidth = 0;
    int preferredHeight = 0;
};

bool IsWindowCaptureSource(const platform::config::MirrorSourceConfig& source);
bool HasConfiguredWindowCaptureSource(const platform::config::MirrorSourceConfig& source);
bool IsConfiguredWindowCaptureRequest(const WindowCaptureRequest& request);
std::string NormalizeMirrorCaptureAnchor(const std::string& relativeTo);
std::string MakeWindowCaptureKey(const WindowCaptureRequest& request);
std::string MakeWindowCaptureKey(const platform::config::MirrorSourceConfig& source);
std::vector<WindowCaptureRequest> NormalizeWindowCaptureRequests(
    const std::vector<WindowCaptureRequest>& requests);
int FindBestMatchingWindowIndex(const std::vector<AvailableWindow>& windows,
                                const std::string& appId,
                                const std::string& windowTitle,
                                platform::config::MirrorSourceTitleMatchMode titleMatchMode =
                                    platform::config::MirrorSourceTitleMatchMode::Exact,
                                platform::config::MirrorSourceFallbackMode fallbackMode =
                                    platform::config::MirrorSourceFallbackMode::None,
                                std::uint64_t preferredWindowId = 0,
                                int preferredWidth = 0,
                                int preferredHeight = 0);

WindowCaptureBackend GetWindowCaptureBackend();
WindowCaptureAccessState GetWindowCaptureAccessState();
void RefreshAvailableWindows();
std::vector<AvailableWindow> GetAvailableWindowsSnapshot();
void SetWindowCaptureRuntimeReady(bool ready);
bool IsWindowCaptureRuntimeReady();
void SetWindowCaptureRequests(const std::vector<WindowCaptureRequest>& requests);
void InvalidateWindowCaptureTransientState();
void ForgetWindowCaptureSource(const platform::config::MirrorSourceConfig& source);
WindowCaptureStatus GetWindowCaptureStatus(const platform::config::MirrorSourceConfig& source);
bool CopyLatestWindowCaptureTexture(const platform::config::MirrorSourceConfig& source,
                                    WindowCaptureTextureSnapshot& outTexture);
bool CopyLatestWindowCaptureFrame(const platform::config::MirrorSourceConfig& source,
                                  LatestFrameSnapshot& outFrame);
WindowCaptureSelectionResult RequestWindowCaptureSelection(platform::config::MirrorSourceConfig& ioSource,
                                                           int fps,
                                                           bool forceInteractiveSelection = false,
                                                           std::string* outMessage = nullptr);
void ShutdownWindowCaptureForProcessExit();

WindowCaptureBackend DetectLinuxWindowCaptureBackendForEnvironment(const char* sessionType,
                                                                   const char* display,
                                                                   const char* waylandDisplay);
bool ShouldDowngradeX11CompositeCapture(bool invalidGeometry,
                                        std::uint32_t consecutiveFailures,
                                        std::uint32_t consecutiveStaleFrames);

} // namespace platform::x11

#ifdef LINUXSCREEN_RESTORE_X11_NONE_MACRO
#pragma pop_macro("None")
#undef LINUXSCREEN_RESTORE_X11_NONE_MACRO
#endif
