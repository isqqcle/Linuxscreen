#include "../overlay_internal.h"
#include "imgui_overlay_helpers.h"
#include "tab_mirrors_helpers.h"
#include "tab_eyezoom.h"

namespace platform::x11 {

float ResolveUniformScaleByFitMode(float scaleX, float scaleY, const std::string& fitMode) {
    if (fitMode == "fitWidth") {
        return scaleX;
    }
    if (fitMode == "fitHeight") {
        return scaleY;
    }
    return std::min(scaleX, scaleY);
}

void ResolveEyeZoomAspectBasis(const platform::config::EyeZoomConfig& zoomConfig, int& outWidth, int& outHeight) {
    outWidth = 0;
    outHeight = 0;

    if (zoomConfig.zoomAreaWidth > 0) {
        outWidth = zoomConfig.zoomAreaWidth;
    } else if (zoomConfig.stretchWidth > 0) {
        outWidth = zoomConfig.stretchWidth;
    }

    if (zoomConfig.zoomAreaHeight > 0) {
        outHeight = zoomConfig.zoomAreaHeight;
    } else if (zoomConfig.outputHeight > 0) {
        outHeight = zoomConfig.outputHeight;
    }

    if (outWidth <= 0) {
        outWidth = std::max(1, zoomConfig.cloneWidth);
    }
    if (outHeight <= 0) {
        outHeight = std::max(1, zoomConfig.cloneHeight);
    }
}

bool ResolveEyeZoomModeViewportRect(const platform::config::LinuxscreenConfig& config,
                                    int containerWidth,
                                    int containerHeight,
                                    EyeZoomModeViewportRect& outRect) {
    outRect = EyeZoomModeViewportRect{};
    if (containerWidth <= 0 || containerHeight <= 0) {
        return false;
    }

    const platform::config::ModeConfig* eyeZoomMode = nullptr;
    for (const auto& mode : config.modes) {
        if (mode.name == "EyeZoom") {
            eyeZoomMode = &mode;
            break;
        }
    }
    if (!eyeZoomMode) {
        return false;
    }

    int modeWidth = 0;
    int modeHeight = 0;
    platform::x11::MirrorModeState::CalculateModeDimensions(*eyeZoomMode,
                                                             containerWidth,
                                                             containerHeight,
                                                             modeWidth,
                                                             modeHeight);
    if (modeWidth <= 0 || modeHeight <= 0) {
        return false;
    }

    int topLeftX = 0;
    int topLeftY = 0;
    std::string anchorPreset = eyeZoomMode->positionPreset.empty() ? "topLeftScreen" : eyeZoomMode->positionPreset;
    if (anchorPreset == "custom") {
        anchorPreset = "topLeftScreen";
    }
    platform::config::GetRelativeCoords(anchorPreset,
                                        eyeZoomMode->x,
                                        eyeZoomMode->y,
                                        modeWidth,
                                        modeHeight,
                                        containerWidth,
                                        containerHeight,
                                        topLeftX,
                                        topLeftY);

    outRect.x = topLeftX;
    outRect.y = topLeftY;
    outRect.width = modeWidth;
    outRect.height = modeHeight;
    outRect.valid = true;
    return true;
}

void ResolveEyeZoomOutputAnchorCoords(const std::string& relativeTo,
                                      int configX,
                                      int configY,
                                      int outputW,
                                      int outputH,
                                      int containerWidth,
                                      int containerHeight,
                                      const EyeZoomModeViewportRect& modeViewport,
                                      int& outX,
                                      int& outY) {
    int anchorWidth = containerWidth;
    int anchorHeight = containerHeight;
    int anchorOriginX = 0;
    int anchorOriginY = 0;

    if (modeViewport.valid && ShouldUseViewportRelativeTo(relativeTo)) {
        anchorWidth = modeViewport.width;
        anchorHeight = modeViewport.height;
        anchorOriginX = modeViewport.x;
        anchorOriginY = modeViewport.y;
    }

    platform::config::GetRelativeCoords(relativeTo,
                                        configX,
                                        configY,
                                        outputW,
                                        outputH,
                                        anchorWidth,
                                        anchorHeight,
                                        outX,
                                        outY);

    outX += anchorOriginX;
    outY += anchorOriginY;
}

void ResolveEyeZoomOutputContainerSize(const platform::config::EyeZoomConfig& zoomConfig,
                                       int containerWidth,
                                       int containerHeight,
                                       const EyeZoomModeViewportRect& modeViewport,
                                       int& outContainerWidth,
                                       int& outContainerHeight) {
    outContainerWidth = containerWidth;
    outContainerHeight = containerHeight;
    if (containerWidth <= 0 || containerHeight <= 0) {
        outContainerWidth = 0;
        outContainerHeight = 0;
        return;
    }

    const std::string relativeTo = zoomConfig.outputRelativeTo.empty() ? "middleLeftScreen" : zoomConfig.outputRelativeTo;
    if (modeViewport.valid && ShouldUseViewportRelativeTo(relativeTo)) {
        outContainerWidth = modeViewport.width;
        outContainerHeight = modeViewport.height;
    }
}

int ResolveEyeZoomOutputWidth(const platform::config::EyeZoomConfig& zoomConfig,
                              int containerWidth,
                              int containerHeight,
                              const EyeZoomModeViewportRect& modeViewport,
                              int outputX) {
    if (containerWidth < 1 || containerHeight < 1) {
        return 0;
    }

    if (zoomConfig.outputUseRelativeSize) {
        int outputContainerWidth = 0;
        int outputContainerHeight = 0;
        ResolveEyeZoomOutputContainerSize(zoomConfig,
                                          containerWidth,
                                          containerHeight,
                                          modeViewport,
                                          outputContainerWidth,
                                          outputContainerHeight);
        const float relativeWidth = std::clamp(zoomConfig.outputRelativeWidth, 0.01f, 20.0f);
        const float relativeHeight = std::clamp(zoomConfig.outputRelativeHeight, 0.01f, 20.0f);
        int width = std::max(1, static_cast<int>(static_cast<float>(outputContainerWidth) * relativeWidth));
        int height = std::max(1, static_cast<int>(static_cast<float>(outputContainerHeight) * relativeHeight));

        if (zoomConfig.outputPreserveAspectRatio) {
            int baseWidth = 0;
            int baseHeight = 0;
            ResolveEyeZoomAspectBasis(zoomConfig, baseWidth, baseHeight);
            const float scaleX = static_cast<float>(width) / static_cast<float>(baseWidth);
            const float scaleY = static_cast<float>(height) / static_cast<float>(baseHeight);
            const float uniformScale = ResolveUniformScaleByFitMode(scaleX, scaleY, zoomConfig.outputAspectFitMode);
            if (uniformScale > 0.0f) {
                width = std::max(1, static_cast<int>(static_cast<float>(baseWidth) * uniformScale));
            }
        }

        if (width > containerWidth) {
            width = containerWidth;
        }
        return width;
    }

    if (zoomConfig.stretchWidth > 0) {
        return std::min(containerWidth, zoomConfig.stretchWidth);
    }

    int width = containerWidth;
    const std::string relativeTo = zoomConfig.outputRelativeTo.empty() ? "middleLeftScreen" : zoomConfig.outputRelativeTo;
    if (modeViewport.valid && !ShouldUseViewportRelativeTo(relativeTo)) {
        const std::string anchorBase = GetRelativeToAnchorBase(relativeTo);
        const int modeRight = modeViewport.x + modeViewport.width;
        if (IsLeftAlignedAnchor(anchorBase)) {
            width = modeViewport.x - outputX;
        } else if (IsRightAlignedAnchor(anchorBase)) {
            width = containerWidth - modeRight - outputX;
        }
    }

    if (width < 1) {
        width = 1;
    }
    if (width > containerWidth) {
        width = containerWidth;
    }
    return width;
}

int ResolveEyeZoomOutputHeight(const platform::config::EyeZoomConfig& zoomConfig,
                               int containerWidth,
                               int containerHeight,
                               const EyeZoomModeViewportRect& modeViewport) {
    if (containerWidth < 1 || containerHeight < 1) {
        return 0;
    }

    if (zoomConfig.outputUseRelativeSize) {
        int outputContainerWidth = 0;
        int outputContainerHeight = 0;
        ResolveEyeZoomOutputContainerSize(zoomConfig,
                                          containerWidth,
                                          containerHeight,
                                          modeViewport,
                                          outputContainerWidth,
                                          outputContainerHeight);
        const float relativeWidth = std::clamp(zoomConfig.outputRelativeWidth, 0.01f, 20.0f);
        const float relativeHeight = std::clamp(zoomConfig.outputRelativeHeight, 0.01f, 20.0f);
        int width = std::max(1, static_cast<int>(static_cast<float>(outputContainerWidth) * relativeWidth));
        int height = std::max(1, static_cast<int>(static_cast<float>(outputContainerHeight) * relativeHeight));

        if (zoomConfig.outputPreserveAspectRatio) {
            int baseWidth = 0;
            int baseHeight = 0;
            ResolveEyeZoomAspectBasis(zoomConfig, baseWidth, baseHeight);
            const float scaleX = static_cast<float>(width) / static_cast<float>(baseWidth);
            const float scaleY = static_cast<float>(height) / static_cast<float>(baseHeight);
            const float uniformScale = ResolveUniformScaleByFitMode(scaleX, scaleY, zoomConfig.outputAspectFitMode);
            if (uniformScale > 0.0f) {
                height = std::max(1, static_cast<int>(static_cast<float>(baseHeight) * uniformScale));
            }
        }

        if (height > containerHeight) {
            height = containerHeight;
        }
        return height;
    }

    int height = zoomConfig.outputHeight;
    if (height <= 0) {
        height = containerHeight - (2 * zoomConfig.verticalMargin);
        int minHeight = static_cast<int>(0.2f * containerHeight);
        if (height < minHeight) {
            height = minHeight;
        }
    }

    if (height > containerHeight) {
        height = containerHeight;
    }
    return height;
}

void RenderEyeZoomTab(platform::config::LinuxscreenConfig& config,
                      const std::string& activeMode,
                      platform::x11::MirrorModeState& modeState) {
    ImGui::Text("EyeZoom Settings");
    ImGui::Separator();
    
    auto& ez = config.eyezoom;
    platform::config::ModeConfig* eyeZoomMode = nullptr;
    for (auto& mode : config.modes) {
        if (mode.name == "EyeZoom") {
            eyeZoomMode = &mode;
            break;
        }
    }
    if (eyeZoomMode) {
        ez.windowWidth = eyeZoomMode->width;
        ez.windowHeight = eyeZoomMode->height;
    }

    ImGui::TextDisabled("These settings apply to mode named \"EyeZoom\".");
    if (!eyeZoomMode) {
        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.0f, 1.0f),
                           "Warning: mode \"EyeZoom\" not found. Game Width/Height cannot update mode size.");
    }

    float displayWidth = 0.0f;
    float displayHeight = 0.0f;
    float framebufferScaleX = 1.0f;
    float framebufferScaleY = 1.0f;
    const bool hasDisplayMetrics = GetOverlayDisplayMetrics(displayWidth, displayHeight, framebufferScaleX, framebufferScaleY);
    (void)framebufferScaleX;
    (void)framebufferScaleY;
    const bool hasValidDisplaySize = hasDisplayMetrics && displayWidth > 0.0f && displayHeight > 0.0f;

    auto resolveOutputPositionContainer = [&](int& outContainerWidth, int& outContainerHeight) {
        outContainerWidth = 0;
        outContainerHeight = 0;
        if (!hasValidDisplaySize) {
            return false;
        }

        EyeZoomModeViewportRect modeViewportRect;
        (void)ResolveEyeZoomModeViewportRect(config,
                                             static_cast<int>(displayWidth),
                                             static_cast<int>(displayHeight),
                                             modeViewportRect);
        ResolveEyeZoomOutputContainerSize(ez,
                                          static_cast<int>(displayWidth),
                                          static_cast<int>(displayHeight),
                                          modeViewportRect,
                                          outContainerWidth,
                                          outContainerHeight);
        return outContainerWidth > 0 && outContainerHeight > 0;
    };

    auto updateOutputRelativeFromPixels = [&]() {
        int containerWidth = 0;
        int containerHeight = 0;
        if (!resolveOutputPositionContainer(containerWidth, containerHeight)) {
            return;
        }
        ez.outputRelativeX = static_cast<float>(ez.outputX) / static_cast<float>(containerWidth);
        ez.outputRelativeY = static_cast<float>(ez.outputY) / static_cast<float>(containerHeight);
    };

    auto updateOutputPixelsFromRelative = [&]() {
        int containerWidth = 0;
        int containerHeight = 0;
        if (!resolveOutputPositionContainer(containerWidth, containerHeight)) {
            return;
        }
        ez.outputX = static_cast<int>(ez.outputRelativeX * static_cast<float>(containerWidth));
        ez.outputY = static_cast<int>(ez.outputRelativeY * static_cast<float>(containerHeight));
    };

    auto updateOutputRelativeSizeFromPixels = [&]() {
        if (!hasValidDisplaySize) {
            return;
        }

        EyeZoomModeViewportRect modeViewportRect;
        (void)ResolveEyeZoomModeViewportRect(config,
                                             static_cast<int>(displayWidth),
                                             static_cast<int>(displayHeight),
                                             modeViewportRect);

        int outputX = ez.outputX;
        if (ez.outputUseRelativePosition) {
            int positionContainerWidth = 0;
            int positionContainerHeight = 0;
            if (resolveOutputPositionContainer(positionContainerWidth, positionContainerHeight)) {
                outputX = static_cast<int>(ez.outputRelativeX * static_cast<float>(positionContainerWidth));
                (void)positionContainerHeight;
            }
        }

        const bool oldUseRelativeSize = ez.outputUseRelativeSize;
        ez.outputUseRelativeSize = false;
        const int currentWidth = ResolveEyeZoomOutputWidth(ez,
                                                            static_cast<int>(displayWidth),
                                                            static_cast<int>(displayHeight),
                                                            modeViewportRect,
                                                            outputX);
        const int currentHeight = ResolveEyeZoomOutputHeight(ez,
                                                              static_cast<int>(displayWidth),
                                                              static_cast<int>(displayHeight),
                                                              modeViewportRect);
        ez.outputUseRelativeSize = oldUseRelativeSize;

        int containerWidth = 0;
        int containerHeight = 0;
        ResolveEyeZoomOutputContainerSize(ez,
                                          static_cast<int>(displayWidth),
                                          static_cast<int>(displayHeight),
                                          modeViewportRect,
                                          containerWidth,
                                          containerHeight);
        if (containerWidth > 0) {
            ez.outputRelativeWidth = std::clamp(static_cast<float>(currentWidth) / static_cast<float>(containerWidth), 0.01f, 20.0f);
        }
        if (containerHeight > 0) {
            ez.outputRelativeHeight = std::clamp(static_cast<float>(currentHeight) / static_cast<float>(containerHeight), 0.01f, 20.0f);
        }
    };

    auto resolveOutputRelativeContainer = [&](int& outContainerWidth, int& outContainerHeight) {
        outContainerWidth = 0;
        outContainerHeight = 0;
        if (!hasValidDisplaySize) {
            return false;
        }

        EyeZoomModeViewportRect modeViewportRect;
        (void)ResolveEyeZoomModeViewportRect(config,
                                             static_cast<int>(displayWidth),
                                             static_cast<int>(displayHeight),
                                             modeViewportRect);
        ResolveEyeZoomOutputContainerSize(ez,
                                          static_cast<int>(displayWidth),
                                          static_cast<int>(displayHeight),
                                          modeViewportRect,
                                          outContainerWidth,
                                          outContainerHeight);
        return outContainerWidth > 0 && outContainerHeight > 0;
    };

    auto getOutputRelativeUniformScale = [&]() -> float {
        int containerWidth = 0;
        int containerHeight = 0;
        const std::string fitMode = NormalizeAspectFitMode(ez.outputAspectFitMode);
        if (!resolveOutputRelativeContainer(containerWidth, containerHeight)) {
            return ResolveUniformScaleByFitMode(std::clamp(ez.outputRelativeWidth, 0.01f, 20.0f),
                                                std::clamp(ez.outputRelativeHeight, 0.01f, 20.0f),
                                                fitMode);
        }

        int baseWidth = 0;
        int baseHeight = 0;
        ResolveEyeZoomAspectBasis(ez, baseWidth, baseHeight);
        const float scaleX = (static_cast<float>(containerWidth) * ez.outputRelativeWidth) / static_cast<float>(baseWidth);
        const float scaleY = (static_cast<float>(containerHeight) * ez.outputRelativeHeight) / static_cast<float>(baseHeight);
        return std::clamp(ResolveUniformScaleByFitMode(scaleX, scaleY, fitMode), 0.01f, 20.0f);
    };

    auto setOutputRelativeFromUniformScale = [&](float uniformScale) {
        uniformScale = std::clamp(uniformScale, 0.01f, 20.0f);
        int containerWidth = 0;
        int containerHeight = 0;
        if (!resolveOutputRelativeContainer(containerWidth, containerHeight)) {
            ez.outputRelativeWidth = uniformScale;
            ez.outputRelativeHeight = uniformScale;
            return;
        }

        int baseWidth = 0;
        int baseHeight = 0;
        ResolveEyeZoomAspectBasis(ez, baseWidth, baseHeight);
        ez.outputRelativeWidth = std::clamp((uniformScale * static_cast<float>(baseWidth)) / static_cast<float>(containerWidth), 0.01f, 20.0f);
        ez.outputRelativeHeight = std::clamp((uniformScale * static_cast<float>(baseHeight)) / static_cast<float>(containerHeight), 0.01f, 20.0f);
    };

    bool changed = false;

    ImGui::SeparatorText("Mirror Capture");
    if (ImGui::InputInt("Capture Width", &ez.cloneWidth, 2, 10)) changed = true;
    if (ImGui::InputInt("Capture Height", &ez.cloneHeight)) changed = true;
    if (ImGui::InputInt("Overlay Width (per side)", &ez.overlayWidth)) changed = true;

    ImGui::SeparatorText("Game Viewport (EyeZoom Mode)");
    if (ImGui::InputInt("Game Width", &ez.windowWidth)) changed = true;
    if (ImGui::InputInt("Game Height", &ez.windowHeight)) changed = true;

    bool eyeZoomModeGeometryChanged = false;
    if (eyeZoomMode) {
        if (eyeZoomMode->positionPreset == "custom") {
            eyeZoomMode->positionPreset = "topLeftScreen";
            changed = true;
            eyeZoomModeGeometryChanged = true;
        }

        if (DrawRelativeToCombo("Relative To##EyeZoomMode", eyeZoomMode->positionPreset)) {
            changed = true;
            eyeZoomModeGeometryChanged = true;
        }

        int modeX = eyeZoomMode->x;
        int modeY = eyeZoomMode->y;
        if (ImGui::InputInt("X Offset##EyeZoom", &modeX)) {
            eyeZoomMode->x = modeX;
            changed = true;
            eyeZoomModeGeometryChanged = true;
        }
        if (ImGui::InputInt("Y Offset##EyeZoom", &modeY)) {
            eyeZoomMode->y = modeY;
            changed = true;
            eyeZoomModeGeometryChanged = true;
        }
    } else {
        ImGui::BeginDisabled();
        std::string unavailableRelativeTo = "topLeftScreen";
        (void)DrawRelativeToCombo("Relative To##EyeZoomModeUnavailable", unavailableRelativeTo);
        int unavailable = 0;
        ImGui::InputInt("X Offset##EyeZoomUnavailable", &unavailable);
        ImGui::InputInt("Y Offset##EyeZoomUnavailable", &unavailable);
        ImGui::EndDisabled();
    }

    ImGui::SeparatorText("Viewport Border");
    if (eyeZoomMode) {
        if (ImGui::Checkbox("Enable Border##EyeZoomModeBorder", &eyeZoomMode->border.enabled)) {
            changed = true;
        }
        ImGui::SameLine();
        HelpMarker("Draw a border around the EyeZoom game viewport. Border appears outside the game area.");

        if (eyeZoomMode->border.enabled) {
            float borderColor[3] = {
                eyeZoomMode->border.color.r,
                eyeZoomMode->border.color.g,
                eyeZoomMode->border.color.b,
            };
            if (ImGui::ColorEdit3("Border Color##EyeZoomModeBorder",
                                  borderColor,
                                  ImGuiColorEditFlags_NoInputs)) {
                eyeZoomMode->border.color.r = std::clamp(borderColor[0], 0.0f, 1.0f);
                eyeZoomMode->border.color.g = std::clamp(borderColor[1], 0.0f, 1.0f);
                eyeZoomMode->border.color.b = std::clamp(borderColor[2], 0.0f, 1.0f);
                changed = true;
            }

            if (ImGui::InputInt("Border Width##EyeZoomModeBorder", &eyeZoomMode->border.width, 1, 10)) {
                eyeZoomMode->border.width = std::clamp(eyeZoomMode->border.width, 1, 50);
                changed = true;
            }

            if (ImGui::InputInt("Corner Radius##EyeZoomModeBorder", &eyeZoomMode->border.radius, 1, 10)) {
                eyeZoomMode->border.radius = std::clamp(eyeZoomMode->border.radius, 0, 100);
                changed = true;
            }
        }
    } else {
        ImGui::BeginDisabled();
        bool unavailableBorderEnabled = false;
        ImGui::Checkbox("Enable Border##EyeZoomModeBorderUnavailable", &unavailableBorderEnabled);
        float unavailableBorderColor[3] = {1.0f, 1.0f, 1.0f};
        ImGui::ColorEdit3("Border Color##EyeZoomModeBorderUnavailable",
                          unavailableBorderColor,
                          ImGuiColorEditFlags_NoInputs);
        int unavailableBorderValue = 1;
        ImGui::InputInt("Border Width##EyeZoomModeBorderUnavailable", &unavailableBorderValue, 1, 10);
        ImGui::InputInt("Corner Radius##EyeZoomModeBorderUnavailable", &unavailableBorderValue, 1, 10);
        ImGui::EndDisabled();
    }

    ImGui::SeparatorText("Output Placement");
    if (DrawRelativeToCombo("Relative To##EyeZoomOutput", ez.outputRelativeTo)) changed = true;
    if (ImGui::Checkbox("Relative to screen##EyeZoomOutput", &ez.outputUseRelativePosition)) {
        if (ez.outputUseRelativePosition) {
            updateOutputRelativeFromPixels();
        } else {
            updateOutputPixelsFromRelative();
        }
        changed = true;
    }
    ImGui::SameLine();
    HelpMarker("When enabled, output position is stored as percentages of screen size.\n"
               "This makes configs portable across different screen resolutions.");
    if (ez.outputUseRelativePosition) {
        float xPercent = ez.outputRelativeX * 100.0f;
        if (ImGui::SliderFloat("X %%##EyeZoomOutput", &xPercent, -100.0f, 200.0f, "%.1f%%")) {
            ez.outputRelativeX = xPercent / 100.0f;
            updateOutputPixelsFromRelative();
            changed = true;
        }
        float yPercent = ez.outputRelativeY * 100.0f;
        if (ImGui::SliderFloat("Y %%##EyeZoomOutput", &yPercent, -100.0f, 200.0f, "%.1f%%")) {
            ez.outputRelativeY = yPercent / 100.0f;
            updateOutputPixelsFromRelative();
            changed = true;
        }
    } else {
        if (ImGui::InputInt("X Offset##EyeZoomOutput", &ez.outputX)) changed = true;
        if (ImGui::InputInt("Y Offset##EyeZoomOutput", &ez.outputY)) changed = true;
    }
    if (ImGui::Checkbox("Relative size to container##EyeZoomOutputSize", &ez.outputUseRelativeSize)) {
        if (ez.outputUseRelativeSize) {
            updateOutputRelativeSizeFromPixels();
        }
        changed = true;
    }
    ImGui::SameLine();
    HelpMarker("When enabled, output width/height are stored as percentages of the active anchor container.\n"
               "Container is screen for *Screen anchors and mode viewport for *Viewport/Pie anchors.");
    if (ImGui::Checkbox("Preserve aspect ratio##EyeZoomOutputSize", &ez.outputPreserveAspectRatio)) {
        if (ez.outputPreserveAspectRatio) {
            setOutputRelativeFromUniformScale(getOutputRelativeUniformScale());
        }
        changed = true;
    }
    if (ez.outputPreserveAspectRatio) {
        if (DrawAspectFitModeCombo("Fit Mode##EyeZoomOutputSize", ez.outputAspectFitMode)) {
            ez.outputAspectFitMode = NormalizeAspectFitMode(ez.outputAspectFitMode);
            changed = true;
        }
        float scalePercent = getOutputRelativeUniformScale() * 100.0f;
        if (ImGui::SliderFloat("Scale %%##EyeZoomOutput", &scalePercent, 1.0f, 2000.0f, "%.1f%%")) {
            setOutputRelativeFromUniformScale(scalePercent / 100.0f);
            ez.outputUseRelativeSize = true;
            changed = true;
        }
    } else {
        float widthPercent = ez.outputRelativeWidth * 100.0f;
        if (ImGui::SliderFloat("Width %%##EyeZoomOutput", &widthPercent, 1.0f, 2000.0f, "%.1f%%")) {
            ez.outputRelativeWidth = widthPercent / 100.0f;
            ez.outputUseRelativeSize = true;
            changed = true;
        }
        float heightPercent = ez.outputRelativeHeight * 100.0f;
        if (ImGui::SliderFloat("Height %%##EyeZoomOutput", &heightPercent, 1.0f, 2000.0f, "%.1f%%")) {
            ez.outputRelativeHeight = heightPercent / 100.0f;
            ez.outputUseRelativeSize = true;
            changed = true;
        }
    }
    if (ImGui::InputInt("Output Width (0 = auto)", &ez.stretchWidth)) changed = true;
    if (ImGui::InputInt("Output Height (0 = auto)", &ez.outputHeight)) changed = true;
    HelpMarker("Relative To + X/Y Offset control where the EyeZoom output is placed.\n"
               "Output Width = 0 auto-fits beside the game for left/right screen anchors.\n"
               "Output Height = 0 keeps legacy auto-height for older configs when Relative size is disabled.\n"
               "Relative size controls are always editable; enable Relative size to apply them.");

    ImGui::SeparatorText("Text");
    if (ImGui::Checkbox("Auto Font Size", &ez.autoFontSize)) changed = true;
    ImGui::BeginDisabled(ez.autoFontSize);
    if (ImGui::InputInt("Text Font Size", &ez.textFontSize)) changed = true;
    ImGui::EndDisabled();
    if (ImGui::Checkbox("Link Rect to Font", &ez.linkRectToFont)) changed = true;
    ImGui::BeginDisabled(ez.linkRectToFont);
    if (ImGui::InputInt("Rect Height", &ez.rectHeight)) changed = true;
    ImGui::EndDisabled();

    ImGui::SeparatorText("Colors");
    
    float c1[3] = {ez.gridColor1.r, ez.gridColor1.g, ez.gridColor1.b};
    if (ImGui::ColorEdit3("Grid Color 1", c1)) {
        ez.gridColor1 = {c1[0], c1[1], c1[2], 1.0f};
        changed = true;
    }
    if (ImGui::DragFloat("Grid Color 1 Opacity", &ez.gridColor1Opacity, 0.01f, 0.0f, 1.0f)) changed = true;
    
    float c2[3] = {ez.gridColor2.r, ez.gridColor2.g, ez.gridColor2.b};
    if (ImGui::ColorEdit3("Grid Color 2", c2)) {
        ez.gridColor2 = {c2[0], c2[1], c2[2], 1.0f};
        changed = true;
    }
    if (ImGui::DragFloat("Grid Color 2 Opacity", &ez.gridColor2Opacity, 0.01f, 0.0f, 1.0f)) changed = true;
    
    float c3[3] = {ez.centerLineColor.r, ez.centerLineColor.g, ez.centerLineColor.b};
    if (ImGui::ColorEdit3("Center Line Color", c3)) {
        ez.centerLineColor = {c3[0], c3[1], c3[2], 1.0f};
        changed = true;
    }
    if (ImGui::DragFloat("Center Line Opacity", &ez.centerLineColorOpacity, 0.01f, 0.0f, 1.0f)) changed = true;
    
    float c4[3] = {ez.textColor.r, ez.textColor.g, ez.textColor.b};
    if (ImGui::ColorEdit3("Text Color", c4)) {
        ez.textColor = {c4[0], c4[1], c4[2], 1.0f};
        changed = true;
    }
    if (ImGui::DragFloat("Text Color Opacity", &ez.textColorOpacity, 0.01f, 0.0f, 1.0f)) changed = true;
    
    if (changed) {
        if (ez.outputRelativeTo.empty()) ez.outputRelativeTo = "middleLeftScreen";
        if (ez.cloneWidth < 2) ez.cloneWidth = 2;
        if (ez.cloneWidth % 2 != 0) ez.cloneWidth = (ez.cloneWidth / 2) * 2;
        if (ez.overlayWidth < 0) ez.overlayWidth = 0;
        int maxOverlay = ez.cloneWidth / 2;
        if (ez.overlayWidth > maxOverlay) ez.overlayWidth = maxOverlay;
        if (ez.cloneHeight < 1) ez.cloneHeight = 1;
        if (ez.stretchWidth < 0) ez.stretchWidth = 0;
        if (ez.outputHeight < 0) ez.outputHeight = 0;
        ez.outputRelativeX = std::clamp(ez.outputRelativeX, -1.0f, 2.0f);
        ez.outputRelativeY = std::clamp(ez.outputRelativeY, -1.0f, 2.0f);
        ez.outputRelativeWidth = std::clamp(ez.outputRelativeWidth, 0.01f, 20.0f);
        ez.outputRelativeHeight = std::clamp(ez.outputRelativeHeight, 0.01f, 20.0f);
        ez.outputAspectFitMode = NormalizeAspectFitMode(ez.outputAspectFitMode);
        if (ez.windowWidth < 0) ez.windowWidth = 0;
        if (ez.windowHeight < 0) ez.windowHeight = 0;
        if (ez.horizontalMargin < 0) ez.horizontalMargin = 0;
        if (ez.verticalMargin < 0) ez.verticalMargin = 0;
        if (ez.textFontSize < 1) ez.textFontSize = 1;
        if (ez.rectHeight < 1) ez.rectHeight = 1;

        bool eyeZoomModeResized = eyeZoomModeGeometryChanged;
        if (eyeZoomMode) {
            eyeZoomMode->border.width = std::clamp(eyeZoomMode->border.width, 1, 50);
            eyeZoomMode->border.radius = std::clamp(eyeZoomMode->border.radius, 0, 100);
            if (eyeZoomMode->width != ez.windowWidth) {
                eyeZoomMode->width = ez.windowWidth;
                eyeZoomModeResized = true;
            }
            if (eyeZoomMode->height != ez.windowHeight) {
                eyeZoomMode->height = ez.windowHeight;
                eyeZoomModeResized = true;
            }
        }

        if (eyeZoomModeResized && activeMode == eyeZoomMode->name) {
            StartModeSwitchWithTransition(eyeZoomMode->name, config, modeState);
        }
        AutoSaveConfig(config);
    }
}


void RenderEyeZoomTextDrawList(const platform::config::LinuxscreenConfig& config, float viewportWidth, float viewportHeight) {
    auto& modeState = GetMirrorModeState();
    std::string activeMode = modeState.GetActiveModeName();

    if (activeMode != "EyeZoom") {
        return;
    }

    const auto& zoomConfig = config.eyezoom;
    int cloneWidth = zoomConfig.cloneWidth;
    if (cloneWidth < 2) cloneWidth = 2;
    if (cloneWidth % 2 != 0) cloneWidth = (cloneWidth / 2) * 2;

    EyeZoomModeViewportRect modeViewportRect;
    (void)ResolveEyeZoomModeViewportRect(config,
                                         static_cast<int>(viewportWidth),
                                         static_cast<int>(viewportHeight),
                                         modeViewportRect);

    int outputPositionContainerWidth = static_cast<int>(viewportWidth);
    int outputPositionContainerHeight = static_cast<int>(viewportHeight);
    ResolveEyeZoomOutputContainerSize(zoomConfig,
                                      static_cast<int>(viewportWidth),
                                      static_cast<int>(viewportHeight),
                                      modeViewportRect,
                                      outputPositionContainerWidth,
                                      outputPositionContainerHeight);

    int outputX = zoomConfig.outputX;
    int outputY = zoomConfig.outputY;
    if (zoomConfig.outputUseRelativePosition) {
        outputX = static_cast<int>(zoomConfig.outputRelativeX * static_cast<float>(outputPositionContainerWidth));
        outputY = static_cast<int>(zoomConfig.outputRelativeY * static_cast<float>(outputPositionContainerHeight));
    }

    int zoomOutputWidth = ResolveEyeZoomOutputWidth(zoomConfig,
                                                    static_cast<int>(viewportWidth),
                                                    static_cast<int>(viewportHeight),
                                                    modeViewportRect,
                                                    outputX);
    if (zoomOutputWidth <= 1) return;

    int zoomOutputHeight = ResolveEyeZoomOutputHeight(zoomConfig,
                                                      static_cast<int>(viewportWidth),
                                                      static_cast<int>(viewportHeight),
                                                      modeViewportRect);
    if (zoomOutputHeight <= 1) return;

    int zoomX = zoomConfig.horizontalMargin;
    int zoomY = zoomConfig.verticalMargin;
    if (!zoomConfig.outputRelativeTo.empty()) {
        ResolveEyeZoomOutputAnchorCoords(zoomConfig.outputRelativeTo,
                                         outputX,
                                         outputY,
                                         zoomOutputWidth,
                                         zoomOutputHeight,
                                         static_cast<int>(viewportWidth),
                                         static_cast<int>(viewportHeight),
                                         modeViewportRect,
                                         zoomX,
                                         zoomY);
    }

    float pixelWidthOnScreen = zoomOutputWidth / (float)cloneWidth;
    int labelsPerSide = cloneWidth / 2;
    int overlayLabelsPerSide = zoomConfig.overlayWidth;
    if (overlayLabelsPerSide < 0) overlayLabelsPerSide = labelsPerSide;
    if (overlayLabelsPerSide > labelsPerSide) overlayLabelsPerSide = labelsPerSide;

    float centerY = zoomY + zoomOutputHeight / 2.0f;
    
    ImGuiViewport* mainViewport = ImGui::GetMainViewport();
    if (!mainViewport) {
        return;
    }

    ImDrawList* drawList = ImGui::GetBackgroundDrawList(mainViewport);
    ImU32 textColor = IM_COL32(
        (int)(zoomConfig.textColor.r * 255),
        (int)(zoomConfig.textColor.g * 255),
        (int)(zoomConfig.textColor.b * 255),
        (int)(zoomConfig.textColor.a * zoomConfig.textColorOpacity * 255)
    );

    ImFont* font = nullptr;
    ImGuiIO& io = ImGui::GetIO();
    if (io.Fonts && !io.Fonts->Fonts.empty()) {
        font = io.Fonts->Fonts[0];
    }
    if (!font) {
        font = ImGui::GetFont();
    }

    if (!font) {
        return;
    }

    float requestedFontSize = (float)zoomConfig.textFontSize;
    if (requestedFontSize < 1.0f) {
        requestedFontSize = 1.0f;
    }

    float fontSize = requestedFontSize;
    if (zoomConfig.autoFontSize) {
        fontSize = pixelWidthOnScreen * 0.90f;
        if (!zoomConfig.linkRectToFont) {
            float maxFontByHeight = (float)zoomConfig.rectHeight * 0.85f;
            if (maxFontByHeight > 0.0f) fontSize = (std::min)(fontSize, maxFontByHeight);
        }
        if (fontSize < 6.0f) fontSize = 6.0f;
    }

    for (int xOffset = -overlayLabelsPerSide; xOffset <= overlayLabelsPerSide; xOffset++) {
        if (xOffset == 0) continue;
        int boxIndex = xOffset + labelsPerSide - (xOffset > 0 ? 1 : 0);
        float boxLeft = zoomX + (boxIndex * pixelWidthOnScreen);
        float boxRight = boxLeft + pixelWidthOnScreen;
        float boxCenterX = (boxLeft + boxRight) / 2.0f;
        
        std::string text = std::to_string(abs(xOffset));
        float finalFontSize = fontSize;
        ImGui::PushFont(font, finalFontSize);
        ImVec2 textSize = ImGui::CalcTextSize(text.c_str());
        if (zoomConfig.autoFontSize) {
            float maxTextWidth = pixelWidthOnScreen * 0.94f;
            if (maxTextWidth > 0.0f && textSize.x > maxTextWidth && textSize.x > 0.0f) {
                float scale = maxTextWidth / textSize.x;
                finalFontSize = (std::max)(6.0f, finalFontSize * scale);
                ImGui::PopFont();
                ImGui::PushFont(font, finalFontSize);
                textSize = ImGui::CalcTextSize(text.c_str());
            }
        }
        float textX = boxCenterX - textSize.x / 2.0f;
        float textY = centerY - textSize.y / 2.0f;
        
        drawList->AddText(ImVec2(textX, textY), textColor, text.c_str());
        ImGui::PopFont();
    }
}

} // namespace platform::x11
