#include "game_state_monitor.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdarg>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <exception>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

namespace platform::config {

namespace {

enum class GameStateEquivalenceClass {
    Exact,
    InWorldCursorGrabbed,
    InWorldCursorFree,
};

std::mutex g_monitorMutex;
std::thread g_monitorThread;
bool g_monitorStarted = false;
std::atomic<bool> g_stopRequested{ false };
std::atomic<bool> g_monitorAvailable{ false };
std::atomic<bool> g_loggedMissingFileWarning{ false };
std::string g_stateFilePath;

std::string g_gameStateBuffers[2] = { "title", "title" };
std::atomic<int> g_currentGameStateIndex{ 0 };

GameStateEquivalenceClass GetGameStateEquivalenceClass(std::string_view state) {
    if (state == "inworld,cursor_grabbed" || state == "inworld,unpaused") {
        return GameStateEquivalenceClass::InWorldCursorGrabbed;
    }
    if (state == "inworld,cursor_free" || state == "inworld,paused" || state == "inworld,gamescreenopen") {
        return GameStateEquivalenceClass::InWorldCursorFree;
    }
    return GameStateEquivalenceClass::Exact;
}

bool IsDebugEnabled() {
    static bool checked = false;
    static bool debugEnabled = false;
    if (!checked) {
        const char* env = std::getenv("LINUXSCREEN_X11_DEBUG");
        debugEnabled = env && (env[0] == '1' || env[0] == 'y' || env[0] == 'Y' || env[0] == 't' || env[0] == 'T');
        checked = true;
    }
    return debugEnabled;
}

void LogFormatted(const char* prefix, const char* format, va_list args) {
    std::fprintf(stderr, "%s", prefix);
    std::vfprintf(stderr, format, args);
    std::fprintf(stderr, "\n");
}

void LogAlways(const char* format, ...) {
    va_list args;
    va_start(args, format);
    LogFormatted("[Linuxscreen][game-state] ", format, args);
    va_end(args);
}

void LogWarning(const char* format, ...) {
    va_list args;
    va_start(args, format);
    LogFormatted("[Linuxscreen][game-state][WARNING] ", format, args);
    va_end(args);
}

void LogDebug(const char* format, ...) {
    if (!IsDebugEnabled()) {
        return;
    }

    va_list args;
    va_start(args, format);
    LogFormatted("[Linuxscreen][game-state][debug] ", format, args);
    va_end(args);
}

std::string ResolveStateFilePath() {
    std::array<char, 4096> cwd{};
    if (!getcwd(cwd.data(), cwd.size())) {
        return std::string{};
    }
    return std::string(cwd.data()) + "/wpstateout.txt";
}

void TrimAsciiWhitespace(std::string& value) {
    std::size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin]))) {
        ++begin;
    }

    std::size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }

    if (begin > 0) {
        value.erase(0, begin);
        end -= begin;
    }
    if (end < value.size()) {
        value.resize(end);
    }
}

bool IsValidStateContent(const std::string& content) {
    static const std::array<const char*, 8> kValidStates = {
        "wall",
        "inworld,cursor_free",
        "inworld,cursor_grabbed",
        "inworld,unpaused",
        "inworld,paused",
        "inworld,gamescreenopen",
        "title",
        "waiting",
    };

    if (content.rfind("generating", 0) == 0) {
        return true;
    }

    for (const char* state : kValidStates) {
        if (content == state) {
            return true;
        }
    }
    return false;
}

bool ReadStateFileContent(const std::string& path, std::string& outContent) {
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (!file) {
        return false;
    }

    std::array<char, 128> buffer{};
    const std::size_t bytesRead = std::fread(buffer.data(), 1, buffer.size() - 1, file);
    const bool readFailed = std::ferror(file) != 0;
    std::fclose(file);
    if (readFailed) {
        return false;
    }

    outContent.assign(buffer.data(), bytesRead);
    TrimAsciiWhitespace(outContent);
    return true;
}

void PublishGameState(const std::string& value) {
    const int currentIndex = g_currentGameStateIndex.load(std::memory_order_acquire);
    if (g_gameStateBuffers[currentIndex] == value) {
        return;
    }

    const int nextIndex = 1 - currentIndex;
    g_gameStateBuffers[nextIndex] = value;
    g_currentGameStateIndex.store(nextIndex, std::memory_order_release);
    LogDebug("state transition -> %s", value.c_str());
}

void MonitorThreadMain(std::string path) {
    LogAlways("monitor started (path=%s)", path.c_str());

    std::string content;
    content.reserve(128);

    struct timespec lastWriteTime {};
    bool haveLastWriteTime = false;
    int sleepMs = 16;
    int consecutiveNoChange = 0;

    while (!g_stopRequested.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
        if (g_stopRequested.load(std::memory_order_acquire)) {
            break;
        }

        struct stat stateFileStat {};
        if (::stat(path.c_str(), &stateFileStat) != 0 || !S_ISREG(stateFileStat.st_mode)) {
            g_monitorAvailable.store(false, std::memory_order_release);
            continue;
        }

        const bool unchanged =
            haveLastWriteTime && stateFileStat.st_mtim.tv_sec == lastWriteTime.tv_sec && stateFileStat.st_mtim.tv_nsec == lastWriteTime.tv_nsec;
        if (unchanged) {
            ++consecutiveNoChange;
            if (consecutiveNoChange > 600) {
                sleepMs = 100;
            } else if (consecutiveNoChange > 180) {
                sleepMs = 50;
            } else if (consecutiveNoChange > 60) {
                sleepMs = 33;
            }
            continue;
        }

        lastWriteTime = stateFileStat.st_mtim;
        haveLastWriteTime = true;
        consecutiveNoChange = 0;
        sleepMs = 16;

        content.clear();
        if (!ReadStateFileContent(path, content)) {
            g_monitorAvailable.store(false, std::memory_order_release);
            continue;
        }

        if (!IsValidStateContent(content)) {
            continue;
        }

        g_monitorAvailable.store(true, std::memory_order_release);
        PublishGameState(content);
    }

    LogAlways("monitor stopped");
}

} // namespace

bool MatchesGameStateCondition(std::string_view configuredState, std::string_view currentState) {
    if (configuredState == currentState) {
        return true;
    }

    const GameStateEquivalenceClass configuredClass = GetGameStateEquivalenceClass(configuredState);
    if (configuredClass == GameStateEquivalenceClass::Exact) {
        return false;
    }

    return configuredClass == GetGameStateEquivalenceClass(currentState);
}

bool HasMatchingGameStateCondition(const std::vector<std::string>& configuredStates, std::string_view currentState) {
    for (const auto& configuredState : configuredStates) {
        if (MatchesGameStateCondition(configuredState, currentState)) {
            return true;
        }
    }
    return false;
}

void RemoveMatchingGameStateConditions(std::vector<std::string>& configuredStates, std::string_view gameState) {
    configuredStates.erase(
        std::remove_if(configuredStates.begin(),
                       configuredStates.end(),
                       [&](const std::string& configuredState) {
                           return MatchesGameStateCondition(configuredState, gameState);
                       }),
        configuredStates.end());
}

void StartGameStateMonitor() {
    std::lock_guard<std::mutex> lock(g_monitorMutex);
    if (g_monitorStarted) {
        return;
    }

    g_monitorStarted = true;
    g_stopRequested.store(false, std::memory_order_release);
    g_stateFilePath = ResolveStateFilePath();

    if (g_stateFilePath.empty()) {
        g_monitorAvailable.store(false, std::memory_order_release);
        LogWarning("failed to resolve current working directory for wpstateout.txt; monitor unavailable");
        return;
    }

    LogAlways("starting monitor (path=%s)", g_stateFilePath.c_str());

    struct stat stateFileStat {};
    if (::stat(g_stateFilePath.c_str(), &stateFileStat) != 0 || !S_ISREG(stateFileStat.st_mode)) {
        g_monitorAvailable.store(false, std::memory_order_release);
        bool expected = false;
        if (g_loggedMissingFileWarning.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            LogWarning("wpstateout.txt not found at %s; game-state restrictions will be bypassed", g_stateFilePath.c_str());
        }
        return;
    }

    g_monitorAvailable.store(true, std::memory_order_release);

    std::string initialContent;
    initialContent.reserve(128);
    if (ReadStateFileContent(g_stateFilePath, initialContent) && IsValidStateContent(initialContent)) {
        PublishGameState(initialContent);
    }

    try {
        g_monitorThread = std::thread(MonitorThreadMain, g_stateFilePath);
    } catch (const std::exception& e) {
        g_monitorAvailable.store(false, std::memory_order_release);
        g_monitorStarted = false;
        LogWarning("failed to create game-state monitor thread: %s", e.what());
    }
}

void StopGameStateMonitor() {
    std::thread monitorThread;
    bool wasStarted = false;

    {
        std::lock_guard<std::mutex> lock(g_monitorMutex);
        if (g_monitorStarted) {
            wasStarted = true;
            g_stopRequested.store(true, std::memory_order_release);
            if (g_monitorThread.joinable()) {
                monitorThread = std::move(g_monitorThread);
            }
            g_monitorStarted = false;
        }
    }

    if (monitorThread.joinable()) {
        monitorThread.join();
    }

    g_monitorAvailable.store(false, std::memory_order_release);
    if (wasStarted) {
        LogAlways("stop requested");
    }
}

std::string GetCurrentGameState() {
    if (!g_monitorAvailable.load(std::memory_order_acquire)) {
        return std::string{};
    }

    const int index = g_currentGameStateIndex.load(std::memory_order_acquire);
    if (index < 0 || index > 1) {
        return std::string{};
    }

    return g_gameStateBuffers[index];
}

bool IsGameStateMonitorAvailable() {
    return g_monitorAvailable.load(std::memory_order_acquire);
}

} // namespace platform::config
