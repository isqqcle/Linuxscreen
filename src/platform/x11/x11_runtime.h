#pragma once

#include "../common/platform_runtime.h"
#include "../common/input/input_event.h"
#include "../common/input/vk_codes.h"

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace platform::config {
struct LinuxscreenConfig;
}

namespace platform::x11 {

bool Initialize(const BootstrapConfig& config);
bool InstallHooks();
void Shutdown();
RuntimeHandles GetRuntimeHandles();
RuntimeState GetRuntimeState();
std::string GetLastErrorMessage();

void RecordSwapHandles(void* nativeDisplay, unsigned long drawable, void* glContext);
bool IsInitialized();
std::uint64_t GetSwapObservationCount();

bool ResizeGameWindow(int width, int height);
bool GetGameWindowSize(int& outWidth, int& outHeight);
void RecordGlfwWindowMetrics(int windowWidth, int windowHeight, int framebufferWidth, int framebufferHeight);
bool GetGlfwWindowMetrics(int& outWindowWidth, int& outWindowHeight, int& outFramebufferWidth, int& outFramebufferHeight);
void TriggerImmediateModeResizeEnforcement();
void ClearTempSensitivityOverride();
void UpdateSensitivityStateForModeSwitch(const std::string& targetMode, const config::LinuxscreenConfig& config);

void SetGuiHotkey(const std::vector<input::VkCode>& keys);
std::vector<input::VkCode> GetGuiHotkey();
void SetRebindToggleHotkey(const std::vector<input::VkCode>& keys);
std::vector<input::VkCode> GetRebindToggleHotkey();
bool ToggleGuiVisible();
void SetGuiVisible(bool visible);
bool IsGuiVisible();
std::uint64_t GetGuiToggleCount();
void ShowRebindToggleIndicator(bool rebindsEnabled, std::uint64_t durationMs = 3000);
bool GetRebindToggleIndicator(bool& outRebindsEnabled);

bool EnqueueImGuiInputEvent(const input::InputEvent& event);
std::size_t DrainImGuiInputEvents(std::vector<input::InputEvent>& outEvents, std::size_t maxEvents);
void ClearImGuiInputEvents();
std::uint64_t GetImGuiInputQueuedCount();
std::uint64_t GetImGuiInputDroppedCount();

} // namespace platform::x11
