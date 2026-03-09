#include "imgui_overlay.h"

#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif

#include "glx_mirror_pipeline.h"
#include "mirror/glx_shared_contexts.h"
#include "imgui_input_bridge.h"
#include "x11_clipboard.h"
#include "x11_runtime.h"

#include "../../common/anchor_coords.h"
#include "../../common/config_editor_helpers.h"
#include "../../common/config_io.h"
#include "../../common/game_state_monitor.h"
#include "../../common/input/glfw_vk_mapper.h"
#include "../../common/input/hotkey_capture_state.h"
#include "../../common/input/hotkey_dispatcher.h"
#include "../../common/linuxscreen_config.h"
#include "../../common/input/vk_codes.h"
#include "../../common/font_scanner.h"
#include "im_anim.h"
#include "ImGuiFileDialog.h"
#include "imgui_impl_opengl3.h"
#include "imgui.h"
#include "imgui_internal.h"
#ifdef __APPLE__
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <OpenGL/OpenGL.h>
#else
#include <GL/glx.h>
#include <GL/glext.h>
#include <X11/XKBlib.h>
#endif
#include <array>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <mutex>
#include <set>
#include <sstream>
#include <unordered_map>

#ifdef Status
#undef Status
#endif

#include "overlay_internal.h"
#include "gui/imgui_overlay_helpers.h"
#include "gui/tab_inputs_helpers.h"
#include "gui/tab_inputs_state.h"
#include "gui/tab_misc.h"
#include "gui/tab_modes.h"
#include "gui/tab_modes_state.h"
#include "gui/tab_mirrors.h"
#include "gui/tab_mirrors_helpers.h"
#include "gui/tab_mirrors_state.h"
#include "gui/tab_eyezoom.h"
#include "gui/tab_inputs.h"

namespace platform::x11 {

// Core overlay state (private to this TU)
std::mutex g_imguiOverlayMutex;
ImGuiOverlayState g_imguiOverlayState{};
std::atomic<GLFWwindow*> g_registeredWindow{ nullptr };
std::mutex g_imguiOverlayCaptureMutex;
ImGuiOverlayCaptureState g_imguiOverlayCaptureState{};
std::uint64_t g_lastOverlaySharedGeneration = 0;
std::atomic<bool> g_resetOverlayRequested{ false };

// Save status state
bool g_isSaving = false;

// Font scanner state
std::map<std::string, std::string> g_discoveredFonts;
char g_fontSearchFilter[128] = {};
bool g_fontsScanned = false;

OverlayUiAnimationState g_overlayUiAnimationState;
std::unordered_map<ImGuiID, ButtonRippleState> g_buttonRippleStates;
std::unordered_map<ImGuiID, ButtonRippleState> g_headerRippleStates;
std::unordered_map<ImGuiID, ButtonRippleState> g_rebindKeyCyclePulseStates;

std::unordered_map<ImGuiID, HeaderContentRevealState> g_headerContentRevealStates;

float g_pendingHeaderRevealProgress = 1.0f;
bool g_pendingHeaderRevealValid = false;
bool g_pendingHeaderRevealClosing = false;
ImGuiID g_pendingHeaderRevealId = 0;

namespace {

bool GetOverlayDisplayMetricsImpl(float& outDisplayWidth,
                                  float& outDisplayHeight,
                                  float& outFramebufferScaleX,
                                  float& outFramebufferScaleY) {
    outDisplayWidth = 0.0f;
    outDisplayHeight = 0.0f;
    outFramebufferScaleX = 1.0f;
    outFramebufferScaleY = 1.0f;

    int displayWidth = 0;
    int displayHeight = 0;
    int framebufferWidth = 0;
    int framebufferHeight = 0;

    (void)GetGlfwWindowMetrics(displayWidth, displayHeight, framebufferWidth, framebufferHeight);

    if ((displayWidth <= 0 || displayHeight <= 0) && framebufferWidth > 0 && framebufferHeight > 0) {
        displayWidth = framebufferWidth;
        displayHeight = framebufferHeight;
    }

    if (displayWidth <= 0 || displayHeight <= 0) {
        if (!platform::x11::GetGameWindowSize(displayWidth, displayHeight) || displayWidth <= 0 || displayHeight <= 0) {
            GLint viewport[4] = { 0, 0, 0, 0 };
            glGetIntegerv(GL_VIEWPORT, viewport);
            displayWidth = viewport[2];
            displayHeight = viewport[3];
        }
    }

    if (framebufferWidth <= 0 || framebufferHeight <= 0) {
        framebufferWidth = displayWidth;
        framebufferHeight = displayHeight;
    }

    if (displayWidth <= 0 || displayHeight <= 0) {
        return false;
    }

    outDisplayWidth = static_cast<float>(displayWidth);
    outDisplayHeight = static_cast<float>(displayHeight);
    outFramebufferScaleX = static_cast<float>(framebufferWidth) / outDisplayWidth;
    outFramebufferScaleY = static_cast<float>(framebufferHeight) / outDisplayHeight;

    if (!(outFramebufferScaleX > 0.0f)) {
        outFramebufferScaleX = 1.0f;
    }
    if (!(outFramebufferScaleY > 0.0f)) {
        outFramebufferScaleY = 1.0f;
    }

    return true;
}

bool IsPointerInsideWindow(const ImGuiOverlayCaptureState& state, double x, double y) {
    if (!state.hasWindowRect) { return false; }
    return x >= state.windowX && y >= state.windowY && x < (state.windowX + state.windowWidth) && y < (state.windowY + state.windowHeight);
}


void ResetOverlayCaptureStateLocked() { g_imguiOverlayCaptureState = ImGuiOverlayCaptureState{}; }

void RefreshPointerOverWindowLocked() {
    if (!g_imguiOverlayCaptureState.hasPointerPosition || !g_imguiOverlayCaptureState.hasWindowRect) {
        g_imguiOverlayCaptureState.pointerOverWindow = false;
        return;
    }

    g_imguiOverlayCaptureState.pointerOverWindow = IsPointerInsideWindow(g_imguiOverlayCaptureState, g_imguiOverlayCaptureState.pointerX,
                                                                        g_imguiOverlayCaptureState.pointerY);
}
