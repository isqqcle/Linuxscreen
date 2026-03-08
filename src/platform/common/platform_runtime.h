#pragma once

#include <cstdint>
#include <string>

namespace platform {

enum class PlatformBackend : std::uint8_t {
    Unknown = 0,
    X11 = 1,
};

enum class RuntimeState : std::uint8_t {
    Uninitialized = 0,
    Initialized = 1,
    HooksInstalled = 2,
    ShuttingDown = 3,
    Failed = 4,
};

struct BootstrapConfig {
    bool enableSwapHook = true;
    bool enableViewportHook = true;
};

struct RuntimeHandles {
    void* nativeDisplay = nullptr;
    std::uint64_t nativeWindow = 0;
    void* glContext = nullptr;
};

bool Initialize(const BootstrapConfig& config);
bool InstallHooks();
void Shutdown();
RuntimeHandles GetRuntimeHandles();
PlatformBackend GetBackend();
RuntimeState GetRuntimeState();
std::string GetLastErrorMessage();

} // namespace platform
