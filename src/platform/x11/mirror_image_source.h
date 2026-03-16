#pragma once

#include "../common/linuxscreen_config.h"

#include <cstdint>
#include <string>

namespace platform::x11 {

struct MirrorImageSourceStatus {
    bool configured = false;
    bool loading = false;
    bool decodeFailed = false;
    bool isAnimated = false;
    int width = 0;
    int height = 0;
    std::string resolvedPath;
    std::string message;
};

inline bool IsImageSource(const platform::config::MirrorSourceConfig& source) {
    return source.type == platform::config::MirrorSourceType::Image;
}

inline bool HasConfiguredImageSource(const platform::config::MirrorSourceConfig& source) {
    return IsImageSource(source) && !source.image.empty();
}

MirrorImageSourceStatus GetMirrorImageSourceStatus(const std::string& mirrorName,
                                                   const platform::config::MirrorSourceConfig& source);

void ForgetMirrorImageSource(const std::string& mirrorName);

bool EnsureMirrorImageSourceTexture(const std::string& mirrorName,
                                    const platform::config::MirrorSourceConfig& source,
                                    std::uint32_t& outTexture,
                                    int& outWidth,
                                    int& outHeight,
                                    bool& outYInverted);

void ClearAllMirrorImageSourceTextures();

void StopMirrorImageDecodeWorker();

void ShutdownMirrorImageSources();

} // namespace platform::x11
