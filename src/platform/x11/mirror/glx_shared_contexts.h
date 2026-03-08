#pragma once

#include <cstdint>
#include <string>

namespace platform::x11 {

struct GlxSharedContextHandles {
    void* nativeDisplay = nullptr;
    void* renderContext = nullptr;
    std::uint64_t renderDrawable = 0;
    void* mirrorContext = nullptr;
    std::uint64_t mirrorDrawable = 0;
};

enum class GlxSharedContextRole : std::uint8_t {
    Render = 0,
    Mirror = 1,
};

struct GlxContextRestoreState {
    void* display = nullptr;
    std::uint64_t drawDrawable = 0;
    std::uint64_t readDrawable = 0;
    void* context = nullptr;
    bool valid = false;
};

bool EnsureSharedGlxContexts(void* nativeDisplay, std::uint64_t drawable, void* gameContext);
bool AreSharedGlxContextsReady();
GlxSharedContextHandles GetSharedGlxContextHandles();
std::string GetSharedGlxContextLastError();
std::string GetSharedGlxContextLastInitInfo();
std::uint64_t GetSharedGlxContextGeneration();
bool MakeSharedGlxContextCurrent(GlxSharedContextRole role, GlxContextRestoreState& outRestore);
bool RestoreGlxContext(const GlxContextRestoreState& restore);
void ShutdownSharedGlxContexts();
void ShutdownSharedGlxContextsForProcessExit();

} // namespace platform::x11
