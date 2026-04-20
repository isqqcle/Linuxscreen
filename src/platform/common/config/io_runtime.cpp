LinuxscreenConfig LoadLinuxscreenConfig() {
    const std::string path = GetConfigPath();
    
    std::filesystem::path fsPath(path);
    if (!std::filesystem::exists(fsPath)) {
        LogDebug("Config file not found at %s, using default", path.c_str());
        return LoadDefaultConfig();
    }
    
    try {
        auto parsed = toml::parse_file(path);
        LinuxscreenConfig cfg = LinuxscreenConfigFromToml(parsed);
        
        bool defaultModeValid = false;
        if (!cfg.defaultMode.empty()) {
            for (const auto& mode : cfg.modes) {
                if (mode.name == cfg.defaultMode) {
                    defaultModeValid = true;
                    break;
                }
            }
        }
        
        if (!defaultModeValid && !cfg.modes.empty()) {
            for (const auto& mode : cfg.modes) {
                if (!mode.name.empty()) {
                    cfg.defaultMode = mode.name;
                    LogDebug("Default mode not set or invalid, using '%s'", cfg.defaultMode.c_str());
                    break;
                }
            }
        }
        
        LogDebug("Config loaded from %s (%zu mirrors, %zu modes, %zu hotkeys)",
                 path.c_str(), cfg.mirrors.size(), cfg.modes.size(), cfg.hotkeys.size());

        LoadThemeFile(cfg.guiTheme, cfg.guiCustomColors);

        return cfg;
    } catch (const std::exception& e) {
        LogWarning("Failed to parse config file %s: %s. Using default.", path.c_str(), e.what());
        return LoadDefaultConfig();
    }
}

void SaveLinuxscreenConfig(const LinuxscreenConfig& cfg) {
    EnsureSaveThreadStarted();

    const std::string targetPath = GetConfigPath();
    std::lock_guard<std::mutex> lock(g_saveMutex);
    g_pendingConfig = cfg;
    g_pendingConfigPath = targetPath;
    g_savePending = true;
    g_saveCV.notify_one();
}

void SaveLinuxscreenConfigImmediate(const LinuxscreenConfig& cfg) {
    SaveLinuxscreenConfigAtPathImmediate(cfg, GetConfigPath());
}

void SaveLinuxscreenConfigAtPathImmediate(const LinuxscreenConfig& cfg, const std::string& path) {
    WaitForConfigSaveQueueIdle(3000);
    DoSaveConfig(cfg, path);
}

void WaitForConfigSaveQueueIdle(std::uint64_t timeoutMs) {
    const auto timeout = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < timeout) {
        bool pending = false;
        {
            std::lock_guard<std::mutex> lock(g_saveMutex);
            pending = g_savePending;
        }
        if (!pending && !g_saveInProgress.load(std::memory_order_acquire)) {
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

void ShutdownConfigSaveThread() {
    std::thread saveThread;

    {
        std::lock_guard<std::mutex> lock(g_saveMutex);
        g_saveThreadRunning.store(false, std::memory_order_release);
        if (g_saveThread.joinable()) {
            saveThread = std::move(g_saveThread);
        }
    }

    g_saveCV.notify_all();

    if (saveThread.joinable()) {
        saveThread.join();
    }
}

void PublishConfigSnapshot(LinuxscreenConfig cfg) {
    auto p = std::make_shared<const LinuxscreenConfig>(std::move(cfg));
    std::lock_guard<std::mutex> lock(g_configMutex);
    g_config = std::move(p);
    g_configSnapshotVersion.fetch_add(1, std::memory_order_relaxed);
}

uint64_t GetConfigSnapshotVersion() {
    return g_configSnapshotVersion.load(std::memory_order_relaxed);
}

std::shared_ptr<const LinuxscreenConfig> GetConfigSnapshot() {
    std::lock_guard<std::mutex> lock(g_configMutex);
    return g_config;
}
