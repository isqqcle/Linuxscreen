#pragma once

#include "linuxscreen_config.h"

#include <toml.hpp>

#include <string>

namespace platform::config {

toml::array ColorToTomlArray(const Color& color);
Color ColorFromTomlArray(const toml::array* arr, const Color& defaultColor = {0.0f, 0.0f, 0.0f, 1.0f});

void MirrorCaptureConfigToToml(const MirrorCaptureConfig& cfg, toml::table& out);
MirrorCaptureConfig MirrorCaptureConfigFromToml(const toml::table& tbl);

void MirrorRenderConfigToToml(const MirrorRenderConfig& cfg, toml::table& out);
MirrorRenderConfig MirrorRenderConfigFromToml(const toml::table& tbl);

void MirrorColorsToToml(const MirrorColors& cfg, toml::table& out);
MirrorColors MirrorColorsFromToml(const toml::table& tbl);

void MirrorConfigToToml(const MirrorConfig& cfg, toml::table& out);
MirrorConfig MirrorConfigFromToml(const toml::table& tbl);

void MirrorGroupItemToToml(const MirrorGroupItem& cfg, toml::table& out);
MirrorGroupItem MirrorGroupItemFromToml(const toml::table& tbl);

void MirrorGroupConfigToToml(const MirrorGroupConfig& cfg, toml::table& out);
MirrorGroupConfig MirrorGroupConfigFromToml(const toml::table& tbl);

void ModeBackgroundConfigToToml(const ModeBackgroundConfig& cfg, toml::table& out);
ModeBackgroundConfig ModeBackgroundConfigFromToml(const toml::table& tbl);

void ModeConfigToToml(const ModeConfig& cfg, toml::table& out);
ModeConfig ModeConfigFromToml(const toml::table& tbl);

void HotkeyConditionsToToml(const HotkeyConditions& cfg, toml::table& out);
HotkeyConditions HotkeyConditionsFromToml(const toml::table& tbl);

void HotkeyConfigToToml(const HotkeyConfig& cfg, toml::table& out);
HotkeyConfig HotkeyConfigFromToml(const toml::table& tbl);

void SensitivityHotkeyConfigToToml(const SensitivityHotkeyConfig& cfg, toml::table& out);
SensitivityHotkeyConfig SensitivityHotkeyConfigFromToml(const toml::table& tbl);

void KeyRebindToToml(const KeyRebind& cfg, toml::table& out);
KeyRebind KeyRebindFromToml(const toml::table& tbl);

void KeyRebindsConfigToToml(const KeyRebindsConfig& cfg, toml::table& out);
KeyRebindsConfig KeyRebindsConfigFromToml(const toml::table& tbl);

void EyeZoomConfigToToml(const EyeZoomConfig& cfg, toml::table& out);
EyeZoomConfig EyeZoomConfigFromToml(const toml::table& tbl);

toml::table LinuxscreenConfigToToml(const LinuxscreenConfig& cfg);
LinuxscreenConfig LinuxscreenConfigFromToml(const toml::table& tbl);

} // namespace platform::config
