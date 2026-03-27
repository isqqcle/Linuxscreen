#pragma once

#include "../overlay_internal.h"
#include "tab_inputs_state.h"

namespace platform::x11 {

std::string FormatSingleBinding(const platform::input::BindingKey& key);
std::string FormatBindingChord(const std::vector<platform::input::BindingKey>& keys);
std::string FormatHotkey(const std::vector<platform::input::BindingKey>& keys);
std::string FormatSingleVk(uint32_t vk);
std::string FormatHotkey(const std::vector<uint32_t>& keys);
bool IsWindowsKeyVk(uint32_t vk);
void InsertCaptureKeyOrdered(std::vector<uint32_t>& keys, uint32_t vk);
void InsertCaptureBindingOrdered(std::vector<platform::input::BindingKey>& keys,
                                 const platform::input::BindingKey& key,
                                 bool isModifier);
platform::input::BindingKey BindingFromLegacyUiVk(uint32_t vk);
uint32_t LegacyUiVkFromBinding(const platform::input::BindingKey& binding);
bool CaptureTargetUsesSharedModal(platform::config::CaptureTarget target);
bool CanClearCaptureTarget(platform::config::CaptureTarget target);
bool CaptureTargetUsesSingleKeyDisplay(platform::config::CaptureTarget target);
bool IsRebindCaptureTarget(platform::config::CaptureTarget target);
uint32_t ResolveSingleCaptureVk(const std::vector<uint32_t>& keys);
platform::input::BindingKey ResolveSingleCaptureBinding(const std::vector<platform::input::BindingKey>& keys);
bool IsNoOpRebindForKey(const platform::config::KeyRebind& rebind, const platform::input::BindingKey& originalBinding);
bool IsNoOpRebindForKey(const platform::config::KeyRebind& rebind, uint32_t originalVk);
void EraseRebindAdjustingLayoutState(platform::config::LinuxscreenConfig& config, int eraseIndex);
bool HasExplicitTextOverride(const platform::config::KeyRebind& rebind);

} // namespace platform::x11
