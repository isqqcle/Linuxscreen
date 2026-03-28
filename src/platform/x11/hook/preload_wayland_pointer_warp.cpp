#include "../input_scancode_runtime.h"

#ifndef __APPLE__
#include <wayland-client.h>
#include "pointer-warp-v1-client-protocol.h"

namespace {

struct WaylandPointerWarpState {
    wl_display* display = nullptr;
    wl_registry* registry = nullptr;
    wl_seat* seat = nullptr;
    wl_pointer* pointer = nullptr;
    wl_surface* focusedSurface = nullptr;
    wp_pointer_warp_v1* warp = nullptr;
    std::uint32_t lastEnterSerial = 0;
    bool globalsReady = false;
};

WaylandPointerWarpState g_waylandPointerWarpState;
extern const wl_pointer_listener kWaylandPointerListener;

void DestroyWaylandPointer(WaylandPointerWarpState& state) {
    if (!state.pointer) {
        return;
    }
    if (wl_proxy_get_version(reinterpret_cast<wl_proxy*>(state.pointer)) >= WL_POINTER_RELEASE_SINCE_VERSION) {
        wl_pointer_release(state.pointer);
    } else {
        wl_pointer_destroy(state.pointer);
    }
    state.pointer = nullptr;
    state.focusedSurface = nullptr;
    state.lastEnterSerial = 0;
}

void CreateWaylandPointer(WaylandPointerWarpState& state) {
    if (!state.seat || state.pointer) {
        return;
    }
    state.pointer = wl_seat_get_pointer(state.seat);
    if (state.pointer) {
        wl_pointer_add_listener(state.pointer, &kWaylandPointerListener, &state);
    }
}

void ResetWaylandPointerWarpState() {
    WaylandPointerWarpState& state = g_waylandPointerWarpState;

    DestroyWaylandPointer(state);
    if (state.seat) {
        if (wl_proxy_get_version(reinterpret_cast<wl_proxy*>(state.seat)) >= WL_SEAT_RELEASE_SINCE_VERSION) {
            wl_seat_release(state.seat);
        } else {
            wl_seat_destroy(state.seat);
        }
    }
    if (state.warp) {
        wp_pointer_warp_v1_destroy(state.warp);
    }
    if (state.registry) {
        wl_registry_destroy(state.registry);
    }

    state = {};
}

void HandlePointerEnter(void* data,
                        wl_pointer* /*pointer*/,
                        std::uint32_t serial,
                        wl_surface* surface,
                        wl_fixed_t /*surfaceX*/,
                        wl_fixed_t /*surfaceY*/) {
    auto* state = static_cast<WaylandPointerWarpState*>(data);
    state->focusedSurface = surface;
    state->lastEnterSerial = serial;
}

void HandlePointerLeave(void* data,
                        wl_pointer* /*pointer*/,
                        std::uint32_t /*serial*/,
                        wl_surface* surface) {
    auto* state = static_cast<WaylandPointerWarpState*>(data);
    if (state->focusedSurface == surface) {
        state->focusedSurface = nullptr;
        state->lastEnterSerial = 0;
    }
}

void HandlePointerMotion(void* /*data*/, wl_pointer* /*pointer*/, std::uint32_t /*time*/, wl_fixed_t /*surfaceX*/, wl_fixed_t /*surfaceY*/) {}
void HandlePointerButton(void* /*data*/,
                         wl_pointer* /*pointer*/,
                         std::uint32_t /*serial*/,
                         std::uint32_t /*time*/,
                         std::uint32_t /*button*/,
                         std::uint32_t /*state*/) {}
void HandlePointerAxis(void* /*data*/, wl_pointer* /*pointer*/, std::uint32_t /*time*/, std::uint32_t /*axis*/, wl_fixed_t /*value*/) {}
void HandlePointerFrame(void* /*data*/, wl_pointer* /*pointer*/) {}
void HandlePointerAxisSource(void* /*data*/, wl_pointer* /*pointer*/, std::uint32_t /*axisSource*/) {}
void HandlePointerAxisStop(void* /*data*/, wl_pointer* /*pointer*/, std::uint32_t /*time*/, std::uint32_t /*axis*/) {}
void HandlePointerAxisDiscrete(void* /*data*/, wl_pointer* /*pointer*/, std::uint32_t /*axis*/, std::int32_t /*discrete*/) {}
void HandlePointerAxisValue120(void* /*data*/, wl_pointer* /*pointer*/, std::uint32_t /*axis*/, std::int32_t /*value120*/) {}
void HandlePointerAxisRelativeDirection(void* /*data*/,
                                        wl_pointer* /*pointer*/,
                                        std::uint32_t /*axis*/,
                                        std::uint32_t /*direction*/) {}

const wl_pointer_listener kWaylandPointerListener = {
    HandlePointerEnter,
    HandlePointerLeave,
    HandlePointerMotion,
    HandlePointerButton,
    HandlePointerAxis,
    HandlePointerFrame,
    HandlePointerAxisSource,
    HandlePointerAxisStop,
    HandlePointerAxisDiscrete,
    HandlePointerAxisValue120,
    HandlePointerAxisRelativeDirection,
};

void HandleSeatCapabilities(void* data, wl_seat* seat, std::uint32_t capabilities) {
    auto* state = static_cast<WaylandPointerWarpState*>(data);
    const bool hasPointer = (capabilities & WL_SEAT_CAPABILITY_POINTER) != 0;
    if (hasPointer && !state->pointer) {
        CreateWaylandPointer(*state);
        return;
    }

    if (!hasPointer && state->pointer) {
        DestroyWaylandPointer(*state);
    }
}

void HandleSeatName(void* /*data*/, wl_seat* /*seat*/, const char* /*name*/) {}

const wl_seat_listener kWaylandSeatListener = {
    HandleSeatCapabilities,
    HandleSeatName,
};

void HandleRegistryGlobal(void* data,
                          wl_registry* registry,
                          std::uint32_t name,
                          const char* interfaceName,
                          std::uint32_t version) {
    auto* state = static_cast<WaylandPointerWarpState*>(data);
    if (!interfaceName) {
        return;
    }

    if (!state->seat && std::strcmp(interfaceName, wl_seat_interface.name) == 0) {
        const std::uint32_t bindVersion = std::min<std::uint32_t>(version, 5u);
        state->seat = static_cast<wl_seat*>(wl_registry_bind(registry, name, &wl_seat_interface, bindVersion));
        if (state->seat) {
            wl_seat_add_listener(state->seat, &kWaylandSeatListener, state);
        }
        return;
    }

    if (!state->warp && std::strcmp(interfaceName, wp_pointer_warp_v1_interface.name) == 0) {
        state->warp = static_cast<wp_pointer_warp_v1*>(wl_registry_bind(
            registry,
            name,
            &wp_pointer_warp_v1_interface,
            1));
    }
}

void HandleRegistryGlobalRemove(void* /*data*/, wl_registry* /*registry*/, std::uint32_t /*name*/) {}

const wl_registry_listener kWaylandRegistryListener = {
    HandleRegistryGlobal,
    HandleRegistryGlobalRemove,
};

bool IsNativeWaylandCursorWarpBackend(GLFWwindow* window) {
    if (!window) {
        return false;
    }

    if (platform::x11::IsWaylandGlfwPlatform()) {
        return true;
    }

    const platform::x11::CurrentGlBackend backend = platform::x11::GetCurrentGlBackend();
    if (backend == platform::x11::CurrentGlBackend::Egl) {
        return true;
    }
    if (backend == platform::x11::CurrentGlBackend::Glx) {
        return false;
    }

    return false;
}

bool EnsureWaylandPointerWarpReady(GLFWwindow* window) {
    if (!window) {
        LogAlways("Wayland pointer warp unavailable: null GLFW window");
        return false;
    }
    if (!IsNativeWaylandCursorWarpBackend(window)) {
        return false;
    }

    GlfwGetWaylandDisplayFn getWaylandDisplay = GetRealGlfwGetWaylandDisplay();
    GlfwGetWaylandWindowFn getWaylandWindow = GetRealGlfwGetWaylandWindow();
    if (!getWaylandDisplay || !getWaylandWindow) {
        LogAlways("Wayland pointer warp unavailable: GLFW native Wayland symbols missing display=%p window=%p",
                 reinterpret_cast<void*>(getWaylandDisplay),
                 reinterpret_cast<void*>(getWaylandWindow));
        return false;
    }

    wl_display* display = getWaylandDisplay();
    wl_surface* surface = getWaylandWindow(window);
    if (!display || !surface) {
        LogAlways("Wayland pointer warp unavailable: display=%p surface=%p", static_cast<void*>(display), static_cast<void*>(surface));
        return false;
    }

    WaylandPointerWarpState& state = g_waylandPointerWarpState;
    if (state.display && state.display != display) {
        ResetWaylandPointerWarpState();
    }

    if (!state.display) {
        state.display = display;
    }
    if (!state.registry) {
        state.registry = wl_display_get_registry(display);
        if (!state.registry) {
            return false;
        }
        wl_registry_add_listener(state.registry, &kWaylandRegistryListener, &state);
    }
    if (!state.globalsReady) {
        wl_display_roundtrip(display);
        wl_display_roundtrip(display);
        state.globalsReady = true;
    }

    if (!state.pointer && state.seat) {
        CreateWaylandPointer(state);
    }
    if (state.lastEnterSerial == 0) {
        wl_display_dispatch_pending(display);
        wl_display_roundtrip(display);
    }

    if (!state.warp) {
        LogAlways("Wayland pointer warp unavailable: wp_pointer_warp_v1 global missing");
    }
    if (!state.pointer) {
        LogAlways("Wayland pointer warp unavailable: wl_pointer missing");
    }
    return state.warp && state.pointer;
}

} // namespace

void PrimeWaylandPointerWarp(GLFWwindow* window) {
    if (!IsNativeWaylandCursorWarpBackend(window)) {
        return;
    }
    (void)EnsureWaylandPointerWarpReady(window);
}

bool ShouldAttemptWaylandCursorWarp(GLFWwindow* window) {
    return IsNativeWaylandCursorWarpBackend(window);
}

void NotifyWaylandPointerWarpFocusChanged(GLFWwindow* window, bool focused) {
    if (!ShouldAttemptWaylandCursorWarp(window)) {
        return;
    }

    WaylandPointerWarpState& state = g_waylandPointerWarpState;
    if (!focused) {
        state.focusedSurface = nullptr;
        state.lastEnterSerial = 0;
        return;
    }

    PrimeWaylandPointerWarp(window);
}

bool TryWarpWaylandCursorToWindowPosition(GLFWwindow* window, double xpos, double ypos) {
    if (!EnsureWaylandPointerWarpReady(window)) {
        return false;
    }

    GlfwGetWaylandDisplayFn getWaylandDisplay = GetRealGlfwGetWaylandDisplay();
    GlfwGetWaylandWindowFn getWaylandWindow = GetRealGlfwGetWaylandWindow();
    if (!getWaylandDisplay || !getWaylandWindow) {
        return false;
    }

    wl_display* display = getWaylandDisplay();
    wl_surface* surface = getWaylandWindow(window);
    if (!display || !surface) {
        LogAlways("Wayland pointer warp aborted: display=%p surface=%p", static_cast<void*>(display), static_cast<void*>(surface));
        return false;
    }

    wl_display_dispatch_pending(display);

    WaylandPointerWarpState& state = g_waylandPointerWarpState;
    if (!state.warp || !state.pointer || state.lastEnterSerial == 0 || state.focusedSurface != surface) {
        LogAlways("Wayland pointer warp aborted: warp=%p pointer=%p enterSerial=%u focusedSurface=%p targetSurface=%p",
                 static_cast<void*>(state.warp),
                 static_cast<void*>(state.pointer),
                 static_cast<unsigned>(state.lastEnterSerial),
                 static_cast<void*>(state.focusedSurface),
                 static_cast<void*>(surface));
        return false;
    }

    wp_pointer_warp_v1_warp_pointer(state.warp,
                                    surface,
                                    state.pointer,
                                    wl_fixed_from_double(xpos),
                                    wl_fixed_from_double(ypos),
                                    state.lastEnterSerial);
    wl_display_flush(display);
    return true;
}

#else

bool ShouldAttemptWaylandCursorWarp(GLFWwindow* /*window*/) {
    return false;
}

void PrimeWaylandPointerWarp(GLFWwindow* /*window*/) {}

void NotifyWaylandPointerWarpFocusChanged(GLFWwindow* /*window*/, bool /*focused*/) {}

bool TryWarpWaylandCursorToWindowPosition(GLFWwindow* /*window*/, double /*xpos*/, double /*ypos*/) {
    return false;
}

#endif
