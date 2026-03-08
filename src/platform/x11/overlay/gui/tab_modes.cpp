#include "../overlay_internal.h"
#include "imgui_overlay_helpers.h"
#include "tab_modes.h"
#include "tab_modes_state.h"

namespace platform::x11 {

ModeEditorState g_modeEditorState;
ModeBackgroundPickerState g_modeBackgroundPickerState;

std::string NormalizeBackgroundSelectedMode(const std::string& value) {
    if (value == "image") {
        return "image";
    }
    if (value == "gradient") {
        return "gradient";
    }
    return "color";
}

const char* BackgroundTypeLabel(const std::string& selectedMode) {
    if (selectedMode == "image") {
        return "Image";
    }
    if (selectedMode == "gradient") {
        return "Gradient";
    }
    return "Color";
}

int GradientAnimationTypeToComboIndex(platform::config::GradientAnimationType type) {
    constexpr platform::config::GradientAnimationType kGradientAnimationNone =
        static_cast<platform::config::GradientAnimationType>(0);
    switch (type) {
    case platform::config::GradientAnimationType::Rotate:
        return 1;
    case platform::config::GradientAnimationType::Slide:
        return 2;
    case platform::config::GradientAnimationType::Wave:
        return 3;
    case platform::config::GradientAnimationType::Spiral:
        return 4;
    case platform::config::GradientAnimationType::Fade:
        return 5;
    case kGradientAnimationNone:
    default:
        return 0;
    }
}

platform::config::GradientAnimationType ComboIndexToGradientAnimationType(int index) {
    constexpr platform::config::GradientAnimationType kGradientAnimationNone =
        static_cast<platform::config::GradientAnimationType>(0);
    switch (index) {
    case 1:
        return platform::config::GradientAnimationType::Rotate;
    case 2:
        return platform::config::GradientAnimationType::Slide;
    case 3:
        return platform::config::GradientAnimationType::Wave;
    case 4:
        return platform::config::GradientAnimationType::Spiral;
    case 5:
        return platform::config::GradientAnimationType::Fade;
    case 0:
    default:
        return kGradientAnimationNone;
    }
}

void SortGradientStops(std::vector<platform::config::GradientStop>& stops) {
    std::stable_sort(stops.begin(),
                     stops.end(),
                     [](const platform::config::GradientStop& a, const platform::config::GradientStop& b) {
                         return a.position < b.position;
                     });
}

bool GetCurrentModeSizingContainer(int& outWidth, int& outHeight) {
    outWidth = 0;
    outHeight = 0;

    int windowWidth = 0;
    int windowHeight = 0;
    int framebufferWidth = 0;
    int framebufferHeight = 0;
    if (GetGlfwWindowMetrics(windowWidth, windowHeight, framebufferWidth, framebufferHeight)) {
        if (framebufferWidth > 0 && framebufferHeight > 0) {
            outWidth = framebufferWidth;
            outHeight = framebufferHeight;
        } else if (windowWidth > 0 && windowHeight > 0) {
            outWidth = windowWidth;
            outHeight = windowHeight;
        }
    }

    if (outWidth <= 0 || outHeight <= 0) {
        if (!platform::x11::GetGameWindowSize(outWidth, outHeight) || outWidth <= 0 || outHeight <= 0) {
            GLint viewport[4] = { 0, 0, 0, 0 };
            glGetIntegerv(GL_VIEWPORT, viewport);
            outWidth = viewport[2];
            outHeight = viewport[3];
        }
    }

    return outWidth > 0 && outHeight > 0;
}

bool ResolveModeDimensionsForEditor(const platform::config::ModeConfig& mode,
                                    int containerWidth,
                                    int containerHeight,
                                    int& outWidth,
                                    int& outHeight) {
    outWidth = 0;
    outHeight = 0;

    int calcWidth = containerWidth;
    int calcHeight = containerHeight;
    if (calcWidth <= 0 || calcHeight <= 0) {
        calcWidth = mode.width;
        calcHeight = mode.height;
    }

    platform::x11::MirrorModeState::CalculateModeDimensions(mode, calcWidth, calcHeight, outWidth, outHeight);
    return outWidth > 0 && outHeight > 0;
}

#include "tab_modes_editor.cpp"

} // namespace platform::x11
