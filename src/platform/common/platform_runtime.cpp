#include "platform_runtime.h"

#include "game_state_monitor.h"

#if defined(__linux__) || defined(__APPLE__)
#include "../x11/x11_runtime.h"
#endif

#include <atomic>
#include <mutex>
#include <string>

namespace platform {

namespace {
std::atomic<PlatformBackend> g_backend{ PlatformBackend::Unknown };
std::atomic<RuntimeState> g_runtimeState{ RuntimeState::Uninitialized };
std::mutex g_lastErrorMutex;
std::string g_lastErrorMessage;

void SetLastErrorMessage(const std::string& message) {
    std::lock_guard<std::mutex> lock(g_lastErrorMutex);
    g_lastErrorMessage = message;
}

void ClearLastErrorMessage() {
    std::lock_guard<std::mutex> lock(g_lastErrorMutex);
    g_lastErrorMessage.clear();
}
}

bool Initialize(const BootstrapConfig& config) {
    ClearLastErrorMessage();

#if defined(__linux__) || defined(__APPLE__)
    if (!x11::Initialize(config)) {
        g_backend.store(PlatformBackend::Unknown, std::memory_order_release);
        g_runtimeState.store(RuntimeState::Failed, std::memory_order_release);
        SetLastErrorMessage(x11::GetLastErrorMessage());
        return false;
    }

    g_backend.store(PlatformBackend::X11, std::memory_order_release);
    g_runtimeState.store(x11::GetRuntimeState(), std::memory_order_release);
    return true;
#else
    (void)config;
    g_backend.store(PlatformBackend::Unknown, std::memory_order_release);
    g_runtimeState.store(RuntimeState::Failed, std::memory_order_release);
    SetLastErrorMessage("Platform initialize failed: non-Linux build has no X11 runtime");
    return false;
#endif
}

bool InstallHooks() {
#if defined(__linux__) || defined(__APPLE__)
    if (g_backend.load(std::memory_order_acquire) != PlatformBackend::X11) {
        g_runtimeState.store(RuntimeState::Failed, std::memory_order_release);
        SetLastErrorMessage("InstallHooks failed: X11 backend not initialized");
        return false;
    }

    if (!x11::InstallHooks()) {
        g_runtimeState.store(x11::GetRuntimeState(), std::memory_order_release);
        SetLastErrorMessage(x11::GetLastErrorMessage());
        return false;
    }

    g_runtimeState.store(x11::GetRuntimeState(), std::memory_order_release);
    return true;
#endif

    g_runtimeState.store(RuntimeState::Failed, std::memory_order_release);
    SetLastErrorMessage("InstallHooks failed: platform backend unavailable");
    return false;
}

void Shutdown() {
    g_runtimeState.store(RuntimeState::ShuttingDown, std::memory_order_release);

    config::StopGameStateMonitor();

#if defined(__linux__) || defined(__APPLE__)
    if (g_backend.load(std::memory_order_acquire) == PlatformBackend::X11) {
        x11::Shutdown();
        g_runtimeState.store(x11::GetRuntimeState(), std::memory_order_release);
    } else {
        g_runtimeState.store(RuntimeState::Uninitialized, std::memory_order_release);
    }
#else
    g_runtimeState.store(RuntimeState::Uninitialized, std::memory_order_release);
#endif

    ClearLastErrorMessage();
    g_backend.store(PlatformBackend::Unknown, std::memory_order_release);
}

RuntimeHandles GetRuntimeHandles() {
#if defined(__linux__) || defined(__APPLE__)
    if (g_backend.load(std::memory_order_acquire) == PlatformBackend::X11) { return x11::GetRuntimeHandles(); }
#endif
    return RuntimeHandles{};
}

PlatformBackend GetBackend() { return g_backend.load(std::memory_order_acquire); }

RuntimeState GetRuntimeState() { return g_runtimeState.load(std::memory_order_acquire); }

std::string GetLastErrorMessage() {
    std::lock_guard<std::mutex> lock(g_lastErrorMutex);
    return g_lastErrorMessage;
}

} // namespace platform
