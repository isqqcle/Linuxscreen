#ifdef __APPLE__

#include "window_capture.h"

#import <CoreGraphics/CGWindow.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#import <Foundation/Foundation.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>
#import <objc/message.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <map>
#include <memory>
#include <mutex>
#include <utility>

namespace platform::x11 {
void AcceptFrameForKeyFromObjC(const std::string& key, CMSampleBufferRef sampleBuffer);
void HandleStreamStopForKeyFromObjC(const std::string& key, NSError* error);
}

@interface LinuxscreenWindowCaptureOutput : NSObject<SCStreamOutput, SCStreamDelegate> {
@public
    std::string _key;
}
- (instancetype)initWithKey:(const std::string&)key;
@end

@implementation LinuxscreenWindowCaptureOutput

- (instancetype)initWithKey:(const std::string&)key {
    self = [super init];
    if (self) {
        _key = key;
    }
    return self;
}

- (void)stream:(SCStream *)stream didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer ofType:(SCStreamOutputType)type {
    (void)stream;
    if (type != SCStreamOutputTypeScreen) {
        return;
    }
    platform::x11::AcceptFrameForKeyFromObjC(_key, sampleBuffer);
}

- (void)stream:(SCStream *)stream didStopWithError:(NSError *)error {
    (void)stream;
    platform::x11::HandleStreamStopForKeyFromObjC(_key, error);
}

@end

namespace platform::x11 {
namespace {

template <typename Fn>
Fn ObjCMessageSend() {
    return reinterpret_cast<Fn>(objc_msgSend);
}

struct SourceRecord {
    WindowCaptureRequest request;
    WindowCaptureStatus status;
    std::vector<std::uint8_t> pixels;
    int width = 0;
    int height = 0;
    int bytesPerRow = 0;
    int contentX = 0;
    int contentY = 0;
    int contentWidth = 0;
    int contentHeight = 0;
    std::uint64_t frameNumber = 0;
    bool frameRestoredFromCache = false;
    std::uint32_t resolvedWindowId = 0;
    int configuredFps = 0;
    bool stopRequested = false;
    SCStream* stream = nil;
    id output = nil;
    dispatch_queue_t sampleQueue = nullptr;
    std::chrono::steady_clock::time_point lastRecoveryAttempt{};
};

struct RuntimeWindowBinding {
    std::string appId;
    std::uint32_t windowId = 0;
};

std::mutex g_captureMutex;
std::atomic<bool> g_shutdownComplete{false};
std::map<std::string, std::unique_ptr<SourceRecord>> g_captureRecords;
std::map<std::string, LatestFrameSnapshot> g_cachedFrames;
std::map<std::string, RuntimeWindowBinding> g_runtimeBindings;
std::vector<AvailableWindow> g_availableWindows;
std::vector<WindowCaptureRequest> g_desiredRequests;
bool g_refreshInFlight = false;
bool g_runtimeReady = false;

dispatch_queue_t ManagerQueue() {
    static dispatch_queue_t queue =
        dispatch_queue_create("linuxscreen.macos.window_capture.manager", DISPATCH_QUEUE_SERIAL);
    return queue;
}

bool IsCacheableState(WindowCaptureState state) {
    return state == WindowCaptureState::Streaming ||
           state == WindowCaptureState::Starting ||
           state == WindowCaptureState::Idle;
}

WindowCaptureStatus MakeDeferredStatus(const WindowCaptureRequest& request) {
    WindowCaptureStatus status;
    status.backend = WindowCaptureBackend::MacOS;
    status.access = WindowCaptureAccessState::Unknown;
    status.state = WindowCaptureState::Idle;
    status.canPersistSelection = false;
    if (request.appId.empty()) {
        status.message = "Pick a macOS window to capture.";
    } else if (request.titleMatchMode != platform::config::MirrorSourceTitleMatchMode::Disabled && request.windowTitle.empty()) {
        status.message = "Enter a window title pattern or enable same-app fallback.";
    } else {
        status.message = "Waiting for host app startup before arming capture.";
    }
    return status;
}

WindowCaptureAccessState QueryPermissionState() {
    return CGPreflightScreenCaptureAccess()
        ? WindowCaptureAccessState::Granted
        : WindowCaptureAccessState::Denied;
}

WindowCaptureStatus MakeDefaultStatus(const WindowCaptureRequest& request) {
    if (!g_runtimeReady) {
        return MakeDeferredStatus(request);
    }

    WindowCaptureStatus status;
    status.backend = WindowCaptureBackend::MacOS;
    status.access = QueryPermissionState();
    if (request.appId.empty()) {
        status.state = WindowCaptureState::Idle;
        status.message = "Pick a macOS window to capture.";
    } else if (request.titleMatchMode != platform::config::MirrorSourceTitleMatchMode::Disabled && request.windowTitle.empty()) {
        status.state = WindowCaptureState::Idle;
        status.message = "Enter a window title pattern or enable same-app fallback.";
    } else {
        status.state = WindowCaptureState::Idle;
        status.message = "Starts when the active mode uses this source.";
    }
    return status;
}

AvailableWindow ToAvailableWindow(SCWindow* window) {
    AvailableWindow result;
    if (!window) {
        return result;
    }

    result.windowId = static_cast<std::uint32_t>(window.windowID);
    result.windowTitle = window.title ? std::string(window.title.UTF8String) : std::string();
    result.width = std::max(0, static_cast<int>(std::lround(window.frame.size.width)));
    result.height = std::max(0, static_cast<int>(std::lround(window.frame.size.height)));
    result.onScreen = window.isOnScreen;
    if ([(id)window respondsToSelector:@selector(isActive)]) {
        result.active = ObjCMessageSend<BOOL (*)(id, SEL)>()(window, @selector(isActive));
    }

    SCRunningApplication* application = window.owningApplication;
    if (application) {
        result.appId = application.bundleIdentifier ? std::string(application.bundleIdentifier.UTF8String) : std::string();
        result.appName = application.applicationName ? std::string(application.applicationName.UTF8String) : std::string();
    }
    return result;
}

void StopRecord(SourceRecord& record) {
    record.stopRequested = true;
    if (record.stream && record.output) {
        NSError* removeError = nil;
        [record.stream removeStreamOutput:record.output type:SCStreamOutputTypeScreen error:&removeError];
        (void)removeError;
    }
    if (record.stream) {
        [record.stream stopCaptureWithCompletionHandler:nil];
        [record.stream release];
        record.stream = nil;
    }
    if (record.output) {
        [record.output release];
        record.output = nil;
    }
    if (record.sampleQueue) {
        dispatch_release(record.sampleQueue);
    }
    record.sampleQueue = nullptr;
    record.resolvedWindowId = 0;
    record.configuredFps = 0;
    record.pixels.clear();
    record.width = 0;
    record.height = 0;
    record.bytesPerRow = 0;
    record.contentX = 0;
    record.contentY = 0;
    record.contentWidth = 0;
    record.contentHeight = 0;
    record.frameNumber = 0;
    record.frameRestoredFromCache = false;
}

void CopyRecordFrameToSnapshot(const SourceRecord& record, LatestFrameSnapshot& snapshot) {
    snapshot.pixels = record.pixels;
    snapshot.width = record.width;
    snapshot.height = record.height;
    snapshot.bytesPerRow = record.bytesPerRow;
    snapshot.contentX = record.contentX;
    snapshot.contentY = record.contentY;
    snapshot.contentWidth = record.contentWidth;
    snapshot.contentHeight = record.contentHeight;
    snapshot.frameNumber = record.frameNumber;
    snapshot.fromCache = record.frameRestoredFromCache;
}

void UpdateRuntimeBindingLocked(const std::string& key, const AvailableWindow& window) {
    RuntimeWindowBinding& binding = g_runtimeBindings[key];
    binding.appId = window.appId;
    binding.windowId = window.windowId;
}

void CacheRecordFrameLocked(const std::string& key, const SourceRecord& record) {
    if (record.pixels.empty() || record.width <= 0 || record.height <= 0 || record.bytesPerRow <= 0) {
        return;
    }

    LatestFrameSnapshot snapshot;
    CopyRecordFrameToSnapshot(record, snapshot);
    g_cachedFrames[key] = std::move(snapshot);
}

bool RestoreRecordFrameFromCacheLocked(const std::string& key, SourceRecord& record) {
    auto cachedIt = g_cachedFrames.find(key);
    if (cachedIt == g_cachedFrames.end()) {
        return false;
    }

    const LatestFrameSnapshot& snapshot = cachedIt->second;
    if (snapshot.pixels.empty() || snapshot.width <= 0 || snapshot.height <= 0 || snapshot.bytesPerRow <= 0) {
        return false;
    }

    record.pixels = snapshot.pixels;
    record.width = snapshot.width;
    record.height = snapshot.height;
    record.bytesPerRow = snapshot.bytesPerRow;
    record.contentX = snapshot.contentX;
    record.contentY = snapshot.contentY;
    record.contentWidth = snapshot.contentWidth;
    record.contentHeight = snapshot.contentHeight;
    record.frameNumber = snapshot.frameNumber;
    record.frameRestoredFromCache = true;
    record.status.width = snapshot.contentWidth;
    record.status.height = snapshot.contentHeight;
    return true;
}

void RefreshShareableContentOnManagerQueue();

void ReconcileDesiredRequestsOnManagerQueue() {
    bool shouldRefresh = false;
    {
        std::lock_guard<std::mutex> lock(g_captureMutex);
        auto findDesiredRequest = [&](const std::string& key) -> const WindowCaptureRequest* {
            auto it = std::find_if(g_desiredRequests.begin(), g_desiredRequests.end(), [&](const auto& request) {
                return MakeWindowCaptureKey(request) == key;
            });
            return it != g_desiredRequests.end() ? &*it : nullptr;
        };

        for (auto it = g_captureRecords.begin(); it != g_captureRecords.end();) {
            if (!findDesiredRequest(it->first)) {
                if (IsCacheableState(it->second->status.state)) {
                    CacheRecordFrameLocked(it->first, *it->second);
                } else {
                    g_cachedFrames.erase(it->first);
                }
                StopRecord(*it->second);
                it = g_captureRecords.erase(it);
            } else {
                ++it;
            }
        }

        for (const auto& request : g_desiredRequests) {
            const std::string key = MakeWindowCaptureKey(request);
            auto it = g_captureRecords.find(key);
            if (it == g_captureRecords.end()) {
                auto record = std::make_unique<SourceRecord>();
                record->request = request;
                record->status = MakeDefaultStatus(request);
                if (IsConfiguredWindowCaptureRequest(request)) {
                    RestoreRecordFrameFromCacheLocked(key, *record);
                }
                g_captureRecords.emplace(key, std::move(record));
            } else {
                it->second->request = request;
                if (it->second->status.message.empty() || it->second->status.state == WindowCaptureState::Idle) {
                    it->second->status = MakeDefaultStatus(request);
                }
            }
        }

        shouldRefresh = g_runtimeReady && !g_captureRecords.empty();
        if (!g_runtimeReady) {
            g_availableWindows.clear();
        }
    }

    if (shouldRefresh) {
        dispatch_async(ManagerQueue(), ^{
            RefreshShareableContentOnManagerQueue();
        });
    }
}

void HandleStreamStopForKeyImpl(const std::string& key, NSError* error) {
    std::lock_guard<std::mutex> lock(g_captureMutex);
    auto it = g_captureRecords.find(key);
    if (it == g_captureRecords.end()) {
        return;
    }

    SourceRecord& record = *it->second;
    if (record.stopRequested) {
        return;
    }

    record.status.access = QueryPermissionState();
    record.status.state = WindowCaptureState::Error;
    record.status.message = error && error.localizedDescription
        ? std::string(error.localizedDescription.UTF8String)
        : std::string("Capture stream stopped.");
    record.lastRecoveryAttempt = std::chrono::steady_clock::time_point{};
    g_cachedFrames.erase(key);
}

bool ShouldAcceptSampleBuffer(CMSampleBufferRef sampleBuffer) {
    CFArrayRef attachments = CMSampleBufferGetSampleAttachmentsArray(sampleBuffer, false);
    if (!attachments || CFArrayGetCount(attachments) <= 0) {
        return true;
    }

    const auto attachment = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(attachments, 0));
    if (!attachment) {
        return true;
    }

    CFTypeRef statusValue = CFDictionaryGetValue(attachment, (__bridge const void*)SCStreamFrameInfoStatus);
    if (!statusValue || CFGetTypeID(statusValue) != CFNumberGetTypeID()) {
        return true;
    }

    NSInteger status = static_cast<NSInteger>(SCFrameStatusComplete);
    CFNumberGetValue(static_cast<CFNumberRef>(statusValue), kCFNumberNSIntegerType, &status);
    return status == static_cast<NSInteger>(SCFrameStatusComplete);
}

void ExtractFrameContentRect(CMSampleBufferRef sampleBuffer,
                             int frameWidth,
                             int frameHeight,
                             int& outX,
                             int& outY,
                             int& outWidth,
                             int& outHeight) {
    outX = 0;
    outY = 0;
    outWidth = std::max(0, frameWidth);
    outHeight = std::max(0, frameHeight);

    if (frameWidth <= 0 || frameHeight <= 0) {
        return;
    }

    CFArrayRef attachments = CMSampleBufferGetSampleAttachmentsArray(sampleBuffer, false);
    if (!attachments || CFArrayGetCount(attachments) <= 0) {
        return;
    }

    const auto attachment = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(attachments, 0));
    if (!attachment) {
        return;
    }

    double scaleFactor = 1.0;
    CFTypeRef scaleFactorValue = CFDictionaryGetValue(attachment, (__bridge const void*)SCStreamFrameInfoScaleFactor);
    if (scaleFactorValue && CFGetTypeID(scaleFactorValue) == CFNumberGetTypeID()) {
        CFNumberGetValue(static_cast<CFNumberRef>(scaleFactorValue), kCFNumberDoubleType, &scaleFactor);
    }
    if (scaleFactor <= 0.0) {
        scaleFactor = 1.0;
    }

    CFTypeRef contentRectValue = CFDictionaryGetValue(attachment, (__bridge const void*)SCStreamFrameInfoContentRect);
    if (!contentRectValue || CFGetTypeID(contentRectValue) != CFDictionaryGetTypeID()) {
        return;
    }

    CGRect contentRect = CGRectZero;
    if (!CGRectMakeWithDictionaryRepresentation(static_cast<CFDictionaryRef>(contentRectValue), &contentRect)) {
        return;
    }

    const int x = std::clamp(static_cast<int>(std::lround(contentRect.origin.x * scaleFactor)), 0, frameWidth);
    const int y = std::clamp(static_cast<int>(std::lround(contentRect.origin.y * scaleFactor)), 0, frameHeight);
    const int width = std::clamp(static_cast<int>(std::lround(contentRect.size.width * scaleFactor)), 0, frameWidth - x);
    const int height = std::clamp(static_cast<int>(std::lround(contentRect.size.height * scaleFactor)), 0, frameHeight - y);
    if (width <= 0 || height <= 0) {
        return;
    }

    outX = x;
    outY = y;
    outWidth = width;
    outHeight = height;
}

void AcceptFrameForKeyImpl(const std::string& key, CMSampleBufferRef sampleBuffer) {
    if (!sampleBuffer) {
        return;
    }
    if (!ShouldAcceptSampleBuffer(sampleBuffer)) {
        return;
    }

    CVImageBufferRef imageBuffer = CMSampleBufferGetImageBuffer(sampleBuffer);
    if (!imageBuffer) {
        return;
    }

    CVPixelBufferRef pixelBuffer = reinterpret_cast<CVPixelBufferRef>(imageBuffer);
    if (CVPixelBufferLockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly) != kCVReturnSuccess) {
        return;
    }

    const std::size_t bytesPerRow = static_cast<std::size_t>(CVPixelBufferGetBytesPerRow(pixelBuffer));
    const std::size_t width = static_cast<std::size_t>(CVPixelBufferGetWidth(pixelBuffer));
    const std::size_t height = static_cast<std::size_t>(CVPixelBufferGetHeight(pixelBuffer));
    const std::size_t copyBytes = bytesPerRow * height;
    void* baseAddress = CVPixelBufferGetBaseAddress(pixelBuffer);

    if (!baseAddress || copyBytes == 0) {
        CVPixelBufferUnlockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);
        return;
    }

    int contentX = 0;
    int contentY = 0;
    int contentWidth = static_cast<int>(width);
    int contentHeight = static_cast<int>(height);
    ExtractFrameContentRect(sampleBuffer,
                            static_cast<int>(width),
                            static_cast<int>(height),
                            contentX,
                            contentY,
                            contentWidth,
                            contentHeight);

    std::lock_guard<std::mutex> lock(g_captureMutex);
    auto it = g_captureRecords.find(key);
    if (it != g_captureRecords.end()) {
        SourceRecord& record = *it->second;
        record.pixels.resize(copyBytes);
        std::memcpy(record.pixels.data(), baseAddress, copyBytes);
        record.width = static_cast<int>(width);
        record.height = static_cast<int>(height);
        record.bytesPerRow = static_cast<int>(bytesPerRow);
        record.contentX = contentX;
        record.contentY = contentY;
        record.contentWidth = contentWidth;
        record.contentHeight = contentHeight;
        ++record.frameNumber;
        record.frameRestoredFromCache = false;
        record.status.access = QueryPermissionState();
        record.status.state = WindowCaptureState::Streaming;
        record.status.message = "Streaming";
        record.status.frameNumber = record.frameNumber;
        record.status.width = record.contentWidth;
        record.status.height = record.contentHeight;
        CacheRecordFrameLocked(key, record);
    }

    CVPixelBufferUnlockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);
}

void StartRecordForWindowOnManagerQueue(const std::string& key, SourceRecord& record, SCWindow* window) {
    StopRecord(record);

    if (!window) {
        record.status.access = QueryPermissionState();
        record.status.state = WindowCaptureState::NotFound;
        record.status.message = "Selected window is no longer capturable.";
        return;
    }

    SCContentFilter* filter = [[SCContentFilter alloc] initWithDesktopIndependentWindow:window];
    const int captureFps = std::clamp(record.request.fps, 1, 240);
    int configuredWidth = std::max(1, static_cast<int>(std::lround(window.frame.size.width)));
    int configuredHeight = std::max(1, static_cast<int>(std::lround(window.frame.size.height)));
    if ([(id)filter respondsToSelector:@selector(contentRect)] &&
        [(id)filter respondsToSelector:@selector(pointPixelScale)]) {
        const CGRect contentRect = ObjCMessageSend<CGRect (*)(id, SEL)>()(filter, @selector(contentRect));
        const CGFloat pointPixelScale = ObjCMessageSend<CGFloat (*)(id, SEL)>()(filter, @selector(pointPixelScale));
        const int scaledWidth = std::max(1, static_cast<int>(std::lround(contentRect.size.width * pointPixelScale)));
        const int scaledHeight = std::max(1, static_cast<int>(std::lround(contentRect.size.height * pointPixelScale)));
        if (scaledWidth > 1 && scaledHeight > 1) {
            configuredWidth = scaledWidth;
            configuredHeight = scaledHeight;
        }
    }

    SCStreamConfiguration* configuration = [[SCStreamConfiguration alloc] init];
    configuration.width = static_cast<std::size_t>(configuredWidth);
    configuration.height = static_cast<std::size_t>(configuredHeight);
    configuration.pixelFormat = kCVPixelFormatType_32BGRA;
    configuration.minimumFrameInterval = CMTimeMake(1, captureFps);
    configuration.scalesToFit = NO;
    configuration.showsCursor = NO;
    configuration.queueDepth = 2;

    LinuxscreenWindowCaptureOutput* output = [[LinuxscreenWindowCaptureOutput alloc] initWithKey:key];
    dispatch_queue_t sampleQueue = dispatch_queue_create(
        [[NSString stringWithFormat:@"linuxscreen.macos.window_capture.sample.%u", window.windowID] UTF8String],
        DISPATCH_QUEUE_SERIAL);

    SCStream* stream = [[SCStream alloc] initWithFilter:filter configuration:configuration delegate:output];
    [filter release];
    [configuration release];

    NSError* addOutputError = nil;
    if (![stream addStreamOutput:output type:SCStreamOutputTypeScreen sampleHandlerQueue:sampleQueue error:&addOutputError]) {
        record.status.access = QueryPermissionState();
        record.status.state = WindowCaptureState::Error;
        record.status.message = addOutputError && addOutputError.localizedDescription
            ? std::string(addOutputError.localizedDescription.UTF8String)
            : std::string("Failed to attach ScreenCaptureKit output.");
        [stream release];
        [output release];
        dispatch_release(sampleQueue);
        return;
    }

    record.stream = stream;
    record.output = output;
    record.sampleQueue = sampleQueue;
    record.configuredFps = captureFps;
    record.resolvedWindowId = static_cast<std::uint32_t>(window.windowID);
    record.stopRequested = false;
    record.status.access = QueryPermissionState();
    record.status.state = WindowCaptureState::Starting;
    record.status.message = "Starting capture...";

    [stream startCaptureWithCompletionHandler:^(NSError * _Nullable error) {
        dispatch_async(ManagerQueue(), ^{
            if (g_shutdownComplete.load(std::memory_order_acquire)) {
                return;
            }
            std::lock_guard<std::mutex> lock(g_captureMutex);
            auto it = g_captureRecords.find(key);
            if (it == g_captureRecords.end()) {
                return;
            }
            SourceRecord& innerRecord = *it->second;
            if (error) {
                innerRecord.status.access = QueryPermissionState();
                innerRecord.status.state = WindowCaptureState::Error;
                innerRecord.status.message = error.localizedDescription
                    ? std::string(error.localizedDescription.UTF8String)
                    : std::string("Failed to start ScreenCaptureKit stream.");
                StopRecord(innerRecord);
                return;
            }
            if (innerRecord.status.state != WindowCaptureState::Streaming) {
                innerRecord.status.access = QueryPermissionState();
                innerRecord.status.state = WindowCaptureState::Starting;
                innerRecord.status.message = "Waiting for first frame...";
            }
        });
    }];
}

void ReconcileRecordsWithWindowsOnManagerQueue(NSArray<SCWindow*>* windows) {
    std::vector<AvailableWindow> availableWindows;
    availableWindows.reserve(windows.count);
    for (SCWindow* window in windows) {
        AvailableWindow converted = ToAvailableWindow(window);
        if (converted.appId.empty()) {
            continue;
        }
        availableWindows.push_back(std::move(converted));
    }

    std::sort(availableWindows.begin(), availableWindows.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs.appName != rhs.appName) {
            return lhs.appName < rhs.appName;
        }
        if (lhs.windowTitle != rhs.windowTitle) {
            return lhs.windowTitle < rhs.windowTitle;
        }
        return lhs.windowId < rhs.windowId;
    });

    std::lock_guard<std::mutex> lock(g_captureMutex);
    g_availableWindows = availableWindows;
    for (auto& entry : g_captureRecords) {
        const std::string& key = entry.first;
        SourceRecord& record = *entry.second;
        record.status.access = QueryPermissionState();
        if (record.status.access != WindowCaptureAccessState::Granted) {
            record.status.state = WindowCaptureState::NoAccess;
            record.status.message = "Grant Screen Recording permission to the host app, then refresh.";
            g_cachedFrames.erase(key);
            StopRecord(record);
            continue;
        }

        std::uint32_t preferredWindowId = 0;
        auto bindingIt = g_runtimeBindings.find(key);
        if (bindingIt != g_runtimeBindings.end() && bindingIt->second.appId == record.request.appId) {
            preferredWindowId = bindingIt->second.windowId;
        }

        const int matchIndex = FindBestMatchingWindowIndex(availableWindows,
                                                                record.request.appId,
                                                                record.request.windowTitle,
                                                                record.request.titleMatchMode,
                                                                record.request.fallbackMode,
                                                                preferredWindowId,
                                                                record.request.preferredWidth,
                                                                record.request.preferredHeight);
        if (matchIndex < 0) {
            record.status.state = WindowCaptureState::NotFound;
            record.status.message = "Selected window is not currently available.";
            g_cachedFrames.erase(key);
            StopRecord(record);
            continue;
        }

        SCWindow* matchedWindow = nil;
        const AvailableWindow& match = availableWindows[static_cast<std::size_t>(matchIndex)];
        UpdateRuntimeBindingLocked(key, match);
        record.request.preferredWidth = match.width;
        record.request.preferredHeight = match.height;
        for (SCWindow* candidate in windows) {
            if (static_cast<std::uint32_t>(candidate.windowID) == match.windowId) {
                matchedWindow = candidate;
                break;
            }
        }

        if (record.stream &&
            record.resolvedWindowId == match.windowId &&
            record.configuredFps == std::clamp(record.request.fps, 1, 240)) {
            continue;
        }

        StartRecordForWindowOnManagerQueue(key, record, matchedWindow);
    }
}

void RefreshShareableContentOnManagerQueue() {
    {
        std::lock_guard<std::mutex> lock(g_captureMutex);
        if (g_refreshInFlight) {
            return;
        }
        g_refreshInFlight = true;
    }

    [SCShareableContent getShareableContentExcludingDesktopWindows:YES
                                              onScreenWindowsOnly:NO
                                                completionHandler:^(SCShareableContent * _Nullable shareableContent,
                                                                    NSError * _Nullable error) {
        dispatch_async(ManagerQueue(), ^{
            if (g_shutdownComplete.load(std::memory_order_acquire)) {
                return;
            }
            {
                std::lock_guard<std::mutex> lock(g_captureMutex);
                g_refreshInFlight = false;
            }

            if (error || !shareableContent) {
                std::lock_guard<std::mutex> lock(g_captureMutex);
                for (auto& entry : g_captureRecords) {
                    SourceRecord& record = *entry.second;
                    record.status.access = QueryPermissionState();
                    record.status.state = (record.status.access == WindowCaptureAccessState::Granted)
                        ? WindowCaptureState::Error
                        : WindowCaptureState::NoAccess;
                    record.status.message = error && error.localizedDescription
                        ? std::string(error.localizedDescription.UTF8String)
                        : std::string("Failed to enumerate capturable windows.");
                    g_cachedFrames.erase(entry.first);
                    StopRecord(record);
                }
                g_availableWindows.clear();
                return;
            }

            ReconcileRecordsWithWindowsOnManagerQueue(shareableContent.windows);
        });
    }];
}

} // namespace

void AcceptFrameForKeyFromObjC(const std::string& key, CMSampleBufferRef sampleBuffer) {
    if (g_shutdownComplete.load(std::memory_order_acquire)) {
        return;
    }
    AcceptFrameForKeyImpl(key, sampleBuffer);
}

void HandleStreamStopForKeyFromObjC(const std::string& key, NSError* error) {
    if (g_shutdownComplete.load(std::memory_order_acquire)) {
        return;
    }
    HandleStreamStopForKeyImpl(key, error);
}

WindowCaptureAccessState GetWindowCaptureAccessState() {
    return QueryPermissionState();
}

WindowCaptureBackend GetWindowCaptureBackend() {
    return WindowCaptureBackend::MacOS;
}

void RefreshAvailableWindows() {
    dispatch_async(ManagerQueue(), ^{
        RefreshShareableContentOnManagerQueue();
    });
}

std::vector<AvailableWindow> GetAvailableWindowsSnapshot() {
    std::lock_guard<std::mutex> lock(g_captureMutex);
    return g_availableWindows;
}

void SetWindowCaptureRuntimeReady(bool ready) {
    dispatch_async(ManagerQueue(), ^{
        {
            std::lock_guard<std::mutex> lock(g_captureMutex);
            if (g_runtimeReady == ready) {
                return;
            }
            g_runtimeReady = ready;
            if (!ready) {
                for (auto& entry : g_captureRecords) {
                    StopRecord(*entry.second);
                }
                g_captureRecords.clear();
                g_cachedFrames.clear();
                g_runtimeBindings.clear();
                g_availableWindows.clear();
                g_refreshInFlight = false;
                return;
            }
        }

        ReconcileDesiredRequestsOnManagerQueue();
    });
}

bool IsWindowCaptureRuntimeReady() {
    std::lock_guard<std::mutex> lock(g_captureMutex);
    return g_runtimeReady;
}

void SetWindowCaptureRequests(const std::vector<WindowCaptureRequest>& requests) {
    const std::vector<WindowCaptureRequest> normalized = NormalizeWindowCaptureRequests(requests);
    bool shouldScheduleReconcile = false;
    {
        std::lock_guard<std::mutex> lock(g_captureMutex);
        g_desiredRequests = normalized;
        shouldScheduleReconcile = g_runtimeReady;
    }
    if (!shouldScheduleReconcile) {
        return;
    }
    dispatch_async(ManagerQueue(), ^{
        ReconcileDesiredRequestsOnManagerQueue();
    });
}

void InvalidateWindowCaptureTransientState() {
    dispatch_async(ManagerQueue(), ^{
        std::lock_guard<std::mutex> lock(g_captureMutex);
        g_cachedFrames.clear();
        g_runtimeBindings.clear();
    });
}

void ForgetWindowCaptureSource(const platform::config::MirrorSourceConfig& source) {
    (void)source;
}

WindowCaptureStatus GetWindowCaptureStatus(const platform::config::MirrorSourceConfig& source) {
    std::lock_guard<std::mutex> lock(g_captureMutex);
    WindowCaptureStatus status;
    status.backend = WindowCaptureBackend::MacOS;
    status.canPersistSelection = false;
    if (source.type != platform::config::MirrorSourceType::Window) {
        status.access = g_runtimeReady ? QueryPermissionState() : WindowCaptureAccessState::Unknown;
        status.state = WindowCaptureState::Idle;
        status.message = "Starts when the active mode uses this source.";
        return status;
    }

    const WindowCaptureRequest sourceRequest{
        source.appId,
        source.windowTitle,
        source.titleMatchMode,
        source.fallbackMode,
        source.selectionToken,
        30,
        source.lastKnownWidth,
        source.lastKnownHeight,
    };
    if (!IsConfiguredWindowCaptureRequest(sourceRequest)) {
        return g_runtimeReady ? MakeDefaultStatus(sourceRequest) : MakeDeferredStatus(sourceRequest);
    }

    const std::string key = MakeWindowCaptureKey(sourceRequest);
    if (!g_runtimeReady) {
        auto desiredIt = std::find_if(g_desiredRequests.begin(), g_desiredRequests.end(), [&](const auto& request) {
            return MakeWindowCaptureKey(request) == key;
        });
        if (desiredIt != g_desiredRequests.end()) {
            return MakeDeferredStatus(*desiredIt);
        }
        status.access = WindowCaptureAccessState::Unknown;
        status.state = WindowCaptureState::Idle;
        status.message = "Starts when the active mode uses this source.";
        return status;
    }

    status.access = QueryPermissionState();
    auto it = g_captureRecords.find(key);
    if (it == g_captureRecords.end()) {
        status.state = WindowCaptureState::Idle;
        status.message = "Starts when the active mode uses this source.";
        return status;
    }
    return it->second->status;
}

bool CopyLatestWindowCaptureTexture(const platform::config::MirrorSourceConfig& source,
                                    WindowCaptureTextureSnapshot& outTexture) {
    (void)source;
    outTexture = {};
    return false;
}

bool CopyLatestWindowCaptureFrame(const platform::config::MirrorSourceConfig& source,
                                  LatestFrameSnapshot& outFrame) {
    outFrame = {};
    if (!HasConfiguredWindowCaptureSource(source)) {
        return false;
    }

    const std::string key = MakeWindowCaptureKey(source);
    bool shouldRetryRefresh = false;
    {
        std::lock_guard<std::mutex> lock(g_captureMutex);
        if (!g_runtimeReady) {
            return false;
        }
        auto it = g_captureRecords.find(key);
        if (it == g_captureRecords.end()) {
            return false;
        }

        SourceRecord& record = *it->second;
        if (record.status.state != WindowCaptureState::Streaming) {
            const auto now = std::chrono::steady_clock::now();
            if (record.lastRecoveryAttempt.time_since_epoch().count() == 0 ||
                now - record.lastRecoveryAttempt > std::chrono::seconds(2)) {
                record.lastRecoveryAttempt = now;
                shouldRetryRefresh = true;
            }
        }

        if (record.pixels.empty() || record.width <= 0 || record.height <= 0 || record.bytesPerRow <= 0) {
            if (record.status.state == WindowCaptureState::Error ||
                record.status.state == WindowCaptureState::NoAccess ||
                record.status.state == WindowCaptureState::NotFound) {
                g_cachedFrames.erase(key);
                return false;
            }

            auto cachedIt = g_cachedFrames.find(key);
            if (cachedIt == g_cachedFrames.end() ||
                cachedIt->second.pixels.empty() ||
                cachedIt->second.width <= 0 ||
                cachedIt->second.height <= 0 ||
                cachedIt->second.bytesPerRow <= 0) {
                return false;
            }

            outFrame = cachedIt->second;
            return true;
        }

        CopyRecordFrameToSnapshot(record, outFrame);
    }

    if (shouldRetryRefresh) {
        RefreshAvailableWindows();
    }
    return !outFrame.pixels.empty();
}

WindowCaptureSelectionResult RequestWindowCaptureSelection(platform::config::MirrorSourceConfig& ioSource,
                                                           int fps,
                                                           bool forceInteractiveSelection,
                                                           std::string* outMessage) {
    (void)fps;
    (void)forceInteractiveSelection;
    const bool granted = CGRequestScreenCaptureAccess();
    if (!granted) {
        if (outMessage) {
            *outMessage = "Grant Screen Recording permission to the host app, then retry.";
        }
        return WindowCaptureSelectionResult::Failed;
    }

    RefreshAvailableWindows();
    if (outMessage) {
        *outMessage = "Use the picker list to choose a macOS window.";
    }
    ioSource.selectionToken.clear();
    return WindowCaptureSelectionResult::Unsupported;
}

void ShutdownWindowCaptureForProcessExit() {
    if (g_shutdownComplete.exchange(true, std::memory_order_acq_rel)) {
        return;
    }
    dispatch_sync(ManagerQueue(), ^{
        std::lock_guard<std::mutex> lock(g_captureMutex);
        for (auto& entry : g_captureRecords) {
            StopRecord(*entry.second);
        }
        g_captureRecords.clear();
        g_availableWindows.clear();
        g_desiredRequests.clear();
        g_cachedFrames.clear();
        g_runtimeBindings.clear();
        g_refreshInFlight = false;
        g_runtimeReady = false;
    });
}

} // namespace platform::x11

#endif
