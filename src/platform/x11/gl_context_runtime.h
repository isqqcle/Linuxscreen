#pragma once

#include <cstdint>
#include <dlfcn.h>

#ifdef __APPLE__
#include <OpenGL/OpenGL.h>
#else
#include <EGL/egl.h>
#include <GL/glx.h>
#endif

#ifdef None
#undef None
#endif

namespace platform::x11 {

enum class CurrentGlBackend : std::uint8_t {
    None = 0,
    Cgl = 1,
    Glx = 2,
    Egl = 3,
};

inline CurrentGlBackend GetCurrentGlBackend() {
#ifdef __APPLE__
    return CGLGetCurrentContext() != nullptr ? CurrentGlBackend::Cgl : CurrentGlBackend::None;
#else
    if (glXGetCurrentContext() != nullptr) {
        return CurrentGlBackend::Glx;
    }
    if (eglGetCurrentContext() != EGL_NO_CONTEXT) {
        return CurrentGlBackend::Egl;
    }
    return CurrentGlBackend::None;
#endif
}

inline void* GetCurrentGlContextHandle() {
#ifdef __APPLE__
    return reinterpret_cast<void*>(CGLGetCurrentContext());
#else
    if (glXGetCurrentContext() != nullptr) {
        return reinterpret_cast<void*>(glXGetCurrentContext());
    }
    const EGLContext context = eglGetCurrentContext();
    return context != EGL_NO_CONTEXT ? reinterpret_cast<void*>(context) : nullptr;
#endif
}

inline bool HasCurrentGlContext() {
    return GetCurrentGlContextHandle() != nullptr;
}

inline void* GetCurrentGlDisplayHandle() {
#ifdef __APPLE__
    return nullptr;
#else
    if (glXGetCurrentContext() != nullptr) {
        return reinterpret_cast<void*>(glXGetCurrentDisplay());
    }
    const EGLDisplay display = eglGetCurrentDisplay();
    return display != EGL_NO_DISPLAY ? reinterpret_cast<void*>(display) : nullptr;
#endif
}

inline std::uint64_t GetCurrentGlDrawableHandle() {
#ifdef __APPLE__
    return 0;
#else
    if (glXGetCurrentContext() != nullptr) {
        return static_cast<std::uint64_t>(glXGetCurrentDrawable());
    }
    const EGLSurface surface = eglGetCurrentSurface(EGL_DRAW);
    return surface != EGL_NO_SURFACE ? static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(surface)) : 0;
#endif
}

inline void* ResolveCurrentGlProcAddress(const char* name) {
    if (!name || name[0] == '\0') {
        return nullptr;
    }

#ifdef __APPLE__
    void* ptr = dlsym(RTLD_NEXT, name);
    if (!ptr) {
        ptr = dlsym(RTLD_DEFAULT, name);
    }
    return ptr;
#else
    void* ptr = reinterpret_cast<void*>(glXGetProcAddressARB(reinterpret_cast<const GLubyte*>(name)));
    if (!ptr) {
        ptr = reinterpret_cast<void*>(glXGetProcAddress(reinterpret_cast<const GLubyte*>(name)));
    }
    if (!ptr) {
        ptr = reinterpret_cast<void*>(eglGetProcAddress(name));
    }
    if (!ptr) {
        ptr = dlsym(RTLD_NEXT, name);
    }
    if (!ptr) {
        ptr = dlsym(RTLD_DEFAULT, name);
    }
    return ptr;
#endif
}

} // namespace platform::x11
