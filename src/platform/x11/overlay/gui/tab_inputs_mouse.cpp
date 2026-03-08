void RenderMouseInputsTab(platform::config::LinuxscreenConfig& config, bool isCapturing) {
    ImGui::Text("Mouse Sensitivity");
    ImGui::Separator();

    float mouseSensitivity = config.mouseSensitivity;
    if (ImGui::SliderFloat("Global Sensitivity", &mouseSensitivity, 0.001f, 10.0f, "%.3fx")) {
        config.mouseSensitivity = mouseSensitivity;
        AutoSaveConfig(config);
    }

    ImGui::TextWrapped("Multiplies cursor-disabled mouselook movement. 1.0 = normal.");

    if (!platform::config::IsGameStateMonitorAvailable()) {
        ImGui::Separator();
        ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.2f, 1.0f),
                           "wpstateout.txt not found; state-based restrictions are disabled");
    }

    ImGui::Spacing();
    ImGui::Text("Sensitivity Hotkeys");
    ImGui::Separator();

    const platform::config::CaptureTarget captureTarget = platform::config::GetCaptureTarget();
    const int captureTargetIndex = platform::config::GetCaptureTargetIndex();
    const int captureSubIndex = platform::config::GetCaptureTargetSubIndex();

    const std::array<std::pair<const char*, const char*>, 6> gameStateOptions = {
        std::pair<const char*, const char*>{"wall", "Wall"},
        std::pair<const char*, const char*>{"inworld,unpaused", "In World (Unpaused)"},
        std::pair<const char*, const char*>{"inworld,paused", "In World (Paused)"},
        std::pair<const char*, const char*>{"inworld,gamescreenopen", "In World (Game Screen Open)"},
        std::pair<const char*, const char*>{"title", "Title"},
        std::pair<const char*, const char*>{"generating", "Generating / Waiting"},
    };

    auto hasState = [](const std::vector<std::string>& states, const char* value) {
        return platform::config::HasMatchingGameStateCondition(states, value);
    };

    auto setState = [](std::vector<std::string>& states, const char* value, bool enabled) {
        if (enabled) {
            if (std::find(states.begin(), states.end(), value) == states.end()) {
                states.emplace_back(value);
            }
            return;
        }
        platform::config::RemoveMatchingGameStateConditions(states, value);
    };

    if (config.sensitivityHotkeys.empty()) {
        ImGui::TextWrapped("No sensitivity hotkeys configured.");
    }

    int sensitivityHotkeyToDelete = -1;
    for (size_t i = 0; i < config.sensitivityHotkeys.size(); ++i) {
        auto& sensitivityHotkey = config.sensitivityHotkeys[i];
        ImGui::PushID(static_cast<int>(i));

        bool optionsOpen = false;
        if (ImGui::BeginTable("##sens_row", 2, ImGuiTableFlags_SizingStretchSame)) {
            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            const bool thisRowCapturing = isCapturing &&
                                          captureTarget == platform::config::CaptureTarget::SensitivityHotkey &&
                                          captureTargetIndex == static_cast<int>(i);
            const std::string bindBtnLabel = (thisRowCapturing ? std::string("Capturing...") : FormatHotkey(sensitivityHotkey.keys)) + "##sensbind";
            if (AnimatedButton(bindBtnLabel.c_str(), ImVec2(-1.0f, 0.0f))) {
                platform::config::StartSensitivityHotkeyCapture(static_cast<int>(i));
            }

            ImGui::TableNextColumn();
            optionsOpen = AnimatedCollapsingHeader("Options##sensopts");

            ImGui::EndTable();
        }

        if (optionsOpen) {
            HeaderRevealScope headerRevealScope = BeginAnimatedHeaderContentReveal();
            ImGui::Indent();

            bool toggle = sensitivityHotkey.toggle;
            if (ImGui::Checkbox("Toggle on repeated press", &toggle)) {
                sensitivityHotkey.toggle = toggle;
                AutoSaveConfig(config);
            }

            bool separateXY = sensitivityHotkey.separateXY;
            if (ImGui::Checkbox("Separate X/Y", &separateXY)) {
                sensitivityHotkey.separateXY = separateXY;
                if (sensitivityHotkey.separateXY) {
                    sensitivityHotkey.sensitivityX = sensitivityHotkey.sensitivity;
                    sensitivityHotkey.sensitivityY = sensitivityHotkey.sensitivity;
                }
                AutoSaveConfig(config);
            }

            if (sensitivityHotkey.separateXY) {
                float sensitivityX = sensitivityHotkey.sensitivityX;
                float sensitivityY = sensitivityHotkey.sensitivityY;
                if (ImGui::SliderFloat("Sensitivity X##sensx", &sensitivityX, 0.001f, 10.0f, "%.3fx")) {
                    sensitivityHotkey.sensitivityX = sensitivityX;
                    AutoSaveConfig(config);
                }
                if (ImGui::SliderFloat("Sensitivity Y##sensy", &sensitivityY, 0.001f, 10.0f, "%.3fx")) {
                    sensitivityHotkey.sensitivityY = sensitivityY;
                    AutoSaveConfig(config);
                }
            } else {
                float sensitivity = sensitivityHotkey.sensitivity;
                if (ImGui::SliderFloat("Sensitivity##sens", &sensitivity, 0.001f, 10.0f, "%.3fx")) {
                    sensitivityHotkey.sensitivity = sensitivity;
                    AutoSaveConfig(config);
                }
            }

            int debounceMs = sensitivityHotkey.debounce;
            if (ImGui::InputInt("Debounce (ms)##sensdebounce", &debounceMs)) {
                if (debounceMs < 0) {
                    debounceMs = 0;
                }
                sensitivityHotkey.debounce = debounceMs;
                AutoSaveConfig(config);
            }

            if (AnimatedCollapsingHeader("Required Game States##sensstates")) {


                HeaderRevealScope headerRevealScope = BeginAnimatedHeaderContentReveal();
                ImGui::Indent();
                std::string statesSummary;
                if (sensitivityHotkey.conditions.gameState.empty()) {
                    statesSummary = "Any";
                } else {
                    for (const auto& option : gameStateOptions) {
                        const bool active = (std::string(option.first) == "generating")
                            ? (hasState(sensitivityHotkey.conditions.gameState, "generating") ||
                               hasState(sensitivityHotkey.conditions.gameState, "waiting"))
                            : hasState(sensitivityHotkey.conditions.gameState, option.first);
                        if (active) {
                            if (!statesSummary.empty()) {
                                statesSummary += ", ";
                            }
                            statesSummary += option.second;
                        }
                    }
                    if (statesSummary.empty()) {
                        statesSummary = "Any";
                    }
                }

                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::BeginCombo("##sensstatescombo", statesSummary.c_str())) {
                    if (ImGui::Selectable("Any##sensstany",
                                          sensitivityHotkey.conditions.gameState.empty(),
                                          ImGuiSelectableFlags_DontClosePopups)) {
                        sensitivityHotkey.conditions.gameState.clear();
                        AutoSaveConfig(config);
                    }
                    ImGui::Separator();

                    bool statesChanged = false;
                    for (const auto& option : gameStateOptions) {
                        bool selected;
                        if (std::string(option.first) == "generating") {
                            selected = hasState(sensitivityHotkey.conditions.gameState, "generating") ||
                                       hasState(sensitivityHotkey.conditions.gameState, "waiting");
                        } else {
                            selected = hasState(sensitivityHotkey.conditions.gameState, option.first);
                        }
                        if (ImGui::Selectable(option.second, selected,
                                              ImGuiSelectableFlags_DontClosePopups)) {
                            if (std::string(option.first) == "generating") {
                                setState(sensitivityHotkey.conditions.gameState, "generating", !selected);
                                setState(sensitivityHotkey.conditions.gameState, "waiting", !selected);
                            } else {
                                setState(sensitivityHotkey.conditions.gameState, option.first, !selected);
                            }
                            statesChanged = true;
                        }
                    }
                    if (statesChanged) {
                        AutoSaveConfig(config);
                    }
                    ImGui::EndCombo();
                }
                ImGui::Unindent();
            }

            if (AnimatedCollapsingHeader("Exclusion Keys##sensexcl")) {


                HeaderRevealScope headerRevealScope = BeginAnimatedHeaderContentReveal();
                ImGui::Indent();
                int exclusionToRemove = -1;
                if (!sensitivityHotkey.conditions.exclusions.empty()) {
                    if (ImGui::BeginTable("##sensexcltable", 2, ImGuiTableFlags_SizingStretchSame)) {
                        for (size_t exclusionIndex = 0; exclusionIndex < sensitivityHotkey.conditions.exclusions.size(); ++exclusionIndex) {
                            const uint32_t exclusionVk = sensitivityHotkey.conditions.exclusions[exclusionIndex];
                            ImGui::PushID(static_cast<int>(exclusionIndex));
                            ImGui::TableNextRow();

                            ImGui::TableNextColumn();
                            const bool exclusionCapturing = isCapturing &&
                                                            captureTarget == platform::config::CaptureTarget::SensitivityExclusion &&
                                                            captureTargetIndex == static_cast<int>(i) &&
                                                            captureSubIndex == static_cast<int>(exclusionIndex);
                            const std::string exclusionLabel =
                                (exclusionCapturing ? std::string("Capturing...") : FormatSingleVk(exclusionVk)) + "##sensexclbind";
                            if (AnimatedButton(exclusionLabel.c_str(), ImVec2(-1.0f, 0.0f))) {
                                platform::config::StartSensitivityExclusionCapture(static_cast<int>(i), static_cast<int>(exclusionIndex));
                            }

                            ImGui::TableNextColumn();
                            if (AnimatedButton("Delete##sensexclrem", ImVec2(-1.0f, 0.0f))) {
                                exclusionToRemove = static_cast<int>(exclusionIndex);
                            }

                            ImGui::PopID();
                        }
                        ImGui::EndTable();
                    }
                }

                if (exclusionToRemove >= 0) {
                    sensitivityHotkey.conditions.exclusions.erase(
                        sensitivityHotkey.conditions.exclusions.begin() + exclusionToRemove);
                    AutoSaveConfig(config);
                }

                const bool addExclusionCapturing = isCapturing &&
                                                   captureTarget == platform::config::CaptureTarget::SensitivityExclusion &&
                                                   captureTargetIndex == static_cast<int>(i) &&
                                                   captureSubIndex == static_cast<int>(sensitivityHotkey.conditions.exclusions.size());
                const std::string addExclusionLabel =
                    std::string(addExclusionCapturing ? "Capturing..." : "Add Exclusion") + "##addsensexcl";
                if (AnimatedButton(addExclusionLabel.c_str())) {
                    platform::config::StartSensitivityExclusionCapture(
                        static_cast<int>(i),
                        static_cast<int>(sensitivityHotkey.conditions.exclusions.size()));
                }
                ImGui::Unindent();
            }

            ImGui::Spacing();
            if (AnimatedButton("Delete Sensitivity Hotkey##delsens")) {
                sensitivityHotkeyToDelete = static_cast<int>(i);
            }

            ImGui::Unindent();
        }

        if (i < config.sensitivityHotkeys.size() - 1) {
            ImGui::Separator();
        }

        ImGui::PopID();
    }

    if (sensitivityHotkeyToDelete >= 0) {
        config.sensitivityHotkeys.erase(config.sensitivityHotkeys.begin() + sensitivityHotkeyToDelete);
        AutoSaveConfig(config);
    }

    if (AnimatedButton("Add Sensitivity Hotkey")) {
        platform::config::SensitivityHotkeyConfig hotkey;
        hotkey.keys.push_back(static_cast<uint32_t>(platform::input::VK_F1));
        config.sensitivityHotkeys.push_back(std::move(hotkey));
        AutoSaveConfig(config);
    }
}
