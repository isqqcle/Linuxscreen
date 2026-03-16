#include "config_io.h"

#include "config_toml.h"
#include "../font_scanner.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>

#include <toml.hpp>

namespace platform::config {

static const char kDefaultConfigToml[] = R"(
configVersion = 1
defaultMode = ""
guiHotkey = []
rebindToggleHotkey = []

[keyRebinds]
enabled = false
)";

static const char kEmbeddedDefaultConfigToml[] =
#include "default.inc"
;

namespace {

static std::mutex g_configMutex;
static std::shared_ptr<const LinuxscreenConfig> g_config;
static std::atomic<uint64_t> g_configSnapshotVersion{0};

static std::mutex g_saveMutex;
static std::condition_variable g_saveCV;
static bool g_savePending = false;
static LinuxscreenConfig g_pendingConfig;
static std::chrono::steady_clock::time_point g_lastSaveTime;
static constexpr auto kSaveThrottleMs = std::chrono::milliseconds(1000);
static std::thread g_saveThread;
static std::atomic<bool> g_saveThreadRunning{false};
static std::once_flag g_saveThreadOnce;

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

void MergeTomlTables(toml::table& base, const toml::table& overlay) {
    for (const auto& [key, value] : overlay) {
        if (const toml::table* overlayTable = value.as_table()) {
            if (toml::node* existing = base.get(key.str())) {
                if (toml::table* existingTable = existing->as_table()) {
                    MergeTomlTables(*existingTable, *overlayTable);
                    continue;
                }
            }
        }

        base.insert_or_assign(key.str(), value);
    }
}

std::string PickDefaultGuiFont() {
    static constexpr const char* kPreferredFonts[] = {
#ifdef __APPLE__
        "SFNSRounded",
        "Arial",
        "Trebuchet MS",
        "SFNSMono",
        "SFNS",
#endif
        "NotoSans-Regular",
        "OpenSans-Regular",
        "DroidSans",
        "Montserrat-Regular",
        "Monserrat-Regular",
        "SourceCodePro-Regular",
    };

    const auto discoveredFonts = platform::common::ScanForFonts();
    for (const char* fontName : kPreferredFonts) {
        if (discoveredFonts.find(fontName) != discoveredFonts.end()) {
            return fontName;
        }
    }

    return "";
}

LinuxscreenConfig LoadDefaultConfig() {
    try {
        auto merged = toml::parse(kDefaultConfigToml);
        try {
            auto embeddedDefault = toml::parse(kEmbeddedDefaultConfigToml);
            MergeTomlTables(merged, embeddedDefault);
            LogDebug("Applied embedded default config");
        } catch (const std::exception& e) {
            LogWarning("Failed to parse embedded default config: %s", e.what());
        }

        LinuxscreenConfig cfg = LinuxscreenConfigFromToml(merged);
        if (cfg.guiFontPath.empty()) {
            cfg.guiFontPath = PickDefaultGuiFont();
        }

        return cfg;
    } catch (const std::exception& e) {
        LogWarning("Failed to parse embedded default config: %s", e.what());
        LinuxscreenConfig cfg;
        cfg.configVersion = 1;
        return cfg;
    }
}

std::string GetConfigPathInternal();
