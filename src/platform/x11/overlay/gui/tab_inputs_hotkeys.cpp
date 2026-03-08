void RenderHotkeysTab(platform::config::LinuxscreenConfig& config, bool isCapturing) {
    ImGui::Text("Modes");
    ImGui::Separator();

    if (!platform::config::IsGameStateMonitorAvailable()) {
        ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.2f, 1.0f),
                           "wpstateout.txt not found; state-based restrictions are disabled");
        ImGui::Separator();
    }

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

    if (!config.hotkeys.empty()) {
        int hotkeyToDelete = -1;
        for (size_t i = 0; i < config.hotkeys.size(); ++i) {
            auto& hk = config.hotkeys[i];
            ImGui::PushID(static_cast<int>(i));

            bool optionsOpen = false;
            if (ImGui::BeginTable("##hk_row", 3, ImGuiTableFlags_SizingStretchSame)) {
                ImGui::TableNextRow();

                ImGui::TableNextColumn();
                const bool thisRowCapturing = isCapturing &&
                                              captureTarget == platform::config::CaptureTarget::Hotkey &&
                                              captureTargetIndex == static_cast<int>(i);
                const std::string bindBtnLabel = (thisRowCapturing ? std::string("Capturing...") : FormatHotkey(hk.keys)) + "##hkbind";
                if (AnimatedButton(bindBtnLabel.c_str(), ImVec2(-1.0f, 0.0f))) {
                    platform::config::StartHotkeyCapture(static_cast<int>(i));
                }

                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-1.0f);
                const std::string currentMode = GetHotkeyTargetMode(hk);
                const char* previewMode = currentMode.empty() ? "<select mode>" : currentMode.c_str();
                if (ImGui::BeginCombo("##targetmode", previewMode)) {
                    for (const auto& mode : config.modes) {
                        if (mode.name.empty()) continue;
                        const bool isDefaultMode = (!config.defaultMode.empty() && mode.name == config.defaultMode);
                        const bool isSelected = (currentMode == mode.name);
                        if (isDefaultMode) {
                            ImGui::BeginDisabled();
                        }
                        if (ImGui::Selectable(mode.name.c_str(), isSelected)) {
                            SetHotkeyTargetMode(hk, mode.name);
                            AutoSaveConfig(config);
                        }
                        if (isDefaultMode && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                            ImGui::SetTooltip("Default mode (%s) is reserved as the toggle-back mode.",
                                              config.defaultMode.c_str());
                        }
                        if (isDefaultMode) {
                            ImGui::EndDisabled();
                        }
                        if (isSelected) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }

                ImGui::TableNextColumn();
                optionsOpen = AnimatedCollapsingHeader("Options##hkopts");

                ImGui::EndTable();
            }

            if (optionsOpen) {
                HeaderRevealScope headerRevealScope = BeginAnimatedHeaderContentReveal();
                ImGui::Indent();

                bool returnToDefault = hk.returnToDefaultOnRepeat;
                if (ImGui::Checkbox("Return to default if active", &returnToDefault)) {
                    hk.returnToDefaultOnRepeat = returnToDefault;
                    AutoSaveConfig(config);
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("When enabled, pressing this hotkey while the target mode is already active will return to the default mode");
                }

                bool allowExitBypass = hk.allowExitToFullscreenRegardlessOfGameState;
                if (ImGui::Checkbox("Allow exit regardless of state", &allowExitBypass)) {
                    hk.allowExitToFullscreenRegardlessOfGameState = allowExitBypass;
                    AutoSaveConfig(config);
                }

                bool triggerOnRelease = hk.triggerOnRelease;
                if (ImGui::Checkbox("Trigger on bound input release", &triggerOnRelease)) {
                    hk.triggerOnRelease = triggerOnRelease;
                    AutoSaveConfig(config);
                }

                bool blockKey = hk.blockKeyFromGame;
                if (ImGui::Checkbox("Block bound input from game", &blockKey)) {
                    hk.blockKeyFromGame = blockKey;
                    AutoSaveConfig(config);
                }

                int debounceMs = hk.debounce;
                if (ImGui::InputInt("Debounce (ms)", &debounceMs)) {
                    if (debounceMs < 0) debounceMs = 0;
                    hk.debounce = debounceMs;
                    AutoSaveConfig(config);
                }

                if (AnimatedCollapsingHeader("Alternative Secondary Modes##altsec")) {


                    HeaderRevealScope headerRevealScope = BeginAnimatedHeaderContentReveal();
                    ImGui::Indent();
                    int altToRemove = -1;
                    if (!hk.altSecondaryModes.empty()) {
                        if (ImGui::BeginTable("##alt_table", 3, ImGuiTableFlags_SizingStretchSame)) {
                            for (size_t altIndex = 0; altIndex < hk.altSecondaryModes.size(); ++altIndex) {
                                auto& alt = hk.altSecondaryModes[altIndex];
                                ImGui::PushID(static_cast<int>(altIndex));
                                ImGui::TableNextRow();

                                ImGui::TableNextColumn();
                                const bool altCapturing = isCapturing &&
                                                          captureTarget == platform::config::CaptureTarget::AltSecondary &&
                                                          captureTargetIndex == static_cast<int>(i) &&
                                                          captureSubIndex == static_cast<int>(altIndex);
                                const std::string altBindLabel = (altCapturing ? std::string("Capturing...") : FormatHotkey(alt.keys)) + "##altbind";
                                if (AnimatedButton(altBindLabel.c_str(), ImVec2(-1.0f, 0.0f))) {
                                    platform::config::StartAltSecondaryCapture(static_cast<int>(i), static_cast<int>(altIndex));
                                }

                                ImGui::TableNextColumn();
                                ImGui::SetNextItemWidth(-1.0f);
                                const std::string altModeLabel = alt.mode.empty() ? "<select mode>" : alt.mode;
                                if (ImGui::BeginCombo("##altmode", altModeLabel.c_str())) {
                                    if (ImGui::Selectable("<none>##altnone", alt.mode.empty())) {
                                        alt.mode.clear();
                                        AutoSaveConfig(config);
                                    }
                                    for (const auto& mode : config.modes) {
                                        if (mode.name.empty()) continue;
                                        const bool isDefaultMode = (!config.defaultMode.empty() &&
                                                                    mode.name == config.defaultMode);
                                        const bool selected = (alt.mode == mode.name);
                                        if (isDefaultMode) {
                                            ImGui::BeginDisabled();
                                        }
                                        if (ImGui::Selectable(mode.name.c_str(), selected)) {
                                            alt.mode = mode.name;
                                            AutoSaveConfig(config);
                                        }
                                        if (isDefaultMode &&
                                            ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                                            ImGui::SetTooltip("Default mode (%s) is reserved as the toggle-back mode.",
                                                              config.defaultMode.c_str());
                                        }
                                        if (isDefaultMode) {
                                            ImGui::EndDisabled();
                                        }
                                        if (selected) ImGui::SetItemDefaultFocus();
                                    }
                                    ImGui::EndCombo();
                                }

                                ImGui::TableNextColumn();
                                if (AnimatedButton("Delete##altrem", ImVec2(-1.0f, 0.0f))) {
                                    altToRemove = static_cast<int>(altIndex);
                                }

                                ImGui::PopID();
                            }
                            ImGui::EndTable();
                        }
                    }
                    if (altToRemove >= 0) {
                        hk.altSecondaryModes.erase(hk.altSecondaryModes.begin() + altToRemove);
                        AutoSaveConfig(config);
                    }
                    if (AnimatedButton("Add Alternative Mode##addalt")) {
                        hk.altSecondaryModes.push_back(platform::config::AltSecondaryModeConfig{});
                        AutoSaveConfig(config);
                    }
                    ImGui::Unindent();
                }

                if (AnimatedCollapsingHeader("Required Game States##states")) {


                    HeaderRevealScope headerRevealScope = BeginAnimatedHeaderContentReveal();
                    ImGui::Indent();
                    std::string statesSummary;
                    if (hk.conditions.gameState.empty()) {
                        statesSummary = "Any";
                    } else {
                        for (const auto& option : gameStateOptions) {
                            const bool active = (std::string(option.first) == "generating")
                                ? (hasState(hk.conditions.gameState, "generating") ||
                                   hasState(hk.conditions.gameState, "waiting"))
                                : hasState(hk.conditions.gameState, option.first);
                            if (active) {
                                if (!statesSummary.empty()) statesSummary += ", ";
                                statesSummary += option.second;
                            }
                        }
                        if (statesSummary.empty()) statesSummary = "Any";
                    }
                    ImGui::SetNextItemWidth(-1.0f);
                    if (ImGui::BeginCombo("##states_combo", statesSummary.c_str())) {
                        if (ImGui::Selectable("Any##stany", hk.conditions.gameState.empty(),
                                              ImGuiSelectableFlags_DontClosePopups)) {
                            hk.conditions.gameState.clear();
                            AutoSaveConfig(config);
                        }
                        ImGui::Separator();
                        bool statesChanged = false;
                        for (const auto& option : gameStateOptions) {
                            bool selected;
                            if (std::string(option.first) == "generating") {
                                selected = hasState(hk.conditions.gameState, "generating") ||
                                           hasState(hk.conditions.gameState, "waiting");
                            } else {
                                selected = hasState(hk.conditions.gameState, option.first);
                            }
                            if (ImGui::Selectable(option.second, selected,
                                                  ImGuiSelectableFlags_DontClosePopups)) {
                                if (std::string(option.first) == "generating") {
                                    setState(hk.conditions.gameState, "generating", !selected);
                                    setState(hk.conditions.gameState, "waiting", !selected);
                                } else {
                                    setState(hk.conditions.gameState, option.first, !selected);
                                }
                                statesChanged = true;
                            }
                        }
                        if (statesChanged) AutoSaveConfig(config);
                        ImGui::EndCombo();
                    }
                    ImGui::Unindent();
                }

                if (AnimatedCollapsingHeader("Exclusion Keys##excl")) {


                    HeaderRevealScope headerRevealScope = BeginAnimatedHeaderContentReveal();
                    ImGui::Indent();
                    int exclToRemove = -1;
                    if (!hk.conditions.exclusions.empty()) {
                        if (ImGui::BeginTable("##excl_table", 2, ImGuiTableFlags_SizingStretchSame)) {
                            for (size_t exclusionIndex = 0; exclusionIndex < hk.conditions.exclusions.size(); ++exclusionIndex) {
                                const uint32_t exclusionVk = hk.conditions.exclusions[exclusionIndex];
                                ImGui::PushID(static_cast<int>(exclusionIndex));
                                ImGui::TableNextRow();

                                ImGui::TableNextColumn();
                                const bool exclusionCapturing = isCapturing &&
                                                                captureTarget == platform::config::CaptureTarget::Exclusion &&
                                                                captureTargetIndex == static_cast<int>(i) &&
                                                                captureSubIndex == static_cast<int>(exclusionIndex);
                                const std::string exclBtnLabel = (exclusionCapturing ? std::string("Capturing...") : FormatSingleVk(exclusionVk)) + "##exclbind";
                                if (AnimatedButton(exclBtnLabel.c_str(), ImVec2(-1.0f, 0.0f))) {
                                    platform::config::StartExclusionCapture(static_cast<int>(i), static_cast<int>(exclusionIndex));
                                }

                                ImGui::TableNextColumn();
                                if (AnimatedButton("Delete##exclrem", ImVec2(-1.0f, 0.0f))) {
                                    exclToRemove = static_cast<int>(exclusionIndex);
                                }

                                ImGui::PopID();
                            }
                            ImGui::EndTable();
                        }
                    }
                    if (exclToRemove >= 0) {
                        hk.conditions.exclusions.erase(hk.conditions.exclusions.begin() + exclToRemove);
                        AutoSaveConfig(config);
                    }
                    const bool addExclCapturing = isCapturing &&
                                                  captureTarget == platform::config::CaptureTarget::Exclusion &&
                                                  captureTargetIndex == static_cast<int>(i) &&
                                                  captureSubIndex == static_cast<int>(hk.conditions.exclusions.size());
                    const std::string addExclLabel = std::string(addExclCapturing ? "Capturing..." : "Add Exclusion") + "##addexcl";
                    if (AnimatedButton(addExclLabel.c_str())) {
                        platform::config::StartExclusionCapture(static_cast<int>(i),
                                                                static_cast<int>(hk.conditions.exclusions.size()));
                    }
                    ImGui::Unindent();
                }

                ImGui::Spacing();
                if (AnimatedButton("Delete Hotkey##delhk")) {
                    hotkeyToDelete = static_cast<int>(i);
                }

                ImGui::Unindent();
            }

            if (i < config.hotkeys.size() - 1) {
                ImGui::Separator();
            }
            ImGui::PopID();
        }

        if (hotkeyToDelete >= 0) {
            DeleteHotkey(config, static_cast<size_t>(hotkeyToDelete));
            AutoSaveConfig(config);
        }
    } else {
        ImGui::TextWrapped("No mode hotkeys configured.");
    }

    ImGui::Separator();
    if (!g_hotkeyEditorState.showAddHotkeyDialog) {
        if (AnimatedButton("Add Hotkey")) {
            g_hotkeyEditorState.showAddHotkeyDialog = true;
            g_hotkeyEditorState.selectedModeForNewHotkey = 0;
        }
    } else {
        ImGui::Text("Target mode for new hotkey:");

        std::string selectedModeName;
        int validModeCount = 0;
        for (int j = 0; j < static_cast<int>(config.modes.size()); ++j) {
            if (!config.modes[j].name.empty() && config.modes[j].name != config.defaultMode) {
                if (validModeCount == 0) {
                    selectedModeName = config.modes[j].name;
                }
                if (validModeCount == g_hotkeyEditorState.selectedModeForNewHotkey) {
                    selectedModeName = config.modes[j].name;
                    break;
                }
                validModeCount++;
            }
        }

        ImGui::SetNextItemWidth(220.0f);
        if (ImGui::BeginCombo("##newhotkeymode", selectedModeName.empty() ? "<no modes>" : selectedModeName.c_str())) {
            int modeIdx = 0;
            for (const auto& mode : config.modes) {
                if (mode.name.empty() || mode.name == config.defaultMode) continue;
                bool isSelected = (modeIdx == g_hotkeyEditorState.selectedModeForNewHotkey);
                if (ImGui::Selectable(mode.name.c_str(), isSelected)) {
                    g_hotkeyEditorState.selectedModeForNewHotkey = modeIdx;
                }
                if (isSelected) {
                    ImGui::SetItemDefaultFocus();
                }
                modeIdx++;
            }
            ImGui::EndCombo();
        }

        if (AnimatedButton("Create")) {
            if (!selectedModeName.empty()) {
                AddNewHotkey(config, selectedModeName);
                AutoSaveConfig(config);
                g_hotkeyEditorState.showAddHotkeyDialog = false;
            }
        }
        ImGui::SameLine();
        if (AnimatedButton("Cancel")) {
            g_hotkeyEditorState.showAddHotkeyDialog = false;
        }
    }

    ImGui::Spacing();
    ImGui::Text("Other");
    ImGui::Separator();
    ImGui::Text("Open/Close GUI:");
    ImGui::SameLine();
    const bool guiCapturing = isCapturing && captureTarget == platform::config::CaptureTarget::GuiHotkey;
    const std::string guiBtnLabel = (guiCapturing ? std::string("Capturing...") : FormatHotkey(config.guiHotkey)) + "##guibind";
    if (AnimatedButton(guiBtnLabel.c_str())) {
        platform::config::StartGuiHotkeyCapture();
    }

    ImGui::Text("Toggle key rebinds:");
    ImGui::SameLine();
    const bool rebindToggleCapturing = isCapturing && captureTarget == platform::config::CaptureTarget::RebindToggleHotkey;
    const std::string rebindToggleLabel =
        (rebindToggleCapturing ? std::string("Capturing...") : FormatHotkey(config.rebindToggleHotkey)) + "##rebindtogglebind";
    if (AnimatedButton(rebindToggleLabel.c_str())) {
        platform::config::StartRebindToggleHotkeyCapture();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("A green indicator in the bottom-left of the window shows when key rebinds are active; red when inactive.");
    }
}
