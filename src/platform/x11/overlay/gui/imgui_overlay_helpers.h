#pragma once

#include "../overlay_internal.h"

namespace platform::x11 {

void ApplyThemePreset(const std::string& theme);
void ApplyCustomColorsOverlay(const std::map<std::string, std::array<float, 4>>& customColors);
void HelpMarker(const char* desc);
float GetOverlayAnimationDeltaTime();
bool AnimatedButton(const char* label, const ImVec2& size = ImVec2(0.0f, 0.0f));
bool AnimatedCollapsingHeader(const char* label, ImGuiTreeNodeFlags flags = 0);
HeaderRevealScope BeginAnimatedHeaderContentReveal();
void TriggerRebindKeyCyclePulse(ImGuiID itemId);
void DrawRebindKeyButtonEffects(ImGuiID itemId);
void AutoSaveConfig(const platform::config::LinuxscreenConfig& config);
void FlushPendingConfigSave(bool force = false);
void StartThemeTransitionToStyle(const ImGuiStyle& targetStyle);
void UpdateThemeTransitionForFrame();
void NotifyMainTabSelected(MainSettingsTab selectedTab);
void PushMainTabContentAnimationStyle();
void GarbageCollectButtonRippleStates();
void FinalizeButtonRippleStatesForFrame();
void CreateNewMode(platform::config::LinuxscreenConfig& config, const std::string& modeName);
void StartModeSwitchWithTransition(const std::string& modeName,
                                   const platform::config::LinuxscreenConfig& config,
                                   platform::x11::MirrorModeState& modeState);
void DeleteMode(platform::config::LinuxscreenConfig& config, size_t modeIndex);
void AddNewHotkey(platform::config::LinuxscreenConfig& config, const std::string& targetMode);
void DeleteHotkey(platform::config::LinuxscreenConfig& config, size_t hotkeyIndex);
std::string GetHotkeyTargetMode(const platform::config::HotkeyConfig& hotkey);
void SetHotkeyTargetMode(platform::config::HotkeyConfig& hotkey, const std::string& modeName);
std::string GetHotkeyReturnMode(const platform::config::HotkeyConfig& hotkey,
                                const platform::config::LinuxscreenConfig& config);
void SetHotkeyReturnMode(platform::config::HotkeyConfig& hotkey,
                         const platform::config::LinuxscreenConfig& config,
                         const std::string& modeName);
void RenderHotkeySlotRepeatRateWarningMarker();
void RenderHotkeyCaptureModal();

} // namespace platform::x11
