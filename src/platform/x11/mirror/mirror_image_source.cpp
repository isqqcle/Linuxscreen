#include "../mirror_image_source.h"

#include "../../common/config_io.h"

#ifdef __APPLE__
#include <OpenGL/gl.h>
#include <OpenGL/OpenGL.h>
#else
#include <GL/gl.h>
#include <GL/glx.h>
#endif

#include "stb_image.h"

#include <algorithm>
#include <atomic>
#include <chrono>

#include "mirror_image_common.cpp"
#include <condition_variable>
#include <cstdio>
#include <cctype>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace platform::x11 {

namespace {

constexpr int kMinMirrorImageReloadPollMs = 1;
constexpr int kMaxMirrorImageReloadPollMs = 10000;

struct MirrorSourceImageGpu {
    std::string resolvedPath;
    bool loading = false;
    bool decodeFailed = false;
    bool isAnimated = false;
    bool filePresent = false;
    bool hasLastWriteTime = false;
    int width = 0;
    int height = 0;
    std::uint64_t generation = 0;
    std::vector<GLuint> frameTextures;
    std::vector<int> frameDelaysMs;
    int currentFrameIndex = 0;
    bool hasNextFrameTime = false;
    std::chrono::steady_clock::time_point nextFrameTime{};
    std::filesystem::file_time_type lastWriteTime{};
    std::chrono::steady_clock::time_point nextWriteCheckTime{};
    int lastPollIntervalMs = platform::config::MirrorSourceConfig::kDefaultImageReloadPollMs;
};

struct MirrorImageDecodeRequest {
    std::string mirrorName;
    std::string resolvedPath;
    std::uint64_t generation = 0;
};

struct DecodedMirrorImage {
    std::string mirrorName;
    std::string resolvedPath;
    std::uint64_t generation = 0;
    bool success = false;
    bool isAnimated = false;
    int width = 0;
    int dataHeight = 0;
    int frameHeight = 0;
    int frameCount = 1;
    std::vector<int> frameDelaysMs;
    unsigned char* pixelData = nullptr;
};

std::unordered_map<std::string, MirrorSourceImageGpu> g_mirrorSourceImages;
std::mutex g_mirrorImageStateMutex;

std::deque<MirrorImageDecodeRequest> g_mirrorImageDecodeRequests;
std::deque<DecodedMirrorImage> g_decodedMirrorImages;
std::mutex g_mirrorImageDecodeMutex;
std::condition_variable g_mirrorImageDecodeCv;
std::thread g_mirrorImageDecodeThread;
std::atomic<bool> g_mirrorImageDecodeStop{false};
std::atomic<bool> g_mirrorImageDecodeStarted{false};
std::atomic<bool> g_mirrorImageShutdownComplete{false};

void ResetMirrorImageShutdownGuard() {
    g_mirrorImageShutdownComplete.store(false, std::memory_order_release);
}

bool HasCurrentGlContext() {
#ifdef __APPLE__
    return CGLGetCurrentContext() != nullptr;
#else
    return glXGetCurrentContext() != nullptr;
#endif
}

std::string GetMirrorImageStateKey(const std::string& mirrorName, const std::string& resolvedPath) {
    return mirrorName.empty() ? resolvedPath : mirrorName;
}

std::string ResolveMirrorImageSourcePath(const platform::config::MirrorSourceConfig& source) {
    if (!HasConfiguredImageSource(source)) {
        return {};
    }
    return platform::config::ResolvePathFromConfigDir(source.image);
}

int GetMirrorImageReloadPollMs(const platform::config::MirrorSourceConfig& source) {
    return std::clamp(source.imageReloadPollMs,
                      kMinMirrorImageReloadPollMs,
                      kMaxMirrorImageReloadPollMs);
}

void ClearMirrorImageGpuTextures(MirrorSourceImageGpu& state) {
    if (HasCurrentGlContext()) {
        for (GLuint texture : state.frameTextures) {
            if (texture != 0) {
                glDeleteTextures(1, &texture);
            }
        }
    }
    state.frameTextures.clear();
    state.frameDelaysMs.clear();
    state.isAnimated = false;
    state.width = 0;
    state.height = 0;
    state.currentFrameIndex = 0;
    state.hasNextFrameTime = false;
}

void ResetMirrorImageDecodeState(MirrorSourceImageGpu& state) {
    ClearMirrorImageGpuTextures(state);
    state.loading = false;
    state.decodeFailed = false;
}

void ResetMirrorImageFileTracking(MirrorSourceImageGpu& state) {
    state.filePresent = false;
    state.hasLastWriteTime = false;
    state.nextWriteCheckTime = std::chrono::steady_clock::time_point{};
    state.lastPollIntervalMs = platform::config::MirrorSourceConfig::kDefaultImageReloadPollMs;
}

void BeginMirrorImageReload(MirrorSourceImageGpu& state, bool preserveCurrentTexture) {
    if (!preserveCurrentTexture) {
        ResetMirrorImageDecodeState(state);
    }
    state.loading = true;
    state.decodeFailed = false;
    ++state.generation;
    if (state.generation == 0) {
        state.generation = 1;
    }
}

void ResetMirrorImagePathState(MirrorSourceImageGpu& state, const std::string& resolvedPath) {
    if (state.resolvedPath == resolvedPath) {
        return;
    }

    ResetMirrorImageDecodeState(state);
    state.resolvedPath = resolvedPath;
    state.generation = 0;
    ResetMirrorImageFileTracking(state);
}

bool RefreshMirrorImageSourcePathState(MirrorSourceImageGpu& state,
                                       const platform::config::MirrorSourceConfig& source) {
    if (state.resolvedPath.empty()) {
        return false;
    }

    const int pollIntervalMs = GetMirrorImageReloadPollMs(source);
    if (state.lastPollIntervalMs != pollIntervalMs) {
        state.lastPollIntervalMs = pollIntervalMs;
        state.nextWriteCheckTime = std::chrono::steady_clock::time_point{};
    }

    const auto now = std::chrono::steady_clock::now();
    if (state.nextWriteCheckTime != std::chrono::steady_clock::time_point{} &&
        now < state.nextWriteCheckTime) {
        return false;
    }
    state.nextWriteCheckTime = now + std::chrono::milliseconds(pollIntervalMs);

    std::error_code ec;
    const bool exists = std::filesystem::exists(state.resolvedPath, ec);
    if (ec || !exists) {
        if (state.filePresent) {
            state.filePresent = false;
            state.hasLastWriteTime = false;
            BeginMirrorImageReload(state, !state.frameTextures.empty());
            return true;
        }
        return false;
    }

    auto currentWriteTime = std::filesystem::last_write_time(state.resolvedPath, ec);
    if (ec) {
        return false;
    }

    const bool changed =
        !state.filePresent ||
        !state.hasLastWriteTime ||
        state.lastWriteTime != currentWriteTime;
    state.filePresent = true;
    state.lastWriteTime = currentWriteTime;
    state.hasLastWriteTime = true;
    if (changed) {
        BeginMirrorImageReload(state, !state.frameTextures.empty());
        return true;
    }
    return false;
}

struct MirrorImageLookup {
    std::string resolvedPath;
    MirrorSourceImageGpu* state = nullptr;
    bool shouldQueueDecode = false;
    std::uint64_t queuedGeneration = 0;
};

DecodedMirrorImage DecodeMirrorImage(const MirrorImageDecodeRequest& request) {
    DecodedMirrorImage decoded;
    decoded.mirrorName = request.mirrorName;
    decoded.resolvedPath = request.resolvedPath;
    decoded.generation = request.generation;
    DecodedImageFramesCommon commonDecoded = DecodeImageFramesCommon(request.resolvedPath);
    decoded.success = commonDecoded.success;
    decoded.isAnimated = commonDecoded.isAnimated;
    decoded.width = commonDecoded.width;
    decoded.dataHeight = commonDecoded.dataHeight;
    decoded.frameHeight = commonDecoded.frameHeight;
    decoded.frameCount = commonDecoded.frameCount;
    decoded.frameDelaysMs = std::move(commonDecoded.frameDelaysMs);
    decoded.pixelData = commonDecoded.pixelData;
    return decoded;
}

bool UploadDecodedMirrorImage(MirrorSourceImageGpu& state, DecodedMirrorImage& decoded) {
    state.loading = false;
    if (!decoded.success || !decoded.pixelData) {
        state.decodeFailed = true;
        return false;
    }

    std::vector<GLuint> newFrameTextures;
    std::vector<int> newFrameDelaysMs;
    bool newIsAnimated = decoded.isAnimated;
    const int newWidth = decoded.width;
    const int newHeight = decoded.isAnimated ? decoded.frameHeight : decoded.dataHeight;

    if (decoded.isAnimated && decoded.frameCount > 1 && decoded.frameHeight > 0) {
        const std::size_t frameByteSize =
            static_cast<std::size_t>(decoded.width) * static_cast<std::size_t>(decoded.frameHeight) * 4u;
        newFrameTextures.reserve(static_cast<std::size_t>(decoded.frameCount));
        newFrameDelaysMs = decoded.frameDelaysMs;
        if (newFrameDelaysMs.size() < static_cast<std::size_t>(decoded.frameCount)) {
            newFrameDelaysMs.resize(static_cast<std::size_t>(decoded.frameCount), 100);
        }
        for (int frame = 0; frame < decoded.frameCount; ++frame) {
            const unsigned char* framePixels = decoded.pixelData + (static_cast<std::size_t>(frame) * frameByteSize);
            GLuint texture = CreateRgbaTexture(decoded.width, decoded.frameHeight, framePixels);
            if (texture != 0) {
                newFrameTextures.push_back(texture);
            }
        }
    } else {
        GLuint texture = CreateRgbaTexture(decoded.width, decoded.dataHeight, decoded.pixelData);
        if (texture != 0) {
            newFrameTextures.push_back(texture);
        }
    }

    stbi_image_free(decoded.pixelData);
    decoded.pixelData = nullptr;
    if (newFrameTextures.empty()) {
        state.decodeFailed = true;
        return false;
    }

    ClearMirrorImageGpuTextures(state);
    state.decodeFailed = false;
    state.isAnimated = newIsAnimated;
    state.width = newWidth;
    state.height = newHeight;
    state.frameTextures = std::move(newFrameTextures);
    state.frameDelaysMs = std::move(newFrameDelaysMs);
    state.currentFrameIndex = 0;
    state.hasNextFrameTime = false;
    return true;
}

GLuint GetCurrentMirrorImageTexture(MirrorSourceImageGpu& state) {
    return GetAnimatedImageTexture(state);
}

void FreeDecodedMirrorImagePixels(DecodedMirrorImage& decoded) {
    if (!decoded.pixelData) {
        return;
    }
    stbi_image_free(decoded.pixelData);
    decoded.pixelData = nullptr;
}

MirrorImageLookup PrepareMirrorImageLookupLocked(const std::string& mirrorName,
                                                 const platform::config::MirrorSourceConfig& source) {
    MirrorImageLookup lookup;
    lookup.resolvedPath = ResolveMirrorImageSourcePath(source);
    if (lookup.resolvedPath.empty()) {
        return lookup;
    }

    auto& state = g_mirrorSourceImages[GetMirrorImageStateKey(mirrorName, lookup.resolvedPath)];
    ResetMirrorImagePathState(state, lookup.resolvedPath);
    if (RefreshMirrorImageSourcePathState(state, source)) {
        lookup.shouldQueueDecode = true;
        lookup.queuedGeneration = state.generation;
    }
    lookup.state = &state;
    return lookup;
}

void EnsureMirrorImageDecodeQueuedLocked(MirrorImageLookup& lookup) {
    if (!lookup.state || lookup.state->loading || lookup.state->decodeFailed || !lookup.state->frameTextures.empty()) {
        return;
    }

    lookup.state->loading = true;
    lookup.shouldQueueDecode = true;
    lookup.queuedGeneration = lookup.state->generation;
}

void DrainDecodedMirrorImages() {
    std::deque<DecodedMirrorImage> decodedQueue;
    {
        std::lock_guard<std::mutex> lock(g_mirrorImageDecodeMutex);
        if (g_decodedMirrorImages.empty()) {
            return;
        }
        decodedQueue.swap(g_decodedMirrorImages);
    }

    std::lock_guard<std::mutex> stateLock(g_mirrorImageStateMutex);
    for (auto& decoded : decodedQueue) {
        if (!decoded.pixelData && decoded.success) {
            decoded.success = false;
        }

        const std::string stateKey = GetMirrorImageStateKey(decoded.mirrorName, decoded.resolvedPath);
        auto it = g_mirrorSourceImages.find(stateKey);
        if (it == g_mirrorSourceImages.end()) {
            FreeDecodedMirrorImagePixels(decoded);
            continue;
        }

        auto& state = it->second;
        if (state.resolvedPath != decoded.resolvedPath) {
            FreeDecodedMirrorImagePixels(decoded);
            continue;
        }
        if (state.generation != decoded.generation) {
            FreeDecodedMirrorImagePixels(decoded);
            continue;
        }

        if (!UploadDecodedMirrorImage(state, decoded)) {
            std::fprintf(stderr,
                         "[Linuxscreen][mirror] WARNING: Failed to decode mirror image source '%s': %s\n",
                         decoded.mirrorName.empty() ? "<unnamed>" : decoded.mirrorName.c_str(),
                         decoded.resolvedPath.empty() ? "<empty path>" : decoded.resolvedPath.c_str());
        }
    }
}

void EnsureMirrorImageDecodeWorkerStarted() {
    bool expected = false;
    if (!g_mirrorImageDecodeStarted.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return;
    }

    g_mirrorImageDecodeStop.store(false, std::memory_order_release);
    g_mirrorImageDecodeThread = std::thread([]() {
        while (!g_mirrorImageDecodeStop.load(std::memory_order_acquire)) {
            MirrorImageDecodeRequest request;
            bool hasRequest = false;

            {
                std::unique_lock<std::mutex> lock(g_mirrorImageDecodeMutex);
                g_mirrorImageDecodeCv.wait(lock, []() {
                    return g_mirrorImageDecodeStop.load(std::memory_order_acquire) || !g_mirrorImageDecodeRequests.empty();
                });

                if (g_mirrorImageDecodeStop.load(std::memory_order_acquire)) {
                    break;
                }
                if (!g_mirrorImageDecodeRequests.empty()) {
                    request = std::move(g_mirrorImageDecodeRequests.front());
                    g_mirrorImageDecodeRequests.pop_front();
                    hasRequest = true;
                }
            }

            if (!hasRequest) {
                continue;
            }

            DecodedMirrorImage decoded = DecodeMirrorImage(request);
            {
                std::lock_guard<std::mutex> lock(g_mirrorImageDecodeMutex);
                g_decodedMirrorImages.push_back(std::move(decoded));
            }
        }
    });
}

void EnqueueMirrorImageDecode(const std::string& mirrorName,
                              const std::string& resolvedPath,
                              std::uint64_t generation) {
    if (resolvedPath.empty()) {
        return;
    }

    EnsureMirrorImageDecodeWorkerStarted();
    {
        std::lock_guard<std::mutex> lock(g_mirrorImageDecodeMutex);
        g_mirrorImageDecodeRequests.push_back(MirrorImageDecodeRequest{mirrorName, resolvedPath, generation});
    }
    g_mirrorImageDecodeCv.notify_one();
}

} // namespace

MirrorImageSourceStatus GetMirrorImageSourceStatus(const std::string& mirrorName,
                                                   const platform::config::MirrorSourceConfig& source) {
    ResetMirrorImageShutdownGuard();

    MirrorImageSourceStatus status;
    status.configured = HasConfiguredImageSource(source);
    if (!status.configured) {
        status.message = "No image selected.";
        return status;
    }

    DrainDecodedMirrorImages();

    const std::string resolvedPath = ResolveMirrorImageSourcePath(source);
    status.resolvedPath = resolvedPath;
    if (resolvedPath.empty()) {
        status.decodeFailed = true;
        status.message = "Invalid image path.";
        return status;
    }

    bool shouldQueueDecode = false;
    std::uint64_t queuedGeneration = 0;
    {
        std::lock_guard<std::mutex> lock(g_mirrorImageStateMutex);
        MirrorImageLookup lookup = PrepareMirrorImageLookupLocked(mirrorName, source);
        EnsureMirrorImageDecodeQueuedLocked(lookup);
        if (lookup.state) {
            shouldQueueDecode = lookup.shouldQueueDecode;
            queuedGeneration = lookup.queuedGeneration;

            status.loading = lookup.state->loading;
            status.decodeFailed = lookup.state->decodeFailed;
            status.isAnimated = lookup.state->isAnimated;
            status.width = lookup.state->width;
            status.height = lookup.state->height;
        }
    }

    if (shouldQueueDecode) {
        EnqueueMirrorImageDecode(mirrorName, resolvedPath, queuedGeneration);
        status.loading = true;
    }

    if (status.loading) {
        status.message = (status.width > 0 && status.height > 0)
            ? "Reloading image..."
            : "Loading image...";
    } else if (status.decodeFailed) {
        if (status.width > 0 && status.height > 0) {
            status.message = "Reload failed; showing previous image.";
        } else {
            std::error_code ec;
            const bool exists = std::filesystem::exists(resolvedPath, ec);
            status.message = exists ? "Failed to decode image." : "Image file not found.";
        }
    } else if (status.width > 0 && status.height > 0) {
        status.message = status.isAnimated ? "Animated image ready." : "Image ready.";
    } else {
        status.message = "Waiting for image decode.";
    }

    return status;
}

void ForgetMirrorImageSource(const std::string& mirrorName) {
    if (mirrorName.empty()) {
        return;
    }

    std::lock_guard<std::mutex> lock(g_mirrorImageStateMutex);
    auto it = g_mirrorSourceImages.find(mirrorName);
    if (it == g_mirrorSourceImages.end()) {
        return;
    }
    ClearMirrorImageGpuTextures(it->second);
    g_mirrorSourceImages.erase(it);
}

bool EnsureMirrorImageSourceTexture(const std::string& mirrorName,
                                    const platform::config::MirrorSourceConfig& source,
                                    std::uint32_t& outTexture,
                                    int& outWidth,
                                    int& outHeight,
                                    bool& outYInverted) {
    ResetMirrorImageShutdownGuard();

    outTexture = 0;
    outWidth = 0;
    outHeight = 0;
    outYInverted = false;

    if (!HasConfiguredImageSource(source)) {
        return false;
    }

    DrainDecodedMirrorImages();

    const std::string resolvedPath = ResolveMirrorImageSourcePath(source);
    if (resolvedPath.empty()) {
        return false;
    }

    bool shouldQueueDecode = false;
    std::uint64_t queuedGeneration = 0;
    {
        std::lock_guard<std::mutex> lock(g_mirrorImageStateMutex);
        MirrorImageLookup lookup = PrepareMirrorImageLookupLocked(mirrorName, source);
        EnsureMirrorImageDecodeQueuedLocked(lookup);
        if (lookup.state && !lookup.state->frameTextures.empty()) {
            outTexture = GetCurrentMirrorImageTexture(*lookup.state);
            outWidth = lookup.state->width;
            outHeight = lookup.state->height;
        }
        shouldQueueDecode = lookup.shouldQueueDecode;
        queuedGeneration = lookup.queuedGeneration;
    }

    if (shouldQueueDecode) {
        EnqueueMirrorImageDecode(mirrorName, resolvedPath, queuedGeneration);
    }

    return outTexture != 0 && outWidth > 0 && outHeight > 0;
}

void ClearAllMirrorImageSourceTextures() {
    std::lock_guard<std::mutex> lock(g_mirrorImageStateMutex);
    for (auto& kv : g_mirrorSourceImages) {
        ClearMirrorImageGpuTextures(kv.second);
    }
    g_mirrorSourceImages.clear();
}

void StopMirrorImageDecodeWorker() {
    g_mirrorImageDecodeStop.store(true, std::memory_order_release);
    g_mirrorImageDecodeCv.notify_all();

    if (g_mirrorImageDecodeThread.joinable()) {
        g_mirrorImageDecodeThread.join();
    }

    g_mirrorImageDecodeStarted.store(false, std::memory_order_release);

    std::deque<DecodedMirrorImage> leftoverDecoded;
    {
        std::lock_guard<std::mutex> lock(g_mirrorImageDecodeMutex);
        g_mirrorImageDecodeRequests.clear();
        leftoverDecoded.swap(g_decodedMirrorImages);
    }
    for (auto& decoded : leftoverDecoded) {
        if (decoded.pixelData) {
            stbi_image_free(decoded.pixelData);
            decoded.pixelData = nullptr;
        }
    }
}

void ShutdownMirrorImageSources() {
    if (g_mirrorImageShutdownComplete.exchange(true, std::memory_order_acq_rel)) {
        return;
    }

    StopMirrorImageDecodeWorker();
    ClearAllMirrorImageSourceTextures();
}

} // namespace platform::x11
