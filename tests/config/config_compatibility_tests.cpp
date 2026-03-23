#include "config_toml.h"
#include "game_state_monitor.h"
#include "mirror_image_source.h"
#include "window_capture.h"

#include <cmath>
#include <filesystem>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;
using namespace platform::config;

namespace {

fs::path FixtureDir() {
    return fs::path(LINUXSCREEN_TEST_FIXTURE_DIR);
}

fs::path WindowsDefaultConfigPath() {
    return fs::path(LINUXSCREEN_WINDOWS_DEFAULT_CONFIG_PATH);
}

void Require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

bool ApproxEq(float actual, float expected, float epsilon = 0.0001f) {
    return std::fabs(actual - expected) <= epsilon;
}

LinuxscreenConfig LoadConfigFixture(const fs::path& path) {
    return LinuxscreenConfigFromToml(toml::parse_file(path.string()));
}

const ModeConfig& FindMode(const LinuxscreenConfig& cfg, std::string_view name) {
    for (const auto& mode : cfg.modes) {
        if (mode.name == name) {
            return mode;
        }
    }
    throw std::runtime_error("Mode not found: " + std::string(name));
}

const MirrorConfig& FindMirror(const LinuxscreenConfig& cfg, std::string_view name) {
    for (const auto& mirror : cfg.mirrors) {
        if (mirror.name == name) {
            return mirror;
        }
    }
    throw std::runtime_error("Mirror not found: " + std::string(name));
}

const toml::table& RequireTable(const toml::table& tbl, const char* key) {
    const toml::node* node = tbl.get(key);
    Require(node && node->is_table(), std::string("Missing table: ") + key);
    return *node->as_table();
}

const toml::array& RequireArray(const toml::table& tbl, const char* key) {
    const toml::node* node = tbl.get(key);
    Require(node && node->is_array(), std::string("Missing array: ") + key);
    return *node->as_array();
}

const toml::table& RequireTableAt(const toml::array& arr, std::size_t index, std::string_view context) {
    Require(index < arr.size(), "Array index out of range for " + std::string(context));
    const toml::node& node = arr[index];
    Require(node.is_table(), "Expected table entry for " + std::string(context));
    return *node.as_table();
}

std::string RequireString(const toml::table& tbl, const char* key) {
    const toml::node* node = tbl.get(key);
    Require(node, std::string("Missing string key: ") + key);
    const auto value = node->value<std::string>();
    Require(value.has_value(), std::string("Expected string key: ") + key);
    return *value;
}

bool RequireBool(const toml::table& tbl, const char* key) {
    const toml::node* node = tbl.get(key);
    Require(node, std::string("Missing bool key: ") + key);
    const auto value = node->value<bool>();
    Require(value.has_value(), std::string("Expected bool key: ") + key);
    return *value;
}

std::int64_t RequireInt(const toml::table& tbl, const char* key) {
    const toml::node* node = tbl.get(key);
    Require(node, std::string("Missing integer key: ") + key);
    const auto value = node->value<std::int64_t>();
    Require(value.has_value(), std::string("Expected integer key: ") + key);
    return *value;
}

std::int64_t RequireIntAt(const toml::array& arr, std::size_t index, std::string_view context) {
    Require(index < arr.size(), "Array index out of range for " + std::string(context));
    const auto value = arr[index].value<std::int64_t>();
    Require(value.has_value(), "Expected integer for " + std::string(context));
    return *value;
}

void TestWindowsDefaultFixture() {
    const LinuxscreenConfig cfg = LoadConfigFixture(WindowsDefaultConfigPath());
    Require(!cfg.modes.empty(), "Windows default fixture should load modes");
    Require(cfg.defaultMode == "Fullscreen", "Windows default fixture should keep defaultMode");
    Require(cfg.fpsLimit == 0, "Windows default fixture should import fpsLimit");

    const toml::table saved = LinuxscreenConfigToToml(cfg);
    Require(saved.contains("mode"), "Saved default config should keep modes");
    Require(saved.contains("mirror"), "Saved default config should keep mirrors");
    Require(RequireInt(saved, "fpsLimit") == 0, "Saved default config should keep fpsLimit");

    const toml::array& savedModes = RequireArray(saved, "mode");
    const toml::table& fullscreenMode = RequireTableAt(savedModes, 0, "saved mode");
    Require(fullscreenMode.contains("id"), "Saved Windows default mode should use id");
    Require(!fullscreenMode.contains("name"), "Saved Windows default mode should not use legacy name");

    const LinuxscreenConfig reparsed = LinuxscreenConfigFromToml(saved);
    Require(!FindMode(reparsed, "Fullscreen").mirrorIds.empty(), "Reparsed Windows default should keep mirror ids");
}

void TestWindowsWriterFixture() {
    const LinuxscreenConfig cfg = LoadConfigFixture(FixtureDir() / "windows_writer.toml");
    const ModeConfig& mode = FindMode(cfg, "WinPrimary");
    const MirrorConfig& mirror = FindMirror(cfg, "WinMirror");

    Require(mode.groupIds.size() == 1 && mode.groupIds[0] == "WinGroup", "Windows mode should import mirrorGroupIds");
    Require(mode.imageIds.size() == 1 && mode.imageIds[0] == "Reference Image", "Windows mode should import imageIds");
    Require(mode.windowOverlayIds.size() == 1 && mode.windowOverlayIds[0] == "Unused Overlay", "Windows mode should import windowOverlayIds");
    Require(cfg.rebindToggleHotkey == std::vector<uint32_t>({162u, 84u}), "Nested keyRebinds.toggleHotkey should drive Linux rebind toggle state");
    Require(cfg.keyRebinds.toggleHotkey == cfg.rebindToggleHotkey, "Key rebind toggle hotkey should stay mirrored");
    Require(!cfg.keyRebinds.resolveRebindTargetsForHotkeys, "Windows resolveRebindTargetsForHotkeys should import");
    Require(cfg.guiTheme == "Custom", "appearance.theme should import");
    Require(cfg.guiCustomColors.count("WindowBg") == 1, "appearance.customColors should import");
    Require(mirror.gammaMode == "Linear", "Mirror gammaMode should import");
    Require(cfg.eyezoom.outputRelativeTo == "topLeftScreen", "Windows EyeZoom placement should translate to topLeftScreen");
    Require(cfg.eyezoom.outputX == 123 && cfg.eyezoom.outputY == 456, "Windows EyeZoom placement should translate absolute position");
    Require(!cfg.eyezoom.outputUseRelativeSize, "Windows EyeZoom placement should disable relative size");
    Require(cfg.eyezoom.stretchWidth == 450, "Windows EyeZoom zoomAreaWidth should translate to stretchWidth");
    Require(cfg.eyezoom.outputHeight == 600, "Windows EyeZoom zoomAreaHeight should translate to outputHeight");
    Require(cfg.eyezoom.overlays.size() == 1, "Windows EyeZoom overlay list should import");
    Require(cfg.windowsPassthroughRoot.contains("debug"), "Windows fixture should preserve unsupported root tables");

    const toml::table saved = LinuxscreenConfigToToml(cfg);
    Require(!saved.contains("rebindToggleHotkey"), "Saved config should not emit legacy top-level rebindToggleHotkey");
    Require(saved.contains("debug"), "Saved config should preserve debug table");
    Require(saved.contains("image"), "Saved config should preserve image array");
    Require(saved.contains("windowOverlay"), "Saved config should preserve windowOverlay array");

    const toml::table& savedAppearance = RequireTable(saved, "appearance");
    Require(RequireString(savedAppearance, "theme") == "Custom", "Saved config should emit appearance.theme");

    const toml::table& savedKeyRebinds = RequireTable(saved, "keyRebinds");
    const toml::array& savedToggleHotkey = RequireArray(savedKeyRebinds, "toggleHotkey");
    Require(savedToggleHotkey.size() == 2, "Saved nested toggleHotkey should be preserved");
    Require(RequireIntAt(savedToggleHotkey, 0, "saved toggleHotkey[0]") == 162, "Saved nested toggleHotkey[0] should match");
    Require(RequireIntAt(savedToggleHotkey, 1, "saved toggleHotkey[1]") == 84, "Saved nested toggleHotkey[1] should match");

    const toml::array& savedModes = RequireArray(saved, "mode");
    const toml::table& savedMode = RequireTableAt(savedModes, 0, "saved windows mode");
    Require(RequireString(savedMode, "id") == "WinPrimary", "Saved windows mode should emit id");
    Require(!savedMode.contains("name"), "Saved windows mode should not emit legacy name");
    Require(savedMode.contains("mirrorGroupIds"), "Saved windows mode should emit mirrorGroupIds");
    Require(savedMode.contains("transition"), "Saved windows mode should preserve unsupported transition table");

    const toml::table& savedEyezoom = RequireTable(saved, "eyezoom");
    Require(savedEyezoom.contains("overlay"), "Saved eyezoom should emit overlay array");
    Require(savedEyezoom.contains("zoomAreaWidth"), "Saved eyezoom should emit Windows placement keys");
    Require(savedEyezoom["positionX"].value<int64_t>() == 123, "Saved eyezoom should emit translated Windows positionX");
    Require(savedEyezoom["positionY"].value<int64_t>() == 456, "Saved eyezoom should emit translated Windows positionY");
    Require(savedEyezoom["zoomAreaWidth"].value<int64_t>() == 450, "Saved eyezoom should emit translated Windows zoomAreaWidth");
    Require(savedEyezoom["zoomAreaHeight"].value<int64_t>() == 600, "Saved eyezoom should emit translated Windows zoomAreaHeight");

    const LinuxscreenConfig reparsed = LinuxscreenConfigFromToml(saved);
    Require(reparsed.windowsPassthroughRoot.contains("debug"), "Windows save/reparse should preserve debug passthrough");
    Require(FindMode(reparsed, "WinPrimary").windowsPassthrough.contains("transition"),
            "Windows save/reparse should preserve per-mode passthrough");
}

void TestAliasFixture() {
    const LinuxscreenConfig cfg = LoadConfigFixture(FixtureDir() / "legacy_alias.toml");
    const ModeConfig& mode = FindMode(cfg, "LegacyMode");
    const MirrorConfig& mirror = FindMirror(cfg, "LegacyMirror");

    Require(mode.groupIds.size() == 1 && mode.groupIds[0] == "LegacyGroup", "Legacy groupIds should import into mode.groupIds");
    Require(cfg.rebindToggleHotkey == std::vector<uint32_t>({164u, 82u}), "Legacy top-level rebindToggleHotkey should import");
    Require(cfg.keyRebinds.toggleHotkey == cfg.rebindToggleHotkey, "Legacy top-level rebind toggle should mirror into keyRebinds");
    Require(mirror.border.dynamicThickness == 9, "Legacy dynamicBorderThickness should import into border.dynamicThickness");
    Require(cfg.mirrorGroups.size() == 1, "Legacy mirror group fixture should load");
    Require(cfg.mirrorGroups[0].mirrors.size() == 1 &&
                cfg.mirrorGroups[0].mirrors[0].mirrorId == "LegacyMirror",
            "Legacy mirrorGroup.mirrorIds should map into mirrors list");

    const toml::table saved = LinuxscreenConfigToToml(cfg);
    Require(!saved.contains("rebindToggleHotkey"), "Saved alias fixture should not emit legacy top-level rebindToggleHotkey");

    const toml::table& savedKeyRebinds = RequireTable(saved, "keyRebinds");
    const toml::array& savedToggleHotkey = RequireArray(savedKeyRebinds, "toggleHotkey");
    Require(savedToggleHotkey.size() == 2, "Saved alias fixture should emit nested toggleHotkey");
    Require(RequireIntAt(savedToggleHotkey, 0, "alias saved toggleHotkey[0]") == 164, "Alias toggleHotkey[0] should match");
    Require(RequireIntAt(savedToggleHotkey, 1, "alias saved toggleHotkey[1]") == 82, "Alias toggleHotkey[1] should match");

    const toml::array& savedModes = RequireArray(saved, "mode");
    const toml::table& savedMode = RequireTableAt(savedModes, 0, "saved alias mode");
    Require(RequireString(savedMode, "id") == "LegacyMode", "Saved alias mode should emit canonical id");
    Require(savedMode.contains("mirrorGroupIds"), "Saved alias mode should emit canonical mirrorGroupIds");
}

void TestNegativeFixture() {
    const LinuxscreenConfig cfg = LoadConfigFixture(FixtureDir() / "negative.toml");
    const ModeConfig& mode = FindMode(cfg, "BrokenButLoadable");
    const MirrorConfig& mirror = FindMirror(cfg, "NegativeMirror");

    Require(cfg.defaultMode.empty(), "Negative fixture should preserve missing defaultMode at parse layer");
    Require(mode.useRelativeSize, "Width percentage should mark mode as relative size");
    Require(ApproxEq(mode.relativeWidth, 0.75f), "Width percentage should import into relativeWidth");
    Require(mode.height == 1440, "Height float should coerce to integer height");
    Require(mode.background.gradientAnimation == GradientAnimationType::None, "Invalid gradient animation should normalize to None");
    Require(!mirror.output.useRelativePosition, "Mixed absolute/relative output coordinates should stay absolute without both percentages");
    Require(mirror.output.x == 0, "Relative X percentage should not overwrite absolute output X");
    Require(mirror.output.y == 320, "Absolute floating Y should coerce to integer");
    Require(cfg.eyezoom.outputAspectFitMode == "contain", "Invalid eyezoom aspect fit mode should normalize");

    const toml::table saved = LinuxscreenConfigToToml(cfg);
    const LinuxscreenConfig reparsed = LinuxscreenConfigFromToml(saved);
    const ModeConfig& reparsedMode = FindMode(reparsed, "BrokenButLoadable");
    Require(reparsedMode.mirrorIds.size() == 1 && reparsedMode.mirrorIds[0] == "MissingMirror",
            "Dangling mirror references should survive save/reparse");
}

void TestGameStateEquivalence() {
    Require(MatchesGameStateCondition("inworld,cursor_grabbed", "inworld,unpaused"),
            "Windows cursor_grabbed should match Linux unpaused");
    Require(MatchesGameStateCondition("inworld,unpaused", "inworld,cursor_grabbed"),
            "Linux unpaused should match Windows cursor_grabbed");
    Require(MatchesGameStateCondition("inworld,cursor_free", "inworld,paused"),
            "Windows cursor_free should match Linux paused");
    Require(MatchesGameStateCondition("inworld,cursor_free", "inworld,gamescreenopen"),
            "Windows cursor_free should match Linux gamescreenopen");
    Require(MatchesGameStateCondition("inworld,paused", "inworld,gamescreenopen"),
            "Linux paused should match Linux gamescreenopen");
    Require(!MatchesGameStateCondition("inworld,cursor_grabbed", "inworld,paused"),
            "Grabbed and paused states should not match");

    std::vector<std::string> importedWindowsStates = {"inworld,cursor_free"};
    Require(HasMatchingGameStateCondition(importedWindowsStates, "inworld,paused"),
            "Imported Windows cursor_free state should allow Linux paused");
    Require(HasMatchingGameStateCondition(importedWindowsStates, "inworld,gamescreenopen"),
            "Imported Windows cursor_free state should allow Linux gamescreenopen");

    RemoveMatchingGameStateConditions(importedWindowsStates, "inworld,paused");
    Require(importedWindowsStates.empty(),
            "Removing Linux paused should also remove equivalent imported Windows cursor_free state");
}

void TestHotkeyRoundTripFields() {
    toml::table hotkeyTbl;
    hotkeyTbl.insert("keys", toml::array{int64_t(162), int64_t(90)});
    hotkeyTbl.insert("mainMode", "Fullscreen");
    hotkeyTbl.insert("secondaryMode", "Thin");
    hotkeyTbl.insert("returnMode", "Wide");
    hotkeyTbl.insert("debounce", int64_t(75));
    hotkeyTbl.insert("triggerOnRelease", false);
    hotkeyTbl.insert("triggerOnHold", true);
    hotkeyTbl.insert("blockKeyFromGame", true);
    hotkeyTbl.insert("returnToDefaultOnRepeat", true);
    hotkeyTbl.insert("allowExitToFullscreenRegardlessOfGameState", true);
    hotkeyTbl.insert("conditions", toml::table{});

    const HotkeyConfig hotkey = HotkeyConfigFromToml(hotkeyTbl);
    Require(hotkey.returnMode == "Wide", "Hotkey returnMode should parse");
    Require(hotkey.triggerOnHold, "Hotkey triggerOnHold should parse");

    toml::table savedHotkey;
    HotkeyConfigToToml(hotkey, savedHotkey);
    Require(RequireString(savedHotkey, "returnMode") == "Wide", "Hotkey returnMode should serialize");
    Require(RequireBool(savedHotkey, "triggerOnHold"), "Hotkey triggerOnHold should serialize");

    toml::table sensitivityTbl;
    sensitivityTbl.insert("keys", toml::array{int64_t(88)});
    sensitivityTbl.insert("sensitivity", 0.5);
    sensitivityTbl.insert("separateXY", true);
    sensitivityTbl.insert("sensitivityX", 0.5);
    sensitivityTbl.insert("sensitivityY", 0.75);
    sensitivityTbl.insert("toggle", false);
    sensitivityTbl.insert("triggerOnHold", true);
    sensitivityTbl.insert("debounce", int64_t(40));
    sensitivityTbl.insert("conditions", toml::table{});

    const SensitivityHotkeyConfig sensitivityHotkey = SensitivityHotkeyConfigFromToml(sensitivityTbl);
    Require(sensitivityHotkey.triggerOnHold, "Sensitivity hotkey triggerOnHold should parse");

    toml::table savedSensitivity;
    SensitivityHotkeyConfigToToml(sensitivityHotkey, savedSensitivity);
    Require(RequireBool(savedSensitivity, "triggerOnHold"), "Sensitivity hotkey triggerOnHold should serialize");
}

void TestRootFpsLimitRoundTrip() {
    toml::table root;
    root.insert("fpsLimit", int64_t(120));

    const LinuxscreenConfig cfg = LinuxscreenConfigFromToml(root);
    Require(cfg.fpsLimit == 120, "Root fpsLimit should parse");

    const toml::table saved = LinuxscreenConfigToToml(cfg);
    Require(RequireInt(saved, "fpsLimit") == 120, "Root fpsLimit should serialize");
}

void TestMirrorSourceRoundTrip() {
    toml::table mirrorTbl;
    mirrorTbl.insert("name", "Chat");
    mirrorTbl.insert("captureWidth", int64_t(640));
    mirrorTbl.insert("captureHeight", int64_t(360));
    mirrorTbl.insert("fps", int64_t(45));
    mirrorTbl.insert("input", toml::array{
        toml::table{{"relativeTo", "topLeftScreen"}, {"x", int64_t(12)}, {"y", int64_t(18)}, {"enabled", true}}
    });
    mirrorTbl.insert("source", toml::table{
        {"type", "window"},
        {"appId", "com.apple.TextEdit"},
        {"windowTitle", "Notes"},
        {"titleMatchMode", "startsWith"},
        {"fallbackMode", "sameApp"},
        {"useWindowSize", true},
        {"selectionToken", "persist-token-123"}
    });

    const MirrorConfig mirror = MirrorConfigFromToml(mirrorTbl);
    Require(mirror.source.type == MirrorSourceType::Window, "Mirror source type should parse");
    Require(mirror.source.appId == "com.apple.TextEdit", "Mirror source appId should parse");
    Require(mirror.source.windowTitle == "Notes", "Mirror source windowTitle should parse");
    Require(mirror.source.titleMatchMode == MirrorSourceTitleMatchMode::StartsWith,
            "Mirror source title match mode should parse");
    Require(mirror.source.fallbackMode == MirrorSourceFallbackMode::SameApp,
            "Mirror source fallback mode should parse");
    Require(mirror.source.useWindowSize, "Mirror source useWindowSize should parse");
    Require(mirror.source.selectionToken == "persist-token-123", "Mirror source selectionToken should parse");
    Require(mirror.input.size() == 1 && mirror.input[0].relativeTo == "topLeftScreen",
            "Mirror input anchors should parse");

    toml::table savedMirror;
    MirrorConfigToToml(mirror, savedMirror);
    const toml::table& savedSource = RequireTable(savedMirror, "source");
    Require(RequireString(savedSource, "type") == "window", "Mirror source type should serialize");
    Require(RequireString(savedSource, "appId") == "com.apple.TextEdit", "Mirror source appId should serialize");
    Require(RequireString(savedSource, "windowTitle") == "Notes", "Mirror source windowTitle should serialize");
    Require(RequireString(savedSource, "titleMatchMode") == "startsWith",
            "Mirror source title match mode should serialize");
    Require(RequireString(savedSource, "fallbackMode") == "sameApp",
            "Mirror source fallback mode should serialize");
    Require(RequireBool(savedSource, "useWindowSize"), "Mirror source useWindowSize should serialize");
    Require(RequireString(savedSource, "selectionToken") == "persist-token-123",
            "Mirror source selectionToken should serialize");

    const MirrorConfig defaultMirror = MirrorConfigFromToml(toml::table{});
    Require(defaultMirror.source.type == MirrorSourceType::GameFramebuffer,
            "Missing mirror source should default to gameFramebuffer");

    toml::table imageMirrorTbl;
    imageMirrorTbl.insert("name", "Reference");
    imageMirrorTbl.insert("captureWidth", int64_t(320));
    imageMirrorTbl.insert("captureHeight", int64_t(180));
    imageMirrorTbl.insert("source", toml::table{
        {"type", "image"},
        {"image", "assets/reference.gif"},
        {"useImageSize", true},
        {"imageReloadPollMs", int64_t(100)},
        {"lastKnownWidth", int64_t(640)},
        {"lastKnownHeight", int64_t(360)}
    });

    const MirrorConfig imageMirror = MirrorConfigFromToml(imageMirrorTbl);
    Require(imageMirror.source.type == MirrorSourceType::Image, "Image mirror source type should parse");
    Require(imageMirror.source.image == "assets/reference.gif", "Image mirror source path should parse");
    Require(imageMirror.source.useImageSize, "Image mirror source useImageSize should parse");
    Require(imageMirror.source.imageReloadPollMs == 100, "Image mirror source poll speed should parse");
    Require(imageMirror.source.lastKnownWidth == 640, "Image mirror width hint should parse");
    Require(imageMirror.source.lastKnownHeight == 360, "Image mirror height hint should parse");

    toml::table savedImageMirror;
    MirrorConfigToToml(imageMirror, savedImageMirror);
    const toml::table& savedImageSource = RequireTable(savedImageMirror, "source");
    Require(RequireString(savedImageSource, "type") == "image", "Image mirror source type should serialize");
    Require(RequireString(savedImageSource, "image") == "assets/reference.gif",
            "Image mirror source path should serialize");
    Require(RequireBool(savedImageSource, "useImageSize"), "Image mirror source useImageSize should serialize");
    Require(RequireInt(savedImageSource, "imageReloadPollMs") == 100,
            "Image mirror source poll speed should serialize");

    toml::table calcOverlayMirrorTbl;
    calcOverlayMirrorTbl.insert("name", "Calc Overlay");
    calcOverlayMirrorTbl.insert("source", toml::table{
        {"type", "calcOverlay"},
        {"image", "calc-overlay/calc-overlay.png"},
        {"useImageSize", true},
        {"imageReloadPollMs", int64_t(250)}
    });

    const MirrorConfig calcOverlayMirror = MirrorConfigFromToml(calcOverlayMirrorTbl);
    Require(calcOverlayMirror.source.type == MirrorSourceType::CalcOverlay,
            "Calc overlay mirror source type should parse");
    Require(calcOverlayMirror.source.image == "calc-overlay/calc-overlay.png",
            "Calc overlay mirror source path should parse");
    Require(calcOverlayMirror.source.useImageSize,
            "Calc overlay mirror source image size flag should parse");

    toml::table savedCalcOverlayMirror;
    MirrorConfigToToml(calcOverlayMirror, savedCalcOverlayMirror);
    const toml::table& savedCalcOverlaySource = RequireTable(savedCalcOverlayMirror, "source");
    Require(RequireString(savedCalcOverlaySource, "type") == "calcOverlay",
            "Calc overlay mirror source type should serialize");
    Require(RequireString(savedCalcOverlaySource, "image") == "calc-overlay/calc-overlay.png",
            "Calc overlay mirror source path should serialize");
}

void TestWindowCaptureHelpers() {
    Require(platform::x11::NormalizeMirrorCaptureAnchor("bottomRightScreen") == "bottomRightScreen",
            "Known anchors should normalize to themselves");
    MirrorSourceConfig source;
    source.type = MirrorSourceType::Window;
    source.appId = "com.apple.TextEdit";
    source.titleMatchMode = MirrorSourceTitleMatchMode::Exact;
    source.fallbackMode = MirrorSourceFallbackMode::None;
    Require(platform::x11::IsWindowCaptureSource(source),
            "Window sources should remain window-backed even when the match pattern is incomplete");
    Require(!platform::x11::HasConfiguredWindowCaptureSource(source),
            "Incomplete window sources should not be considered capture-ready");

    std::vector<platform::x11::WindowCaptureRequest> requests = {
        { "com.apple.TextEdit", "Notes", MirrorSourceTitleMatchMode::Exact, MirrorSourceFallbackMode::None, "", 30, 640, 480 },
        { "com.apple.TextEdit", "Notes", MirrorSourceTitleMatchMode::Exact, MirrorSourceFallbackMode::None, "", 75, 0, 0 },
        { "com.apple.TextEdit", "Notes", MirrorSourceTitleMatchMode::Contains, MirrorSourceFallbackMode::SameApp, "", 45, 0, 0 },
        { "com.apple.Terminal", "Shell", MirrorSourceTitleMatchMode::Exact, MirrorSourceFallbackMode::None, "", 12, 0, 0 },
        { "", "", MirrorSourceTitleMatchMode::Disabled, MirrorSourceFallbackMode::None, "persist-token-123", 20, 0, 0 },
        { "", "", MirrorSourceTitleMatchMode::Disabled, MirrorSourceFallbackMode::None, "persist-token-123", 60, 1920, 1080 },
    };
    const auto normalized = platform::x11::NormalizeWindowCaptureRequests(requests);
    Require(normalized.size() == 4, "Window capture requests should dedupe token-backed and identity-backed requests separately");
    Require(normalized[0].fps == 75, "Window capture dedupe should keep the highest fps request");
    Require(normalized[0].titleMatchMode == MirrorSourceTitleMatchMode::Exact,
            "Window capture dedupe should preserve the requested title match mode");
    Require(normalized[0].fallbackMode == MirrorSourceFallbackMode::None,
            "Window capture dedupe should preserve the requested fallback mode");
    Require(normalized[0].preferredWidth == 640 && normalized[0].preferredHeight == 480,
            "Window capture dedupe should preserve preferred size hints");
    Require(normalized[3].selectionToken == "persist-token-123",
            "Window capture dedupe should preserve token-backed selection identity");
    Require(normalized[3].fps == 60,
            "Token-backed dedupe should keep the highest fps request");

    std::vector<platform::x11::AvailableWindow> windows = {
        { 1u, "com.apple.TextEdit", "TextEdit", "Notes", "", 400, 300, false, false },
        { 2u, "com.apple.TextEdit", "TextEdit", "Notes", "", 800, 600, true, false },
        { 3u, "com.apple.TextEdit", "TextEdit", "Notes", "", 700, 500, true, true },
    };
    const int bestIndex = platform::x11::FindBestMatchingWindowIndex(windows,
                                                                     "com.apple.TextEdit",
                                                                     "Notes");
    Require(bestIndex == 2, "Window matching should prefer active onscreen windows");

    std::vector<platform::x11::AvailableWindow> renamedWindows = {
        { 11u, "com.apple.TextEdit", "TextEdit", "Notes - Edited", "", 700, 500, true, false },
        { 12u, "com.apple.TextEdit", "TextEdit", "Notes", "", 300, 200, false, false },
    };
    const int reboundIndex = platform::x11::FindBestMatchingWindowIndex(renamedWindows,
                                                                        "com.apple.TextEdit",
                                                                        "Notes",
                                                                        MirrorSourceTitleMatchMode::Contains,
                                                                        MirrorSourceFallbackMode::SameApp,
                                                                        11u,
                                                                        700,
                                                                        500);
    Require(reboundIndex == 0, "Window matching should prefer a remembered windowId even if the title changed");

    const int exactMismatchIndex = platform::x11::FindBestMatchingWindowIndex(renamedWindows,
                                                                               "com.apple.TextEdit",
                                                                               "Notes",
                                                                               MirrorSourceTitleMatchMode::Exact,
                                                                               MirrorSourceFallbackMode::None,
                                                                               11u,
                                                                               700,
                                                                               500);
    Require(exactMismatchIndex == 1,
            "Remembered windowId should not bypass the current title policy when another valid match exists");

    std::vector<platform::x11::AvailableWindow> noExactMatchWindows = {
        { 51u, "com.apple.TextEdit", "TextEdit", "Completely Different", "", 700, 500, true, false },
    };
    const int noExactMatchIndex = platform::x11::FindBestMatchingWindowIndex(noExactMatchWindows,
                                                                              "com.apple.TextEdit",
                                                                              "Notes",
                                                                              MirrorSourceTitleMatchMode::Exact,
                                                                              MirrorSourceFallbackMode::None,
                                                                              51u,
                                                                              700,
                                                                              500);
    Require(noExactMatchIndex == -1,
            "Remembered windowId should not make a non-matching window eligible by itself");

    std::vector<platform::x11::AvailableWindow> fallbackWindows = {
        { 21u, "com.apple.TextEdit", "TextEdit", "Project Alpha", "", 640, 480, true, false },
        { 22u, "com.apple.TextEdit", "TextEdit", "Scratch Pad", "", 1200, 900, true, true },
    };
    const int fallbackIndex = platform::x11::FindBestMatchingWindowIndex(fallbackWindows,
                                                                         "com.apple.TextEdit",
                                                                         "Notes",
                                                                         MirrorSourceTitleMatchMode::Disabled,
                                                                         MirrorSourceFallbackMode::SameApp,
                                                                         0,
                                                                         640,
                                                                         480);
    Require(fallbackIndex == 0, "Same-app fallback should prefer the closest remembered size");

    std::vector<platform::x11::AvailableWindow> prefixWindows = {
        { 31u, "com.apple.TextEdit", "TextEdit", "Notes - Edited", "", 640, 480, true, false },
        { 32u, "com.apple.TextEdit", "TextEdit", "Edited Notes", "", 640, 480, true, false },
    };
    const int prefixIndex = platform::x11::FindBestMatchingWindowIndex(prefixWindows,
                                                                       "com.apple.TextEdit",
                                                                       "Notes",
                                                                       MirrorSourceTitleMatchMode::StartsWith,
                                                                       MirrorSourceFallbackMode::None);
    Require(prefixIndex == 0, "Starts-with title matching should prefer windows that begin with the pattern");

    std::vector<platform::x11::AvailableWindow> suffixWindows = {
        { 41u, "com.apple.Safari", "Safari", "Docs - Safari", "", 900, 600, true, false },
        { 42u, "com.apple.Safari", "Safari", "Safari - Docs", "", 900, 600, true, false },
    };
    const int suffixIndex = platform::x11::FindBestMatchingWindowIndex(suffixWindows,
                                                                       "com.apple.Safari",
                                                                       "Safari",
                                                                       MirrorSourceTitleMatchMode::EndsWith,
                                                                       MirrorSourceFallbackMode::None);
    Require(suffixIndex == 0, "Ends-with title matching should prefer windows that end with the pattern");

    Require(platform::x11::DetectLinuxWindowCaptureBackendForEnvironment("wayland", nullptr, nullptr) ==
                platform::x11::WindowCaptureBackend::Wayland,
            "Explicit wayland session type should select the Wayland backend");
    Require(platform::x11::DetectLinuxWindowCaptureBackendForEnvironment("x11", ":0", "wayland-0") ==
                platform::x11::WindowCaptureBackend::X11,
            "Explicit x11 session type should select the X11 backend even when both displays exist");
    Require(platform::x11::DetectLinuxWindowCaptureBackendForEnvironment(nullptr, ":0", nullptr) ==
                platform::x11::WindowCaptureBackend::X11,
            "DISPLAY alone should select the X11 backend");
    Require(platform::x11::DetectLinuxWindowCaptureBackendForEnvironment(nullptr, nullptr, "wayland-0") ==
                platform::x11::WindowCaptureBackend::Wayland,
            "WAYLAND_DISPLAY alone should select the Wayland backend");

    Require(platform::x11::ShouldDowngradeX11CompositeCapture(true, 0, 0),
            "Invalid composite geometry should immediately downgrade to the image path");
    Require(platform::x11::ShouldDowngradeX11CompositeCapture(false, 3, 0),
            "Repeated composite failures should downgrade to the image path");
    Require(platform::x11::ShouldDowngradeX11CompositeCapture(false, 0, 6),
            "Repeated stale composite frames should downgrade to the image path");
    Require(!platform::x11::ShouldDowngradeX11CompositeCapture(false, 2, 5),
            "Composite capture should not downgrade before the policy thresholds are reached");
}

void TestImageSourceHelpers() {
    MirrorSourceConfig imageSource;
    imageSource.type = MirrorSourceType::Image;
    Require(platform::x11::IsImageSource(imageSource), "Image sources should be identified as image-backed");
    Require(!platform::x11::HasConfiguredImageSource(imageSource),
            "Empty image sources should not be considered configured");
    Require(!platform::x11::IsWindowCaptureSource(imageSource),
            "Image sources should not be treated as window capture sources");

    imageSource.image = "assets/reference.png";
    Require(platform::x11::HasConfiguredImageSource(imageSource),
            "Configured image sources should require a path");
    Require(imageSource.imageReloadPollMs == MirrorSourceConfig::kDefaultImageReloadPollMs,
            "Image sources should default to the standard reload poll interval");

    const MirrorSourceConfig defaultSource;
    Require(!platform::x11::IsImageSource(defaultSource), "Default source should remain the game framebuffer");

    MirrorSourceConfig calcOverlaySource;
    calcOverlaySource.type = MirrorSourceType::CalcOverlay;
    Require(platform::x11::IsImageSource(calcOverlaySource),
            "Calc overlay sources should reuse the image-backed mirror path");
    Require(platform::x11::HasConfiguredImageSource(calcOverlaySource),
            "Calc overlay sources should be treated as configured without manual paths");
    Require(!platform::x11::IsWindowCaptureSource(calcOverlaySource),
            "Calc overlay sources should not be treated as window capture sources");
}

} // namespace

int main() {
    const std::vector<std::pair<const char*, std::function<void()>>> tests = {
        { "windows_default_fixture", TestWindowsDefaultFixture },
        { "windows_writer_fixture", TestWindowsWriterFixture },
        { "legacy_alias_fixture", TestAliasFixture },
        { "negative_fixture", TestNegativeFixture },
        { "game_state_equivalence", TestGameStateEquivalence },
        { "hotkey_round_trip_fields", TestHotkeyRoundTripFields },
        { "root_fps_limit_round_trip", TestRootFpsLimitRoundTrip },
        { "mirror_source_round_trip", TestMirrorSourceRoundTrip },
        { "window_capture_helpers", TestWindowCaptureHelpers },
        { "image_source_helpers", TestImageSourceHelpers },
    };

    try {
        for (const auto& [name, fn] : tests) {
            fn();
            std::cout << "[PASS] " << name << '\n';
        }
    } catch (const std::exception& e) {
        std::cerr << "[FAIL] " << e.what() << '\n';
        return 1;
    }

    return 0;
}
