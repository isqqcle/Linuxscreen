#include "io_detail.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>

namespace platform::config::detail {

std::mutex g_configMutex;
std::shared_ptr<const LinuxscreenConfig> g_config;
std::atomic<uint64_t> g_configSnapshotVersion{0};

std::mutex g_saveMutex;
std::condition_variable g_saveCV;
bool g_savePending = false;
LinuxscreenConfig g_pendingConfig;
std::chrono::steady_clock::time_point g_lastSaveTime;
std::thread g_saveThread;
std::atomic<bool> g_saveThreadRunning{false};
std::once_flag g_saveThreadOnce;

bool IsDebugEnabled() {
    static bool checked = false;
    static bool debug = false;
    if (!checked) {
        const char* env = std::getenv("LINUXSCREEN_X11_DEBUG");
        debug = env && (env[0] == '1' || env[0] == 'y' || env[0] == 'Y' || env[0] == 't' || env[0] == 'T');
        checked = true;
    }
    return debug;
}

void LogDebug(const char* format, ...) {
    if (!IsDebugEnabled()) return;
    va_list args;
    va_start(args, format);
    std::fprintf(stderr, "[Linuxscreen][config] ");
    std::vfprintf(stderr, format, args);
    std::fprintf(stderr, "\n");
    va_end(args);
}

void LogWarning(const char* format, ...) {
    va_list args;
    va_start(args, format);
    std::fprintf(stderr, "[Linuxscreen][config][WARNING] ");
    std::vfprintf(stderr, format, args);
    std::fprintf(stderr, "\n");
    va_end(args);
}

} // namespace platform::config::detail
