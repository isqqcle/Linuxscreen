#pragma once

#include "../config_io.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

#include <toml.hpp>

namespace platform::config::detail {

extern std::mutex g_configMutex;
extern std::shared_ptr<const LinuxscreenConfig> g_config;
extern std::atomic<uint64_t> g_configSnapshotVersion;

extern std::mutex g_saveMutex;
extern std::condition_variable g_saveCV;
extern bool g_savePending;
extern LinuxscreenConfig g_pendingConfig;
extern std::chrono::steady_clock::time_point g_lastSaveTime;
inline constexpr auto kSaveThrottleMs = std::chrono::milliseconds(1000);
extern std::thread g_saveThread;
extern std::atomic<bool> g_saveThreadRunning;
extern std::once_flag g_saveThreadOnce;

bool IsDebugEnabled();
void LogDebug(const char* format, ...);
void LogWarning(const char* format, ...);

void MergeTomlTables(toml::table& base, const toml::table& overlay);
LinuxscreenConfig LoadDefaultConfig();

std::string GetConfigPathInternal();
void DoSaveConfig(const LinuxscreenConfig& cfg, const std::string& path);
void SaveThreadMain();
void EnsureSaveThreadStarted();

} // namespace platform::config::detail
