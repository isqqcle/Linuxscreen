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
    for (GLuint texture : state.frameTextures) {
        if (texture != 0) {
            glDeleteTextures(1, &texture);
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

void ClearAllModeBackgroundGpuTextures() {
    for (auto& kv : g_modeBackgroundImages) {
        ClearModeBackgroundGpuTextures(kv.second);
    }
    g_modeBackgroundImages.clear();
}

GLuint CreateRgbaTexture(int width, int height, const unsigned char* pixels) {
    if (width <= 0 || height <= 0 || !pixels) {
        return 0;
    }

    GLuint texture = 0;
    glGenTextures(1, &texture);
    if (texture == 0) {
        return 0;
    }

    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glBindTexture(GL_TEXTURE_2D, 0);
    return texture;
}

bool HasGifExtension(const std::string& path) {
    if (path.size() < 4) {
        return false;
    }
    std::string extension = path.substr(path.size() - 4);
    std::transform(extension.begin(),
                   extension.end(),
                   extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension == ".gif";
}

DecodedModeBackgroundImage DecodeModeBackgroundImage(const BackgroundDecodeRequest& request) {
    DecodedModeBackgroundImage decoded;
    decoded.modeName = request.modeName;
    decoded.resolvedPath = request.resolvedPath;

    if (request.resolvedPath.empty()) {
        return decoded;
    }

    stbi_set_flip_vertically_on_load_thread(1);

    int width = 0;
    int height = 0;
    int channels = 0;
    int frameCount = 0;
    int* delays = nullptr;
    unsigned char* data = nullptr;

    if (HasGifExtension(request.resolvedPath)) {
        std::ifstream file(request.resolvedPath, std::ios::binary | std::ios::ate);
        if (file) {
            const std::streamsize size = file.tellg();
            file.seekg(0, std::ios::beg);
            if (size > 0) {
                std::vector<unsigned char> fileData(static_cast<std::size_t>(size));
                if (file.read(reinterpret_cast<char*>(fileData.data()), size)) {
                    data = stbi_load_gif_from_memory(fileData.data(),
                                                     static_cast<int>(fileData.size()),
                                                     &delays,
                                                     &width,
                                                     &height,
                                                     &frameCount,
                                                     &channels,
                                                     4);
                }
            }
        }

        if (!data) {
            frameCount = 0;
            data = stbi_load(request.resolvedPath.c_str(), &width, &height, &channels, 4);
        }
    } else {
        data = stbi_load(request.resolvedPath.c_str(), &width, &height, &channels, 4);
    }

    if (!data || width <= 0 || height <= 0) {
        if (data) {
            stbi_image_free(data);
        }
        if (delays) {
            stbi_image_free(delays);
        }
        return decoded;
    }

    decoded.success = true;
    decoded.pixelData = data;
    decoded.width = width;
    decoded.dataHeight = height;
    decoded.frameHeight = height;
    decoded.frameCount = 1;
    decoded.isAnimated = false;

    if (frameCount > 1 && delays) {
        decoded.isAnimated = true;
        decoded.frameCount = frameCount;
        decoded.frameHeight = height;
        decoded.dataHeight = height * frameCount;
        decoded.frameDelaysMs.reserve(static_cast<std::size_t>(frameCount));
        for (int i = 0; i < frameCount; ++i) {
            const int delayMs = (delays[i] > 0) ? delays[i] : 100;
            decoded.frameDelaysMs.push_back(delayMs);
        }
    }

    if (delays) {
        stbi_image_free(delays);
    }
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

        ClearModeBackgroundGpuTextures(state);
        state.decodeFailed = false;
        state.isAnimated = decoded.isAnimated;
        state.width = decoded.width;
        state.height = decoded.isAnimated ? decoded.frameHeight : decoded.dataHeight;
        state.currentFrameIndex = 0;
        state.hasNextFrameTime = false;

        if (decoded.isAnimated && decoded.frameCount > 1 && decoded.frameHeight > 0) {
            const std::size_t frameByteSize =
                static_cast<std::size_t>(decoded.width) * static_cast<std::size_t>(decoded.frameHeight) * 4u;
            state.frameTextures.reserve(static_cast<std::size_t>(decoded.frameCount));
            state.frameDelaysMs = decoded.frameDelaysMs;
            if (state.frameDelaysMs.size() < static_cast<std::size_t>(decoded.frameCount)) {
                state.frameDelaysMs.resize(static_cast<std::size_t>(decoded.frameCount), 100);
            }
            for (int frame = 0; frame < decoded.frameCount; ++frame) {
                const unsigned char* framePixels = decoded.pixelData + (static_cast<std::size_t>(frame) * frameByteSize);
                GLuint texture = CreateRgbaTexture(decoded.width, decoded.frameHeight, framePixels);
                if (texture != 0) {
                    state.frameTextures.push_back(texture);
                }
            }
        } else {
            GLuint texture = CreateRgbaTexture(decoded.width, decoded.dataHeight, decoded.pixelData);
            if (texture != 0) {
                state.frameTextures.push_back(texture);
            }
        }

        stbi_image_free(decoded.pixelData);
        decoded.pixelData = nullptr;
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
    if (state.frameTextures.empty()) {
        return 0;
    }
    if (!state.isAnimated || state.frameTextures.size() == 1) {
        return state.frameTextures.front();
    }

    const auto now = std::chrono::steady_clock::now();
    if (!state.hasNextFrameTime) {
        const int delayMs = (state.frameDelaysMs.empty() ? 100 : std::max(1, state.frameDelaysMs[state.currentFrameIndex]));
        state.nextFrameTime = now + std::chrono::milliseconds(delayMs);
        state.hasNextFrameTime = true;
    } else if (now >= state.nextFrameTime) {
        int safety = 0;
        while (now >= state.nextFrameTime && safety < 32) {
            state.currentFrameIndex = (state.currentFrameIndex + 1) % static_cast<int>(state.frameTextures.size());
            const int delayMs = (state.frameDelaysMs.empty()
                                     ? 100
                                     : std::max(1, state.frameDelaysMs[static_cast<std::size_t>(state.currentFrameIndex)]));
            state.nextFrameTime += std::chrono::milliseconds(delayMs);
            ++safety;
        }
    }

    const std::size_t frameIndex = static_cast<std::size_t>(state.currentFrameIndex);
    if (frameIndex >= state.frameTextures.size()) {
        return state.frameTextures.front();
    }
    return state.frameTextures[frameIndex];
}

