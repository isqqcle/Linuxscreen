#pragma once
#ifndef __APPLE__

#include "window_capture.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

struct _XDisplay;
typedef struct _XDisplay Display;
typedef struct _GDBusConnection GDBusConnection;

namespace platform::x11 {

// Debug logging

inline bool IsWindowCaptureDebugEnabled() {
    static const bool enabled = []() {
        const char* value = std::getenv("LINUXSCREEN_X11_DEBUG");
        return value && value[0] != '\0' && std::strcmp(value, "0") != 0;
    }();
    return enabled;
}

inline std::string SanitizeDebugValue(std::string value) {
    for (char& ch : value) {
        if (ch == '\n' || ch == '\r' || ch == '\t') {
            ch = ' ';
        }
    }
    return value;
}

template <typename... Args>
void DebugWindowCaptureLog(const char* format, Args... args) {
    if (!IsWindowCaptureDebugEnabled() || !format) {
        return;
    }

    std::fprintf(stderr, "[Linuxscreen][x11][window-capture] ");
    std::fprintf(stderr, format, args...);
    std::fprintf(stderr, "\n");
    std::fflush(stderr);
}

// Shared utility

bool SleepInterruptible(const std::atomic<bool>& stopRequested, std::chrono::milliseconds duration);

constexpr const char* kWaylandManualRepickMessage =
    "Previously selected Wayland window is unavailable. Use Re-pick Window to resume capture.";

// Structs

struct X11SourceRecord {
    WindowCaptureRequest request;
    WindowCaptureStatus status;
    WindowCaptureTextureSnapshot textureSnapshot;
    LatestFrameSnapshot frame;
    std::thread worker;
    std::atomic<bool> stopRequested{ false };
};

struct WaylandRememberedSelection {
    WindowCaptureRequest request;
    int width = 0;
    int height = 0;
};

// Shared globals (defined in window_capture_linux.cpp)

extern std::mutex g_mutex;
extern std::unordered_map<std::string, std::unique_ptr<X11SourceRecord>> g_records;
extern std::vector<WindowCaptureRequest> g_desiredRequests;
extern std::vector<AvailableWindow> g_availableWindows;
extern std::unordered_map<std::string, std::string> g_waylandManualRepickMessages;
extern std::unordered_map<std::string, WaylandRememberedSelection> g_waylandRememberedSelections;
extern std::atomic<bool> g_runtimeReady;
extern WindowCaptureBackend g_backend;

inline void ApplyRememberedWaylandSelectionLocked(const std::string& key,
                                                  WindowCaptureRequest& ioRequest,
                                                  int* outWidth = nullptr,
                                                  int* outHeight = nullptr) {
    const auto rememberedIt = g_waylandRememberedSelections.find(key);
    if (rememberedIt == g_waylandRememberedSelections.end()) {
        return;
    }

    const WaylandRememberedSelection& remembered = rememberedIt->second;
    if (!remembered.request.selectionToken.empty()) {
        ioRequest.selectionToken = remembered.request.selectionToken;
    }
    if (!remembered.request.appId.empty()) {
        ioRequest.appId = remembered.request.appId;
    }
    if (!remembered.request.windowTitle.empty()) {
        ioRequest.windowTitle = remembered.request.windowTitle;
    }
    if (outWidth) {
        *outWidth = remembered.width;
    }
    if (outHeight) {
        *outHeight = remembered.height;
    }
}

inline AvailableWindow MakeSelectedWindowSnapshot(const WindowCaptureRequest& request,
                                                  std::uint64_t windowId,
                                                  int width,
                                                  int height) {
    AvailableWindow selected;
    selected.windowId = windowId;
    selected.appId = request.appId;
    selected.appName = request.appId;
    selected.windowTitle = request.windowTitle;
    selected.selectionToken = request.selectionToken;
    selected.width = width;
    selected.height = height;
    selected.onScreen = true;
    selected.active = true;
    return selected;
}

// Backend initialization

void EnsureBackendInitialized();

// Record lifecycle (defined in window_capture_linux.cpp)

void StartOrUpdateRecordLocked(const std::string& key, const WindowCaptureRequest& request);
void StopRecordWorker(X11SourceRecord& record);
void StopRecordWorkersAsync(std::vector<std::unique_ptr<X11SourceRecord>> records);

// X11 backend (defined in window_capture_x11.cpp)

void RunX11CaptureThread(const std::string& key, X11SourceRecord* record);
std::vector<AvailableWindow> EnumerateX11Windows(Display* display);

// Wayland backend (defined in window_capture_wayland.cpp)

struct WaylandPortalSession {
    GDBusConnection* connection = nullptr;
    std::string sessionPath;
    std::string restoreToken;
    std::string appId;
    std::string windowTitle;
    std::uint32_t nodeId = 0;
    int width = 0;
    int height = 0;
};

void RunWaylandCaptureThread(const std::string& key, X11SourceRecord* record);
bool HasWaylandPortalScreenCastSupport();
std::string GetWaylandPortalCapabilityMessage();
void RefreshWaylandPortalCapability(bool force = false);
bool CreateWaylandPortalSession(const WindowCaptureRequest& request,
                                int fps,
                                bool allowInteractiveSelection,
                                bool keepSessionOpen,
                                WaylandPortalSession& outSession,
                                std::string& outMessage,
                                WindowCaptureSelectionResult* outSelectionResult = nullptr);
void ShutdownWaylandDmaBufDisplay();

} // namespace platform::x11

#endif
