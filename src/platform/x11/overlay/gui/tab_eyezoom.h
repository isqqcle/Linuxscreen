#pragma once
#include "../overlay_internal.h"

namespace platform::x11 {

struct EyeZoomModeViewportRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    bool valid = false;
};

bool ResolveEyeZoomModeViewportRect(const platform::config::LinuxscreenConfig& config,
                                    int containerWidth,
                                    int containerHeight,
                                    EyeZoomModeViewportRect& outRect);
void ResolveEyeZoomOutputAnchorCoords(const std::string& relativeTo,
                                      int configX,
                                      int configY,
                                      int outputW,
                                      int outputH,
                                      int containerWidth,
                                      int containerHeight,
                                      const EyeZoomModeViewportRect& modeViewport,
                                      int& outX,
                                      int& outY);
void ResolveEyeZoomOutputContainerSize(const platform::config::EyeZoomConfig& zoomConfig,
                                       int containerWidth,
                                       int containerHeight,
                                       const EyeZoomModeViewportRect& modeViewport,
                                       int& outContainerWidth,
                                       int& outContainerHeight);
int ResolveEyeZoomOutputWidth(const platform::config::EyeZoomConfig& zoomConfig,
                              int containerWidth,
                              int containerHeight,
                              const EyeZoomModeViewportRect& modeViewport,
                              int outputX);
int ResolveEyeZoomOutputHeight(const platform::config::EyeZoomConfig& zoomConfig,
                               int containerWidth,
                               int containerHeight,
                               const EyeZoomModeViewportRect& modeViewport);
float ResolveUniformScaleByFitMode(float scaleX, float scaleY, const std::string& fitMode);

void RenderEyeZoomTab(platform::config::LinuxscreenConfig& config,
                      const std::string& activeMode,
                      platform::x11::MirrorModeState& modeState);

void RenderEyeZoomTextDrawList(const platform::config::LinuxscreenConfig& config,
                               float viewportWidth,
                               float viewportHeight);

} // namespace platform::x11
