#include "../overlay_internal.h"
#include "imgui_overlay_helpers.h"
#include "tab_inputs_helpers.h"
#include "tab_inputs_state.h"

namespace platform::x11 {

namespace {

constexpr auto kDeferredConfigSaveDelay = std::chrono::milliseconds(150);

bool g_hasPendingConfigSave = false;
platform::config::LinuxscreenConfig g_pendingConfigSave;
std::chrono::steady_clock::time_point g_lastConfigSaveRequest{};

} // namespace

void SyncEyeZoomWindowSizeWithMode(platform::config::LinuxscreenConfig& config) {
    for (const auto& mode : config.modes) {
        if (mode.name == "EyeZoom") {
            config.eyezoom.windowWidth = mode.width;
            config.eyezoom.windowHeight = mode.height;
            return;
        }
    }
}

// Used in hotkeys tab
void RenderHotkeySlotRepeatRateWarningMarker() {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.35f, 0.35f, 1.0f));
    ImGui::TextUnformatted("[!]");
    ImGui::PopStyleColor();
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
        ImGui::TextUnformatted(
            "Repeat rates above 20 Hz can cause issues with hotkey switching. This is due to a bug with keybind "
            "handling, and can be avoided by either using a longer start delay, keeping repeat delay to 20 Hz or "
            "lower (globally or for affected inputs).");
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

void AutoSaveConfig(const platform::config::LinuxscreenConfig& config) {
    platform::config::LinuxscreenConfig toSave = config;
    SyncEyeZoomWindowSizeWithMode(toSave);
    platform::config::PublishConfigSnapshot(toSave);
    g_pendingConfigSave = std::move(toSave);
    g_lastConfigSaveRequest = std::chrono::steady_clock::now();
    g_hasPendingConfigSave = true;
}

void FlushPendingConfigSave(bool force) {
    if (!g_hasPendingConfigSave) {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    if (!force && (now - g_lastConfigSaveRequest) < kDeferredConfigSaveDelay) {
        return;
    }

    g_isSaving = true;
    platform::config::SaveLinuxscreenConfig(g_pendingConfigSave);
    g_isSaving = false;
    g_hasPendingConfigSave = false;
}

#include "overlay_gui_animation.cpp"

void CreateNewMode(platform::config::LinuxscreenConfig& config, const std::string& modeName) {
    platform::config::ModeConfig newMode;
    newMode.name = modeName;
    config.modes.push_back(std::move(newMode));
}

void StartModeSwitchWithTransition(const std::string& modeName,
                                   const platform::config::LinuxscreenConfig& config,
                                   platform::x11::MirrorModeState& modeState) {
    GLint viewport[4] = { 0, 0, 0, 0 };
    glGetIntegerv(GL_VIEWPORT, viewport);

    int currentWidth = viewport[2];
    int currentHeight = viewport[3];
    if (currentWidth <= 0 || currentHeight <= 0) {
        if (!platform::x11::GetGameWindowSize(currentWidth, currentHeight)) {
            currentWidth = 0;
            currentHeight = 0;
        }
    }

    if (modeState.GetActiveModeName() != modeName) {
        platform::x11::UpdateSensitivityStateForModeSwitch(modeName, config);
    }
    modeState.ApplyModeSwitch(modeName, config);
    platform::x11::TriggerImmediateModeResizeEnforcement();
}

void DeleteMode(platform::config::LinuxscreenConfig& config, size_t modeIndex) {
    if (modeIndex >= config.modes.size()) {
        return;
    }

    std::string deletedModeName = config.modes[modeIndex].name;
    config.modes.erase(config.modes.begin() + modeIndex);

    if (config.defaultMode == deletedModeName) {
        config.defaultMode.clear();
        for (const auto& mode : config.modes) {
            if (!mode.name.empty()) {
                config.defaultMode = mode.name;
                break;
            }
        }
    }

    platform::config::RemoveModeFromHotkeys(config, deletedModeName);
}

// Add a new hotkey
void AddNewHotkey(platform::config::LinuxscreenConfig& config, const std::string& targetMode) {
    platform::config::HotkeyConfig newHotkey;
    newHotkey.keys = { 0x70 }; // Default to F1
    newHotkey.mainMode = targetMode;
    newHotkey.altSecondaryModes.clear();
    newHotkey.debounce = 100;
    newHotkey.triggerOnRelease = false;
    newHotkey.triggerOnHold = false;
    newHotkey.blockKeyFromGame = false;
    config.hotkeys.push_back(std::move(newHotkey));
}

// Delete a hotkey by index
void DeleteHotkey(platform::config::LinuxscreenConfig& config, size_t hotkeyIndex) {
    if (hotkeyIndex < config.hotkeys.size()) {
        config.hotkeys.erase(config.hotkeys.begin() + hotkeyIndex);
    }
}

std::string GetHotkeyTargetMode(const platform::config::HotkeyConfig& hotkey) {
    if (!hotkey.secondaryMode.empty()) {
        return hotkey.secondaryMode;
    }
    return hotkey.mainMode;
}

// Update hotkey target mode
void SetHotkeyTargetMode(platform::config::HotkeyConfig& hotkey, const std::string& modeName) {
    if (!hotkey.secondaryMode.empty()) {
        hotkey.secondaryMode = modeName;
        return;
    }
    hotkey.mainMode = modeName;
}

std::string GetHotkeyReturnMode(const platform::config::HotkeyConfig& hotkey,
                                const platform::config::LinuxscreenConfig& config) {
    if (!hotkey.returnMode.empty()) {
        return hotkey.returnMode;
    }
    if (!hotkey.secondaryMode.empty() && !hotkey.mainMode.empty()) {
        return hotkey.mainMode;
    }
    return config.defaultMode;
}

void SetHotkeyReturnMode(platform::config::HotkeyConfig& hotkey,
                         const platform::config::LinuxscreenConfig& config,
                         const std::string& modeName) {
    if (modeName.empty() || (!config.defaultMode.empty() && modeName == config.defaultMode)) {
        hotkey.returnMode.clear();
        return;
    }
    hotkey.returnMode = modeName;
}

void ResetHotkeyCaptureModalState() {
    g_hotkeyCaptureModalState = HotkeyCaptureModalState{};
}

void RenderHotkeyCaptureModal() {
    if (!platform::config::IsHotkeyCapturing()) {
        ResetHotkeyCaptureModalState();
        return;
    }

    const platform::config::CaptureTarget captureTarget = platform::config::GetCaptureTarget();
    if (!CaptureTargetUsesSharedModal(captureTarget)) {
        return;
    }

    if (!g_hotkeyCaptureModalState.initialized) {
        g_hotkeyCaptureModalState.initialized = true;
        g_hotkeyCaptureModalState.lastSequence = platform::config::GetLatestBindingInputSequence();
        g_hotkeyCaptureModalState.hadKeysPressed = false;
        g_hotkeyCaptureModalState.bindingKeys.clear();
        g_hotkeyCaptureModalState.currentlyPressed.clear();
    }

    ImGui::OpenPopup("Bind Hotkey");
    if (!ImGui::BeginPopupModal("Bind Hotkey", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
        return;
    }

    constexpr ImGuiID kCaptureModalPulseId = static_cast<ImGuiID>(0x3E4C9F0Du);
    const float pulseWave = iam_oscillate(kCaptureModalPulseId,
                                          1.0f,
                                          1.35f,
                                          iam_wave_sine,
                                          0.0f,
                                          GetOverlayAnimationDeltaTime());
    const float pulse01 = 0.5f + (0.5f * pulseWave);
    const float styleAlpha = ImGui::GetStyle().Alpha;
    const ImVec4 listeningBaseColor = ImVec4(0.35f, 0.86f, 0.46f, 1.0f);
    const ImVec4 listeningTextColor = ImVec4(listeningBaseColor.x,
                                             listeningBaseColor.y,
                                             listeningBaseColor.z,
                                             styleAlpha * (0.55f + (0.45f * pulse01)));
    const ImVec4 listeningBorderColor = ImVec4(listeningBaseColor.x,
                                               listeningBaseColor.y,
                                               listeningBaseColor.z,
                                               styleAlpha * (0.20f + (0.30f * pulse01)));

    ImDrawList* popupDrawList = ImGui::GetWindowDrawList();
    const ImVec2 popupMin = ImGui::GetWindowPos();
    const ImVec2 popupMax = ImVec2(popupMin.x + ImGui::GetWindowSize().x,
                                   popupMin.y + ImGui::GetWindowSize().y);
    popupDrawList->AddRect(popupMin,
                           popupMax,
                           ImGui::ColorConvertFloat4ToU32(listeningBorderColor),
                           8.0f,
                           0,
                           2.0f);
    ImGui::TextColored(listeningTextColor, "Listening for input...");

    const bool singleKeyDisplay = CaptureTargetUsesSingleKeyDisplay(captureTarget);
    const bool canClear = CanClearCaptureTarget(captureTarget);

    if (singleKeyDisplay) {
        ImGui::Text("Press a key or mouse button.");
    } else {
        ImGui::Text("Press a key or key combination.");
    }
    ImGui::Text("Release key to confirm.");
    if (canClear) {
        ImGui::Text("Press Backspace/Delete to clear.");
    }
    ImGui::Text("Press ESC to cancel.");
    ImGui::Separator();

    platform::config::BindingInputEvent capturedEvent;
    while (platform::config::ConsumeBindingInputEventSince(g_hotkeyCaptureModalState.lastSequence, capturedEvent)) {
        if (capturedEvent.vk == platform::input::VK_NONE) {
            continue;
        }

        const bool isPress = capturedEvent.action == platform::input::InputAction::Press;
        const bool isRelease = capturedEvent.action == platform::input::InputAction::Release;
        if (!isPress && !isRelease) {
            continue;
        }

        const uint32_t vk = capturedEvent.vk;

        if (isPress) {
            if (vk == platform::input::VK_ESCAPE) {
                platform::config::CompleteCaptureCanceled();
                ResetHotkeyCaptureModalState();
                ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
                return;
            }

            if (canClear && (vk == platform::input::VK_BACK || vk == platform::input::VK_DELETE)) {
                platform::config::CompleteCaptureCleared();
                ResetHotkeyCaptureModalState();
                ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
                return;
            }

            if (!IsWindowsKeyVk(vk)) {
                g_hotkeyCaptureModalState.currentlyPressed.insert(vk);
                if (std::find(g_hotkeyCaptureModalState.bindingKeys.begin(),
                              g_hotkeyCaptureModalState.bindingKeys.end(),
                              vk) == g_hotkeyCaptureModalState.bindingKeys.end()) {
                    InsertCaptureKeyOrdered(g_hotkeyCaptureModalState.bindingKeys, vk);
                }
                g_hotkeyCaptureModalState.hadKeysPressed = true;
            }
        } else {
            g_hotkeyCaptureModalState.currentlyPressed.erase(vk);
        }
    }

    if (!canClear) { ImGui::BeginDisabled(); }
    if (AnimatedButton("Clear")) {
        platform::config::CompleteCaptureCleared();
        ResetHotkeyCaptureModalState();
        ImGui::CloseCurrentPopup();
        if (!canClear) { ImGui::EndDisabled(); }
        ImGui::EndPopup();
        return;
    }
    if (!canClear) { ImGui::EndDisabled(); }

    ImGui::SameLine();
    if (AnimatedButton("Cancel")) {
        platform::config::CompleteCaptureCanceled();
        ResetHotkeyCaptureModalState();
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    ImGui::Separator();

    if (g_hotkeyCaptureModalState.hadKeysPressed && g_hotkeyCaptureModalState.currentlyPressed.empty()) {
        if (singleKeyDisplay) {
            const uint32_t capturedVk = ResolveSingleCaptureVk(g_hotkeyCaptureModalState.bindingKeys);
            if (capturedVk != 0) {
                platform::config::CompleteCaptureConfirmed({ capturedVk });
            } else {
                platform::config::CompleteCaptureCanceled();
            }
        } else {
            platform::config::CompleteCaptureConfirmed(g_hotkeyCaptureModalState.bindingKeys);
        }
        ResetHotkeyCaptureModalState();
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    if (g_hotkeyCaptureModalState.bindingKeys.empty()) {
        ImGui::Text("Current: [None]");
    } else {
        if (singleKeyDisplay) {
            ImGui::Text("Current: %s", FormatSingleVk(ResolveSingleCaptureVk(g_hotkeyCaptureModalState.bindingKeys)).c_str());
        } else {
            ImGui::Text("Current: %s", FormatHotkey(g_hotkeyCaptureModalState.bindingKeys).c_str());
        }
    }

    ImGui::EndPopup();
}

#include "overlay_gui_theme.cpp"

} // namespace platform::x11
