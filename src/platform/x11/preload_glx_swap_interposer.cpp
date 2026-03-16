#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <OpenGL/OpenGL.h>
#endif

#include "hook/preload_runtime.cpp"
#include "hook/preload_mode_runtime.cpp"
#include "hook/preload_symbols.cpp"
#include "hook/preload_input_pipeline.cpp"
#include "hook/preload_swap_path.cpp"
#include "hook/preload_glfw_hooks.cpp"
#include "hook/preload_lifecycle.cpp"

#ifdef __APPLE__
// DYLD_INTERPOSE macro — tells dyld to globally replace function call sites.
// Calls to the original from *within* the replacement are NOT replaced.
#define DYLD_INTERPOSE(_replacement, _replacee) \
    __attribute__((used)) static struct { \
        const void* replacement; \
        const void* replacee; \
    } _interpose_##_replacee __attribute__((section("__DATA,__interpose"))) = { \
        reinterpret_cast<const void*>(static_cast<decltype(&_replacee)>(&_replacement)), \
        reinterpret_cast<const void*>(&_replacee) \
    };

DYLD_INTERPOSE(my_CGLFlushDrawable, CGLFlushDrawable)
DYLD_INTERPOSE(my_dlsym, dlsym)
#endif
