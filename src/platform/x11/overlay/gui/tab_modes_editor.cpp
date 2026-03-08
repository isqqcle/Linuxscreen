void RenderModesTab(platform::config::LinuxscreenConfig& config, const std::string& activeMode,
                     platform::x11::MirrorModeState& modeState) {
    if (g_modeEditorState.lastModeCountForBackgroundCopy != config.modes.size()) {
        g_modeEditorState.lastModeCountForBackgroundCopy = config.modes.size();
        g_modeEditorState.backgroundCopySourceIndex = -1;
        g_modeEditorState.backgroundCopyTargets.clear();
        g_modeEditorState.backgroundCopyModalSourceIndex = -1;
        g_modeEditorState.backgroundCopyModalTargets.clear();
    }

    if (!config.modes.empty()) {
        int modeContainerWidth = 0;
        int modeContainerHeight = 0;
        GetCurrentModeSizingContainer(modeContainerWidth, modeContainerHeight);

        int modeToDelete = -1;
        for (size_t modeIndex = 0; modeIndex < config.modes.size(); ++modeIndex) {
            auto& mode = config.modes[modeIndex];
            ImGui::PushID(static_cast<int>(modeIndex));

            if (mode.name.empty()) {
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "[Mode %zu: missing name]", modeIndex);
                ImGui::PopID();
                continue;
            }

            bool isActive = (mode.name == activeMode);
            
            bool editModeOpen = false;
            const float defaultButtonWidth = 120.0f;
            if (ImGui::BeginTable("##mode_row", 3, ImGuiTableFlags_SizingFixedFit)) {
                ImGui::TableSetupColumn("##mode", ImGuiTableColumnFlags_WidthFixed, 150.0f);
                ImGui::TableSetupColumn("##edit", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("##default", ImGuiTableColumnFlags_WidthFixed, defaultButtonWidth);
                ImGui::TableNextRow();

                ImGui::TableNextColumn();
                if (isActive) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.2f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.7f, 0.3f, 1.0f));
                }

                if (AnimatedButton(mode.name.c_str(), ImVec2(150, 0))) {
                    if (!isActive) {
                        StartModeSwitchWithTransition(mode.name, config, modeState);
                    }
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Click to switch to this mode.");
                }

                if (isActive) {
                    ImGui::PopStyleColor(2);
                }

                ImGui::TableNextColumn();
                editModeOpen = AnimatedCollapsingHeader("Edit Mode");

                ImGui::TableNextColumn();
                bool isDefault = (config.defaultMode == mode.name);
                if (isDefault) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.2f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.7f, 0.3f, 1.0f));
                }

                if (AnimatedButton("Default", ImVec2(defaultButtonWidth, 0))) {
                    if (!isDefault) {
                        config.defaultMode = mode.name;
                        AutoSaveConfig(config);
                    }
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("A default mode is active on startup.");
                }

                if (isDefault) {
                    ImGui::PopStyleColor(2);
                }

                ImGui::EndTable();
            }

            bool canDelete = (config.modes.size() > 1 && !isActive);

            if (editModeOpen) {
                HeaderRevealScope headerRevealScope = BeginAnimatedHeaderContentReveal();

                std::string& renameBuffer = g_modeEditorState.renameBuffers[modeIndex];
                if (renameBuffer.empty() || renameBuffer != mode.name) {
                    renameBuffer = mode.name;
                }

                const float nameRowStartX = ImGui::GetCursorPosX();
                ImGui::Text("Name:");
                ImGui::SameLine();
                ImGui::SetCursorPosX(nameRowStartX + 80.0f);
                char renameBufferCstr[256];
                strncpy(renameBufferCstr, renameBuffer.c_str(), sizeof(renameBufferCstr) - 1);
                renameBufferCstr[sizeof(renameBufferCstr) - 1] = '\0';
                std::string inputId = "##modename" + std::to_string(modeIndex);
                if (ImGui::InputText(inputId.c_str(), renameBufferCstr, sizeof(renameBufferCstr))) {
                    renameBuffer = renameBufferCstr;
                    std::string newName = renameBuffer;
                    bool isValid = !newName.empty();
                    if (isValid && newName != mode.name) {
                        for (const auto& otherMode : config.modes) {
                            if (otherMode.name == newName) {
                                isValid = false;
                                break;
                            }
                        }
                    }
                    
                    if (isValid && newName != mode.name) {
                        std::string oldName = mode.name;
                        bool renamedActiveMode = (activeMode == oldName);
                        mode.name = newName;

                        platform::config::RenameModeInHotkeys(config, oldName, newName);

                        if (config.defaultMode == oldName) {
                            config.defaultMode = newName;
                        }

                        if (renamedActiveMode) {
                            modeState.ApplyModeSwitch(newName, config);
                            platform::x11::TriggerImmediateModeResizeEnforcement();
                        }
                        
                        AutoSaveConfig(config);
                    }
                }
                
                ImGui::Indent();
                {
                    int resolvedW = 0, resolvedH = 0;
                    const char* resType = "Static";
                    if (mode.useRelativeSize) {
                        resType = "Relative";
                        ResolveModeDimensionsForEditor(mode, modeContainerWidth, modeContainerHeight, resolvedW, resolvedH);
                    } else {
                        resolvedW = mode.width;
                        resolvedH = mode.height;
                    }
                    std::string resLabel = "Resolution: " + std::to_string(resolvedW) + "x" + std::to_string(resolvedH) + " (" + resType + ")###mode_resolution";
                    if (AnimatedCollapsingHeader(resLabel.c_str())) {

                        HeaderRevealScope headerRevealScope = BeginAnimatedHeaderContentReveal();
                    ImGui::Indent();
                bool refreshActiveModeSizing = false;
                bool useRelativeSize = mode.useRelativeSize;
                std::string useRelativeSizeLabel = "Use Relative Size";
                if (useRelativeSize) {
                    int relativeResolvedWidth = 0;
                    int relativeResolvedHeight = 0;
                    if (ResolveModeDimensionsForEditor(mode,
                                                       modeContainerWidth,
                                                       modeContainerHeight,
                                                       relativeResolvedWidth,
                                                       relativeResolvedHeight)) {
                        useRelativeSizeLabel.push_back(' ');
                        useRelativeSizeLabel.push_back('(');
                        useRelativeSizeLabel.append(std::to_string(relativeResolvedWidth));
                        useRelativeSizeLabel.push_back('x');
                        useRelativeSizeLabel.append(std::to_string(relativeResolvedHeight));
                        useRelativeSizeLabel.push_back(')');
                    }
                }
                useRelativeSizeLabel.append("##use_relative_size");

                if (ImGui::Checkbox(useRelativeSizeLabel.c_str(), &useRelativeSize)) {
                    mode.useRelativeSize = useRelativeSize;
                    refreshActiveModeSizing = true;
                    AutoSaveConfig(config);
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("When enabled, computes width/height as a percentage of the current window size, rather than absolute pixel inputs.");
                }

                if (mode.useRelativeSize) {
                    float relativeWidth = mode.relativeWidth;
                    float relativeHeight = mode.relativeHeight;
                    bool widthChanged = false;
                    bool heightChanged = false;
                    if (ImGui::DragFloat("Width %", &relativeWidth, 0.01f, 0.1f, 1.0f, "%.2f")) {
                        mode.relativeWidth = relativeWidth;
                        widthChanged = true;
                        AutoSaveConfig(config);
                    }
                    if (ImGui::DragFloat("Height %", &relativeHeight, 0.01f, 0.1f, 1.0f, "%.2f")) {
                        mode.relativeHeight = relativeHeight;
                        heightChanged = true;
                        AutoSaveConfig(config);
                    }
                    if ((widthChanged || heightChanged) && mode.name == activeMode) {
                        StartModeSwitchWithTransition(mode.name, config, modeState);
                    }
                } else {
                    int width = mode.width;
                    int height = mode.height;
                    bool widthChanged = false;
                    bool heightChanged = false;
                    constexpr int kModeDimensionMin = 0;
                    constexpr int kModeDimensionMax = 16384;
                    if (ImGui::InputInt("Width", &width, 1, 100)) {
                        width = std::clamp(width, kModeDimensionMin, kModeDimensionMax);
                        mode.width = width;
                        widthChanged = true;
                        AutoSaveConfig(config);
                    }
                    if (ImGui::InputInt("Height", &height, 1, 100)) {
                        height = std::clamp(height, kModeDimensionMin, kModeDimensionMax);
                        mode.height = height;
                        heightChanged = true;
                        AutoSaveConfig(config);
                    }
                    if ((widthChanged || heightChanged) && mode.name == activeMode) {
                        StartModeSwitchWithTransition(mode.name, config, modeState);
                    }
                }

                if (refreshActiveModeSizing && mode.name == activeMode) {
                    StartModeSwitchWithTransition(mode.name, config, modeState);
                }
                ImGui::Unindent();
                }
                }

                {
                    int resolvedPosW = 0, resolvedPosH = 0;
                    ResolveModeDimensionsForEditor(mode, modeContainerWidth, modeContainerHeight, resolvedPosW, resolvedPosH);
                    int actualX = 0, actualY = 0;
                    std::string anchorPreset = mode.positionPreset.empty() ? "topLeftScreen" : mode.positionPreset;
                    platform::config::GetRelativeCoords(anchorPreset, mode.x, mode.y, resolvedPosW, resolvedPosH,
                                                        modeContainerWidth, modeContainerHeight, actualX, actualY);
                    
                    std::string anchorLabel = "Custom";
                    static const char* modePositionPresets[] = {"centerScreen", "topLeftScreen", "topCenterScreen", "topRightScreen", "middleLeftScreen", "middleRightScreen", "bottomLeftScreen", "bottomCenterScreen", "bottomRightScreen"};
                    static const char* modePositionPresetLabels[] = {"Center", "Top Left", "Top Center", "Top Right", "Middle Left", "Middle Right", "Bottom Left", "Bottom Center", "Bottom Right"};
                    for (int i = 0; i < IM_ARRAYSIZE(modePositionPresets); ++i) {
                        if (anchorPreset == modePositionPresets[i]) {
                            anchorLabel = modePositionPresetLabels[i];
                            break;
                        }
                    }
                    std::string posLabel = "Position: " + anchorLabel;
                    posLabel += " + (" + std::to_string(mode.x) + ", " + std::to_string(mode.y) + ")";
                    posLabel += " = (" + std::to_string(actualX) + ", " + std::to_string(actualY) + ")###mode_position";
                    if (AnimatedCollapsingHeader(posLabel.c_str())) {

                        HeaderRevealScope headerRevealScope = BeginAnimatedHeaderContentReveal();
                    ImGui::Indent();
                ImGui::Text("Window Anchor:");
                ImGui::SameLine();
                HelpMarker("Select where the mode is anchored. X/Y are offsets from that anchor.");

                static const char* modePositionPresets[] = {
                    "centerScreen", "topLeftScreen", "topCenterScreen", "topRightScreen",
                    "middleLeftScreen", "middleRightScreen",
                    "bottomLeftScreen", "bottomCenterScreen", "bottomRightScreen"
                };
                static const char* modePositionPresetLabels[] = {
                    "Center", "Top Left", "Top Center", "Top Right",
                    "Middle Left", "Middle Right",
                    "Bottom Left", "Bottom Center", "Bottom Right"
                };

                // Legacy alias: "custom" behaves the same as topLeftScreen.
                if (mode.positionPreset == "custom") {
                    mode.positionPreset = "topLeftScreen";
                    AutoSaveConfig(config);
                }

                int currentModePreset = 0;
                for (int i = 0; i < IM_ARRAYSIZE(modePositionPresets); ++i) {
                    if (mode.positionPreset == modePositionPresets[i]) {
                        currentModePreset = i;
                        break;
                    }
                }

                if (ImGui::Combo("Anchor Preset", &currentModePreset, modePositionPresetLabels, IM_ARRAYSIZE(modePositionPresetLabels))) {
                    mode.positionPreset = modePositionPresets[currentModePreset];
                    AutoSaveConfig(config);
                    if (mode.name == activeMode) {
                        StartModeSwitchWithTransition(mode.name, config, modeState);
                    }
                }

                int modeX = mode.x;
                int modeY = mode.y;
                bool posChanged = false;
                if (ImGui::InputInt("X Offset", &modeX)) {
                    mode.x = modeX;
                    posChanged = true;
                    AutoSaveConfig(config);
                }
                if (ImGui::InputInt("Y Offset", &modeY)) {
                    mode.y = modeY;
                    posChanged = true;
                    AutoSaveConfig(config);
                }
                if (posChanged && mode.name == activeMode) {
                    StartModeSwitchWithTransition(mode.name, config, modeState);
                }
                ImGui::Unindent();
                }
                }

                {
                    std::string borderLabel = "Border";
                    if (mode.border.enabled) {
                        borderLabel += " (enabled)";
                    }
                    borderLabel += "###mode_border";
                    if (AnimatedCollapsingHeader(borderLabel.c_str())) {

                        HeaderRevealScope headerRevealScope = BeginAnimatedHeaderContentReveal();
                    ImGui::Indent();

                    bool borderEnabled = mode.border.enabled;
                    if (ImGui::Checkbox("Enable Border##mode_border_enable", &borderEnabled)) {
                        mode.border.enabled = borderEnabled;
                        AutoSaveConfig(config);
                    }
                    ImGui::SameLine();
                    HelpMarker("Draw a border around the game viewport. Border appears outside the game area.");

                    if (mode.border.enabled) {
                        float borderColor[3] = {
                            mode.border.color.r,
                            mode.border.color.g,
                            mode.border.color.b,
                        };
                        if (ImGui::ColorEdit3("Border Color##mode_border_color",
                                              borderColor,
                                              ImGuiColorEditFlags_NoInputs)) {
                            mode.border.color.r = std::clamp(borderColor[0], 0.0f, 1.0f);
                            mode.border.color.g = std::clamp(borderColor[1], 0.0f, 1.0f);
                            mode.border.color.b = std::clamp(borderColor[2], 0.0f, 1.0f);
                            AutoSaveConfig(config);
                        }

                        int borderWidth = mode.border.width;
                        if (ImGui::InputInt("Width##mode_border_width", &borderWidth, 1, 10)) {
                            mode.border.width = std::clamp(borderWidth, 1, 50);
                            AutoSaveConfig(config);
                        }

                        int borderRadius = mode.border.radius;
                        if (ImGui::InputInt("Corner Radius##mode_border_radius", &borderRadius, 1, 10)) {
                            mode.border.radius = std::clamp(borderRadius, 0, 100);
                            AutoSaveConfig(config);
                        }
                    }

                    ImGui::Unindent();
                    }
                }

                {
                    std::string sensitivityLabel = "Sensitivity Override";
                    if (mode.sensitivityOverrideEnabled) {
                        sensitivityLabel += " (enabled)";
                    }
                    sensitivityLabel += "###mode_sensitivity";
                    if (AnimatedCollapsingHeader(sensitivityLabel.c_str())) {

                        HeaderRevealScope headerRevealScope = BeginAnimatedHeaderContentReveal();
                    ImGui::Indent();

                    bool sensitivityOverrideEnabled = mode.sensitivityOverrideEnabled;
                    if (ImGui::Checkbox("Enable Override##mode_sensitivity_enable", &sensitivityOverrideEnabled)) {
                        mode.sensitivityOverrideEnabled = sensitivityOverrideEnabled;
                        AutoSaveConfig(config);
                    }

                    if (mode.sensitivityOverrideEnabled) {
                        bool separateXY = mode.separateXYSensitivity;
                        if (ImGui::Checkbox("Separate X/Y##mode_sensitivity_xy", &separateXY)) {
                            mode.separateXYSensitivity = separateXY;
                            if (mode.separateXYSensitivity) {
                                mode.modeSensitivityX = mode.modeSensitivity;
                                mode.modeSensitivityY = mode.modeSensitivity;
                            }
                            AutoSaveConfig(config);
                        }

                        if (mode.separateXYSensitivity) {
                            float sensitivityX = mode.modeSensitivityX;
                            float sensitivityY = mode.modeSensitivityY;
                            if (ImGui::SliderFloat("Sensitivity X##mode_sensitivity_x", &sensitivityX, 0.001f, 10.0f, "%.3fx")) {
                                mode.modeSensitivityX = sensitivityX;
                                AutoSaveConfig(config);
                            }
                            if (ImGui::SliderFloat("Sensitivity Y##mode_sensitivity_y", &sensitivityY, 0.001f, 10.0f, "%.3fx")) {
                                mode.modeSensitivityY = sensitivityY;
                                AutoSaveConfig(config);
                            }
                        } else {
                            float sensitivity = mode.modeSensitivity;
                            if (ImGui::SliderFloat("Sensitivity##mode_sensitivity", &sensitivity, 0.001f, 10.0f, "%.3fx")) {
                                mode.modeSensitivity = sensitivity;
                                AutoSaveConfig(config);
                            }
                        }
                    }

                    ImGui::Unindent();
                    }
                }

                {
                    mode.background.selectedMode = NormalizeBackgroundSelectedMode(mode.background.selectedMode);
                    std::string backgroundLabel = std::string("Background (") +
                                                  BackgroundTypeLabel(mode.background.selectedMode) +
                                                  ")###mode_background";
                    if (AnimatedCollapsingHeader(backgroundLabel.c_str())) {

                        HeaderRevealScope headerRevealScope = BeginAnimatedHeaderContentReveal();
                    ImGui::Indent();

                    if (ImGui::RadioButton("Color##mode_bg_type", mode.background.selectedMode == "color")) {
                        mode.background.selectedMode = "color";
                        AutoSaveConfig(config);
                    }
                    ImGui::SameLine();
                    if (ImGui::RadioButton("Gradient##mode_bg_type", mode.background.selectedMode == "gradient")) {
                        mode.background.selectedMode = "gradient";
                        AutoSaveConfig(config);
                    }
                    ImGui::SameLine();
                    if (ImGui::RadioButton("Image##mode_bg_type", mode.background.selectedMode == "image")) {
                        mode.background.selectedMode = "image";
                        AutoSaveConfig(config);
                    }

                    if (mode.background.selectedMode == "color") {
                        float color[3] = {
                            mode.background.color.r,
                            mode.background.color.g,
                            mode.background.color.b,
                        };
                        if (ImGui::ColorEdit3("Background Color##mode_bg_color", color)) {
                            mode.background.color.r = std::clamp(color[0], 0.0f, 1.0f);
                            mode.background.color.g = std::clamp(color[1], 0.0f, 1.0f);
                            mode.background.color.b = std::clamp(color[2], 0.0f, 1.0f);
                            AutoSaveConfig(config);
                        }
                    } else if (mode.background.selectedMode == "gradient") {
                        if (mode.background.gradientStops.size() < 2) {
                            mode.background.gradientStops.clear();
                            mode.background.gradientStops.push_back(
                                platform::config::GradientStop{platform::config::Color{0.0f, 0.0f, 0.0f, 1.0f}, 0.0f});
                            mode.background.gradientStops.push_back(
                                platform::config::GradientStop{platform::config::Color{1.0f, 1.0f, 1.0f, 1.0f}, 1.0f});
                        }

                        if (ImGui::SliderFloat("Gradient Angle##mode_bg_angle",
                                               &mode.background.gradientAngle,
                                               0.0f,
                                               360.0f,
                                               "%.1f")) {
                            AutoSaveConfig(config);
                        }

                        static const char* kAnimationNames[] = {
                            "None",
                            "Rotate",
                            "Slide",
                            "Wave",
                            "Spiral",
                            "Fade",
                        };
                        int animationIndex = GradientAnimationTypeToComboIndex(mode.background.gradientAnimation);
                        if (ImGui::Combo("Animation##mode_bg_anim",
                                         &animationIndex,
                                         kAnimationNames,
                                         IM_ARRAYSIZE(kAnimationNames))) {
                            mode.background.gradientAnimation = ComboIndexToGradientAnimationType(animationIndex);
                            AutoSaveConfig(config);
                        }
                        if (ImGui::SliderFloat("Animation Speed##mode_bg_anim_speed",
                                               &mode.background.gradientAnimationSpeed,
                                               0.01f,
                                               10.0f,
                                               "%.2f")) {
                            mode.background.gradientAnimationSpeed =
                                std::max(0.01f, mode.background.gradientAnimationSpeed);
                            AutoSaveConfig(config);
                        }

                        ImGui::Text("Gradient Stops:");
                        int stopToRemove = -1;
                        bool stopsChanged = false;
                        if (ImGui::BeginTable("mode_bg_gradient_stops",
                                              4,
                                              ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV)) {
                            ImGui::TableSetupColumn("Stop###mode_bg_grad_stop_col",
                                                    ImGuiTableColumnFlags_WidthFixed,
                                                    52.0f);
                            ImGui::TableSetupColumn("Color###mode_bg_grad_color_col",
                                                    ImGuiTableColumnFlags_WidthFixed,
                                                    110.0f);
                            ImGui::TableSetupColumn("Position###mode_bg_grad_pos_col",
                                                    ImGuiTableColumnFlags_WidthStretch,
                                                    1.0f);
                            ImGui::TableSetupColumn("###mode_bg_grad_remove_col",
                                                    ImGuiTableColumnFlags_WidthFixed,
                                                    ImGui::GetFrameHeight() + 6.0f);
                            ImGui::TableHeadersRow();

                            for (size_t stopIndex = 0; stopIndex < mode.background.gradientStops.size(); ++stopIndex) {
                                auto& stop = mode.background.gradientStops[stopIndex];
                                ImGui::PushID(static_cast<int>(stopIndex));

                                ImGui::TableNextRow();

                                ImGui::TableSetColumnIndex(0);
                                ImGui::Text("%d", static_cast<int>(stopIndex) + 1);

                                ImGui::TableSetColumnIndex(1);
                                float stopColor[3] = {stop.color.r, stop.color.g, stop.color.b};
                                if (ImGui::ColorEdit3("##mode_bg_stop_color",
                                                      stopColor,
                                                      ImGuiColorEditFlags_NoInputs)) {
                                    stop.color.r = std::clamp(stopColor[0], 0.0f, 1.0f);
                                    stop.color.g = std::clamp(stopColor[1], 0.0f, 1.0f);
                                    stop.color.b = std::clamp(stopColor[2], 0.0f, 1.0f);
                                    stopsChanged = true;
                                }

                                ImGui::TableSetColumnIndex(2);
                                ImGui::SetNextItemWidth(-1.0f);
                                float stopPosition = stop.position;
                                if (ImGui::SliderFloat("##mode_bg_stop_pos", &stopPosition, 0.0f, 1.0f, "%.3f")) {
                                    stop.position = std::clamp(stopPosition, 0.0f, 1.0f);
                                    stopsChanged = true;
                                }

                                ImGui::TableSetColumnIndex(3);
                                if (mode.background.gradientStops.size() <= 2) {
                                    ImGui::BeginDisabled();
                                }
                                if (ImGui::SmallButton("X##mode_bg_stop_remove")) {
                                    stopToRemove = static_cast<int>(stopIndex);
                                }
                                if (mode.background.gradientStops.size() <= 2) {
                                    ImGui::EndDisabled();
                                }

                                ImGui::PopID();
                            }

                            ImGui::EndTable();
                        }

                        if (stopToRemove >= 0 &&
                            stopToRemove < static_cast<int>(mode.background.gradientStops.size()) &&
                            mode.background.gradientStops.size() > 2) {
                            mode.background.gradientStops.erase(mode.background.gradientStops.begin() + stopToRemove);
                            stopsChanged = true;
                        }

                        if (mode.background.gradientStops.size() < 8 &&
                            ImGui::SmallButton("Add Stop##mode_bg_add_stop")) {
                            const float defaultPosition = mode.background.gradientStops.empty()
                                                              ? 0.5f
                                                              : std::clamp(mode.background.gradientStops.back().position + 0.1f, 0.0f, 1.0f);
                            mode.background.gradientStops.push_back(
                                platform::config::GradientStop{platform::config::Color{1.0f, 1.0f, 1.0f, 1.0f}, defaultPosition});
                            stopsChanged = true;
                        }

                        if (stopsChanged) {
                            SortGradientStops(mode.background.gradientStops);
                            AutoSaveConfig(config);
                        }
                    } else {
                        char imagePathBuffer[1024];
                        std::snprintf(imagePathBuffer, sizeof(imagePathBuffer), "%s", mode.background.image.c_str());
                        if (ImGui::InputText("Image Path##mode_bg_image_path", imagePathBuffer, sizeof(imagePathBuffer))) {
                            mode.background.image = imagePathBuffer;
                            AutoSaveConfig(config);
                        }
                        ImGui::SameLine();
                        if (AnimatedButton("Browse##mode_bg_image_browse")) {
                            IGFD::FileDialogConfig dialogConfig;
                            dialogConfig.path = platform::config::GetConfigDirectoryPath();
                            IGFD::FileDialog::Instance()->OpenDialog("mode_background_image_picker",
                                                                     "Select Background Image",
                                                                     "Image Files{.png,.jpg,.jpeg,.bmp,.gif},.*",
                                                                     dialogConfig);
                            g_modeBackgroundPickerState.modeIndex = static_cast<int>(modeIndex);
                            g_modeBackgroundPickerState.dialogOpen = true;
                        }
                    }

                    ImGui::Separator();
                    ImGui::TextUnformatted("Copy Background To:");

                    auto modeExistsByName = [&](const std::string& modeName) {
                        for (const auto& candidateMode : config.modes) {
                            if (candidateMode.name == modeName) {
                                return true;
                            }
                        }
                        return false;
                    };

                    const int currentModeIndex = static_cast<int>(modeIndex);
                    bool isCopySourceMode = (g_modeEditorState.backgroundCopySourceIndex == currentModeIndex);
                    if (isCopySourceMode) {
                        for (auto it = g_modeEditorState.backgroundCopyTargets.begin();
                             it != g_modeEditorState.backgroundCopyTargets.end();) {
                            if (it->empty() || *it == mode.name || !modeExistsByName(*it)) {
                                it = g_modeEditorState.backgroundCopyTargets.erase(it);
                            } else {
                                ++it;
                            }
                        }
                    }

                    std::string copyTargetPreview = "[Select modes]";
                    if (isCopySourceMode && !g_modeEditorState.backgroundCopyTargets.empty()) {
                        if (g_modeEditorState.backgroundCopyTargets.size() == 1) {
                            copyTargetPreview = *g_modeEditorState.backgroundCopyTargets.begin();
                        } else {
                            copyTargetPreview = std::to_string(g_modeEditorState.backgroundCopyTargets.size()) + " modes selected";
                        }
                    }

                    if (ImGui::BeginCombo("##mode_bg_copy_targets", copyTargetPreview.c_str())) {
                        if (g_modeEditorState.backgroundCopySourceIndex != currentModeIndex) {
                            g_modeEditorState.backgroundCopySourceIndex = currentModeIndex;
                            g_modeEditorState.backgroundCopyTargets.clear();
                        }

                        for (const auto& candidateMode : config.modes) {
                            if (candidateMode.name.empty() || candidateMode.name == mode.name) {
                                continue;
                            }

                            const bool isTargetSelected =
                                g_modeEditorState.backgroundCopyTargets.find(candidateMode.name) !=
                                g_modeEditorState.backgroundCopyTargets.end();
                            if (ImGui::Selectable(candidateMode.name.c_str(),
                                                  isTargetSelected,
                                                  ImGuiSelectableFlags_DontClosePopups)) {
                                if (isTargetSelected) {
                                    g_modeEditorState.backgroundCopyTargets.erase(candidateMode.name);
                                } else {
                                    g_modeEditorState.backgroundCopyTargets.insert(candidateMode.name);
                                }
                            }
                        }

                        ImGui::EndCombo();
                    }

                    ImGui::SameLine();
                    const bool canCopyBackground = isCopySourceMode && !g_modeEditorState.backgroundCopyTargets.empty();
                    if (!canCopyBackground) {
                        ImGui::BeginDisabled();
                    }
                    if (AnimatedButton("Copy...##mode_bg_copy")) {
                        g_modeEditorState.backgroundCopyModalSourceIndex = currentModeIndex;
                        g_modeEditorState.backgroundCopyModalTargets.assign(g_modeEditorState.backgroundCopyTargets.begin(),
                                                                            g_modeEditorState.backgroundCopyTargets.end());
                        ImGui::OpenPopup("##mode_bg_copy_confirm");
                    }
                    if (!canCopyBackground) {
                        ImGui::EndDisabled();
                    }

                    ImGui::SetNextWindowSizeConstraints(ImVec2(520.0f, 0.0f), ImVec2(10000.0f, 10000.0f));
                    if (ImGui::BeginPopupModal("##mode_bg_copy_confirm", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                        const int sourceIndex = g_modeEditorState.backgroundCopyModalSourceIndex;
                        const bool sourceValid =
                            sourceIndex >= 0 && sourceIndex < static_cast<int>(config.modes.size());
                        const std::string sourceName =
                            sourceValid ? config.modes[static_cast<size_t>(sourceIndex)].name : mode.name;

                        std::vector<std::string> validTargets;
                        validTargets.reserve(g_modeEditorState.backgroundCopyModalTargets.size());
                        for (const auto& targetName : g_modeEditorState.backgroundCopyModalTargets) {
                            if (targetName.empty() || targetName == sourceName) {
                                continue;
                            }
                            if (modeExistsByName(targetName)) {
                                validTargets.push_back(targetName);
                            }
                        }

                        auto quotedTargetList = [](const std::vector<std::string>& targets) {
                            std::string out;
                            for (size_t i = 0; i < targets.size(); ++i) {
                                if (i > 0) {
                                    out.append(", ");
                                }
                                out.push_back('\'');
                                out.append(targets[i]);
                                out.push_back('\'');
                            }
                            return out;
                        };

                        if (!validTargets.empty()) {
                            const std::string targetSummary = quotedTargetList(validTargets);
                            ImGui::TextWrapped("Copy background from '%s' to %s?", sourceName.c_str(), targetSummary.c_str());
                            ImGui::TextWrapped("This will overwrite background type, color/gradient settings, and image path for each target.");
                        } else {
                            ImGui::TextUnformatted("No valid target modes selected.");
                        }

                        ImGui::Separator();
                        const bool canConfirmCopy = sourceValid && !validTargets.empty();
                        if (!canConfirmCopy) {
                            ImGui::BeginDisabled();
                        }
                        if (AnimatedButton("Copy")) {
                            bool copiedAny = false;
                            const platform::config::ModeBackgroundConfig sourceBackground =
                                config.modes[static_cast<size_t>(sourceIndex)].background;
                            for (const auto& targetName : validTargets) {
                                for (auto& candidateMode : config.modes) {
                                    if (candidateMode.name == targetName) {
                                        candidateMode.background = sourceBackground;
                                        copiedAny = true;
                                        break;
                                    }
                                }
                            }
                            if (copiedAny) {
                                AutoSaveConfig(config);
                            }
                            g_modeEditorState.backgroundCopySourceIndex = -1;
                            g_modeEditorState.backgroundCopyTargets.clear();
                            g_modeEditorState.backgroundCopyModalSourceIndex = -1;
                            g_modeEditorState.backgroundCopyModalTargets.clear();
                            ImGui::CloseCurrentPopup();
                        }
                        if (!canConfirmCopy) {
                            ImGui::EndDisabled();
                        }
                        ImGui::SameLine();
                        if (AnimatedButton("Cancel")) {
                            g_modeEditorState.backgroundCopyModalSourceIndex = -1;
                            g_modeEditorState.backgroundCopyModalTargets.clear();
                            ImGui::CloseCurrentPopup();
                        }
                        ImGui::EndPopup();
                    }

                    ImGui::Unindent();
                }
                }

                {
                    platform::config::SyncModeLayerLegacyLists(mode);
                    std::string layersLabel = "Layers (" + std::to_string(mode.layers.size()) + ")###mode_layers";
                    if (AnimatedCollapsingHeader(layersLabel.c_str())) {

                        HeaderRevealScope headerRevealScope = BeginAnimatedHeaderContentReveal();
                    ImGui::Indent();
                    ImGui::TextDisabled("Drag to reorder z-index (bottom -> top).");

                    int layerIdxToRemove = -1;
                    int layerDragSource = -1;
                    int layerDragTarget = -1;
                    bool layerDropAfter = false;
                    int layerPreviewRow = -1;
                    bool layerPreviewAfter = false;
                    for (size_t k = 0; k < mode.layers.size(); ++k) {
                        auto& layer = mode.layers[k];
                        ImGui::PushID(static_cast<int>(k));

                        const bool isMirrorLayer = layer.type == platform::config::ModeLayerType::Mirror;
                        const char* typeLabel = isMirrorLayer ? "[Mirror]" : "[Group]";

                        ImGui::SmallButton("::##mode_layer_drag");
                        const ImVec2 rowMin = ImGui::GetItemRectMin();
                        const ImVec2 rowMax = ImGui::GetItemRectMax();
                        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoDisableHover)) {
                            const int payloadIndex = static_cast<int>(k);
                            ImGui::SetDragDropPayload("LINUXSCREEN_MODE_LAYER_REORDER", &payloadIndex, sizeof(payloadIndex));
                            ImGui::Text("%s %s", typeLabel, layer.id.c_str());
                            ImGui::EndDragDropSource();
                        }
                        if (ImGui::BeginDragDropTarget()) {
                            const float midY = (rowMin.y + rowMax.y) * 0.5f;
                            const bool dropAfter = ImGui::GetIO().MousePos.y > midY;
                            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("LINUXSCREEN_MODE_LAYER_REORDER",
                                                                                            ImGuiDragDropFlags_AcceptBeforeDelivery)) {
                                if (payload->DataSize == sizeof(int)) {
                                    layerPreviewRow = static_cast<int>(k);
                                    layerPreviewAfter = dropAfter;
                                    if (payload->IsDelivery()) {
                                        layerDragSource = *static_cast<const int*>(payload->Data);
                                        layerDragTarget = static_cast<int>(k);
                                        layerDropAfter = dropAfter;
                                    }
                                }
                            }
                            ImGui::EndDragDropTarget();
                        }
                        if (layerPreviewRow == static_cast<int>(k) && ImGui::GetDragDropPayload() != nullptr) {
                            ImDrawList* dl = ImGui::GetWindowDrawList();
                            ImVec2 fillMin = rowMin;
                            ImVec2 fillMax = rowMax;
                            fillMin.x = ImGui::GetWindowContentRegionMin().x + ImGui::GetWindowPos().x;
                            fillMax.x = ImGui::GetWindowContentRegionMax().x + ImGui::GetWindowPos().x;
                            dl->AddRectFilled(fillMin, fillMax, IM_COL32(88, 166, 236, 34));
                            const float lineY = layerPreviewAfter ? fillMax.y : fillMin.y;
                            dl->AddLine(ImVec2(fillMin.x, lineY), ImVec2(fillMax.x, lineY), IM_COL32(72, 190, 255, 255), 2.0f);
                        }
                        ImGui::SameLine();

                        if (ImGui::Checkbox("##layer_enabled", &layer.enabled)) {
                            AutoSaveConfig(config);
                        }
                        ImGui::SameLine();

                        if (ImGui::SmallButton("X##mode_layer_remove")) {
                            layerIdxToRemove = static_cast<int>(k);
                        }
                        ImGui::SameLine();

                        ImGui::Text("%s %s", typeLabel, layer.id.c_str());

                        bool referenceExists = false;
                        if (isMirrorLayer) {
                            for (const auto& mirrorConf : config.mirrors) {
                                if (mirrorConf.name == layer.id) {
                                    referenceExists = true;
                                    break;
                                }
                            }
                        } else {
                            for (const auto& groupConf : config.mirrorGroups) {
                                if (groupConf.name == layer.id) {
                                    referenceExists = true;
                                    break;
                                }
                            }
                        }
                        if (!referenceExists) {
                            ImGui::SameLine();
                            ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.45f, 1.0f), "(missing)");
                        }

                        ImGui::PopID();
                    }

                    if (layerDragSource >= 0 &&
                        layerDragTarget >= 0 &&
                        layerDragSource < static_cast<int>(mode.layers.size()) &&
                        layerDragTarget < static_cast<int>(mode.layers.size()) &&
                        layerDragSource != layerDragTarget) {
                        const int source = layerDragSource;
                        const int target = layerDragTarget;
                        int insertIndex = target + (layerDropAfter ? 1 : 0);
                        if (source < insertIndex) {
                            insertIndex -= 1;
                        }
                        auto movedLayer = std::move(mode.layers[static_cast<size_t>(source)]);
                        mode.layers.erase(mode.layers.begin() + source);
                        mode.layers.insert(mode.layers.begin() + insertIndex, std::move(movedLayer));
                        platform::config::SyncModeLayerLegacyLists(mode);
                        AutoSaveConfig(config);
                    }
                    if (layerIdxToRemove >= 0 &&
                        layerIdxToRemove < static_cast<int>(mode.layers.size())) {
                        const auto layerToRemove = mode.layers[static_cast<size_t>(layerIdxToRemove)];
                        platform::config::RemoveLayerFromMode(mode, layerToRemove.type, layerToRemove.id);
                        AutoSaveConfig(config);
                    }

                    if (ImGui::BeginCombo("Add Layer##mode_add_layer", "[Select Mirror or Group]")) {
                        if (ImGui::BeginMenu("Mirrors")) {
                            for (const auto& mirrorConf : config.mirrors) {
                                if (!platform::config::IsLayerInMode(mode,
                                                                     platform::config::ModeLayerType::Mirror,
                                                                     mirrorConf.name)) {
                                    if (ImGui::Selectable(mirrorConf.name.c_str())) {
                                        platform::config::AddLayerToMode(mode,
                                                                         platform::config::ModeLayerType::Mirror,
                                                                         mirrorConf.name);
                                        AutoSaveConfig(config);
                                    }
                                }
                            }
                            ImGui::EndMenu();
                        }
                        if (ImGui::BeginMenu("Mirror Groups")) {
                            for (const auto& groupConf : config.mirrorGroups) {
                                if (!platform::config::IsLayerInMode(mode,
                                                                     platform::config::ModeLayerType::Group,
                                                                     groupConf.name)) {
                                    if (ImGui::Selectable(groupConf.name.c_str())) {
                                        platform::config::AddLayerToMode(mode,
                                                                         platform::config::ModeLayerType::Group,
                                                                         groupConf.name);
                                        AutoSaveConfig(config);
                                    }
                                }
                            }
                            ImGui::EndMenu();
                        }
                        ImGui::EndCombo();
                    }

                    ImGui::Unindent();
                    }
                }
                ImGui::Unindent();

                if (config.mirrors.empty() && config.mirrorGroups.empty()) {
                    ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.2f, 1.0f), "No mirrors or groups defined.");
                    ImGui::Text("Add mirrors in the Mirrors tab first.");
                }

                ImGui::Spacing();
                if (!canDelete) ImGui::BeginDisabled();
                if (AnimatedButton("Delete Mode")) ImGui::OpenPopup("##del_mode");
                if (!canDelete) ImGui::EndDisabled();
                if (!canDelete && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                    ImGui::SetTooltip(isActive ? "Cannot delete active mode" : "Cannot delete last mode");
                }
                if (ImGui::BeginPopupModal("##del_mode", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                    ImGui::Text("Delete mode '%s'?", mode.name.c_str());
                    ImGui::Separator();
                    if (AnimatedButton("Yes")) { modeToDelete = static_cast<int>(modeIndex); ImGui::CloseCurrentPopup(); }
                    ImGui::SameLine();
                    if (AnimatedButton("Cancel")) ImGui::CloseCurrentPopup();
                    ImGui::EndPopup();
                }

            }


            if (modeIndex < config.modes.size() - 1) {
                ImGui::Separator();
            }
            ImGui::PopID();
        }

        if (modeToDelete >= 0 && modeToDelete < static_cast<int>(config.modes.size())) {
            DeleteMode(config, static_cast<size_t>(modeToDelete));
            AutoSaveConfig(config);
        }
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "No modes configured.");
    }

    if (!config.modes.empty()) {
        ImGui::Separator();
    }

    ImGui::Spacing();
    if (!g_modeEditorState.showNewModeInput) {
        if (AnimatedButton("Create New Mode")) {
            g_modeEditorState.showNewModeInput = true;
            g_modeEditorState.newModeNameBuffer[0] = '\0';
            g_modeEditorState.requestFocusNewMode = true;
        }
    } else {
        ImGui::Text("New mode name:");
        if (g_modeEditorState.requestFocusNewMode) {
            ImGui::SetKeyboardFocusHere();
            g_modeEditorState.requestFocusNewMode = false;
        }
        ImGui::InputText("##newmodename", g_modeEditorState.newModeNameBuffer, sizeof(g_modeEditorState.newModeNameBuffer));
        
        if (AnimatedButton("Create")) {
            if (g_modeEditorState.newModeNameBuffer[0] != '\0') {
                bool nameExists = false;
                for (const auto& mode : config.modes) {
                    if (mode.name == g_modeEditorState.newModeNameBuffer) {
                        nameExists = true;
                        break;
                    }
                }
                
                if (!nameExists) {
                    CreateNewMode(config, g_modeEditorState.newModeNameBuffer);
                    AutoSaveConfig(config);
                    g_modeEditorState.showNewModeInput = false;
                    g_modeEditorState.newModeNameBuffer[0] = '\0';
                }
            }
        }
        ImGui::SameLine();
        if (AnimatedButton("Cancel")) {
            g_modeEditorState.showNewModeInput = false;
            g_modeEditorState.newModeNameBuffer[0] = '\0';
        }
    }

    if (g_modeBackgroundPickerState.dialogOpen) {
        if (IGFD::FileDialog::Instance()->Display("mode_background_image_picker")) {
            if (IGFD::FileDialog::Instance()->IsOk()) {
                const int modeIndex = g_modeBackgroundPickerState.modeIndex;
                if (modeIndex >= 0 && modeIndex < static_cast<int>(config.modes.size())) {
                    std::string selectedPath =
                        IGFD::FileDialog::Instance()->GetFilePathName(IGFD_ResultMode_KeepInputFile);
                    if (!selectedPath.empty()) {
                        config.modes[modeIndex].background.image =
                            platform::config::NormalizePathForConfig(selectedPath);
                        AutoSaveConfig(config);
                    }
                }
            }

            IGFD::FileDialog::Instance()->Close();
            g_modeBackgroundPickerState.dialogOpen = false;
            g_modeBackgroundPickerState.modeIndex = -1;
        }
    }
}
