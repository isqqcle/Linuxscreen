#include "config_toml.h"
#include "game_state_monitor.h"

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
    Require(cfg.windowsPassthroughRoot.contains("debug"), "Windows default should preserve debug passthrough");
    Require(cfg.windowsPassthroughRoot.contains("image"), "Windows default should preserve image passthrough");
    Require(cfg.windowsPassthroughRoot.contains("windowOverlay"), "Windows default should preserve windowOverlay passthrough");

    const toml::table saved = LinuxscreenConfigToToml(cfg);
    Require(saved.contains("debug"), "Saved Windows default should keep debug table");
    Require(saved.contains("image"), "Saved Windows default should keep image array");
    Require(saved.contains("windowOverlay"), "Saved Windows default should keep windowOverlay array");

    const toml::array& savedModes = RequireArray(saved, "mode");
    const toml::table& fullscreenMode = RequireTableAt(savedModes, 0, "saved mode");
    Require(fullscreenMode.contains("id"), "Saved Windows default mode should use id");
    Require(!fullscreenMode.contains("name"), "Saved Windows default mode should not use legacy name");

    const LinuxscreenConfig reparsed = LinuxscreenConfigFromToml(saved);
    Require(reparsed.windowsPassthroughRoot.contains("debug"), "Reparsed Windows default should keep debug passthrough");
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

} // namespace

int main() {
    const std::vector<std::pair<const char*, std::function<void()>>> tests = {
        { "windows_default_fixture", TestWindowsDefaultFixture },
        { "windows_writer_fixture", TestWindowsWriterFixture },
        { "legacy_alias_fixture", TestAliasFixture },
        { "negative_fixture", TestNegativeFixture },
        { "game_state_equivalence", TestGameStateEquivalence },
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
