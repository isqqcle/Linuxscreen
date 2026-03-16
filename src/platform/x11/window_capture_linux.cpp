#ifndef __APPLE__

#include "window_capture_linux_internal.h"

#include <X11/Xlib.h>
#include <pipewire/pipewire.h>

#include <algorithm>

namespace platform::x11 {

// Shared globals (declared extern in window_capture_linux_internal.h)

std::mutex g_mutex;
std::unordered_map<std::string, std::unique_ptr<X11SourceRecord>> g_records;
std::vector<WindowCaptureRequest> g_desiredRequests;
std::vector<AvailableWindow> g_availableWindows;
std::unordered_map<std::string, std::string> g_waylandManualRepickMessages;
std::unordered_map<std::string, WaylandRememberedSelection> g_waylandRememberedSelections;
std::atomic<bool> g_runtimeReady{false};
WindowCaptureBackend g_backend = WindowCaptureBackend::Unknown;

namespace {

std::once_flag g_backendInitOnce;
std::once_flag g_x11ThreadsOnce;
std::once_flag g_pipeWireInitOnce;

WindowCaptureBackend DetectBackend() {
    return DetectLinuxWindowCaptureBackendForEnvironment(std::getenv("XDG_SESSION_TYPE"),
                                                         std::getenv("DISPLAY"),
                                                         std::getenv("WAYLAND_DISPLAY"));
}

} // namespace

// Shared utility

bool SleepInterruptible(const std::atomic<bool>& stopRequested, std::chrono::milliseconds duration) {
    constexpr auto kSleepSlice = std::chrono::milliseconds(16);
    auto remaining = duration;
    while (remaining.count() > 0) {
        if (stopRequested.load(std::memory_order_acquire)) {
            return false;
        }
        const auto slice = std::min(remaining, kSleepSlice);
        std::this_thread::sleep_for(slice);
        remaining -= slice;
    }
    return !stopRequested.load(std::memory_order_acquire);
}

// Backend initialization

void EnsureBackendInitialized() {
    std::call_once(g_backendInitOnce, []() {
        g_backend = DetectBackend();
        DebugWindowCaptureLog("detected backend=%d", static_cast<int>(g_backend));
        if (g_backend == WindowCaptureBackend::X11) {
            std::call_once(g_x11ThreadsOnce, []() {
                DebugWindowCaptureLog("initializing X11 threading");
                XInitThreads();
            });
        } else if (g_backend == WindowCaptureBackend::Wayland) {
            std::call_once(g_pipeWireInitOnce, []() {
                DebugWindowCaptureLog("initializing PipeWire");
                pw_init(nullptr, nullptr);
            });
        }
    });
}

// Record lifecycle

void StartOrUpdateRecordLocked(const std::string& key, const WindowCaptureRequest& request) {
    auto it = g_records.find(key);
    if (it == g_records.end()) {
        const std::string safeKey = SanitizeDebugValue(key);
        DebugWindowCaptureLog("start record key=%s fps=%d appId='%s' title='%s' token=%s",
                              safeKey.c_str(),
                              request.fps,
                              request.appId.c_str(),
                              request.windowTitle.c_str(),
                              request.selectionToken.empty() ? "no" : "yes");
        auto record = std::make_unique<X11SourceRecord>();
        int rememberedWidth = 0;
        int rememberedHeight = 0;
        record->request = request;
        if (g_backend == WindowCaptureBackend::Wayland) {
            ApplyRememberedWaylandSelectionLocked(key, record->request, &rememberedWidth, &rememberedHeight);
        }
        record->status.backend = g_backend;
        record->status.access = (g_backend == WindowCaptureBackend::X11 || g_backend == WindowCaptureBackend::Wayland)
            ? WindowCaptureAccessState::Granted
            : WindowCaptureAccessState::Unsupported;
        if (g_backend == WindowCaptureBackend::Wayland) {
            const auto manualRepickIt = g_waylandManualRepickMessages.find(key);
            const bool manualRepickRequired = manualRepickIt != g_waylandManualRepickMessages.end();
            if (!HasWaylandPortalScreenCastSupport()) {
                record->status.state = WindowCaptureState::Unsupported;
                record->status.access = WindowCaptureAccessState::Unsupported;
                record->status.message = GetWaylandPortalCapabilityMessage();
            } else if (manualRepickRequired) {
                record->status.state = WindowCaptureState::NotFound;
                record->status.message = manualRepickIt->second.empty()
                    ? std::string(kWaylandManualRepickMessage)
                    : manualRepickIt->second;
            } else {
                record->status.state = request.selectionToken.empty()
                    ? WindowCaptureState::SelectionRequired
                    : WindowCaptureState::Starting;
                record->status.message = request.selectionToken.empty()
                    ? "Pick a Wayland window via the portal before capture can start."
                    : "Starting Wayland portal capture...";
            }
            record->status.canPersistSelection = true;
            record->status.selectionPersistent = !record->request.selectionToken.empty();
            record->status.width = rememberedWidth;
            record->status.height = rememberedHeight;
        } else {
            record->status.state = WindowCaptureState::Starting;
            record->status.message = "Starting window capture...";
        }
        X11SourceRecord* rawRecord = record.get();
        g_records.emplace(key, std::move(record));
        if (g_backend == WindowCaptureBackend::X11) {
            rawRecord->worker = std::thread(RunX11CaptureThread, key, rawRecord);
        } else if (g_backend == WindowCaptureBackend::Wayland &&
                   HasWaylandPortalScreenCastSupport() &&
                   g_waylandManualRepickMessages.find(key) == g_waylandManualRepickMessages.end()) {
            rawRecord->worker = std::thread(RunWaylandCaptureThread, key, rawRecord);
        }
        return;
    }

    it->second->request = request;
    if (g_backend == WindowCaptureBackend::Wayland) {
        ApplyRememberedWaylandSelectionLocked(key, it->second->request);
    }
}

void StopRecordWorker(X11SourceRecord& record) {
    record.stopRequested.store(true, std::memory_order_release);
    if (record.worker.joinable()) {
        if (record.worker.get_id() == std::this_thread::get_id()) {
            record.worker.detach();
        } else {
            record.worker.join();
        }
    }
}

void StopRecordWorkersAsync(std::vector<std::unique_ptr<X11SourceRecord>> records) {
    if (records.empty()) {
        return;
    }

    std::thread([records = std::move(records)]() mutable {
        for (auto& record : records) {
            if (record) {
                StopRecordWorker(*record);
            }
        }
    }).detach();
}

// Public API

WindowCaptureBackend GetWindowCaptureBackend() {
    EnsureBackendInitialized();
    return g_backend;
}

WindowCaptureAccessState GetWindowCaptureAccessState() {
    EnsureBackendInitialized();
    if (g_backend == WindowCaptureBackend::Wayland && !HasWaylandPortalScreenCastSupport()) {
        return WindowCaptureAccessState::Unsupported;
    }
    if (g_backend == WindowCaptureBackend::X11 || g_backend == WindowCaptureBackend::Wayland) {
        return WindowCaptureAccessState::Granted;
    }
    return WindowCaptureAccessState::Unsupported;
}

void RefreshAvailableWindows() {
    EnsureBackendInitialized();
    if (g_backend == WindowCaptureBackend::Wayland) {
        return;
    }
    if (g_backend != WindowCaptureBackend::X11) {
        return;
    }

    Display* display = XOpenDisplay(nullptr);
    if (!display) {
        return;
    }
    std::vector<AvailableWindow> windows = EnumerateX11Windows(display);
    XCloseDisplay(display);

    std::lock_guard<std::mutex> lock(g_mutex);
    g_availableWindows = std::move(windows);
}

std::vector<AvailableWindow> GetAvailableWindowsSnapshot() {
    EnsureBackendInitialized();
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_availableWindows;
}

void SetWindowCaptureRuntimeReady(bool ready) {
    bool expected = !ready;
    if (!g_runtimeReady.compare_exchange_strong(expected, ready, std::memory_order_acq_rel)) {
        return;
    }
    std::vector<std::unique_ptr<X11SourceRecord>> stoppedRecords;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!ready) {
            for (auto& entry : g_records) {
                entry.second->stopRequested.store(true, std::memory_order_release);
                stoppedRecords.push_back(std::move(entry.second));
            }
            g_records.clear();
        } else {
            for (const auto& request : g_desiredRequests) {
                StartOrUpdateRecordLocked(MakeWindowCaptureKey(request), request);
            }
        }
    }

    StopRecordWorkersAsync(std::move(stoppedRecords));
}

bool IsWindowCaptureRuntimeReady() {
    return g_runtimeReady.load(std::memory_order_acquire);
}

void SetWindowCaptureRequests(const std::vector<WindowCaptureRequest>& requests) {
    EnsureBackendInitialized();
    const std::vector<WindowCaptureRequest> normalized = NormalizeWindowCaptureRequests(requests);
    std::vector<std::unique_ptr<X11SourceRecord>> stoppedRecords;

    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_desiredRequests = normalized;

        for (auto it = g_records.begin(); it != g_records.end();) {
            const bool wanted = std::any_of(g_desiredRequests.begin(), g_desiredRequests.end(), [&](const auto& request) {
                return MakeWindowCaptureKey(request) == it->first;
            });
            if (!g_runtimeReady || !wanted) {
                it->second->stopRequested.store(true, std::memory_order_release);
                stoppedRecords.push_back(std::move(it->second));
                it = g_records.erase(it);
            } else {
                ++it;
            }
        }
    }

    StopRecordWorkersAsync(std::move(stoppedRecords));

    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_runtimeReady) {
        return;
    }
    for (const auto& request : g_desiredRequests) {
        StartOrUpdateRecordLocked(MakeWindowCaptureKey(request), request);
    }
}

void InvalidateWindowCaptureTransientState() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_availableWindows.clear();
}

void ForgetWindowCaptureSource(const platform::config::MirrorSourceConfig& source) {
    EnsureBackendInitialized();
    if (!HasConfiguredWindowCaptureSource(source)) {
        return;
    }

    const std::string key = MakeWindowCaptureKey(source);
    std::vector<std::unique_ptr<X11SourceRecord>> stoppedRecords;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_waylandManualRepickMessages.erase(key);
        g_waylandRememberedSelections.erase(key);
        g_desiredRequests.erase(std::remove_if(g_desiredRequests.begin(),
                                               g_desiredRequests.end(),
                                               [&](const auto& request) {
                                                   return MakeWindowCaptureKey(request) == key;
                                               }),
                                g_desiredRequests.end());
        g_availableWindows.erase(std::remove_if(g_availableWindows.begin(),
                                                g_availableWindows.end(),
                                                [&](const auto& window) {
                                                    if (!source.selectionToken.empty()) {
                                                        return window.selectionToken == source.selectionToken;
                                                    }
                                                    return window.appId == source.appId &&
                                                           window.windowTitle == source.windowTitle;
                                                }),
                                 g_availableWindows.end());

        auto it = g_records.find(key);
        if (it != g_records.end()) {
            it->second->stopRequested.store(true, std::memory_order_release);
            stoppedRecords.push_back(std::move(it->second));
            g_records.erase(it);
        }
    }

    StopRecordWorkersAsync(std::move(stoppedRecords));
}

WindowCaptureStatus GetWindowCaptureStatus(const platform::config::MirrorSourceConfig& source) {
    EnsureBackendInitialized();
    WindowCaptureStatus status;
    status.backend = g_backend;
    status.access = GetWindowCaptureAccessState();
    status.canPersistSelection = (g_backend == WindowCaptureBackend::Wayland);
    status.selectionPersistent = !source.selectionToken.empty();

    if (g_backend == WindowCaptureBackend::Wayland && !HasWaylandPortalScreenCastSupport()) {
        status.state = WindowCaptureState::Unsupported;
        status.message = GetWaylandPortalCapabilityMessage();
        return status;
    }

    if (!IsWindowCaptureSource(source)) {
        status.state = WindowCaptureState::Idle;
        status.message = "Starts when the active mode uses this source.";
        return status;
    }

    if (!HasConfiguredWindowCaptureSource(source)) {
        status.state = WindowCaptureState::Idle;
        status.message = (g_backend == WindowCaptureBackend::Wayland)
            ? "Pick a Wayland window via the portal before capture can start."
            : "Pick an X11 window to capture.";
        return status;
    }

    const std::string key = MakeWindowCaptureKey(source);
    std::lock_guard<std::mutex> lock(g_mutex);
    auto it = g_records.find(key);
    if (it == g_records.end()) {
        if (g_backend == WindowCaptureBackend::Wayland) {
            const auto manualRepickIt = g_waylandManualRepickMessages.find(key);
            if (manualRepickIt != g_waylandManualRepickMessages.end()) {
                status.state = WindowCaptureState::NotFound;
                status.message = manualRepickIt->second.empty()
                    ? std::string(kWaylandManualRepickMessage)
                    : manualRepickIt->second;
                status.width = source.lastKnownWidth;
                status.height = source.lastKnownHeight;
                return status;
            }
            const auto rememberedIt = g_waylandRememberedSelections.find(key);
            if (rememberedIt != g_waylandRememberedSelections.end()) {
                status.width = rememberedIt->second.width;
                status.height = rememberedIt->second.height;
            }
        }
        status.state = WindowCaptureState::Idle;
        status.message = "Starts when the active mode uses this source.";
        return status;
    }
    return it->second->status;
}

bool CopyLatestWindowCaptureTexture(const platform::config::MirrorSourceConfig& source,
                                    WindowCaptureTextureSnapshot& outTexture) {
    outTexture = {};
    EnsureBackendInitialized();
    if (!HasConfiguredWindowCaptureSource(source) ||
        (g_backend != WindowCaptureBackend::X11 && g_backend != WindowCaptureBackend::Wayland)) {
        return false;
    }

    const std::string key = MakeWindowCaptureKey(source);
    std::lock_guard<std::mutex> lock(g_mutex);
    auto it = g_records.find(key);
    if (it == g_records.end() || it->second->textureSnapshot.textureId == 0) {
        return false;
    }

    outTexture = it->second->textureSnapshot;
    return outTexture.textureId != 0 && outTexture.width > 0 && outTexture.height > 0;
}

bool CopyLatestWindowCaptureFrame(const platform::config::MirrorSourceConfig& source,
                                  LatestFrameSnapshot& outFrame) {
    outFrame = {};
    EnsureBackendInitialized();
    if (!HasConfiguredWindowCaptureSource(source)) {
        return false;
    }
    if (g_backend != WindowCaptureBackend::X11 && g_backend != WindowCaptureBackend::Wayland) {
        return false;
    }

    const std::string key = MakeWindowCaptureKey(source);
    std::lock_guard<std::mutex> lock(g_mutex);
    auto it = g_records.find(key);
    if (it == g_records.end() || it->second->frame.pixels.empty()) {
        return false;
    }
    outFrame = it->second->frame;
    return true;
}

WindowCaptureSelectionResult RequestWindowCaptureSelection(platform::config::MirrorSourceConfig& ioSource,
                                                           int fps,
                                                           bool forceInteractiveSelection,
                                                           std::string* outMessage) {
    EnsureBackendInitialized();
    (void)fps;

    if (g_backend == WindowCaptureBackend::X11) {
        RefreshAvailableWindows();
        if (outMessage) {
            *outMessage = "Use the picker list to choose an X11 window.";
        }
        return WindowCaptureSelectionResult::Unsupported;
    }

    if (g_backend == WindowCaptureBackend::Wayland) {
        RefreshWaylandPortalCapability(true);
        if (!HasWaylandPortalScreenCastSupport()) {
            if (outMessage) {
                *outMessage = GetWaylandPortalCapabilityMessage();
            }
            return WindowCaptureSelectionResult::Unsupported;
        }

        const std::string previousKey = MakeWindowCaptureKey(ioSource);
        WindowCaptureRequest request;
        request.selectionToken = forceInteractiveSelection ? std::string() : ioSource.selectionToken;
        request.fps = fps;

        WaylandPortalSession session;
        std::string message;
        WindowCaptureSelectionResult result = WindowCaptureSelectionResult::Failed;
        if (!CreateWaylandPortalSession(request, fps, true, false, session, message, &result)) {
            if (outMessage) {
                *outMessage = message;
            }
            return result;
        }

        ioSource.selectionToken = session.restoreToken.empty() ? ioSource.selectionToken : session.restoreToken;
        if (!session.appId.empty()) {
            ioSource.appId = session.appId;
        }
        if (!session.windowTitle.empty()) {
            ioSource.windowTitle = session.windowTitle;
        }
        if (session.width > 0) {
            ioSource.lastKnownWidth = session.width;
        }
        if (session.height > 0) {
            ioSource.lastKnownHeight = session.height;
        }
        if (outMessage) {
            *outMessage = message;
        }

        std::lock_guard<std::mutex> lock(g_mutex);
        WaylandRememberedSelection remembered;
        remembered.request.appId = ioSource.appId;
        remembered.request.windowTitle = ioSource.windowTitle;
        remembered.request.selectionToken = ioSource.selectionToken;
        remembered.width = ioSource.lastKnownWidth;
        remembered.height = ioSource.lastKnownHeight;
        g_waylandManualRepickMessages.erase(previousKey);
        g_waylandRememberedSelections.erase(previousKey);
        g_waylandManualRepickMessages.erase(MakeWindowCaptureKey(ioSource));
        g_waylandRememberedSelections[MakeWindowCaptureKey(ioSource)] = remembered;
        g_availableWindows = { MakeSelectedWindowSnapshot(remembered.request,
                                                          session.nodeId,
                                                          ioSource.lastKnownWidth,
                                                          ioSource.lastKnownHeight) };
        return WindowCaptureSelectionResult::Selected;
    }

    if (outMessage) {
        *outMessage = "No Linux window capture backend is available for this session.";
    }
    return WindowCaptureSelectionResult::Unsupported;
}

void ShutdownWindowCaptureForProcessExit() {
    std::vector<std::unique_ptr<X11SourceRecord>> stoppedRecords;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        for (auto& entry : g_records) {
            entry.second->stopRequested.store(true, std::memory_order_release);
            stoppedRecords.push_back(std::move(entry.second));
        }
        g_records.clear();
        g_desiredRequests.clear();
        g_availableWindows.clear();
        g_waylandManualRepickMessages.clear();
        g_waylandRememberedSelections.clear();
    }

    for (auto& record : stoppedRecords) {
        if (record) {
            StopRecordWorker(*record);
        }
    }

    ShutdownWaylandDmaBufDisplay();
}

} // namespace platform::x11

#endif
