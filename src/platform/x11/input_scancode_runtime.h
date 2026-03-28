#pragma once

#include "gl_context_runtime.h"

#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <mutex>

namespace platform::x11 {

inline bool IsWaylandGlfwPlatform() {
#ifdef __APPLE__
    return false;
#else
    using GlfwGetPlatformFn = int (*)();
    constexpr int kGlfwPlatformX11 = 0x00060004;
    constexpr int kGlfwPlatformWayland = 0x00060005;

    static std::once_flag once;
    static bool isWaylandPlatform = false;
    std::call_once(once, []() {
        GlfwGetPlatformFn getPlatform = reinterpret_cast<GlfwGetPlatformFn>(dlsym(RTLD_DEFAULT, "glfwGetPlatform"));
        if (!getPlatform) {
            isWaylandPlatform = false;
            return;
        }

        const int platform = getPlatform();
        isWaylandPlatform = (platform == kGlfwPlatformWayland);
        if (platform == kGlfwPlatformX11) {
            isWaylandPlatform = false;
        }
    });

    return isWaylandPlatform;
#endif
}

inline bool ShouldTranslateGlfwScanCodesForLinuxBindings() {
#ifdef __APPLE__
    return false;
#else
    if (IsWaylandGlfwPlatform()) {
        return true;
    }

    const CurrentGlBackend backend = GetCurrentGlBackend();
    if (backend == CurrentGlBackend::Egl) {
        return true;
    }
    if (backend == CurrentGlBackend::Glx) {
        return false;
    }

    const char* glfwPlatform = std::getenv("GLFW_PLATFORM");
    if (glfwPlatform && std::strcmp(glfwPlatform, "wayland") == 0) {
        return true;
    }
    if (glfwPlatform && std::strcmp(glfwPlatform, "x11") == 0) {
        return false;
    }

    const char* sessionType = std::getenv("XDG_SESSION_TYPE");
    if (sessionType && std::strcmp(sessionType, "wayland") == 0) {
        return true;
    }
    if (sessionType && std::strcmp(sessionType, "x11") == 0) {
        return false;
    }

    const char* waylandDisplay = std::getenv("WAYLAND_DISPLAY");
    return waylandDisplay && *waylandDisplay;
#endif
}

inline int NormalizeGlfwScanCodeForLinuxBindings(int rawScanCode) {
#ifdef __APPLE__
    return rawScanCode;
#else
    if (rawScanCode <= 0) {
        return rawScanCode;
    }
    return ShouldTranslateGlfwScanCodesForLinuxBindings() ? (rawScanCode + 8) : rawScanCode;
#endif
}

inline int DenormalizeLinuxBindingScanCodeForGlfw(int bindingScanCode) {
#ifdef __APPLE__
    return bindingScanCode;
#else
    if (bindingScanCode <= 0) {
        return bindingScanCode;
    }
    if (!ShouldTranslateGlfwScanCodesForLinuxBindings()) {
        return bindingScanCode;
    }
    return bindingScanCode > 8 ? (bindingScanCode - 8) : bindingScanCode;
#endif
}

} // namespace platform::x11
