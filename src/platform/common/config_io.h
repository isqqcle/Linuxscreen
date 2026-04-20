#pragma once

#include "linuxscreen_config.h"

#include <array>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace platform::config {

struct ConfigProfile {
    std::string id;
    std::string name;
    std::string path;
};

struct ConfigProfileRegistry {
    std::string activeProfileId;
    std::vector<ConfigProfile> profiles;
};

// Returns the config file path:
// - LINUXSCREEN_X11_CONFIG_FILE env var if set and non-empty
// - $HOME/.config/linuxscreen/config.toml
// - Fallback to /tmp/linuxscreen_config.toml if HOME is unset
std::string GetConfigPath();
std::string GetConfigDirectoryPath();
std::string GetConfigRootDirectoryPath();

void SetConfigPathOverride(const std::string& path);
void ClearConfigPathOverride();
std::string GetConfigPathOverride();

std::string ResolvePathFromConfigDir(const std::string& path);
std::string ResolvePathFromConfigRootDir(const std::string& path);

std::string NormalizePathForConfig(const std::string& path);

LinuxscreenConfig LoadLinuxscreenConfig();
LinuxscreenConfig LoadEmbeddedDefaultConfig();

void SaveLinuxscreenConfig(const LinuxscreenConfig& cfg);

void SaveLinuxscreenConfigImmediate(const LinuxscreenConfig& cfg);

void SaveLinuxscreenConfigAtPathImmediate(const LinuxscreenConfig& cfg, const std::string& path);

void WaitForConfigSaveQueueIdle(std::uint64_t timeoutMs = 3000);

void ShutdownConfigSaveThread();

void PublishConfigSnapshot(LinuxscreenConfig cfg);

std::shared_ptr<const LinuxscreenConfig> GetConfigSnapshot();

uint64_t GetConfigSnapshotVersion();

std::string GetProfileRegistryPath();
ConfigProfileRegistry LoadConfigProfileRegistry();
ConfigProfileRegistry LoadOrCreateConfigProfileRegistry();
bool SaveConfigProfileRegistry(const ConfigProfileRegistry& registry);

std::string GetThemePath();

void SaveThemeFile(const std::string& theme,
                   const std::map<std::string, std::array<float, 4>>& customColors);

bool LoadThemeFile(std::string& theme,
                   std::map<std::string, std::array<float, 4>>& customColors);

bool InitializeActiveConfigProfile(std::string& outError);
ConfigProfileRegistry GetConfigProfileRegistry();
std::string GetActiveConfigProfileId();
bool SwitchActiveConfigProfile(const std::string& profileId,
                               LinuxscreenConfig& outConfig,
                               std::string& outError);
bool CreateConfigProfile(const std::string& name,
                         bool duplicateCurrent,
                         ConfigProfile& outProfile,
                         std::string& outError);

} // namespace platform::config
