#include "mirror_mode_state.h"
#include "../mirror_image_source.h"
#include "../window_capture.h"
#include "../x11_runtime.h"

#include "../../common/anchor_coords.h"

#include <algorithm>
#include <cstring>
#include <unordered_map>

namespace platform::x11 {

namespace {

constexpr float kMirrorModeOutputScaleMin = 0.01f;
constexpr float kMirrorModeOutputScaleMax = 100.0f;

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

    const float relativeWidth = std::clamp(output.relativeWidth, kMirrorModeOutputScaleMin, kMirrorModeOutputScaleMax);
    const float relativeHeight = std::clamp(output.relativeHeight, kMirrorModeOutputScaleMin, kMirrorModeOutputScaleMax);
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

bool ResolveConfiguredMirrorOutputSize(const config::MirrorConfig& mirrorCfg,
                                       float& outWidth,
                                       float& outHeight) {
    outWidth = 0.0f;
    outHeight = 0.0f;
    const int dynamicBorder = config::GetMirrorDynamicBorderPadding(mirrorCfg.border);
    const float baseWidth = static_cast<float>(mirrorCfg.captureWidth + (2 * dynamicBorder));
    const float baseHeight = static_cast<float>(mirrorCfg.captureHeight + (2 * dynamicBorder));
    if (!(baseWidth > 0.0f) || !(baseHeight > 0.0f)) {
        return false;
    }

    const float rawScaleX = mirrorCfg.output.separateScale ? mirrorCfg.output.scaleX : mirrorCfg.output.scale;
    const float rawScaleY = mirrorCfg.output.separateScale ? mirrorCfg.output.scaleY : mirrorCfg.output.scale;
    const float scaleX = std::clamp(rawScaleX, kMirrorModeOutputScaleMin, kMirrorModeOutputScaleMax);
    const float scaleY = std::clamp(rawScaleY, kMirrorModeOutputScaleMin, kMirrorModeOutputScaleMax);
    if (mirrorCfg.output.preserveAspectRatio) {
        const float uniformScale = std::clamp(ResolveUniformScaleByFitMode(scaleX, scaleY, mirrorCfg.output.aspectFitMode),
                                              kMirrorModeOutputScaleMin,
                                              kMirrorModeOutputScaleMax);
        outWidth = baseWidth * uniformScale;
        outHeight = baseHeight * uniformScale;
    } else {
        outWidth = baseWidth * scaleX;
        outHeight = baseHeight * scaleY;
    }
    return outWidth > 0.0f && outHeight > 0.0f;
}

void ApplyKnownSourceSizeOverride(config::MirrorConfig& mirrorCfg, int width, int height) {
    if (width > 0) {
        mirrorCfg.captureWidth = width;
    }
    if (height > 0) {
        mirrorCfg.captureHeight = height;
    }
}

void ApplyWindowCaptureSizeOverride(config::MirrorConfig& mirrorCfg) {
    if (!HasConfiguredWindowCaptureSource(mirrorCfg.source) || !mirrorCfg.source.useWindowSize) {
        return;
    }

    const std::vector<AvailableWindow> windows = GetAvailableWindowsSnapshot();
    const int matchIndex = FindBestMatchingWindowIndex(windows,
                                                            mirrorCfg.source.appId,
                                                            mirrorCfg.source.windowTitle,
                                                            mirrorCfg.source.titleMatchMode,
                                                            mirrorCfg.source.fallbackMode,
                                                            0,
                                                            mirrorCfg.source.lastKnownWidth,
                                                            mirrorCfg.source.lastKnownHeight);
    if (matchIndex < 0) {
        return;
    }

    const AvailableWindow& window = windows[static_cast<std::size_t>(matchIndex)];
    ApplyKnownSourceSizeOverride(mirrorCfg, window.width, window.height);
}

void ApplyImageSourceSizeOverride(config::MirrorConfig& mirrorCfg) {
    if (!HasConfiguredImageSource(mirrorCfg.source) || !mirrorCfg.source.useImageSize) {
        return;
    }

    ApplyKnownSourceSizeOverride(mirrorCfg,
                                 mirrorCfg.source.lastKnownWidth,
                                 mirrorCfg.source.lastKnownHeight);
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
        SetWindowCaptureRequests({});
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
        resolved.sourceKind = ResolvedMirrorSourceKind::Mirror;
        resolved.sourceMirrorId = mirrorCfg.name;
        ApplyWindowCaptureSizeOverride(resolved.config);
        ApplyImageSourceSizeOverride(resolved.config);
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
        struct PreparedGroupItem {
            ResolvedMirrorRender resolved;
            float localLeft = 0.0f;
            float localTop = 0.0f;
            float localWidth = 0.0f;
            float localHeight = 0.0f;
        };

        std::vector<PreparedGroupItem> preparedItems;
        preparedItems.reserve(groupCfg.mirrors.size());
        bool haveLocalBounds = false;
        float localMinX = 0.0f;
        float localMinY = 0.0f;
        float localMaxX = 0.0f;
        float localMaxY = 0.0f;

        for (const auto& item : groupCfg.mirrors) {
            if (!item.enabled) continue;

            auto mirrorIt = mirrorMap.find(item.mirrorId);
            if (mirrorIt == mirrorMap.end()) continue;

            ResolvedMirrorRender resolved;
            resolved.config = *mirrorIt->second;
            resolved.sourceKind = ResolvedMirrorSourceKind::GroupItem;
            resolved.sourceMirrorId = item.mirrorId;
            resolved.sourceGroupId = groupCfg.name;
            resolved.sourceGroupItemIndex = static_cast<int>(&item - groupCfg.mirrors.data());
            ApplyWindowCaptureSizeOverride(resolved.config);
            ApplyImageSourceSizeOverride(resolved.config);
            if (hasScreenSize) {
                ResolveOutputPositionFromRelative(*modeConfig,
                                                 resolved.config.output,
                                                 screenWidth,
                                                 screenHeight);
                int itemContainerWidth = 0;
                int itemContainerHeight = 0;
                ResolveOutputContainerSize(*modeConfig,
                                           resolved.config.output,
                                           screenWidth,
                                           screenHeight,
                                           itemContainerWidth,
                                           itemContainerHeight);
                ApplyRelativeSizeToOutput(resolved.config.output,
                                          resolved.config.captureWidth,
                                          resolved.config.captureHeight,
                                          config::GetMirrorDynamicBorderPadding(resolved.config.border),
                                          itemContainerWidth,
                                          itemContainerHeight);
            }

            float nativeWidth = 0.0f;
            float nativeHeight = 0.0f;
            if (!ResolveConfiguredMirrorOutputSize(resolved.config, nativeWidth, nativeHeight)) {
                continue;
            }

            PreparedGroupItem prepared;
            prepared.resolved = std::move(resolved);
            prepared.localLeft = static_cast<float>(item.offsetX);
            prepared.localTop = static_cast<float>(item.offsetY);
            prepared.localWidth = std::max(1.0f, nativeWidth * item.widthPercent);
            prepared.localHeight = std::max(1.0f, nativeHeight * item.heightPercent);
            if (!haveLocalBounds) {
                localMinX = prepared.localLeft;
                localMinY = prepared.localTop;
                localMaxX = prepared.localLeft + prepared.localWidth;
                localMaxY = prepared.localTop + prepared.localHeight;
                haveLocalBounds = true;
            } else {
                localMinX = std::min(localMinX, prepared.localLeft);
                localMinY = std::min(localMinY, prepared.localTop);
                localMaxX = std::max(localMaxX, prepared.localLeft + prepared.localWidth);
                localMaxY = std::max(localMaxY, prepared.localTop + prepared.localHeight);
            }
            preparedItems.push_back(std::move(prepared));
        }

        if (preparedItems.empty() || !haveLocalBounds) {
            return;
        }

        const float baseGroupWidth = std::max(1.0f, localMaxX - localMinX);
        const float baseGroupHeight = std::max(1.0f, localMaxY - localMinY);

        int positionContainerWidth = screenWidth;
        int positionContainerHeight = screenHeight;
        if (hasScreenSize) {
            ResolveOutputContainerSize(*modeConfig,
                                       groupCfg.output,
                                       screenWidth,
                                       screenHeight,
                                       positionContainerWidth,
                                       positionContainerHeight);
        }

        float groupScaleX = std::clamp(groupCfg.output.separateScale ? groupCfg.output.scaleX : groupCfg.output.scale,
                                       kMirrorModeOutputScaleMin,
                                       kMirrorModeOutputScaleMax);
        float groupScaleY = std::clamp(groupCfg.output.separateScale ? groupCfg.output.scaleY : groupCfg.output.scale,
                                       kMirrorModeOutputScaleMin,
                                       kMirrorModeOutputScaleMax);

        if (groupCfg.output.useRelativeSize && hasScreenSize &&
            positionContainerWidth > 0 && positionContainerHeight > 0) {
            const float groupRelativeWidth =
                std::clamp(groupCfg.output.relativeWidth, kMirrorModeOutputScaleMin, kMirrorModeOutputScaleMax);
            const float groupRelativeHeight =
                std::clamp(groupCfg.output.relativeHeight, kMirrorModeOutputScaleMin, kMirrorModeOutputScaleMax);
            const float targetWidth = static_cast<float>(positionContainerWidth) * groupRelativeWidth;
            const float targetHeight = static_cast<float>(positionContainerHeight) * groupRelativeHeight;
            groupScaleX = std::clamp(targetWidth / baseGroupWidth, kMirrorModeOutputScaleMin, kMirrorModeOutputScaleMax);
            groupScaleY = std::clamp(targetHeight / baseGroupHeight, kMirrorModeOutputScaleMin, kMirrorModeOutputScaleMax);
        }

        if (groupCfg.output.preserveAspectRatio) {
            const float uniformScale = std::clamp(ResolveUniformScaleByFitMode(groupScaleX,
                                                                               groupScaleY,
                                                                               groupCfg.output.aspectFitMode),
                                                  kMirrorModeOutputScaleMin,
                                                  kMirrorModeOutputScaleMax);
            groupScaleX = uniformScale;
            groupScaleY = uniformScale;
        }

        const float resolvedGroupWidth = baseGroupWidth * groupScaleX;
        const float resolvedGroupHeight = baseGroupHeight * groupScaleY;

        int groupAnchorX = groupCfg.output.x;
        int groupAnchorY = groupCfg.output.y;
        if (groupCfg.output.useRelativePosition && hasScreenSize &&
            positionContainerWidth > 0 && positionContainerHeight > 0) {
            groupAnchorX = static_cast<int>(groupCfg.output.relativeX * static_cast<float>(positionContainerWidth));
            groupAnchorY = static_cast<int>(groupCfg.output.relativeY * static_cast<float>(positionContainerHeight));
        }

        int groupTopLeftX = groupAnchorX;
        int groupTopLeftY = groupAnchorY;
        if (hasScreenSize && positionContainerWidth > 0 && positionContainerHeight > 0) {
            config::GetRelativeCoords(groupCfg.output.relativeTo,
                                      groupAnchorX,
                                      groupAnchorY,
                                      std::max(1, static_cast<int>(std::round(resolvedGroupWidth))),
                                      std::max(1, static_cast<int>(std::round(resolvedGroupHeight))),
                                      positionContainerWidth,
                                      positionContainerHeight,
                                      groupTopLeftX,
                                      groupTopLeftY);
        }

        const std::string childRelativeTo = ShouldUseViewportAnchor(groupCfg.output.relativeTo)
            ? "topLeftViewport"
            : "topLeftScreen";

        for (auto& prepared : preparedItems) {
            const float childLeft = static_cast<float>(groupTopLeftX) + ((prepared.localLeft - localMinX) * groupScaleX);
            const float childTop = static_cast<float>(groupTopLeftY) + ((prepared.localTop - localMinY) * groupScaleY);
            const int targetWidth = std::max(1, static_cast<int>(std::round(prepared.localWidth * groupScaleX)));
            const int targetHeight = std::max(1, static_cast<int>(std::round(prepared.localHeight * groupScaleY)));

            prepared.resolved.config.output.relativeTo = childRelativeTo;
            prepared.resolved.config.output.useRelativePosition = false;
            prepared.resolved.config.output.relativeX = 0.0f;
            prepared.resolved.config.output.relativeY = 0.0f;
            prepared.resolved.config.output.x = static_cast<int>(std::round(childLeft));
            prepared.resolved.config.output.y = static_cast<int>(std::round(childTop));
            prepared.resolved.config.output.useRelativeSize = false;
            prepared.resolved.config.output.preserveAspectRatio = false;
            ApplyAbsoluteTargetSizeToOutput(prepared.resolved.config.output,
                                            prepared.resolved.config.captureWidth,
                                            prepared.resolved.config.captureHeight,
                                            config::GetMirrorDynamicBorderPadding(prepared.resolved.config.border),
                                            targetWidth,
                                            targetHeight);
            activeMirrors_.push_back(std::move(prepared.resolved));
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

    std::vector<WindowCaptureRequest> windowCaptureRequests;
    windowCaptureRequests.reserve(activeMirrors_.size());
    for (const auto& mirror : activeMirrors_) {
        if (HasConfiguredWindowCaptureSource(mirror.config.source)) {
            windowCaptureRequests.push_back(
                WindowCaptureRequest{ mirror.config.source.appId,
                                      mirror.config.source.windowTitle,
                                      mirror.config.source.titleMatchMode,
                                      mirror.config.source.fallbackMode,
                                      mirror.config.source.selectionToken,
                                      mirror.config.fps,
                                      mirror.config.source.lastKnownWidth,
                                      mirror.config.source.lastKnownHeight });
        }
    }
    SetWindowCaptureRequests(windowCaptureRequests);
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
    SetWindowCaptureRequests({});
}

} // namespace platform::x11
