#pragma once

#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif

#include "../glx_mirror_pipeline.h"
#include "../mirror/glx_shared_contexts.h"
#include "imgui_input_bridge.h"
#include "../x11_clipboard.h"
#include "../x11_runtime.h"

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
#include <GL/glx.h>
#include <GL/glext.h>
#include <X11/XKBlib.h>
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

namespace platform::x11 {

struct ImGuiOverlayState {
    ImGuiContext* context = nullptr;
    void* glContext = nullptr;
    bool openglBackendInitialized = false;
    std::chrono::steady_clock::time_point lastFrameTime{};
    bool hasLastFrameTime = false;
    std::uint64_t frameCount = 0;
};

struct ImGuiOverlayCaptureState {
    bool hasFrame = false;
    bool wantCaptureMouse = false;
    bool forceConsumeMouse = false;
    bool wantCaptureKeyboard = false;
    bool wantTextInput = false;
    bool hasWindowRect = false;
    bool hasPointerPosition = false;
    bool pointerOverWindow = false;
    bool mouseInteractionActive = false;
    double windowX = 0.0;
    double windowY = 0.0;
    double windowWidth = 0.0;
    double windowHeight = 0.0;
    double pointerX = 0.0;
    double pointerY = 0.0;
};

enum class MainSettingsTab : int {
    Modes = 0,
    Mirrors = 1,
    EyeZoom = 2,
    Inputs = 3,
    Misc = 4,
};

struct OverlayUiAnimationState {
    bool mainTabInitialized = false;
    MainSettingsTab activeMainTab = MainSettingsTab::Modes;
    float mainTabTransitionProgress = 1.0f;
    float mainTabTransitionDurationSeconds = 0.16f;

    bool themeTransitionActive = false;
    float themeTransitionElapsedSeconds = 0.0f;
    float themeTransitionDurationSeconds = 0.24f;
    ImGuiID themeTransitionTargetStyleId = 0;
};
extern OverlayUiAnimationState g_overlayUiAnimationState;

struct ButtonRippleState {
    bool active = false;
    ImVec2 origin = ImVec2(0.0f, 0.0f);
    float elapsedSeconds = 0.0f;
    std::uint64_t lastTouchedFrame = 0;
};

struct HeaderContentRevealState {
    bool initialized = false;
    bool previousOpen = false;
    float progress = 1.0f;
    float lastContentHeight = 0.0f;
    std::uint64_t lastTouchedFrame = 0;
};
extern std::unordered_map<ImGuiID, HeaderContentRevealState> g_headerContentRevealStates;

void SubmitCursorBoundaryItem();

struct HeaderRevealScope {
    bool active = false;
    bool childBeginCalled = false;
    ImGuiID headerId = 0;
    float contentStartCursorPosY = 0.0f;

    HeaderRevealScope() = default;

    HeaderRevealScope(float yOffset, float alphaScale, ImGuiID id, float revealProgress, bool closing) {
        if (yOffset > 0.001f) {
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + yOffset);
            SubmitCursorBoundaryItem();
        }

        headerId = id;
        const float clampedProgress = std::clamp(revealProgress, 0.0f, 1.0f);
        if (closing && headerId != 0 && clampedProgress < 0.999f) {
            auto stateIt = g_headerContentRevealStates.find(headerId);
            if (stateIt != g_headerContentRevealStates.end() && stateIt->second.lastContentHeight > 1.0f) {
                const float clipHeight = std::max(1.0f, stateIt->second.lastContentHeight * clampedProgress);
                char childLabel[64] = {};
                std::snprintf(childLabel, sizeof(childLabel), "##header_reveal_close_clip_%08X", headerId);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
                ImGui::BeginChild(childLabel,
                                  ImVec2(0.0f, clipHeight),
                                  false,
                                  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground);
                ImGui::PopStyleVar();
                childBeginCalled = true;
            }
        }

        contentStartCursorPosY = ImGui::GetCursorPosY();
        const float clampedAlphaScale = std::clamp(alphaScale, 0.0f, 1.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * clampedAlphaScale);
        active = true;
    }

    HeaderRevealScope(const HeaderRevealScope&) = delete;
    HeaderRevealScope& operator=(const HeaderRevealScope&) = delete;

    HeaderRevealScope(HeaderRevealScope&& other) noexcept { MoveFrom(other); }

    HeaderRevealScope& operator=(HeaderRevealScope&& other) noexcept {
        if (this != &other) {
            Finish();
            MoveFrom(other);
        }
        return *this;
    }

    ~HeaderRevealScope() { Finish(); }

private:
    void MoveFrom(HeaderRevealScope& other) {
        active = other.active;
        childBeginCalled = other.childBeginCalled;
        headerId = other.headerId;
        contentStartCursorPosY = other.contentStartCursorPosY;

        other.active = false;
        other.childBeginCalled = false;
        other.headerId = 0;
        other.contentStartCursorPosY = 0.0f;
    }

    void Finish() {
        if (active) {
            const float endCursorPosY = ImGui::GetCursorPosY();
            const float renderedHeight = std::max(0.0f, endCursorPosY - contentStartCursorPosY);
            if (headerId != 0 && renderedHeight > 1.0f) {
                auto stateIt = g_headerContentRevealStates.find(headerId);
                if (stateIt != g_headerContentRevealStates.end()) {
                    stateIt->second.lastContentHeight = renderedHeight;
                }
            }
            ImGui::PopStyleVar();
            active = false;
        }
        if (childBeginCalled) {
            ImGui::EndChild();
            childBeginCalled = false;
        }
        headerId = 0;
        contentStartCursorPosY = 0.0f;
    }
};

extern float g_pendingHeaderRevealProgress;
extern bool g_pendingHeaderRevealValid;
extern bool g_pendingHeaderRevealClosing;
extern ImGuiID g_pendingHeaderRevealId;

extern std::mutex g_imguiOverlayMutex;
extern ImGuiOverlayState g_imguiOverlayState;
extern ImGuiOverlayCaptureState g_imguiOverlayCaptureState;

extern bool g_isSaving;

extern std::map<std::string, std::string> g_discoveredFonts;
extern char g_fontSearchFilter[128];
extern bool g_fontsScanned;

extern OverlayUiAnimationState g_overlayUiAnimationState;
extern std::unordered_map<ImGuiID, ButtonRippleState> g_buttonRippleStates;
extern std::unordered_map<ImGuiID, ButtonRippleState> g_headerRippleStates;
extern std::unordered_map<ImGuiID, ButtonRippleState> g_rebindKeyCyclePulseStates;
extern std::unordered_map<ImGuiID, HeaderContentRevealState> g_headerContentRevealStates;

extern std::uint64_t g_lastOverlaySharedGeneration;
extern std::atomic<bool> g_resetOverlayRequested;
bool GetOverlayDisplayMetrics(float& outDisplayWidth,
                              float& outDisplayHeight,
                              float& outFramebufferScaleX,
                              float& outFramebufferScaleY);

} // namespace platform::x11
