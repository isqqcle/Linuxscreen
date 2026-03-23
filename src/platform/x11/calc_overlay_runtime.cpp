#include "calc_overlay_runtime.h"

#include "../common/config_io.h"
#include "calc_overlay_helper_embedded.h"

#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <climits>
#endif

#include <atomic>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

extern char** environ;

namespace platform::x11 {

namespace {

constexpr const char* kHelperJarName = "calc-overlay-plugin.jar";
constexpr const char* kHelperVersion = "2.4.0";

std::mutex g_calcOverlayMutex;
CalcOverlayRuntimeStatus g_calcOverlayStatus;
std::string g_lastWrittenSettingsJson;
std::chrono::steady_clock::time_point g_lastStartAttempt;
std::chrono::steady_clock::time_point g_lastStaticPathRefresh;
bool g_stopRequested = false;
bool g_helperJarPrepared = false;

constexpr auto kStaticPathRefreshInterval = std::chrono::seconds(3);

std::string JsonEscape(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 8);
    for (char ch : value) {
        switch (ch) {
        case '\\': out += "\\\\"; break;
        case '"': out += "\\\""; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default: out += ch; break;
        }
    }
    return out;
}

std::string ColorToHexRgb(const platform::config::Color& color) {
    auto clampChannel = [](float value) {
        return std::clamp(static_cast<int>(std::lround(value * 255.0f)), 0, 255);
    };

    char buffer[16];
    std::snprintf(buffer,
                  sizeof(buffer),
                  "%02X%02X%02X",
                  clampChannel(color.r),
                  clampChannel(color.g),
                  clampChannel(color.b));
    return buffer;
}

const char* PositionToJsonString(platform::config::CalcOverlayPosition value) {
    switch (value) {
    case platform::config::CalcOverlayPosition::TopRight:
        return "top right";
    case platform::config::CalcOverlayPosition::BottomLeft:
        return "bottom left";
    case platform::config::CalcOverlayPosition::BottomRight:
        return "bottom right";
    case platform::config::CalcOverlayPosition::TopLeft:
    default:
        return "top left";
    }
}

const char* EyeColumnTypeToJsonString(platform::config::CalcOverlayEyeColumnType value) {
    switch (value) {
    case platform::config::CalcOverlayEyeColumnType::Certainty:
        return "certainty";
    case platform::config::CalcOverlayEyeColumnType::Distance:
        return "distance";
    case platform::config::CalcOverlayEyeColumnType::NetherCoords:
        return "nether coords";
    case platform::config::CalcOverlayEyeColumnType::Angle:
        return "angle";
    case platform::config::CalcOverlayEyeColumnType::OverworldCoords:
    default:
        return "overworld coords";
    }
}

const char* HeaderRowToJsonString(platform::config::CalcOverlayHeaderRow value) {
    switch (value) {
    case platform::config::CalcOverlayHeaderRow::Nothing:
        return "nothing";
    case platform::config::CalcOverlayHeaderRow::Text:
        return "show text";
    case platform::config::CalcOverlayHeaderRow::Icon:
    default:
        return "show icon";
    }
}

const char* OverworldCoordsModeToJsonString(platform::config::CalcOverlayOverworldCoordsMode value) {
    switch (value) {
    case platform::config::CalcOverlayOverworldCoordsMode::EightEight:
        return "(8, 8)";
    case platform::config::CalcOverlayOverworldCoordsMode::FourFour:
        return "(4, 4)";
    case platform::config::CalcOverlayOverworldCoordsMode::Chunk:
    default:
        return "chunk";
    }
}

const char* ClearOverlayTimeUnitToJsonString(platform::config::CalcOverlayClearOverlayTimeUnit value) {
    switch (value) {
    case platform::config::CalcOverlayClearOverlayTimeUnit::Seconds:
        return "seconds";
    case platform::config::CalcOverlayClearOverlayTimeUnit::Minutes:
        return "minutes";
    case platform::config::CalcOverlayClearOverlayTimeUnit::Never:
    default:
        return "never";
    }
}

const char* AngleDisplayModeToJsonString(platform::config::CalcOverlayAngleDisplayMode value) {
    switch (value) {
    case platform::config::CalcOverlayAngleDisplayMode::OnlyAngle:
        return "angle";
    case platform::config::CalcOverlayAngleDisplayMode::OnlyAngleChange:
        return "angle change";
    case platform::config::CalcOverlayAngleDisplayMode::All:
    default:
        return "all";
    }
}

const char* AaColumnTypeToJsonString(platform::config::CalcOverlayAaColumnType value) {
    switch (value) {
    case platform::config::CalcOverlayAaColumnType::Location:
        return "location";
    case platform::config::CalcOverlayAaColumnType::NetherCoords:
        return "nether coords";
    case platform::config::CalcOverlayAaColumnType::Angle:
        return "angle";
    case platform::config::CalcOverlayAaColumnType::Icons:
    default:
        return "icons";
    }
}

const char* AaRowTypeToJsonString(platform::config::CalcOverlayAaRowType value) {
    switch (value) {
    case platform::config::CalcOverlayAaRowType::Spawn:
        return "SPAWN";
    case platform::config::CalcOverlayAaRowType::Outpost:
        return "OUTPOST";
    case platform::config::CalcOverlayAaRowType::Monument:
        return "MONUMENT";
    case platform::config::CalcOverlayAaRowType::Stronghold:
    default:
        return "STRONGHOLD";
    }
}

std::string GetProcessExePath() {
    char pathBuffer[4096];
#ifdef __APPLE__
    uint32_t bufSize = sizeof(pathBuffer);
    if (_NSGetExecutablePath(pathBuffer, &bufSize) != 0) {
        return {};
    }
    char realPath[PATH_MAX];
    if (realpath(pathBuffer, realPath)) {
        return realPath;
    }
    return pathBuffer;
#else
    const ssize_t n = readlink("/proc/self/exe", pathBuffer, sizeof(pathBuffer) - 1);
    if (n <= 0) {
        return {};
    }
    pathBuffer[n] = '\0';
    return pathBuffer;
#endif
}

bool FileMatchesEmbeddedJar(const std::string& path) {
    std::error_code error;
    if (path.empty() || !std::filesystem::is_regular_file(path, error) || error) {
        return false;
    }
    if (std::filesystem::file_size(path, error) != kCalcOverlayHelperJarSize || error) {
        return false;
    }

    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (!file) {
        return false;
    }

    std::vector<unsigned char> buffer(64 * 1024);
    std::size_t compared = 0;
    bool matches = true;
    while (compared < kCalcOverlayHelperJarSize) {
        const std::size_t remaining = kCalcOverlayHelperJarSize - compared;
        const std::size_t toRead = std::min(buffer.size(), remaining);
        if (std::fread(buffer.data(), 1, toRead, file) != toRead) {
            matches = false;
            break;
        }
        if (!std::equal(buffer.begin(),
                        buffer.begin() + static_cast<std::ptrdiff_t>(toRead),
                        kCalcOverlayHelperJarBytes + compared)) {
            matches = false;
            break;
        }
        compared += toRead;
    }
    std::fclose(file);
    return matches && compared == kCalcOverlayHelperJarSize;
}

bool WriteEmbeddedJarFile(const std::string& path) {
    const std::filesystem::path jarPath(path);
    const std::filesystem::path tempPath = jarPath.string() + ".tmp";
    std::error_code error;
    std::filesystem::create_directories(jarPath.parent_path(), error);
    if (error) {
        return false;
    }

    std::FILE* file = std::fopen(tempPath.c_str(), "wb");
    if (!file) {
        return false;
    }

    const std::size_t written = std::fwrite(kCalcOverlayHelperJarBytes, 1, kCalcOverlayHelperJarSize, file);
    const int closeResult = std::fclose(file);
    if (written != kCalcOverlayHelperJarSize || closeResult != 0) {
        std::filesystem::remove(tempPath, error);
        return false;
    }

    std::filesystem::remove(jarPath, error);
    error.clear();
    std::filesystem::rename(tempPath, jarPath, error);
    if (error) {
        std::filesystem::remove(tempPath, error);
        return false;
    }
    return true;
}

bool EnsureEmbeddedHelperJarLocked() {
    if (g_helperJarPrepared && std::filesystem::exists(g_calcOverlayStatus.jarPath)) {
        return true;
    }
    if (FileMatchesEmbeddedJar(g_calcOverlayStatus.jarPath)) {
        g_helperJarPrepared = true;
        return true;
    }
    if (WriteEmbeddedJarFile(g_calcOverlayStatus.jarPath) &&
        FileMatchesEmbeddedJar(g_calcOverlayStatus.jarPath)) {
        g_helperJarPrepared = true;
        return true;
    }
    g_helperJarPrepared = false;
    return false;
}

void RefreshStaticPathsLocked(bool force = false) {
    const auto now = std::chrono::steady_clock::now();
    if (!force &&
        g_lastStaticPathRefresh.time_since_epoch().count() != 0 &&
        now - g_lastStaticPathRefresh < kStaticPathRefreshInterval) {
        return;
    }

    g_calcOverlayStatus.javaPath = GetProcessExePath();
    g_calcOverlayStatus.javaAvailable = !g_calcOverlayStatus.javaPath.empty() &&
                                        std::filesystem::exists(g_calcOverlayStatus.javaPath);

    g_calcOverlayStatus.jarPath = (std::filesystem::path(g_calcOverlayStatus.configDir) / kHelperJarName).string();
    g_calcOverlayStatus.jarAvailable = EnsureEmbeddedHelperJarLocked();

    g_lastStaticPathRefresh = now;
}

std::string BuildSettingsJson(const platform::config::CalcOverlayConfig& cfg) {
    std::ostringstream out;
    out << "{\n";
    out << "  \"calc overlay enabled\": " << (cfg.enabled ? "true" : "false") << ",\n";
    out << "  \"version\": \"" << kHelperVersion << "\",\n";
    out << "  \"overlay position\": \"" << PositionToJsonString(cfg.overlayPosition) << "\",\n";
    out << "  \"font\": {\n";
    out << "    \"name\": \"" << JsonEscape(cfg.fontName) << "\",\n";
    out << "    \"size\": " << cfg.fontSize << "\n";
    out << "  },\n";
    out << "  \"outline width\": " << cfg.outlineWidth << ",\n";
    out << "  \"nether coords color\": \"" << ColorToHexRgb(cfg.netherCoordsColor) << "\",\n";
    out << "  \"negative coords\": {\n";
    out << "    \"use\": " << (cfg.negativeCoords.use ? "true" : "false") << ",\n";
    out << "    \"color\": \"" << ColorToHexRgb(cfg.negativeCoords.color) << "\"\n";
    out << "  },\n";
    out << "  \"clear overlay after\": {\n";
    out << "    \"time unit\": \"" << ClearOverlayTimeUnitToJsonString(cfg.clearOverlayTimeUnit) << "\",\n";
    out << "    \"amount\": " << cfg.clearOverlayAmount << "\n";
    out << "  },\n";
    out << "  \"display overlay\": {\n";
    out << "    \"blind coords\": " << (cfg.blindCoordsEnabled ? "true" : "false") << ",\n";
    out << "    \"all advancements\": " << (cfg.allAdvancementsEnabled ? "true" : "false") << ",\n";
    out << "    \"eye throws\": true\n";
    out << "  },\n";
    out << "  \"columns\": [\n";
    for (std::size_t i = 0; i < cfg.eyeColumns.size(); ++i) {
        const auto& column = cfg.eyeColumns[i];
        out << "    {\n";
        out << "      \"name\": \"" << EyeColumnTypeToJsonString(column.type) << "\",\n";
        out << "      \"header row\": \"" << HeaderRowToJsonString(column.headerRow) << "\",\n";
        out << "      \"visible\": " << (column.visible ? "true" : "false") << "\n";
        out << "    }" << (i + 1 < cfg.eyeColumns.size() ? "," : "") << "\n";
    }
    out << "  ],\n";
    out << "  \"angle display\": \"" << AngleDisplayModeToJsonString(cfg.angleDisplay) << "\",\n";
    out << "  \"show coords based on dimension\": " << (cfg.onlyShowCurrentDimensionCoords ? "true" : "false") << ",\n";
    out << "  \"show information bar\": " << (cfg.showInfoBar ? "true" : "false") << ",\n";
    out << "  \"overworld coords\": \"" << OverworldCoordsModeToJsonString(cfg.overworldCoordsMode) << "\",\n";
    out << "  \"shown measurements\": " << cfg.shownMeasurements << ",\n";
    out << "  \"show direction and distance to blind coords\": " << (cfg.showDirectionAndDistance ? "true" : "false") << ",\n";
    out << "  \"all advancements\": {\n";
    out << "    \"columns\": [\n";
    for (std::size_t i = 0; i < cfg.allAdvancements.columns.size(); ++i) {
        const auto& column = cfg.allAdvancements.columns[i];
        out << "      {\n";
        out << "        \"name\": \"" << AaColumnTypeToJsonString(column.type) << "\",\n";
        out << "        \"header row\": \"" << HeaderRowToJsonString(column.headerRow) << "\",\n";
        out << "        \"visible\": " << (column.visible ? "true" : "false") << "\n";
        out << "      }" << (i + 1 < cfg.allAdvancements.columns.size() ? "," : "") << "\n";
    }
    out << "    ],\n";
    out << "    \"rows\": [\n";
    for (std::size_t i = 0; i < cfg.allAdvancements.rows.size(); ++i) {
        const auto& row = cfg.allAdvancements.rows[i];
        out << "      {\n";
        out << "        \"name\": \"" << AaRowTypeToJsonString(row.type) << "\",\n";
        out << "        \"visible\": " << (row.visible ? "true" : "false") << "\n";
        out << "      }" << (i + 1 < cfg.allAdvancements.rows.size() ? "," : "") << "\n";
    }
    out << "    ]\n";
    out << "  }\n";
    out << "}\n";
    return out.str();
}

bool WriteTextFile(const std::string& path, const std::string& content) {
    std::FILE* file = std::fopen(path.c_str(), "wb");
    if (!file) {
        return false;
    }
    const std::size_t written = std::fwrite(content.data(), 1, content.size(), file);
    std::fclose(file);
    return written == content.size();
}

void PollProcessStateLocked() {
    if (g_calcOverlayStatus.pid <= 0) {
        g_calcOverlayStatus.running = false;
        return;
    }

    int status = 0;
    const pid_t waitResult = waitpid(g_calcOverlayStatus.pid, &status, WNOHANG);
    if (waitResult == 0) {
        g_calcOverlayStatus.running = true;
        return;
    }

    if (waitResult == g_calcOverlayStatus.pid) {
        g_calcOverlayStatus.running = false;
        g_calcOverlayStatus.hadUnexpectedExit = !g_stopRequested;
        if (WIFEXITED(status)) {
            g_calcOverlayStatus.lastExitCode = WEXITSTATUS(status);
            g_calcOverlayStatus.message = "Calc Overlay helper exited with code " + std::to_string(g_calcOverlayStatus.lastExitCode) + ".";
        } else if (WIFSIGNALED(status)) {
            g_calcOverlayStatus.lastExitCode = 128 + WTERMSIG(status);
            g_calcOverlayStatus.message = "Calc Overlay helper was terminated by signal " + std::to_string(WTERMSIG(status)) + ".";
        }
        g_calcOverlayStatus.pid = -1;
        g_stopRequested = false;
    }
}

void RequestStopLocked() {
    if (g_calcOverlayStatus.pid <= 0) {
        g_calcOverlayStatus.running = false;
        return;
    }
    g_stopRequested = true;
    kill(g_calcOverlayStatus.pid, SIGTERM);
}

bool StartProcessLocked() {
    std::filesystem::create_directories(g_calcOverlayStatus.configDir);

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);

    const int logFd = open(g_calcOverlayStatus.logPath.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (logFd >= 0) {
        posix_spawn_file_actions_adddup2(&actions, logFd, STDOUT_FILENO);
        posix_spawn_file_actions_adddup2(&actions, logFd, STDERR_FILENO);
        posix_spawn_file_actions_addclose(&actions, logFd);
    }

    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
#ifdef __APPLE__
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP);
    posix_spawnattr_setpgroup(&attr, 0);
#endif

    const std::string calcOverlayHomeEnv = "CALC_OVERLAY_HOME=" + g_calcOverlayStatus.configDir;
    std::vector<char*> childEnv;
    for (char** env = environ; env && *env; ++env) {
        if (std::strncmp(*env, "CALC_OVERLAY_HOME=", 18) == 0) {
            continue;
        }
        childEnv.push_back(*env);
    }
    childEnv.push_back(const_cast<char*>(calcOverlayHomeEnv.c_str()));
    childEnv.push_back(nullptr);

    std::vector<std::string> argStrings = {
        g_calcOverlayStatus.javaPath,
#ifdef __APPLE__
        "-Djava.awt.headless=true",
#endif
        "-jar",
        g_calcOverlayStatus.jarPath,
        "--headless",
    };
    std::vector<char*> argv;
    argv.reserve(argStrings.size() + 1);
    for (auto& arg : argStrings) {
        argv.push_back(arg.data());
    }
    argv.push_back(nullptr);

    pid_t childPid = -1;
    const int spawnResult = posix_spawn(&childPid,
                                        g_calcOverlayStatus.javaPath.c_str(),
                                        &actions,
                                        &attr,
                                        argv.data(),
                                        childEnv.data());
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attr);
    if (logFd >= 0) {
        close(logFd);
    }
    if (spawnResult != 0) {
        g_calcOverlayStatus.message = "Failed to start Calc Overlay helper: " + std::string(std::strerror(spawnResult));
        g_calcOverlayStatus.running = false;
        g_calcOverlayStatus.pid = -1;
        return false;
    }

    g_calcOverlayStatus.pid = static_cast<int>(childPid);
    g_calcOverlayStatus.running = true;
    g_calcOverlayStatus.hadUnexpectedExit = false;
    g_calcOverlayStatus.message = "Calc Overlay helper is running.";
    g_lastStartAttempt = std::chrono::steady_clock::now();
    g_stopRequested = false;
    return true;
}

} // namespace

std::string GetCalcOverlayConfigDirectoryPath() {
    return (std::filesystem::path(platform::config::GetConfigDirectoryPath()) / "calc-overlay").string();
}

std::string GetCalcOverlaySettingsPath() {
    return (std::filesystem::path(GetCalcOverlayConfigDirectoryPath()) / "settings.json").string();
}

std::string GetCalcOverlayImagePath() {
    return (std::filesystem::path(GetCalcOverlayConfigDirectoryPath()) / "calc-overlay.png").string();
}

std::string GetCalcOverlayLogPath() {
    return (std::filesystem::path(GetCalcOverlayConfigDirectoryPath()) / "runtime.log").string();
}

CalcOverlayRuntimeStatus GetCalcOverlayRuntimeStatus() {
    std::lock_guard<std::mutex> lock(g_calcOverlayMutex);
    return g_calcOverlayStatus;
}

void UpdateCalcOverlayRuntime(const platform::config::LinuxscreenConfig& config) {
    std::lock_guard<std::mutex> lock(g_calcOverlayMutex);

    g_calcOverlayStatus.enabled = config.calcOverlay.enabled;
    g_calcOverlayStatus.configDir = GetCalcOverlayConfigDirectoryPath();
    g_calcOverlayStatus.settingsPath = GetCalcOverlaySettingsPath();
    g_calcOverlayStatus.imagePath = GetCalcOverlayImagePath();
    g_calcOverlayStatus.logPath = GetCalcOverlayLogPath();
    RefreshStaticPathsLocked();

    PollProcessStateLocked();

    const std::string desiredSettingsJson = BuildSettingsJson(config.calcOverlay);
    if (desiredSettingsJson != g_lastWrittenSettingsJson) {
        std::filesystem::create_directories(g_calcOverlayStatus.configDir);
        if (WriteTextFile(g_calcOverlayStatus.settingsPath, desiredSettingsJson)) {
            g_lastWrittenSettingsJson = desiredSettingsJson;
        } else {
            g_calcOverlayStatus.message = "Failed to write Calc Overlay settings.json.";
        }
    }

    if (!config.calcOverlay.enabled) {
        RequestStopLocked();
        if (g_calcOverlayStatus.pid <= 0) {
            g_calcOverlayStatus.message = "Calc Overlay is disabled.";
        }
        return;
    }

    if (!g_calcOverlayStatus.javaAvailable) {
        RefreshStaticPathsLocked(true);
        g_calcOverlayStatus.message = "Could not resolve the game's Java executable.";
        return;
    }
    if (!g_calcOverlayStatus.jarAvailable) {
        RefreshStaticPathsLocked(true);
        g_calcOverlayStatus.message = "Failed to extract Calc Overlay helper jar to " + g_calcOverlayStatus.jarPath + ".";
        return;
    }

    if (g_calcOverlayStatus.pid > 0) {
        g_calcOverlayStatus.running = true;
        g_calcOverlayStatus.message = "Calc Overlay helper is running.";
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    if (g_lastStartAttempt.time_since_epoch().count() != 0 &&
        now - g_lastStartAttempt < std::chrono::seconds(1)) {
        return;
    }

    StartProcessLocked();
}

void ShutdownCalcOverlayRuntime() {
    std::lock_guard<std::mutex> lock(g_calcOverlayMutex);
    RequestStopLocked();
    PollProcessStateLocked();
}

} // namespace platform::x11
