void DoSaveConfig(const LinuxscreenConfig& cfg, const std::string& path) {
    try {
        std::filesystem::path fsPath(path);
        std::filesystem::path parentPath = fsPath.parent_path();
        if (!parentPath.empty() && !std::filesystem::exists(parentPath)) {
            std::filesystem::create_directories(parentPath);
        }
        
        auto tbl = LinuxscreenConfigToToml(cfg);
        std::ofstream file(path);
        if (file) {
            file << tbl;
            LogDebug("Config saved to %s", path.c_str());
        } else {
            LogWarning("Failed to open config file for writing: %s", path.c_str());
        }
    } catch (const std::exception& e) {
        LogWarning("Failed to save config: %s", e.what());
    }
}

void SaveThreadMain() {
    while (true) {
        std::unique_lock<std::mutex> lock(g_saveMutex);
        
        g_saveCV.wait(lock, []() {
            return g_savePending || !g_saveThreadRunning.load(std::memory_order_acquire);
        });
        
        if (!g_saveThreadRunning.load(std::memory_order_acquire) && !g_savePending) {
            break;
        }
        
        if (g_savePending) {
            auto configToSave = std::move(g_pendingConfig);
            g_savePending = false;
            
            auto now = std::chrono::steady_clock::now();
            auto nextAllowedSave = g_lastSaveTime + kSaveThrottleMs;
            
            if (now < nextAllowedSave) {
                // Wait until we're allowed to save again. If the user creates more slider changes
                // while we wait, we abort this stale config, scoop up the newest one, and reset the wait.
                bool interrupted = g_saveCV.wait_until(lock, nextAllowedSave, []() {
                    return g_savePending || !g_saveThreadRunning.load(std::memory_order_acquire);
                });
                
                if (!g_saveThreadRunning.load(std::memory_order_acquire) && !g_savePending) {
                    break;
                }
                
                if (interrupted && g_savePending) {
                    // We received a newer config snapshot while waiting! Discard the current one and loop
                    continue;
                }
            }

            lock.unlock();
            
            DoSaveConfig(configToSave, GetConfigPathInternal());
            
            lock.lock();
            g_lastSaveTime = std::chrono::steady_clock::now();
        }
    }
    
    std::lock_guard<std::mutex> lock(g_saveMutex);
    if (g_savePending) {
        DoSaveConfig(g_pendingConfig, GetConfigPathInternal());
        g_savePending = false;
    }
}

void EnsureSaveThreadStarted() {
    std::call_once(g_saveThreadOnce, []() {
        g_saveThreadRunning.store(true, std::memory_order_release);
        g_saveThread = std::thread([]() { SaveThreadMain(); });
    });
}
