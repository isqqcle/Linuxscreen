bool IsGradientOrImageBackground(const platform::config::ModeBackgroundConfig& background) {
    return background.selectedMode == "gradient" || background.selectedMode == "image";
}

bool ShouldUsePreviousModeBackground(const platform::config::ModeConfig& previousMode,
                                     const platform::config::ModeConfig& nextMode) {
    if (nextMode.name == "Fullscreen") {
        return true;
    }
    return IsGradientOrImageBackground(previousMode.background);
}

void ClearModeBackgroundGpuTextures(ModeBackgroundImageGpu& state) {
    ClearAnimatedImageGpuTextures(state);
}

void ClearAllModeBackgroundGpuTextures() {
    for (auto& kv : g_modeBackgroundImages) {
        ClearModeBackgroundGpuTextures(kv.second);
    }
    g_modeBackgroundImages.clear();
}

DecodedModeBackgroundImage DecodeModeBackgroundImage(const BackgroundDecodeRequest& request) {
    DecodedModeBackgroundImage decoded;
    decoded.modeName = request.modeName;
    decoded.resolvedPath = request.resolvedPath;

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

void BackgroundDecodeWorkerMain() {
    while (!g_backgroundDecodeStop.load(std::memory_order_acquire)) {
        BackgroundDecodeRequest request;
        bool hasRequest = false;

        {
            std::unique_lock<std::mutex> lock(g_backgroundDecodeMutex);
            g_backgroundDecodeCv.wait(lock, []() {
                return g_backgroundDecodeStop.load(std::memory_order_acquire) || !g_backgroundDecodeRequests.empty();
            });

            if (g_backgroundDecodeStop.load(std::memory_order_acquire)) {
                break;
            }
            if (!g_backgroundDecodeRequests.empty()) {
                request = std::move(g_backgroundDecodeRequests.front());
                g_backgroundDecodeRequests.pop_front();
                hasRequest = true;
            }
        }

        if (!hasRequest) {
            continue;
        }

        DecodedModeBackgroundImage decoded = DecodeModeBackgroundImage(request);
        {
            std::lock_guard<std::mutex> lock(g_backgroundDecodeMutex);
            g_decodedModeBackgroundImages.push_back(std::move(decoded));
        }
    }
}

void EnsureBackgroundDecodeWorkerStarted() {
    bool expected = false;
    if (!g_backgroundDecodeStarted.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return;
    }

    g_backgroundDecodeStop.store(false, std::memory_order_release);
    g_backgroundDecodeThread = std::thread(BackgroundDecodeWorkerMain);
}

void StopBackgroundDecodeWorker() {
    g_backgroundDecodeStop.store(true, std::memory_order_release);
    g_backgroundDecodeCv.notify_all();

    if (g_backgroundDecodeThread.joinable()) {
        g_backgroundDecodeThread.join();
    }

    g_backgroundDecodeStarted.store(false, std::memory_order_release);

    std::deque<DecodedModeBackgroundImage> leftoverDecoded;
    {
        std::lock_guard<std::mutex> lock(g_backgroundDecodeMutex);
        g_backgroundDecodeRequests.clear();
        leftoverDecoded.swap(g_decodedModeBackgroundImages);
    }
    for (auto& decoded : leftoverDecoded) {
        if (decoded.pixelData) {
            stbi_image_free(decoded.pixelData);
            decoded.pixelData = nullptr;
        }
    }
}

void EnqueueModeBackgroundDecode(const std::string& modeName, const std::string& resolvedPath) {
    if (modeName.empty() || resolvedPath.empty()) {
        return;
    }

    EnsureBackgroundDecodeWorkerStarted();
    {
        std::lock_guard<std::mutex> lock(g_backgroundDecodeMutex);
        g_backgroundDecodeRequests.push_back(BackgroundDecodeRequest{modeName, resolvedPath});
    }
    g_backgroundDecodeCv.notify_one();
}

void DrainDecodedModeBackgroundImages() {
    std::deque<DecodedModeBackgroundImage> decodedQueue;
    {
        std::lock_guard<std::mutex> lock(g_backgroundDecodeMutex);
        if (g_decodedModeBackgroundImages.empty()) {
            return;
        }
        decodedQueue.swap(g_decodedModeBackgroundImages);
    }

    for (auto& decoded : decodedQueue) {
        if (!decoded.pixelData && decoded.success) {
            decoded.success = false;
        }

        auto it = g_modeBackgroundImages.find(decoded.modeName);
        if (it == g_modeBackgroundImages.end()) {
            if (decoded.pixelData) {
                stbi_image_free(decoded.pixelData);
            }
            continue;
        }

        auto& state = it->second;
        if (state.resolvedPath != decoded.resolvedPath) {
            if (decoded.pixelData) {
                stbi_image_free(decoded.pixelData);
            }
            continue;
        }

        state.loading = false;

        if (!decoded.success || !decoded.pixelData) {
            state.decodeFailed = true;
            fprintf(stderr,
                    "[Linuxscreen][mirror] WARNING: Failed to decode background image for mode '%s': %s\n",
                    decoded.modeName.c_str(),
                    decoded.resolvedPath.empty() ? "<empty path>" : decoded.resolvedPath.c_str());
            continue;
        }

        DecodedImageFramesCommon commonDecoded;
        commonDecoded.success = decoded.success;
        commonDecoded.isAnimated = decoded.isAnimated;
        commonDecoded.width = decoded.width;
        commonDecoded.dataHeight = decoded.dataHeight;
        commonDecoded.frameHeight = decoded.frameHeight;
        commonDecoded.frameCount = decoded.frameCount;
        commonDecoded.frameDelaysMs = std::move(decoded.frameDelaysMs);
        commonDecoded.pixelData = decoded.pixelData;
        UploadDecodedImageCommon(state, commonDecoded);
        decoded.pixelData = commonDecoded.pixelData;
    }
}

void EnsureModeBackgroundImageRequested(const std::string& modeName,
                                        const platform::config::ModeBackgroundConfig& background) {
    if (modeName.empty() || background.selectedMode != "image" || background.image.empty()) {
        return;
    }

    const std::string resolvedPath = platform::config::ResolvePathFromConfigDir(background.image);
    if (resolvedPath.empty()) {
        return;
    }

    auto& state = g_modeBackgroundImages[modeName];
    if (state.resolvedPath != resolvedPath) {
        ClearModeBackgroundGpuTextures(state);
        state.resolvedPath = resolvedPath;
        state.loading = false;
        state.decodeFailed = false;
    }

    if (state.frameTextures.empty() && !state.loading && !state.decodeFailed) {
        state.loading = true;
        EnqueueModeBackgroundDecode(modeName, resolvedPath);
    }
}

GLuint GetModeBackgroundTexture(ModeBackgroundImageGpu& state) {
    return GetAnimatedImageTexture(state);
}
