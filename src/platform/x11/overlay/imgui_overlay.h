#pragma once

#include "../../common/input/input_event.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct GLFWwindow;

namespace platform::x11 {

enum class ImGuiOverlayRenderStatus : std::uint8_t {
    Disabled = 0,
    Hidden,
    MissingWindow,
    MissingGlContext,
    InitFailed,
    Rendered,
};

struct ImGuiOverlayRenderResult {
    ImGuiOverlayRenderStatus status = ImGuiOverlayRenderStatus::Disabled;
    std::size_t drained = 0;
    std::size_t applied = 0;
};

bool IsImGuiRenderEnabled();
void RegisterImGuiOverlayWindow(GLFWwindow* window);
void UpdateImGuiOverlayPointerPosition(double x, double y);
bool ShouldConsumeInputForOverlay(const input::InputEvent& event);
ImGuiOverlayRenderResult RenderImGuiOverlayFrame(GLFWwindow* preferredWindow, const char* sourceLabel);
void ShutdownImGuiOverlay();
void ShutdownImGuiOverlayForProcessExit();

} // namespace platform::x11
