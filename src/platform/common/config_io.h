#pragma once

#include "linuxscreen_config.h"

#include <array>
#include <map>
#include <memory>
#include <string>

namespace platform::config {

// Returns the config file path:
// - LINUXSCREEN_X11_CONFIG_FILE env var if set and non-empty
// - $HOME/.config/linuxscreen/config.toml
// - Fallback to /tmp/linuxscreen_config.toml if HOME is unset
std::string GetConfigPath();
std::string GetConfigDirectoryPath();

std::string ResolvePathFromConfigDir(const std::string& path);

std::string NormalizePathForConfig(const std::string& path);

LinuxscreenConfig LoadLinuxscreenConfig();

void SaveLinuxscreenConfig(const LinuxscreenConfig& cfg);

void SaveLinuxscreenConfigImmediate(const LinuxscreenConfig& cfg);

void ShutdownConfigSaveThread();

void PublishConfigSnapshot(LinuxscreenConfig cfg);

std::shared_ptr<const LinuxscreenConfig> GetConfigSnapshot();

uint64_t GetConfigSnapshotVersion();

std::string GetThemePath();

void SaveThemeFile(const std::string& theme,
                   const std::map<std::string, std::array<float, 4>>& customColors);

bool LoadThemeFile(std::string& theme,
                   std::map<std::string, std::array<float, 4>>& customColors);

} // namespace platform::config
