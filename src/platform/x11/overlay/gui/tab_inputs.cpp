#include "../overlay_internal.h"
#include "imgui_overlay_helpers.h"
#include "tab_inputs.h"
#include "tab_inputs_helpers.h"
#include "tab_inputs_state.h"

namespace platform::x11 {

HotkeyEditorState g_hotkeyEditorState;
HotkeyCaptureModalState g_hotkeyCaptureModalState;
RebindLayoutState g_rebindLayoutState;

#include "tab_inputs_helpers.cpp"
#include "tab_inputs_hotkeys.cpp"
#include "tab_inputs_mouse.cpp"
#include "tab_inputs_rebinds.cpp"

void RenderInputsTab(platform::config::LinuxscreenConfig& config, bool isCapturing) {
    if (ImGui::BeginTabBar("##inputs_subtabs")) {
        if (ImGui::BeginTabItem("Global")) {
            ImGui::Text("Key Repeat");
            ImGui::Separator();

            int globalStartDelay = config.keyRepeatStartDelay;
            if (ImGui::SliderInt("Key Repeat Start Delay", &globalStartDelay, 0, 500, globalStartDelay == 0 ? "Default" : "%d ms")) {
                config.keyRepeatStartDelay = globalStartDelay;
                AutoSaveConfig(config);
            }
            HelpMarker("Delay before a held key starts repeating.\n0 = Use native default.");

            int globalRepeatDelay = config.keyRepeatDelay;
            const std::string globalRepeatDelayFormat = BuildRepeatDelaySliderFormat(globalRepeatDelay, "Default");
            if (ImGui::SliderInt("Key Repeat Delay", &globalRepeatDelay, 0, 500, globalRepeatDelayFormat.c_str())) {
                config.keyRepeatDelay = globalRepeatDelay;
                AutoSaveConfig(config);
            }
            HelpMarker("Time between repeated key events while held.\n0 = Use native default.");
            if (ShouldWarnAboutHotkeySlotRepeatRate(ResolveEffectiveRepeatDelayMs(globalRepeatDelay))) {
                ImGui::SameLine();
                RenderHotkeySlotRepeatRateWarningMarker();
            }

            bool affectMouseButtons = config.keyRepeatAffectsMouseButtons;
            if (ImGui::Checkbox("Affect mouse buttons", &affectMouseButtons)) {
                config.keyRepeatAffectsMouseButtons = affectMouseButtons;
                AutoSaveConfig(config);
            }
            HelpMarker("When off, global repeat shaping applies to keyboard keys only.\nPer-key mouse overrides still apply.\nMB1/MB2 are never repeated.");

            ImGui::Spacing();
            ImGui::Text("Mouse Sensitivity");
            ImGui::Separator();

            float mouseSensitivity = config.mouseSensitivity;
            if (ImGui::SliderFloat("Global Sensitivity", &mouseSensitivity, 0.001f, 10.0f, "%.3fx")) {
                config.mouseSensitivity = mouseSensitivity;
                AutoSaveConfig(config);
            }

            ImGui::TextWrapped("Multiplies cursor-disabled mouselook movement. 1.0 = normal.");

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Hotkeys & Rebinds")) {
            if (!platform::config::IsGameStateMonitorAvailable()) {
                ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.2f, 1.0f),
                                   "wpstateout.txt not found; state-based restrictions are disabled");
                ImGui::Separator();
            }

            RenderRebindsTab(config, isCapturing);
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            RenderHotkeysTab(config, isCapturing);
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            RenderSensitivityHotkeysSection(config, isCapturing);
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }
}

} // namespace platform::x11
