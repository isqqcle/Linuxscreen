#pragma once

#include "../common/linuxscreen_config.h"

#include <string>

namespace platform::x11 {

struct CalcOverlayRuntimeStatus {
    bool enabled = false;
    bool running = false;
    bool javaAvailable = false;
    bool jarAvailable = false;
    int pid = -1;
    int lastExitCode = 0;
    bool hadUnexpectedExit = false;
    std::string javaPath;
    std::string jarPath;
    std::string configDir;
    std::string settingsPath;
    std::string imagePath;
    std::string logPath;
    std::string message;
};

void UpdateCalcOverlayRuntime(const platform::config::LinuxscreenConfig& config);
void ShutdownCalcOverlayRuntime();
CalcOverlayRuntimeStatus GetCalcOverlayRuntimeStatus();

std::string GetCalcOverlayConfigDirectoryPath();
std::string GetCalcOverlaySettingsPath();
std::string GetCalcOverlayImagePath();
std::string GetCalcOverlayLogPath();

} // namespace platform::x11
