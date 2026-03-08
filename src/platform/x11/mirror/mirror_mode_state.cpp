#include "mirror_mode_state.h"
#include "../x11_runtime.h"

#include "../../common/anchor_coords.h"

#include <algorithm>
#include <cstring>
#include <unordered_map>

namespace platform::x11 {

namespace {

bool EndsWithSuffix(const std::string& value, const char* suffix) {
    if (!suffix) {
        return false;
    }

    const std::size_t suffixLen = std::strlen(suffix);
    if (value.size() < suffixLen) {
        return false;
    }

    return value.compare(value.size() - suffixLen, suffixLen, suffix) == 0;
}

bool IsPieAnchor(const std::string& relativeTo) {
    std::string anchor = relativeTo;
    if (EndsWithSuffix(anchor, "Viewport")) {
        anchor = anchor.substr(0, anchor.size() - 8);
    } else if (EndsWithSuffix(anchor, "Screen")) {
        anchor = anchor.substr(0, anchor.size() - 6);
    }
    return anchor == "pieLeft" || anchor == "pieRight";
}

bool ShouldUseViewportAnchor(const std::string& relativeTo) {
    return EndsWithSuffix(relativeTo, "Viewport") || IsPieAnchor(relativeTo);
}

struct ModeViewportRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    bool valid = false;
};

bool ResolveModeViewportRect(const config::ModeConfig& activeMode, int screenWidth, int screenHeight, ModeViewportRect& outRect) {
    outRect = ModeViewportRect{};
    if (screenWidth <= 0 || screenHeight <= 0) {
        return false;
    }

    int modeWidth = 0;
    int modeHeight = 0;
    MirrorModeState::CalculateModeDimensions(activeMode, screenWidth, screenHeight, modeWidth, modeHeight);
    if (modeWidth <= 0 || modeHeight <= 0) {
        return false;
    }

    int topLeftX = 0;
    int topLeftY = 0;
    std::string anchorPreset = activeMode.positionPreset.empty() ? "topLeftScreen" : activeMode.positionPreset;
    if (anchorPreset == "custom") {
        anchorPreset = "topLeftScreen";
    }
    platform::config::GetRelativeCoords(anchorPreset,
                                        activeMode.x,
                                        activeMode.y,
                                        modeWidth,
                                        modeHeight,
                                        screenWidth,
                                        screenHeight,
                                        topLeftX,
                                        topLeftY);

    outRect.x = topLeftX;
    outRect.y = topLeftY;
    outRect.width = modeWidth;
    outRect.height = modeHeight;
    outRect.valid = true;
    return true;
}

void ResolveOutputContainerSize(const config::ModeConfig& activeMode,
                                const config::MirrorRenderConfig& output,
                                int screenWidth,
                                int screenHeight,
                                int& outContainerWidth,
                                int& outContainerHeight) {
    outContainerWidth = screenWidth;
    outContainerHeight = screenHeight;
    if (screenWidth <= 0 || screenHeight <= 0) {
        outContainerWidth = 0;
        outContainerHeight = 0;
        return;
    }

    if (ShouldUseViewportAnchor(output.relativeTo)) {
        ModeViewportRect modeViewportRect;
        if (ResolveModeViewportRect(activeMode, screenWidth, screenHeight, modeViewportRect) && modeViewportRect.valid) {
            outContainerWidth = modeViewportRect.width;
            outContainerHeight = modeViewportRect.height;
        }
    }
}

void ResolveOutputPositionFromRelative(const config::ModeConfig& activeMode,
                                       config::MirrorRenderConfig& output,
                                       int screenWidth,
                                       int screenHeight) {
    if (!output.useRelativePosition) {
        return;
    }
    if (screenWidth <= 0 || screenHeight <= 0) {
        return;
    }

    int containerWidth = screenWidth;
    int containerHeight = screenHeight;
    ResolveOutputContainerSize(activeMode,
                               output,
                               screenWidth,
                               screenHeight,
                               containerWidth,
                               containerHeight);
    if (containerWidth <= 0 || containerHeight <= 0) {
        return;
    }

    output.x = static_cast<int>(output.relativeX * static_cast<float>(containerWidth));
    output.y = static_cast<int>(output.relativeY * static_cast<float>(containerHeight));
}

float ResolveUniformScaleByFitMode(float scaleX, float scaleY, const std::string& fitMode) {
    if (fitMode == "fitWidth") {
        return scaleX;
    }
    if (fitMode == "fitHeight") {
        return scaleY;
    }
    return std::min(scaleX, scaleY);
}

void ApplyAbsoluteTargetSizeToOutput(config::MirrorRenderConfig& output,
                                     int captureWidth,
                                     int captureHeight,
                                     int dynamicBorder,
                                     int targetWidth,
                                     int targetHeight) {
    const int baseWidth = captureWidth + 2 * dynamicBorder;
    const int baseHeight = captureHeight + 2 * dynamicBorder;
    if (baseWidth <= 0 || baseHeight <= 0 || targetWidth <= 0 || targetHeight <= 0) {
        return;
    }

    const float scaleX = static_cast<float>(targetWidth) / static_cast<float>(baseWidth);
    const float scaleY = static_cast<float>(targetHeight) / static_cast<float>(baseHeight);
    if (!(scaleX > 0.0f) || !(scaleY > 0.0f)) {
        return;
    }

    output.separateScale = true;
    output.scaleX = scaleX;
    output.scaleY = scaleY;
    output.scale = scaleX;
}

void ApplyAbsoluteTargetSizeToOutputPreserveAspect(config::MirrorRenderConfig& output,
                                                   int captureWidth,
                                                   int captureHeight,
                                                   int dynamicBorder,
                                                   int targetWidth,
                                                   int targetHeight,
                                                   const std::string& fitMode) {
    const int baseWidth = captureWidth + 2 * dynamicBorder;
    const int baseHeight = captureHeight + 2 * dynamicBorder;
    if (baseWidth <= 0 || baseHeight <= 0 || targetWidth <= 0 || targetHeight <= 0) {
        return;
    }

    const float scaleX = static_cast<float>(targetWidth) / static_cast<float>(baseWidth);
    const float scaleY = static_cast<float>(targetHeight) / static_cast<float>(baseHeight);
    const float uniformScale = ResolveUniformScaleByFitMode(scaleX, scaleY, fitMode);
    if (!(uniformScale > 0.0f)) {
        return;
    }

    output.separateScale = false;
    output.scale = uniformScale;
    output.scaleX = uniformScale;
    output.scaleY = uniformScale;
}

void ApplyRelativeSizeToOutput(config::MirrorRenderConfig& output,
                               int captureWidth,
                               int captureHeight,
                               int dynamicBorder,
                               int containerWidth,
                               int containerHeight) {
    if (!output.useRelativeSize) {
        return;
    }
    if (containerWidth <= 0 || containerHeight <= 0) {
        return;
    }

    const float relativeWidth = std::clamp(output.relativeWidth, 0.01f, 20.0f);
    const float relativeHeight = std::clamp(output.relativeHeight, 0.01f, 20.0f);
    output.relativeWidth = relativeWidth;
    output.relativeHeight = relativeHeight;

    const int targetWidth = std::max(1, static_cast<int>(static_cast<float>(containerWidth) * relativeWidth));
    const int targetHeight = std::max(1, static_cast<int>(static_cast<float>(containerHeight) * relativeHeight));
    if (output.preserveAspectRatio) {
        ApplyAbsoluteTargetSizeToOutputPreserveAspect(output,
                                                      captureWidth,
                                                      captureHeight,
                                                      dynamicBorder,
                                                      targetWidth,
                                                      targetHeight,
                                                      output.aspectFitMode);
    } else {
        ApplyAbsoluteTargetSizeToOutput(output, captureWidth, captureHeight, dynamicBorder, targetWidth, targetHeight);
    }
}

} // namespace

void MirrorModeState::ApplyModeSwitch(const std::string& modeName,
                                      const config::LinuxscreenConfig& config,
                                      int preferredScreenWidth,
                                      int preferredScreenHeight) {
    std::lock_guard<std::mutex> lock(mutex_);

    ApplyModeSwitchLocked(modeName, config, preferredScreenWidth, preferredScreenHeight);
}

void MirrorModeState::ApplyModeSwitchLocked(const std::string& modeName,
                                            const config::LinuxscreenConfig& config,
                                            int preferredScreenWidth,
                                            int preferredScreenHeight) {
    activeModeName_ = modeName;
    activeMirrors_.clear();
    configSnapshot_ = std::make_shared<config::LinuxscreenConfig>(config);

    const config::ModeConfig* modeConfig = nullptr;
    for (const auto& mode : config.modes) {
        if (mode.name == modeName) {
            modeConfig = &mode;
            break;
        }
    }

    if (!modeConfig) {
        return;
    }

    std::unordered_map<std::string, const config::MirrorConfig*> mirrorMap;
    for (const auto& mirror : config.mirrors) {
        mirrorMap[mirror.name] = &mirror;
    }

    std::unordered_map<std::string, const config::MirrorGroupConfig*> groupMap;
    for (const auto& group : config.mirrorGroups) {
        groupMap[group.name] = &group;
    }

    int screenWidth = preferredScreenWidth;
    int screenHeight = preferredScreenHeight;
    bool hasScreenSize = preferredScreenWidth > 0 && preferredScreenHeight > 0;
    if (!hasScreenSize) {
        int windowWidth = 0;
        int windowHeight = 0;
        int framebufferWidth = 0;
        int framebufferHeight = 0;
        if (GetGlfwWindowMetrics(windowWidth, windowHeight, framebufferWidth, framebufferHeight)) {
            if (framebufferWidth > 0 && framebufferHeight > 0) {
                screenWidth = framebufferWidth;
                screenHeight = framebufferHeight;
                hasScreenSize = true;
            } else if (windowWidth > 0 && windowHeight > 0) {
                screenWidth = windowWidth;
                screenHeight = windowHeight;
                hasScreenSize = true;
            }
        }
    }
    if (!hasScreenSize) {
        hasScreenSize = GetGameWindowSize(screenWidth, screenHeight) &&
                        screenWidth > 0 &&
                        screenHeight > 0;
    }

    auto appendResolvedMirror = [&](const config::MirrorConfig& mirrorCfg) {
        ResolvedMirrorRender resolved;
        resolved.config = mirrorCfg;
        if (hasScreenSize) {
            ResolveOutputPositionFromRelative(*modeConfig,
                                             resolved.config.output,
                                             screenWidth,
                                             screenHeight);
            int containerWidth = 0;
            int containerHeight = 0;
            ResolveOutputContainerSize(*modeConfig,
                                       resolved.config.output,
                                       screenWidth,
                                       screenHeight,
                                       containerWidth,
                                       containerHeight);
            ApplyRelativeSizeToOutput(resolved.config.output,
                                      resolved.config.captureWidth,
                                      resolved.config.captureHeight,
                                      config::GetMirrorDynamicBorderPadding(resolved.config.border),
                                      containerWidth,
                                      containerHeight);
        }
        activeMirrors_.push_back(std::move(resolved));
    };

    auto appendResolvedGroup = [&](const config::MirrorGroupConfig& groupCfg) {
        for (const auto& item : groupCfg.mirrors) {
            if (!item.enabled) continue;

            auto mirrorIt = mirrorMap.find(item.mirrorId);
            if (mirrorIt == mirrorMap.end()) continue;

            ResolvedMirrorRender resolved;
            resolved.config = *mirrorIt->second;
            int groupX = groupCfg.output.x;
            int groupY = groupCfg.output.y;
            if (groupCfg.output.useRelativePosition && hasScreenSize) {
                int positionContainerWidth = screenWidth;
                int positionContainerHeight = screenHeight;
                ResolveOutputContainerSize(*modeConfig,
                                           groupCfg.output,
                                           screenWidth,
                                           screenHeight,
                                           positionContainerWidth,
                                           positionContainerHeight);
                if (positionContainerWidth > 0 && positionContainerHeight > 0) {
                    groupX = static_cast<int>(groupCfg.output.relativeX * static_cast<float>(positionContainerWidth));
                    groupY = static_cast<int>(groupCfg.output.relativeY * static_cast<float>(positionContainerHeight));
                }
            }

            resolved.config.output.x = groupX + item.offsetX;
            resolved.config.output.y = groupY + item.offsetY;
            resolved.config.output.relativeTo = groupCfg.output.relativeTo;
            resolved.config.output.useRelativePosition = groupCfg.output.useRelativePosition;
            resolved.config.output.relativeX = groupCfg.output.relativeX;
            resolved.config.output.relativeY = groupCfg.output.relativeY;
            resolved.config.output.useRelativeSize = groupCfg.output.useRelativeSize;
            resolved.config.output.relativeWidth = groupCfg.output.relativeWidth;
            resolved.config.output.relativeHeight = groupCfg.output.relativeHeight;
            resolved.config.output.preserveAspectRatio = groupCfg.output.preserveAspectRatio;
            resolved.config.output.aspectFitMode = groupCfg.output.aspectFitMode;

            bool appliedGroupRelativeSize = false;
            if (groupCfg.output.useRelativeSize && hasScreenSize) {
                int containerWidth = 0;
                int containerHeight = 0;
                ResolveOutputContainerSize(*modeConfig,
                                           groupCfg.output,
                                           screenWidth,
                                           screenHeight,
                                           containerWidth,
                                           containerHeight);

                if (containerWidth > 0 && containerHeight > 0) {
                    const float groupRelativeWidth = std::clamp(groupCfg.output.relativeWidth, 0.01f, 20.0f);
                    const float groupRelativeHeight = std::clamp(groupCfg.output.relativeHeight, 0.01f, 20.0f);
                    int targetWidth = std::max(1, static_cast<int>(static_cast<float>(containerWidth) * groupRelativeWidth));
                    int targetHeight = std::max(1, static_cast<int>(static_cast<float>(containerHeight) * groupRelativeHeight));
                    targetWidth = std::max(1, static_cast<int>(static_cast<float>(targetWidth) * item.widthPercent));
                    targetHeight = std::max(1, static_cast<int>(static_cast<float>(targetHeight) * item.heightPercent));

                    const bool preserveAspect = groupCfg.output.preserveAspectRatio;
                    if (preserveAspect) {
                        ApplyAbsoluteTargetSizeToOutputPreserveAspect(resolved.config.output,
                                                                      resolved.config.captureWidth,
                                                                      resolved.config.captureHeight,
                                                                      config::GetMirrorDynamicBorderPadding(resolved.config.border),
                                                                      targetWidth,
                                                                      targetHeight,
                                                                      groupCfg.output.aspectFitMode);
                    } else {
                        ApplyAbsoluteTargetSizeToOutput(resolved.config.output,
                                                        resolved.config.captureWidth,
                                                        resolved.config.captureHeight,
                                                        config::GetMirrorDynamicBorderPadding(resolved.config.border),
                                                        targetWidth,
                                                        targetHeight);
                    }
                    appliedGroupRelativeSize = true;
                }
            }

            if (!appliedGroupRelativeSize) {
                // Apply group scale and per-item sizing as multipliers over mirror-native scale.
                const float baseScaleX = resolved.config.output.separateScale
                    ? resolved.config.output.scaleX
                    : resolved.config.output.scale;
                const float baseScaleY = resolved.config.output.separateScale
                    ? resolved.config.output.scaleY
                    : resolved.config.output.scale;

                const float scaledX = baseScaleX * groupCfg.output.scale * item.widthPercent;
                const float scaledY = baseScaleY * groupCfg.output.scale * item.heightPercent;
                const bool needsSeparateScale = resolved.config.output.separateScale ||
                                                item.widthPercent != 1.0f ||
                                                item.heightPercent != 1.0f;

                if (needsSeparateScale) {
                    resolved.config.output.separateScale = true;
                    resolved.config.output.scaleX = scaledX;
                    resolved.config.output.scaleY = scaledY;
                    resolved.config.output.scale = scaledX;
                } else {
                    resolved.config.output.scale = scaledX;
                    resolved.config.output.scaleX = scaledX;
                    resolved.config.output.scaleY = scaledX;
                }
            }

            activeMirrors_.push_back(std::move(resolved));
        }
    };

    if (!modeConfig->layers.empty()) {
        for (const auto& layer : modeConfig->layers) {
            if (!layer.enabled || layer.id.empty()) {
                continue;
            }

            if (layer.type == config::ModeLayerType::Mirror) {
                auto it = mirrorMap.find(layer.id);
                if (it != mirrorMap.end()) {
                    appendResolvedMirror(*it->second);
                }
                continue;
            }

            auto groupIt = groupMap.find(layer.id);
            if (groupIt != groupMap.end()) {
                appendResolvedGroup(*groupIt->second);
            }
        }
    } else {
        // Legacy fallback for configs that only provide mirrorIds/groupIds.
        for (const auto& mirrorId : modeConfig->mirrorIds) {
            auto it = mirrorMap.find(mirrorId);
            if (it != mirrorMap.end()) {
                appendResolvedMirror(*it->second);
            }
        }
        for (const auto& groupId : modeConfig->groupIds) {
            auto groupIt = groupMap.find(groupId);
            if (groupIt != groupMap.end()) {
                appendResolvedGroup(*groupIt->second);
            }
        }
    }
}

std::string MirrorModeState::GetActiveModeName() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return activeModeName_;
}

std::vector<ResolvedMirrorRender> MirrorModeState::GetActiveMirrorRenderList() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return activeMirrors_;
}

std::shared_ptr<const config::LinuxscreenConfig> MirrorModeState::GetConfigSnapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return configSnapshot_;
}

void MirrorModeState::CalculateModeDimensions(const config::ModeConfig& mode,
                                              int viewportW,
                                              int viewportH,
                                              int& outWidth,
                                              int& outHeight) {
    outWidth = mode.width;
    outHeight = mode.height;

    if (mode.useRelativeSize) {
        const float relW = std::clamp(mode.relativeWidth, 0.0f, 1.0f);
        const float relH = std::clamp(mode.relativeHeight, 0.0f, 1.0f);

        outWidth = (viewportW > 0 && relW > 0.0f) ? static_cast<int>(static_cast<float>(viewportW) * relW) : 0;
        outHeight = (viewportH > 0 && relH > 0.0f) ? static_cast<int>(static_cast<float>(viewportH) * relH) : 0;

        if (relW > 0.0f && outWidth < 1) {
            outWidth = 1;
        }
        if (relH > 0.0f && outHeight < 1) {
            outHeight = 1;
        }
    }

    if (outWidth < 0) {
        outWidth = 0;
    }
    if (outHeight < 0) {
        outHeight = 0;
    }
}

bool MirrorModeState::GetActiveModeTargetDimensions(int viewportW, int viewportH, int& outWidth, int& outHeight) const {
    std::lock_guard<std::mutex> lock(mutex_);
    outWidth = 0;
    outHeight = 0;
    if (activeModeName_.empty() || !configSnapshot_) {
        return false;
    }
    const config::ModeConfig* activeMode = nullptr;
    for (const auto& mode : configSnapshot_->modes) {
        if (mode.name == activeModeName_) {
            activeMode = &mode;
            break;
        }
    }
    if (!activeMode) {
        return false;
    }
    CalculateModeDimensions(*activeMode, viewportW, viewportH, outWidth, outHeight);
    return (outWidth > 0 && outHeight > 0);
}

void MirrorModeState::Reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    activeModeName_.clear();
    activeMirrors_.clear();
    configSnapshot_.reset();
}

} // namespace platform::x11
