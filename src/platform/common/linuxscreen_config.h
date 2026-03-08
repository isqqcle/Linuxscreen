#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <toml.hpp>

namespace platform::config {

struct Color {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;
};

struct MirrorCaptureConfig {
    int x = 0;
    int y = 0;
    std::string relativeTo = "topLeftScreen";
    bool enabled = true;
};

struct MirrorRenderConfig {
    int x = 0;
    int y = 0;
    bool useRelativePosition = false;
    float relativeX = 0.5f;
    float relativeY = 0.5f;
    bool useRelativeSize = false;
    float relativeWidth = 0.5f;
    float relativeHeight = 0.5f;
    bool preserveAspectRatio = true;
    std::string aspectFitMode = "contain";
    float scale = 1.0f;
    bool separateScale = false;
    float scaleX = 1.0f;
    float scaleY = 1.0f;
    std::string relativeTo = "bottomLeftScreen";
};

struct MirrorColors {
    std::vector<Color> targetColors;
    Color output{0.0f, 0.0f, 0.0f, 1.0f};
    Color border{1.0f, 1.0f, 1.0f, 1.0f};
};

enum class MirrorBorderType {
    Dynamic,
    Static,
};

enum class MirrorBorderShape {
    Rectangle,
    Circle,
};

struct MirrorBorderConfig {
    MirrorBorderType type = MirrorBorderType::Dynamic;
    int dynamicThickness = 1;
    MirrorBorderShape staticShape = MirrorBorderShape::Rectangle;
    Color staticColor{1.0f, 1.0f, 1.0f, 1.0f};
    int staticThickness = 2;
    int staticRadius = 0;
    int staticOffsetX = 0;
    int staticOffsetY = 0;
    int staticWidth = 0;
    int staticHeight = 0;
};

inline int GetMirrorDynamicBorderPadding(const MirrorBorderConfig& border) {
    return (border.type == MirrorBorderType::Dynamic && border.dynamicThickness > 0)
        ? border.dynamicThickness
        : 0;
}

struct MirrorConfig {
    std::string name;
    int captureWidth = 50;
    int captureHeight = 50;
    std::vector<MirrorCaptureConfig> input;
    MirrorRenderConfig output;
    MirrorColors colors;
    float colorSensitivity = 0.001f;
    MirrorBorderConfig border;
    int fps = 30;
    float opacity = 1.0f;
    bool rawOutput = false;
    bool colorPassthrough = false;
    bool onlyOnMyScreen = false;
    std::string gammaMode;
    toml::table windowsPassthrough;
};

struct MirrorGroupItem {
    std::string mirrorId;
    bool enabled = true;
    float widthPercent = 1.0f;
    float heightPercent = 1.0f;
    int offsetX = 0;
    int offsetY = 0;
};

struct MirrorGroupConfig {
    std::string name;
    MirrorRenderConfig output;
    std::vector<MirrorGroupItem> mirrors;
};

struct BorderConfig {
    bool enabled = false;
    Color color{1.0f, 1.0f, 1.0f, 1.0f};
    int width = 4;
    int radius = 0;
};

struct GradientStop {
    Color color{0.0f, 0.0f, 0.0f, 1.0f};
    float position = 0.0f;
};

enum class GradientAnimationType {
    None,
    Rotate,
    Slide,
    Wave,
    Spiral,
    Fade,
};

struct ModeBackgroundConfig {
    std::string selectedMode = "color";
    std::string image;
    Color color{0.0f, 0.0f, 0.0f, 1.0f};
    std::vector<GradientStop> gradientStops{
        GradientStop{Color{0.0f, 0.0f, 0.0f, 1.0f}, 0.0f},
        GradientStop{Color{1.0f, 1.0f, 1.0f, 1.0f}, 1.0f},
    };
    float gradientAngle = 0.0f;
    GradientAnimationType gradientAnimation = GradientAnimationType::None;
    float gradientAnimationSpeed = 0.5f;
    bool gradientColorFade = false;
};

enum class ModeLayerType {
    Mirror,
    Group,
};

struct ModeLayerConfig {
    ModeLayerType type = ModeLayerType::Mirror;
    std::string id;
    bool enabled = true;
};

struct ModeConfig {
    std::string name;
    std::vector<ModeLayerConfig> layers;
    std::vector<std::string> mirrorIds;
    std::vector<std::string> groupIds;
    std::vector<std::string> imageIds;
    std::vector<std::string> windowOverlayIds;

    int width = 0;
    int height = 0;
    bool useRelativeSize = false;
    float relativeWidth = 0.5f;
    float relativeHeight = 0.5f;
    std::string widthExpr;
    std::string heightExpr;
    bool enforceModeSize = true;
    bool matchWindowSize = false;
    bool sensitivityOverrideEnabled = false;
    float modeSensitivity = 1.0f;
    bool separateXYSensitivity = false;
    float modeSensitivityX = 1.0f;
    float modeSensitivityY = 1.0f;
    BorderConfig border;

    int x = 0;
    int y = 0;
    std::string positionPreset = "centerScreen";
    
    ModeBackgroundConfig background;
    toml::table windowsPassthrough;
};

struct HotkeyConditions {
    std::vector<std::string> gameState;
    std::vector<uint32_t> exclusions;
};

struct AltSecondaryModeConfig {
    std::vector<uint32_t> keys;
    std::string mode;
};

struct HotkeyConfig {
    std::vector<uint32_t> keys;
    std::string mainMode;
    std::string secondaryMode;
    std::vector<AltSecondaryModeConfig> altSecondaryModes;
    HotkeyConditions conditions;
    int debounce = 100;
    bool triggerOnRelease = false;
    bool triggerOnHold = false;
    bool blockKeyFromGame = false;
    bool returnToDefaultOnRepeat = true;
    bool allowExitToFullscreenRegardlessOfGameState = false;
};

struct SensitivityHotkeyConfig {
    std::vector<uint32_t> keys;
    float sensitivity = 1.0f;
    bool separateXY = false;
    float sensitivityX = 1.0f;
    float sensitivityY = 1.0f;
    bool toggle = false;
    HotkeyConditions conditions;
    int debounce = 100;
};

struct KeyRebind {
    uint32_t fromKey = 0;
    uint32_t toKey = 0;
    bool enabled = true;
    bool consumeSourceInput = false;
    std::string name;

    bool useCustomOutput = false;
    uint32_t customOutputVK = 0;
    uint32_t customOutputUnicode = 0;
    uint32_t customOutputShiftUnicode = 0;
    uint32_t customOutputScanCode = 0;
    bool keyRepeatDisabled = false;
    int keyRepeatStartDelay = 0;
    int keyRepeatDelay = 0;
};

struct KeyRebindsConfig {
    bool enabled = false;
    bool resolveRebindTargetsForHotkeys = true;
    std::vector<uint32_t> toggleHotkey;
    std::vector<KeyRebind> rebinds;
};

enum class EyeZoomOverlayDisplayMode {
    Manual,
    Fit,
    Stretch,
};

struct EyeZoomOverlayConfig {
    std::string name;
    std::string path;
    EyeZoomOverlayDisplayMode displayMode = EyeZoomOverlayDisplayMode::Fit;
    int manualWidth = 100;
    int manualHeight = 100;
    float opacity = 1.0f;
};

struct EyeZoomConfig {
    int cloneWidth = 24;
    int overlayWidth = 12;
    int cloneHeight = 2080;
    int stretchWidth = 810;
    int windowWidth = 384;
    int windowHeight = 16384;
    int zoomAreaWidth = 0;
    int zoomAreaHeight = 0;
    bool useCustomSizePosition = false;
    int positionX = 0;
    int positionY = 0;
    int horizontalMargin = 0;
    int verticalMargin = 0;
    std::string outputRelativeTo = "middleLeftScreen";
    int outputX = 0;
    int outputY = 0;
    bool outputUseRelativePosition = false;
    float outputRelativeX = 0.0f;
    float outputRelativeY = 0.0f;
    bool outputUseRelativeSize = false;
    float outputRelativeWidth = 0.5f;
    float outputRelativeHeight = 0.5f;
    bool outputPreserveAspectRatio = true;
    std::string outputAspectFitMode = "contain";
    int outputHeight = 0;
    bool autoFontSize = true;
    int textFontSize = 24;
    std::string textFontPath;
    int rectHeight = 24;
    bool linkRectToFont = true;
    
    Color gridColor1{1.0f, 0.714f, 0.757f, 1.0f};
    float gridColor1Opacity = 1.0f;
    Color gridColor2{0.678f, 0.847f, 0.902f, 1.0f};
    float gridColor2Opacity = 1.0f;
    Color centerLineColor{1.0f, 1.0f, 1.0f, 1.0f};
    float centerLineColorOpacity = 1.0f;
    Color textColor{0.0f, 0.0f, 0.0f, 1.0f};
    float textColorOpacity = 1.0f;
    bool slideZoomIn = false;
    bool slideMirrorsIn = false;
    int activeOverlayIndex = -1;
    std::vector<EyeZoomOverlayConfig> overlays;
    toml::table windowsPassthrough;
};

struct LinuxscreenConfig {
    std::string defaultMode;
    std::vector<uint32_t> guiHotkey;
    std::vector<uint32_t> rebindToggleHotkey;
    std::vector<MirrorConfig> mirrors;
    std::vector<MirrorGroupConfig> mirrorGroups;
    std::vector<ModeConfig> modes;
    std::vector<HotkeyConfig> hotkeys;
    std::vector<SensitivityHotkeyConfig> sensitivityHotkeys;
    float mouseSensitivity = 1.0f;
    int windowsMouseSpeed = 0;
    int keyRepeatStartDelay = 0;
    int keyRepeatDelay = 0;
    bool keyRepeatAffectsMouseButtons = false;
    KeyRebindsConfig keyRebinds;
    EyeZoomConfig eyezoom;
    int configVersion = 1;
    float guiScale = 1.0f;
    std::string guiTheme = "Dark";
    std::map<std::string, std::array<float, 4>> guiCustomColors;
    int guiWidth = 800;
    int guiHeight = 600;
    std::string guiFontPath = "";
    int guiFontSize = 13;
    float guiOpacity = 1.0f;
    toml::table windowsPassthroughRoot;
};

} // namespace platform::config
