void RenderMirrorsTab(platform::config::LinuxscreenConfig& config) {
    float displayWidth = 0.0f;
    float displayHeight = 0.0f;
    float framebufferScaleX = 1.0f;
    float framebufferScaleY = 1.0f;
    const bool hasDisplayMetrics = GetOverlayDisplayMetrics(displayWidth, displayHeight, framebufferScaleX, framebufferScaleY);
    (void)framebufferScaleX;
    (void)framebufferScaleY;
    const bool hasValidDisplaySize = hasDisplayMetrics && displayWidth > 0.0f && displayHeight > 0.0f;

    auto processCompletedWaylandSelection = [&]() {
        MirrorEditorState::WaylandSelectionTask completedTask;
        bool hasCompletedTask = false;
        {
            std::lock_guard<std::mutex> lock(g_mirrorEditorState.waylandSelectionMutex);
            if (!g_mirrorEditorState.waylandSelectionTask.completed) {
                return;
            }
            completedTask = g_mirrorEditorState.waylandSelectionTask;
            g_mirrorEditorState.waylandSelectionTask.completed = false;
            g_mirrorEditorState.waylandSelectionTask.inFlight = false;
            g_mirrorEditorState.waylandSelectionTask.mirrorName.clear();
            g_mirrorEditorState.waylandSelectionTask.message.clear();
            hasCompletedTask = true;
        }
        if (!hasCompletedTask) {
            return;
        }

        g_mirrorEditorState.windowSourceCacheKey.clear();
        g_mirrorEditorState.windowSourceCacheExpiresAt = 0.0;
        if (!completedTask.message.empty()) {
            g_mirrorEditorState.windowSourceStatus.message = completedTask.message;
        }

        auto mirrorIt = std::find_if(config.mirrors.begin(), config.mirrors.end(), [&](const auto& candidate) {
            return candidate.name == completedTask.mirrorName;
        });
        if (mirrorIt == config.mirrors.end() ||
            completedTask.result != WindowCaptureSelectionResult::Selected) {
            return;
        }

        const platform::config::MirrorSourceConfig previousSource = mirrorIt->source;
        mirrorIt->source = completedTask.source;
        if (mirrorIt->source.lastKnownWidth > 0) {
            mirrorIt->captureWidth = mirrorIt->source.lastKnownWidth;
        }
        if (mirrorIt->source.lastKnownHeight > 0) {
            mirrorIt->captureHeight = mirrorIt->source.lastKnownHeight;
        }

        if (HasConfiguredWindowCaptureSource(previousSource) &&
            (GetWindowCaptureBackend() == WindowCaptureBackend::Wayland ||
             MakeWindowCaptureKey(previousSource) != MakeWindowCaptureKey(mirrorIt->source))) {
            ForgetWindowCaptureSource(previousSource);
        }

        g_mirrorEditorState.titlePatternBufferKey = mirrorIt->name + "|" +
                                                         mirrorIt->source.appId + "|" +
                                                         mirrorIt->source.windowTitle;
        std::strncpy(g_mirrorEditorState.titlePatternBuffer,
                     mirrorIt->source.windowTitle.c_str(),
                     sizeof(g_mirrorEditorState.titlePatternBuffer) - 1);
        g_mirrorEditorState.titlePatternBuffer[sizeof(g_mirrorEditorState.titlePatternBuffer) - 1] = '\0';
        AutoSaveConfig(config);
    };
    processCompletedWaylandSelection();

    auto updateRelativeFromPixels = [&](platform::config::MirrorRenderConfig& output) {
        if (!hasValidDisplaySize) {
            return;
        }

        float containerWidth = displayWidth;
        float containerHeight = displayHeight;
        if (ShouldUseViewportRelativeTo(output.relativeTo)) {
            const std::string activeModeName = GetMirrorModeState().GetActiveModeName();
            const platform::config::ModeConfig* activeMode = nullptr;
            for (const auto& mode : config.modes) {
                if (mode.name == activeModeName) {
                    activeMode = &mode;
                    break;
                }
            }

            if (activeMode) {
                int modeWidth = 0;
                int modeHeight = 0;
                platform::x11::MirrorModeState::CalculateModeDimensions(*activeMode,
                                                                         static_cast<int>(displayWidth),
                                                                         static_cast<int>(displayHeight),
                                                                         modeWidth,
                                                                         modeHeight);
                if (modeWidth > 0 && modeHeight > 0) {
                    containerWidth = static_cast<float>(modeWidth);
                    containerHeight = static_cast<float>(modeHeight);
                }
            }
        }

        if (containerWidth <= 0.0f || containerHeight <= 0.0f) {
            return;
        }
        output.relativeX = static_cast<float>(output.x) / containerWidth;
        output.relativeY = static_cast<float>(output.y) / containerHeight;
    };

    auto updatePixelsFromRelative = [&](platform::config::MirrorRenderConfig& output) {
        if (!hasValidDisplaySize) {
            return;
        }

        float containerWidth = displayWidth;
        float containerHeight = displayHeight;
        if (ShouldUseViewportRelativeTo(output.relativeTo)) {
            const std::string activeModeName = GetMirrorModeState().GetActiveModeName();
            const platform::config::ModeConfig* activeMode = nullptr;
            for (const auto& mode : config.modes) {
                if (mode.name == activeModeName) {
                    activeMode = &mode;
                    break;
                }
            }

            if (activeMode) {
                int modeWidth = 0;
                int modeHeight = 0;
                platform::x11::MirrorModeState::CalculateModeDimensions(*activeMode,
                                                                         static_cast<int>(displayWidth),
                                                                         static_cast<int>(displayHeight),
                                                                         modeWidth,
                                                                         modeHeight);
                if (modeWidth > 0 && modeHeight > 0) {
                    containerWidth = static_cast<float>(modeWidth);
                    containerHeight = static_cast<float>(modeHeight);
                }
            }
        }

        if (containerWidth <= 0.0f || containerHeight <= 0.0f) {
            return;
        }
        output.x = static_cast<int>(output.relativeX * containerWidth);
        output.y = static_cast<int>(output.relativeY * containerHeight);
    };

    auto drawRelativeToCombo = [&](const char* label, std::string& relativeTo) {
        return DrawRelativeToCombo(label, relativeTo);
    };

    const platform::config::ModeConfig* activeModeConfig = nullptr;
    int activeModeViewportX = 0;
    int activeModeViewportY = 0;
    int activeModeViewportWidth = static_cast<int>(displayWidth);
    int activeModeViewportHeight = static_cast<int>(displayHeight);
    bool hasActiveModeViewport = false;
    if (hasValidDisplaySize) {
        const std::string activeModeName = GetMirrorModeState().GetActiveModeName();
        for (const auto& mode : config.modes) {
            if (mode.name == activeModeName) {
                activeModeConfig = &mode;
                break;
            }
        }

        if (activeModeConfig) {
            int modeWidth = 0;
            int modeHeight = 0;
            platform::x11::MirrorModeState::CalculateModeDimensions(*activeModeConfig,
                                                                     static_cast<int>(displayWidth),
                                                                     static_cast<int>(displayHeight),
                                                                     modeWidth,
                                                                     modeHeight);
            if (modeWidth > 0 && modeHeight > 0) {
                hasActiveModeViewport = true;
                activeModeViewportWidth = modeWidth;
                activeModeViewportHeight = modeHeight;
                std::string anchorPreset = activeModeConfig->positionPreset.empty()
                    ? "topLeftScreen"
                    : activeModeConfig->positionPreset;
                if (anchorPreset == "custom") {
                    anchorPreset = "topLeftScreen";
                }
                platform::config::GetRelativeCoords(anchorPreset,
                                                    activeModeConfig->x,
                                                    activeModeConfig->y,
                                                    modeWidth,
                                                    modeHeight,
                                                    static_cast<int>(displayWidth),
                                                    static_cast<int>(displayHeight),
                                                    activeModeViewportX,
                                                    activeModeViewportY);
            }
        }
    }

    auto resolveOutputContainerSize = [&](const platform::config::MirrorRenderConfig& output,
                                          float& outContainerWidth,
                                          float& outContainerHeight) {
        if (!hasValidDisplaySize) {
            outContainerWidth = 0.0f;
            outContainerHeight = 0.0f;
            return false;
        }

        outContainerWidth = displayWidth;
        outContainerHeight = displayHeight;
        if (hasActiveModeViewport && ShouldUseViewportRelativeTo(output.relativeTo)) {
            outContainerWidth = static_cast<float>(activeModeViewportWidth);
            outContainerHeight = static_cast<float>(activeModeViewportHeight);
        }
        return outContainerWidth > 0.0f && outContainerHeight > 0.0f;
    };

    auto updateMirrorRelativeSizeFromScale = [&](platform::config::MirrorConfig& mirror) {
        float containerWidth = 0.0f;
        float containerHeight = 0.0f;
        if (!resolveOutputContainerSize(mirror.output, containerWidth, containerHeight)) {
            return;
        }

        const float scaleX = mirror.output.separateScale ? mirror.output.scaleX : mirror.output.scale;
        const float scaleY = mirror.output.separateScale ? mirror.output.scaleY : mirror.output.scale;
        const int border = platform::config::GetMirrorDynamicBorderPadding(mirror.border);
        const float baseWidth = static_cast<float>(mirror.captureWidth + (2 * border));
        const float baseHeight = static_cast<float>(mirror.captureHeight + (2 * border));
        if (!(baseWidth > 0.0f) || !(baseHeight > 0.0f)) {
            return;
        }

        mirror.output.relativeWidth = std::clamp((baseWidth * scaleX) / containerWidth, 0.01f, 20.0f);
        mirror.output.relativeHeight = std::clamp((baseHeight * scaleY) / containerHeight, 0.01f, 20.0f);
    };

    auto updateMirrorScaleFromRelativeSize = [&](platform::config::MirrorConfig& mirror) {
        float containerWidth = 0.0f;
        float containerHeight = 0.0f;
        if (!resolveOutputContainerSize(mirror.output, containerWidth, containerHeight)) {
            return;
        }

        const int border = platform::config::GetMirrorDynamicBorderPadding(mirror.border);
        const float baseWidth = static_cast<float>(mirror.captureWidth + (2 * border));
        const float baseHeight = static_cast<float>(mirror.captureHeight + (2 * border));
        if (!(baseWidth > 0.0f) || !(baseHeight > 0.0f)) {
            return;
        }

        const float scaleX = std::clamp((containerWidth * mirror.output.relativeWidth) / baseWidth, 0.01f, 20.0f);
        const float scaleY = std::clamp((containerHeight * mirror.output.relativeHeight) / baseHeight, 0.01f, 20.0f);
        if (mirror.output.preserveAspectRatio) {
            const float uniformScale = std::clamp(ResolveUniformScaleByFitMode(scaleX,
                                                                               scaleY,
                                                                               NormalizeAspectFitMode(mirror.output.aspectFitMode)),
                                                  0.01f,
                                                  20.0f);
            mirror.output.separateScale = false;
            mirror.output.scale = uniformScale;
            mirror.output.scaleX = uniformScale;
            mirror.output.scaleY = uniformScale;
        } else {
            mirror.output.separateScale = true;
            mirror.output.scale = scaleX;
            mirror.output.scaleX = scaleX;
            mirror.output.scaleY = scaleY;
        }
    };

    auto getMirrorUniformRelativeScale = [&](const platform::config::MirrorConfig& mirror, float& outScale) {
        float containerWidth = 0.0f;
        float containerHeight = 0.0f;
        if (!resolveOutputContainerSize(mirror.output, containerWidth, containerHeight)) {
            return false;
        }

        const int border = platform::config::GetMirrorDynamicBorderPadding(mirror.border);
        const float baseWidth = static_cast<float>(mirror.captureWidth + (2 * border));
        const float baseHeight = static_cast<float>(mirror.captureHeight + (2 * border));
        if (!(baseWidth > 0.0f) || !(baseHeight > 0.0f)) {
            return false;
        }

        const float scaleX = (containerWidth * mirror.output.relativeWidth) / baseWidth;
        const float scaleY = (containerHeight * mirror.output.relativeHeight) / baseHeight;
        outScale = std::clamp(ResolveUniformScaleByFitMode(scaleX,
                                                           scaleY,
                                                           NormalizeAspectFitMode(mirror.output.aspectFitMode)),
                              0.01f,
                              20.0f);
        return true;
    };

    auto getMirrorUniformScale = [&](const platform::config::MirrorConfig& mirror, float& outScale) {
        if (mirror.output.useRelativeSize) {
            return getMirrorUniformRelativeScale(mirror, outScale);
        }

        const float scaleX = mirror.output.separateScale ? mirror.output.scaleX : mirror.output.scale;
        const float scaleY = mirror.output.separateScale ? mirror.output.scaleY : mirror.output.scale;
        outScale = std::clamp(ResolveUniformScaleByFitMode(scaleX,
                                                           scaleY,
                                                           NormalizeAspectFitMode(mirror.output.aspectFitMode)),
                              0.01f,
                              20.0f);
        return true;
    };

    auto setMirrorRelativeSizeFromUniformScale = [&](platform::config::MirrorConfig& mirror, float uniformScale) {
        uniformScale = std::clamp(uniformScale, 0.01f, 20.0f);

        float containerWidth = 0.0f;
        float containerHeight = 0.0f;
        if (!resolveOutputContainerSize(mirror.output, containerWidth, containerHeight)) {
            mirror.output.relativeWidth = uniformScale;
            mirror.output.relativeHeight = uniformScale;
            return;
        }

        const int border = platform::config::GetMirrorDynamicBorderPadding(mirror.border);
        const float baseWidth = static_cast<float>(mirror.captureWidth + (2 * border));
        const float baseHeight = static_cast<float>(mirror.captureHeight + (2 * border));
        if (!(baseWidth > 0.0f) || !(baseHeight > 0.0f)) {
            return;
        }

        mirror.output.relativeWidth = std::clamp((uniformScale * baseWidth) / containerWidth, 0.01f, 20.0f);
        mirror.output.relativeHeight = std::clamp((uniformScale * baseHeight) / containerHeight, 0.01f, 20.0f);
    };

    auto setMirrorUniformScale = [&](platform::config::MirrorConfig& mirror, float uniformScale) {
        uniformScale = std::clamp(uniformScale, 0.01f, 20.0f);
        if (mirror.output.useRelativeSize) {
            setMirrorRelativeSizeFromUniformScale(mirror, uniformScale);
            return;
        }

        mirror.output.separateScale = false;
        mirror.output.scale = uniformScale;
        mirror.output.scaleX = uniformScale;
        mirror.output.scaleY = uniformScale;
    };

    int mirrorToRemove = -1;
    int mirrorToDuplicate = -1;
    int groupToRemove = -1;
    int groupToDuplicate = -1;
    int& s_selectedMirrorIndex = g_mirrorEditorState.mirrorListSelectionIndex;
    int& s_selectedGroupIndex = g_mirrorEditorState.groupListSelectionIndex;

    auto resetMirrorEditorState = [&]() {
        g_mirrorEditorState.selectedMirrorIndex = -1;
        g_mirrorEditorState.nameBuffer[0] = '\0';
        g_mirrorEditorState.mirrorNameError.clear();
    };

    auto resetGroupEditorState = [&]() {
        g_mirrorEditorState.selectedGroupIndex = -1;
        g_mirrorEditorState.groupNameBuffer[0] = '\0';
        g_mirrorEditorState.groupNameError.clear();
    };

    auto addNewMirror = [&]() {
        platform::config::MirrorConfig newMirror;
        newMirror.name = "New Mirror " + std::to_string(config.mirrors.size() + 1);
        newMirror.output.relativeTo = "topLeftScreen";
        newMirror.colorSensitivity = 1.0f;
        newMirror.rawOutput = true;
        newMirror.source.useWindowSize = true;
        newMirror.border.dynamicThickness = 0;
        platform::config::MirrorCaptureConfig newZone;
        newZone.relativeTo = "centerViewport";
        newMirror.input.push_back(newZone);
        config.mirrors.push_back(std::move(newMirror));
        s_selectedMirrorIndex = static_cast<int>(config.mirrors.size()) - 1;
        resetMirrorEditorState();
        AutoSaveConfig(config);
    };

    auto addNewGroup = [&]() {
        platform::config::MirrorGroupConfig grp;
        grp.name = "Group" + std::to_string(config.mirrorGroups.size());
        grp.output.relativeTo = "centerViewport";
        config.mirrorGroups.push_back(grp);
        s_selectedGroupIndex = static_cast<int>(config.mirrorGroups.size()) - 1;
        resetGroupEditorState();
        AutoSaveConfig(config);
    };

    const ImGuiTableFlags splitPaneFlags = ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV;

    if (ImGui::BeginTabBar("##mirrors_split_panes")) {
        const ImGuiTabItemFlags mirrorsTabFlags =
            (g_mirrorEditorState.mainEditorTabSelectionPending &&
             g_mirrorEditorState.mainEditorTab == MirrorsMainEditorTab::Mirrors)
                ? ImGuiTabItemFlags_SetSelected
                : 0;
        if (ImGui::BeginTabItem("Mirrors", nullptr, mirrorsTabFlags)) {
            g_mirrorEditorState.mainEditorTab = MirrorsMainEditorTab::Mirrors;
            g_mirrorEditorState.mainEditorTabSelectionPending = false;
            ImGui::Separator();

            if (!config.mirrors.empty()) {
                s_selectedMirrorIndex = std::clamp(s_selectedMirrorIndex, 0, static_cast<int>(config.mirrors.size()) - 1);
            }
            if (ImGui::BeginTable("##mirrors_split_table", 2, splitPaneFlags)) {
                ImGui::TableSetupColumn("Mirror List", ImGuiTableColumnFlags_WidthFixed, 280.0f);
                ImGui::TableSetupColumn("Mirror Editor", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                ImGui::BeginChild("##mirror_list_panel");
                const float mirrorListFooterHeight = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y + 6.0f;
                ImGui::BeginChild("##mirror_list_child", ImVec2(0.0f, -mirrorListFooterHeight), false);
                if (config.mirrors.empty()) {
                    ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "No mirrors configured.");
                } else {
                    for (size_t i = 0; i < config.mirrors.size(); ++i) {
                        const auto& mirror = config.mirrors[i];
                        const bool selected = static_cast<int>(i) == s_selectedMirrorIndex;
                        const std::string displayName = mirror.name.empty() ? "[Unnamed Mirror]" : mirror.name;
                        const std::string listLabel = displayName + "###mirror_list_item_" + std::to_string(i);
                        if (ImGui::Selectable(listLabel.c_str(), selected)) {
                            s_selectedMirrorIndex = static_cast<int>(i);
                        }
                        if (i < config.mirrors.size() - 1) {
                            ImGui::Separator();
                        }
                    }
                }
                ImGui::EndChild();
                ImGui::Separator();

                if (AnimatedButton("+##mirror_sidebar_add", ImVec2(28.0f, 0.0f))) {
                    addNewMirror();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Add");
                }

                const bool hasMirrorSelection = !config.mirrors.empty() &&
                                                s_selectedMirrorIndex >= 0 &&
                                                s_selectedMirrorIndex < static_cast<int>(config.mirrors.size());
                if (!hasMirrorSelection) {
                    ImGui::BeginDisabled();
                }

                ImGui::SameLine();
                if (AnimatedButton("-##mirror_sidebar_delete", ImVec2(28.0f, 0.0f))) {
                    ImGui::OpenPopup("##del_mir");
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                    ImGui::SetTooltip("Delete");
                }

                ImGui::SameLine();
                if (AnimatedButton("x2##mirror_sidebar_duplicate", ImVec2(28.0f, 0.0f))) {
                    mirrorToDuplicate = s_selectedMirrorIndex;
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                    ImGui::SetTooltip("Duplicate");
                }

                if (!hasMirrorSelection) {
                    ImGui::EndDisabled();
                }

                ImGui::SameLine();
                const float presetsButtonWidth = std::max(72.0f, ImGui::GetContentRegionAvail().x);
                if (AnimatedButton("Presets##mirror_sidebar_presets", ImVec2(presetsButtonWidth, 0.0f))) {
                    g_mirrorEditorState.openMirrorPresetPopup = true;
                    g_mirrorEditorState.mirrorPresetStatusMessage.clear();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Import preset mirrors or groups");
                }

                if (g_mirrorEditorState.openMirrorPresetPopup) {
                    ImGui::OpenPopup("##mirror_presets_popup");
                    g_mirrorEditorState.openMirrorPresetPopup = false;
                }

                if (ImGui::BeginPopup("##mirror_presets_popup")) {
                    const platform::config::LinuxscreenConfig presetConfig = platform::config::LoadEmbeddedDefaultConfig();

                    ImGui::TextUnformatted("Mirror Presets");
                    ImGui::Separator();
                    if (presetConfig.mirrors.empty()) {
                        ImGui::TextDisabled("No mirror presets available");
                    } else {
                        for (std::size_t presetMirrorIndex = 0; presetMirrorIndex < presetConfig.mirrors.size(); ++presetMirrorIndex) {
                            const auto& presetMirror = presetConfig.mirrors[presetMirrorIndex];
                            const std::string label = presetMirror.name.empty() ? "[Unnamed Mirror]" : presetMirror.name;
                            const std::string itemId = label + "##mirror_preset_" + std::to_string(presetMirrorIndex);
                            if (ImGui::Selectable(itemId.c_str())) {
                                int importedMirrorIndex = -1;
                                if (platform::config::TryImportMirrorPreset(config,
                                                                            presetConfig,
                                                                            presetMirror.name,
                                                                            importedMirrorIndex)) {
                                    s_selectedMirrorIndex = importedMirrorIndex;
                                    resetMirrorEditorState();
                                    g_mirrorEditorState.mainEditorTab = MirrorsMainEditorTab::Mirrors;
                                    g_mirrorEditorState.mainEditorTabSelectionPending = true;
                                    g_mirrorEditorState.mirrorPresetStatusMessage.clear();
                                    AutoSaveConfig(config);
                                    ImGui::CloseCurrentPopup();
                                } else {
                                    g_mirrorEditorState.mirrorPresetStatusMessage =
                                        "Could not import preset mirror.";
                                }
                            }
                        }
                    }

                    ImGui::Spacing();
                    ImGui::TextUnformatted("Group Presets");
                    ImGui::Separator();
                    if (presetConfig.mirrorGroups.empty()) {
                        ImGui::TextDisabled("No group presets available");
                    } else {
                        for (std::size_t presetGroupIndex = 0; presetGroupIndex < presetConfig.mirrorGroups.size(); ++presetGroupIndex) {
                            const auto& presetGroup = presetConfig.mirrorGroups[presetGroupIndex];
                            const std::string label = presetGroup.name.empty() ? "[Unnamed Group]" : presetGroup.name;
                            const std::string itemId = label + "##group_preset_" + std::to_string(presetGroupIndex);
                            if (ImGui::Selectable(itemId.c_str())) {
                                int importedGroupIndex = -1;
                                std::vector<int> importedMirrorIndices;
                                if (platform::config::TryImportGroupPreset(config,
                                                                           presetConfig,
                                                                           presetGroup.name,
                                                                           importedGroupIndex,
                                                                           importedMirrorIndices)) {
                                    s_selectedGroupIndex = importedGroupIndex;
                                    resetGroupEditorState();
                                    g_mirrorEditorState.mainEditorTab = MirrorsMainEditorTab::Groups;
                                    g_mirrorEditorState.mainEditorTabSelectionPending = true;
                                    g_mirrorEditorState.mirrorPresetStatusMessage.clear();
                                    AutoSaveConfig(config);
                                    ImGui::CloseCurrentPopup();
                                } else {
                                    std::string missingMirrorName;
                                    for (const auto& item : presetGroup.mirrors) {
                                        const bool foundPresetMirror = std::any_of(presetConfig.mirrors.begin(),
                                                                                   presetConfig.mirrors.end(),
                                                                                   [&](const auto& presetMirror) {
                                                                                       return presetMirror.name == item.mirrorId;
                                                                                   });
                                        if (!foundPresetMirror) {
                                            missingMirrorName = item.mirrorId;
                                            break;
                                        }
                                    }
                                    if (!missingMirrorName.empty()) {
                                        g_mirrorEditorState.mirrorPresetStatusMessage =
                                            "Could not import preset group: missing preset mirror '" + missingMirrorName + "'";
                                    } else {
                                        g_mirrorEditorState.mirrorPresetStatusMessage =
                                            "Could not import preset group.";
                                    }
                                }
                            }
                        }
                    }

                    if (!g_mirrorEditorState.mirrorPresetStatusMessage.empty()) {
                        ImGui::Spacing();
                        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f),
                                           "%s",
                                           g_mirrorEditorState.mirrorPresetStatusMessage.c_str());
                    }

                    ImGui::EndPopup();
                }

                if (ImGui::BeginPopupModal("##del_mir", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                    const char* mirrorName = "";
                    if (hasMirrorSelection) {
                        mirrorName = config.mirrors[static_cast<size_t>(s_selectedMirrorIndex)].name.c_str();
                    }
                    ImGui::Text("Delete mirror '%s'?", mirrorName);
                    ImGui::Separator();
                    if (AnimatedButton("Yes")) {
                        mirrorToRemove = s_selectedMirrorIndex;
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::SameLine();
                    if (AnimatedButton("Cancel")) {
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndPopup();
                }
                ImGui::EndChild();

                ImGui::TableSetColumnIndex(1);
                ImGui::BeginChild("##mirror_editor_child");
                if (config.mirrors.empty()) {
                    ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "No mirrors configured.");
                    ImGui::Text("Use + in the mirror list to add one.");
                } else {
                    auto& mirror = config.mirrors[static_cast<size_t>(s_selectedMirrorIndex)];
                    ImGui::PushID(s_selectedMirrorIndex);

                    if (g_mirrorEditorState.selectedMirrorIndex != s_selectedMirrorIndex) {
                        g_mirrorEditorState.selectedMirrorIndex = s_selectedMirrorIndex;
                        CopyEditorNameToBuffer(g_mirrorEditorState.nameBuffer,
                                               sizeof(g_mirrorEditorState.nameBuffer),
                                               mirror.name);
                        g_mirrorEditorState.mirrorNameError.clear();
                        g_mirrorEditorState.visualDrag = MirrorEditorState::VisualDragState{};
                        g_mirrorEditorState.visualEditorCropZoneIndex = 0;
                    }

                    if (AnimatedButton("Edit On Game Screen")) {
                        ImGui::OpenPopup("##mirror_direct_edit_launcher");
                    }
                    if (ImGui::BeginPopup("##mirror_direct_edit_launcher")) {
                        const auto launchMirrorDirectEdit = [&](const std::string& modeName,
                                                                const std::string& mirrorId,
                                                                const std::string& groupId,
                                                                MirrorDirectEditSelectionKind selectionKind) {
                            if (!modeName.empty() && modeName != GetMirrorModeState().GetActiveModeName()) {
                                StartModeSwitchWithTransition(modeName, config, GetMirrorModeState());
                            }
                            g_mirrorEditorState.directEditSelection.kind = selectionKind;
                            g_mirrorEditorState.directEditSelection.mirrorId = mirrorId;
                            g_mirrorEditorState.directEditSelection.groupId = groupId;
                            g_mirrorEditorState.directEditSelection.groupItemIndex = -1;
                            g_mirrorEditorState.directEditSelectedCaptureZoneIndex = 0;
                            SetMirrorDirectEditActive(true, true);
                            SetGuiVisible(false);
                        };

                        const auto directModes = platform::config::GetModesContainingMirrorDirect(config, mirror.name);
                        const auto containingGroups = platform::config::GetGroupsContainingMirror(config, mirror.name);
                        ImGui::TextUnformatted("Edit Relative To");
                        ImGui::Separator();

                        ImGui::TextDisabled("Mirror");
                        if (directModes.empty()) {
                            ImGui::TextDisabled("No modes");
                        } else {
                            for (const auto& modeName : directModes) {
                                const std::string itemId = modeName + "##mirror_scope_mode";
                                if (ImGui::Selectable(itemId.c_str())) {
                                    launchMirrorDirectEdit(modeName, mirror.name, "", MirrorDirectEditSelectionKind::Mirror);
                                    ImGui::CloseCurrentPopup();
                                }
                            }
                        }

                        for (const auto& groupName : containingGroups) {
                            ImGui::Separator();
                            ImGui::TextDisabled("Group: %s", groupName.c_str());
                            const auto groupModes = platform::config::GetModesContainingGroup(config, groupName);
                            if (groupModes.empty()) {
                                ImGui::TextDisabled("No modes");
                                continue;
                            }
                            for (const auto& modeName : groupModes) {
                                const std::string itemId = modeName + "##group_scope_mode_" + groupName;
                                if (ImGui::Selectable(itemId.c_str())) {
                                    launchMirrorDirectEdit(modeName, mirror.name, groupName, MirrorDirectEditSelectionKind::Group);
                                    ImGui::CloseCurrentPopup();
                                }
                            }
                        }
                        ImGui::EndPopup();
                    }

                    if (AnimatedCollapsingHeader("General")) {


                        HeaderRevealScope headerRevealScope = BeginAnimatedHeaderContentReveal();
                        ImGui::Indent();
                        if (ImGui::InputText("Name", g_mirrorEditorState.nameBuffer, sizeof(g_mirrorEditorState.nameBuffer))) {
                            g_mirrorEditorState.mirrorNameError.clear();
                        }
                        if (ImGui::IsItemDeactivatedAfterEdit()) {
                            const std::string newName = TrimEditorName(g_mirrorEditorState.nameBuffer);
                            if (newName.empty()) {
                                g_mirrorEditorState.mirrorNameError = "Mirror name cannot be empty.";
                            } else if (HasDuplicateMirrorName(config, newName, s_selectedMirrorIndex)) {
                                g_mirrorEditorState.mirrorNameError = "Mirror name must be unique.";
                            } else {
                                g_mirrorEditorState.mirrorNameError.clear();
                                if (newName != mirror.name) {
                                    ForgetMirrorImageSource(mirror.name);
                                    platform::config::RenameMirror(config, mirror.name, newName);
                                }
                                CopyEditorNameToBuffer(g_mirrorEditorState.nameBuffer,
                                                       sizeof(g_mirrorEditorState.nameBuffer),
                                                       newName);
                                AutoSaveConfig(config);
                            }
                        }
                        if (!g_mirrorEditorState.mirrorNameError.empty()) {
                            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "%s", g_mirrorEditorState.mirrorNameError.c_str());
                        }

                        int fps = mirror.fps;
                        if (ImGui::InputInt("FPS", &fps)) {
                            if (fps > 0) {
                                mirror.fps = fps;
                                AutoSaveConfig(config);
                            }
                        }

                        const bool useSelectedWindowSize =
                            mirror.source.type == platform::config::MirrorSourceType::Window &&
                            mirror.source.useWindowSize;
                        const bool useSelectedImageSize =
                            IsImageSource(mirror.source) &&
                            mirror.source.useImageSize &&
                            mirror.source.lastKnownWidth > 0 &&
                            mirror.source.lastKnownHeight > 0;
                        const bool useSelectedSourceSize = useSelectedWindowSize || useSelectedImageSize;
                        int captureWidth = mirror.captureWidth;
                        int captureHeight = mirror.captureHeight;
                        ImGui::BeginDisabled(useSelectedSourceSize);
                        if (ImGui::InputInt("Capture Width", &captureWidth)) {
                            if (captureWidth > 0) {
                                mirror.captureWidth = captureWidth;
                                AutoSaveConfig(config);
                            }
                        }
                        if (ImGui::InputInt("Capture Height", &captureHeight)) {
                            if (captureHeight > 0) {
                                mirror.captureHeight = captureHeight;
                                AutoSaveConfig(config);
                            }
                        }
                        ImGui::EndDisabled();
                        if (useSelectedWindowSize) {
                            ImGui::TextDisabled("Using the selected window's live size.");
                        } else if (useSelectedImageSize) {
                            ImGui::TextDisabled("Using the selected image's natural size.");
                        }

                        ImGui::Separator();

                        const char* sourceTypes[] = { "Game Framebuffer", "Window", "Image", "Calc Overlay" };
                        int currentSourceType = static_cast<int>(mirror.source.type);
                        if (ImGui::Combo("Source", &currentSourceType, sourceTypes, IM_ARRAYSIZE(sourceTypes))) {
                            const platform::config::MirrorSourceConfig previousSource = mirror.source;
                            mirror.source.type = static_cast<platform::config::MirrorSourceType>(currentSourceType);
                            if (IsCalcOverlaySource(mirror.source)) {
                                mirror.source.image = GetCalcOverlayImagePath();
                                mirror.source.lastKnownWidth = 0;
                                mirror.source.lastKnownHeight = 0;
                            }
                            if (HasConfiguredWindowCaptureSource(previousSource) &&
                                previousSource.type != mirror.source.type) {
                                ForgetWindowCaptureSource(previousSource);
                            }
                            if (HasConfiguredImageSource(previousSource) &&
                                previousSource.type != mirror.source.type) {
                                ForgetMirrorImageSource(mirror.name);
                            }
                            InvalidateWindowCaptureTransientState();
                            AutoSaveConfig(config);
                        }

                        if (mirror.source.type == platform::config::MirrorSourceType::Window) {
#include "tab_mirrors_window_source.cpp"
                        } else if (IsImageSource(mirror.source)) {
#include "tab_mirrors_image_source.cpp"
                        }
                        ImGui::Unindent();
                    }
                    if (AnimatedCollapsingHeader("Border")) {


                        HeaderRevealScope headerRevealScope = BeginAnimatedHeaderContentReveal();
                        ImGui::Indent();
                        const char* borderTypes[] = { "Dynamic (around content)", "Static (shape overlay)" };
                        int currentBorderType = static_cast<int>(mirror.border.type);
                        if (ImGui::Combo("Border Type", &currentBorderType, borderTypes, IM_ARRAYSIZE(borderTypes))) {
                            mirror.border.type = static_cast<platform::config::MirrorBorderType>(currentBorderType);
                            AutoSaveConfig(config);
                        }

                        if (mirror.border.type == platform::config::MirrorBorderType::Dynamic) {
                            if (ImGui::DragInt("Dynamic Thickness", &mirror.border.dynamicThickness, 1, 0, 32)) {
                                if (mirror.border.dynamicThickness < 0) mirror.border.dynamicThickness = 0;
                                AutoSaveConfig(config);
                            }

                            if (mirror.border.dynamicThickness > 0) {
                                float dynColor[4] = { mirror.colors.border.r, mirror.colors.border.g, mirror.colors.border.b, mirror.colors.border.a };
                                if (ImGui::ColorEdit4("Border Color##dyn", dynColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar)) {
                                    mirror.colors.border = { dynColor[0], dynColor[1], dynColor[2], dynColor[3] };
                                    AutoSaveConfig(config);
                                }
                            }
                        } else {

                            if (ImGui::DragInt("Thickness##sb", &mirror.border.staticThickness, 1, 0, 32)) {
                                if (mirror.border.staticThickness < 0) mirror.border.staticThickness = 0;
                                AutoSaveConfig(config);
                            }

                            if (mirror.border.staticThickness > 0) {
                                const char* shapes[] = { "Rectangle", "Circle/Ellipse" };
                                int currentShape = static_cast<int>(mirror.border.staticShape);
                                ImGui::PushItemWidth(140);
                                if (ImGui::Combo("Shape", &currentShape, shapes, IM_ARRAYSIZE(shapes))) {
                                    mirror.border.staticShape = static_cast<platform::config::MirrorBorderShape>(currentShape);
                                    AutoSaveConfig(config);
                                }
                                ImGui::PopItemWidth();

                                float staticColor[4] = { mirror.border.staticColor.r, mirror.border.staticColor.g, mirror.border.staticColor.b, mirror.border.staticColor.a };
                                if (ImGui::ColorEdit4("Static Color", staticColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar)) {
                                    mirror.border.staticColor = { staticColor[0], staticColor[1], staticColor[2], staticColor[3] };
                                    AutoSaveConfig(config);
                                }

                                if (mirror.border.staticShape == platform::config::MirrorBorderShape::Rectangle) {
                                    if (ImGui::DragInt("Radius##sb", &mirror.border.staticRadius, 1, 0, 128)) {
                                        if (mirror.border.staticRadius < 0) mirror.border.staticRadius = 0;
                                        AutoSaveConfig(config);
                                    }
                                }

                                ImGui::TextDisabled("Position/Size Offsets (relative to mirror)");
                                if (ImGui::DragInt("Offset X##sb", &mirror.border.staticOffsetX, 1)) {
                                    AutoSaveConfig(config);
                                }
                                if (ImGui::DragInt("Offset Y##sb", &mirror.border.staticOffsetY, 1)) {
                                    AutoSaveConfig(config);
                                }
                                if (ImGui::DragInt("Width##sb", &mirror.border.staticWidth, 1, 0, 4096)) {
                                    if (mirror.border.staticWidth < 0) mirror.border.staticWidth = 0;
                                    AutoSaveConfig(config);
                                }
                                if (ImGui::DragInt("Height##sb", &mirror.border.staticHeight, 1, 0, 4096)) {
                                    if (mirror.border.staticHeight < 0) mirror.border.staticHeight = 0;
                                    AutoSaveConfig(config);
                                }
                            }
                        }
                        ImGui::Unindent();
                    }

                    if (AnimatedCollapsingHeader("Scaling")) {


                        HeaderRevealScope headerRevealScope = BeginAnimatedHeaderContentReveal();
                        ImGui::Indent();
                        if (ImGui::Checkbox("Relative size to container##mirror_output_size", &mirror.output.useRelativeSize)) {
                            if (mirror.output.useRelativeSize) {
                                updateMirrorRelativeSizeFromScale(mirror);
                            } else {
                                updateMirrorScaleFromRelativeSize(mirror);
                            }
                            AutoSaveConfig(config);
                        }
                        ImGui::SameLine();
                        HelpMarker("When enabled, output width/height are stored as percentages of the anchor container.\n"
                                   "Container is screen for *Screen anchors and mode viewport for *Viewport/Pie anchors.");

                        if (ImGui::Checkbox("Preserve aspect ratio##mirror_output_size", &mirror.output.preserveAspectRatio)) {
                            if (mirror.output.preserveAspectRatio) {
                                float uniformScale = 1.0f;
                                if (getMirrorUniformScale(mirror, uniformScale)) {
                                    setMirrorUniformScale(mirror, uniformScale);
                                }
                            } else if (!mirror.output.useRelativeSize) {
                                const float currentScale = mirror.output.separateScale ? mirror.output.scaleX : mirror.output.scale;
                                mirror.output.separateScale = true;
                                mirror.output.scaleX = currentScale;
                                mirror.output.scaleY = currentScale;
                            }
                            AutoSaveConfig(config);
                        }

                        if (mirror.output.preserveAspectRatio) {
                            if (DrawAspectFitModeCombo("Fit Mode##mirror_output_size", mirror.output.aspectFitMode)) {
                                mirror.output.aspectFitMode = NormalizeAspectFitMode(mirror.output.aspectFitMode);
                                AutoSaveConfig(config);
                            }
                        }

                        if (mirror.output.preserveAspectRatio) {
                            float uniformScale = 1.0f;
                            if (!getMirrorUniformScale(mirror, uniformScale)) {
                                uniformScale = 1.0f;
                            }
                            float scalePercent = uniformScale * 100.0f;
                            const char* uniformScaleLabel = mirror.output.useRelativeSize
                                                               ? "Size % of container##mirror_output_size"
                                                               : "Scale %##mirror_output_size";
                            if (ImGui::SliderFloat(uniformScaleLabel, &scalePercent, 1.0f, 2000.0f, "%.1f%%")) {
                                setMirrorUniformScale(mirror, scalePercent / 100.0f);
                                AutoSaveConfig(config);
                            }

                            if (mirror.output.useRelativeSize) {
                                ImGui::TextDisabled("Stored size: %.1f%% width, %.1f%% height",
                                                    std::clamp(mirror.output.relativeWidth, 0.01f, 20.0f) * 100.0f,
                                                    std::clamp(mirror.output.relativeHeight, 0.01f, 20.0f) * 100.0f);
                            }
                        } else {
                            if (mirror.output.useRelativeSize) {
                                float widthPercent = std::clamp(mirror.output.relativeWidth, 0.01f, 20.0f) * 100.0f;
                                if (ImGui::SliderFloat("Width % of container##mirror_output_size", &widthPercent, 1.0f, 2000.0f, "%.1f%%")) {
                                    mirror.output.relativeWidth = std::clamp(widthPercent / 100.0f, 0.01f, 20.0f);
                                    AutoSaveConfig(config);
                                }

                                float heightPercent = std::clamp(mirror.output.relativeHeight, 0.01f, 20.0f) * 100.0f;
                                if (ImGui::SliderFloat("Height % of container##mirror_output_size", &heightPercent, 1.0f, 2000.0f, "%.1f%%")) {
                                    mirror.output.relativeHeight = std::clamp(heightPercent / 100.0f, 0.01f, 20.0f);
                                    AutoSaveConfig(config);
                                }
                            } else {
                                const float currentScaleX = std::clamp(mirror.output.separateScale ? mirror.output.scaleX : mirror.output.scale, 0.01f, 20.0f);
                                const float currentScaleY = std::clamp(mirror.output.separateScale ? mirror.output.scaleY : mirror.output.scale, 0.01f, 20.0f);
                                float widthPercent = currentScaleX * 100.0f;
                                if (ImGui::SliderFloat("Width %##mirror_output_size", &widthPercent, 1.0f, 2000.0f, "%.1f%%")) {
                                    const float widthScale = std::clamp(widthPercent / 100.0f, 0.01f, 20.0f);
                                    mirror.output.separateScale = true;
                                    mirror.output.scaleX = widthScale;
                                    mirror.output.scale = widthScale;
                                    AutoSaveConfig(config);
                                }
                                float heightPercent = currentScaleY * 100.0f;
                                if (ImGui::SliderFloat("Height %##mirror_output_size", &heightPercent, 1.0f, 2000.0f, "%.1f%%")) {
                                    const float heightScale = std::clamp(heightPercent / 100.0f, 0.01f, 20.0f);
                                    mirror.output.separateScale = true;
                                    mirror.output.scaleY = heightScale;
                                    AutoSaveConfig(config);
                                }
                            }
                        }
                        ImGui::Unindent();
                    }

                    if (AnimatedCollapsingHeader("Position")) {


                        HeaderRevealScope headerRevealScope = BeginAnimatedHeaderContentReveal();
                        ImGui::Indent();
                        if (ImGui::Checkbox("Relative to screen##mirror_output", &mirror.output.useRelativePosition)) {
                            if (mirror.output.useRelativePosition) {
                                updateRelativeFromPixels(mirror.output);
                            } else {
                                updatePixelsFromRelative(mirror.output);
                            }
                            AutoSaveConfig(config);
                        }
                        ImGui::SameLine();
                        HelpMarker("When enabled, position is stored as percentages of screen size.\n"
                                   "This makes configs portable across different screen resolutions.");

                        if (drawRelativeToCombo("Relative To", mirror.output.relativeTo)) {
                            AutoSaveConfig(config);
                        }

                        if (mirror.output.useRelativePosition) {
                            float xPercent = mirror.output.relativeX * 100.0f;
                            if (ImGui::SliderFloat("X %##mirror_output", &xPercent, -100.0f, 200.0f, "%.1f%%")) {
                                mirror.output.relativeX = xPercent / 100.0f;
                                updatePixelsFromRelative(mirror.output);
                                AutoSaveConfig(config);
                            }

                            float yPercent = mirror.output.relativeY * 100.0f;
                            if (ImGui::SliderFloat("Y %##mirror_output", &yPercent, -100.0f, 200.0f, "%.1f%%")) {
                                mirror.output.relativeY = yPercent / 100.0f;
                                updatePixelsFromRelative(mirror.output);
                                AutoSaveConfig(config);
                            }
                        } else {
                            int pX = mirror.output.x;
                            int pY = mirror.output.y;
                            if (ImGui::DragInt("X Offset##mirror_output", &pX, 1)) {
                                mirror.output.x = pX;
                                AutoSaveConfig(config);
                            }
                            if (ImGui::DragInt("Y Offset##mirror_output", &pY, 1)) {
                                mirror.output.y = pY;
                                AutoSaveConfig(config);
                            }
                        }
                        ImGui::Unindent();
                    }

                    if (AnimatedCollapsingHeader("Color Filters")) {


                        HeaderRevealScope headerRevealScope = BeginAnimatedHeaderContentReveal();
                        ImGui::Indent();
                        float sensitivity = mirror.colorSensitivity;
                        if (ImGui::DragFloat("Color Sensitivity", &sensitivity, 0.0001f, 0.0f, 1.0f, "%.4f")) {
                            mirror.colorSensitivity = sensitivity;
                            AutoSaveConfig(config);
                        }

                        float opacity = mirror.opacity;
                        if (ImGui::DragFloat("Opacity", &opacity, 0.01f, 0.0f, 1.0f)) {
                            mirror.opacity = opacity;
                            AutoSaveConfig(config);
                        }

                        if (ImGui::Checkbox("Color Passthrough", &mirror.colorPassthrough)) {
                            AutoSaveConfig(config);
                        }
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("When enabled, the original pixel colors are used instead of the Output Color.");

                        if (ImGui::Checkbox("Raw Output", &mirror.rawOutput)) {
                            AutoSaveConfig(config);
                        }
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("When enabled, the capture is rendered directly to the screen (useful for zone alignment).");

                        if (mirror.colorPassthrough) ImGui::BeginDisabled();
                        float outCol[4] = { mirror.colors.output.r, mirror.colors.output.g, mirror.colors.output.b, mirror.colors.output.a };
                        if (ImGui::ColorEdit4("Output Color", outCol)) {
                            mirror.colors.output = { outCol[0], outCol[1], outCol[2], outCol[3] };
                            AutoSaveConfig(config);
                        }
                        if (mirror.colorPassthrough) ImGui::EndDisabled();

                        ImGui::Text("Target Colors:");
                        int targetToRemoveIdx = -1;
                        for (size_t colIdx = 0; colIdx < mirror.colors.targetColors.size(); ++colIdx) {
                            ImGui::PushID(static_cast<int>(colIdx));
                            float tCol[3] = { mirror.colors.targetColors[colIdx].r, mirror.colors.targetColors[colIdx].g, mirror.colors.targetColors[colIdx].b };
                            std::string colLabel = "##col_" + std::to_string(colIdx);
                            if (ImGui::ColorEdit3(colLabel.c_str(), tCol)) {
                                mirror.colors.targetColors[colIdx] = { tCol[0], tCol[1], tCol[2], 1.0f };
                                AutoSaveConfig(config);
                            }
                            ImGui::SameLine();
                            if (AnimatedButton("Remove")) {
                                targetToRemoveIdx = static_cast<int>(colIdx);
                            }
                            ImGui::PopID();
                        }
                        if (targetToRemoveIdx != -1) {
                            mirror.colors.targetColors.erase(mirror.colors.targetColors.begin() + targetToRemoveIdx);
                            AutoSaveConfig(config);
                        }
                        if (mirror.colors.targetColors.size() < 8) {
                            if (AnimatedButton("+ Add Target Color")) {
                                mirror.colors.targetColors.push_back({ 1.0f, 1.0f, 1.0f, 1.0f });
                                AutoSaveConfig(config);
                            }
                        }
                        ImGui::Unindent();
                    }

                    if (AnimatedCollapsingHeader("Capture Zones")) {


                        HeaderRevealScope headerRevealScope = BeginAnimatedHeaderContentReveal();
                        ImGui::Indent();
                        int zoneToRemoveIdx = -1;
                        if (ImGui::BeginTable("mirror_capture_zones", 5, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV)) {
                            ImGui::TableSetupColumn("###zone_col_visibility", ImGuiTableColumnFlags_WidthFixed, 86.0f);
                            ImGui::TableSetupColumn("X###zone_col_x", ImGuiTableColumnFlags_WidthStretch, 1.0f);
                            ImGui::TableSetupColumn("Y###zone_col_y", ImGuiTableColumnFlags_WidthStretch, 1.0f);
                            ImGui::TableSetupColumn("Relative To###zone_col_relative", ImGuiTableColumnFlags_WidthStretch, 1.0f);
                            ImGui::TableSetupColumn("###delete_zone_col", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight() + 6.0f);
                            ImGui::TableHeadersRow();

                            for (size_t zIdx = 0; zIdx < mirror.input.size(); ++zIdx) {
                                ImGui::PushID(static_cast<int>(zIdx));
                                auto& zone = mirror.input[zIdx];

                                ImGui::TableNextRow();

                                ImGui::TableSetColumnIndex(0);
                                if (ImGui::Checkbox("##zone_enabled", &zone.enabled)) {
                                    AutoSaveConfig(config);
                                }

                                ImGui::TableSetColumnIndex(1);
                                ImGui::SetNextItemWidth(-1.0f);
                                int zX = zone.x;
                                if (ImGui::InputInt("##zone_x", &zX)) {
                                    zone.x = zX;
                                    AutoSaveConfig(config);
                                }

                                ImGui::TableSetColumnIndex(2);
                                ImGui::SetNextItemWidth(-1.0f);
                                int zY = zone.y;
                                if (ImGui::InputInt("##zone_y", &zY)) {
                                    zone.y = zY;
                                    AutoSaveConfig(config);
                                }

                                ImGui::TableSetColumnIndex(3);
                                ImGui::SetNextItemWidth(-1.0f);
                                if (drawRelativeToCombo("##zone_relative_to",
                                                        zone.relativeTo)) {
                                    AutoSaveConfig(config);
                                }

                                ImGui::TableSetColumnIndex(4);
                                if (mirror.input.size() <= 1) {
                                    ImGui::BeginDisabled();
                                }
                                if (ImGui::SmallButton("X###delete_zone_btn")) {
                                    ImGui::OpenPopup("##delete_zone_confirm");
                                }
                                if (mirror.input.size() <= 1) {
                                    ImGui::EndDisabled();
                                }
                                if (ImGui::BeginPopupModal("##delete_zone_confirm", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                                    ImGui::Text("Delete capture zone %zu?", zIdx + 1);
                                    ImGui::Separator();
                                    if (AnimatedButton("Yes")) {
                                        zoneToRemoveIdx = static_cast<int>(zIdx);
                                        ImGui::CloseCurrentPopup();
                                    }
                                    ImGui::SameLine();
                                    if (AnimatedButton("Cancel")) {
                                        ImGui::CloseCurrentPopup();
                                    }
                                    ImGui::EndPopup();
                                }

                                ImGui::PopID();
                            }

                            ImGui::EndTable();
                        }
                        if (zoneToRemoveIdx != -1) {
                            mirror.input.erase(mirror.input.begin() + zoneToRemoveIdx);
                            AutoSaveConfig(config);
                        }
                        if (AnimatedButton("Add New Capture Zone")) {
                            platform::config::MirrorCaptureConfig nZone;
                            nZone.relativeTo = (mirror.source.type != platform::config::MirrorSourceType::GameFramebuffer)
                                ? "topLeftSource"
                                : "centerViewport";
                            mirror.input.push_back(nZone);
                            AutoSaveConfig(config);
                        }
                        ImGui::Unindent();
                    }

                    ImGui::Separator();
                    {
                        auto containingModes = platform::config::GetModesContainingMirror(config, mirror.name);
                        std::string addToModesPreview = "[Select modes]";
                        if (containingModes.size() == 1) {
                            addToModesPreview = containingModes.front();
                        } else if (!containingModes.empty()) {
                            addToModesPreview = std::to_string(containingModes.size()) + " modes selected";
                        }

                        if (ImGui::BeginCombo("Add to Modes##mirror_refs_add_to_modes", addToModesPreview.c_str())) {
                            for (auto& candidateMode : config.modes) {
                                if (candidateMode.name.empty()) {
                                    continue;
                                }

                                const bool isInMode = platform::config::IsMirrorInMode(candidateMode, mirror.name);
                                if (ImGui::Selectable(candidateMode.name.c_str(),
                                                      isInMode,
                                                      ImGuiSelectableFlags_DontClosePopups)) {
                                    if (isInMode) {
                                        platform::config::RemoveMirrorFromMode(candidateMode, mirror.name);
                                    } else {
                                        platform::config::AddMirrorToMode(candidateMode, mirror.name);
                                    }
                                    AutoSaveConfig(config);
                                }
                            }
                            ImGui::EndCombo();
                        }

                        containingModes = platform::config::GetModesContainingMirror(config, mirror.name);
                        if (!containingModes.empty()) {
                            ImGui::Text("Used in modes:");
                            for (const auto& mName : containingModes) {
                                ImGui::BulletText("%s", mName.c_str());
                            }
                        } else {
                            ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.2f, 1.0f), "Not used in any mode");
                        }

                        std::vector<std::string> containingGroups;
                        for (const auto& grp : config.mirrorGroups) {
                            for (const auto& gmi : grp.mirrors) {
                                if (gmi.mirrorId == mirror.name) {
                                    containingGroups.push_back(grp.name);
                                    break;
                                }
                            }
                        }
                        if (!containingGroups.empty()) {
                            ImGui::Text("Used in groups:");
                            for (const auto& gName : containingGroups) {
                                ImGui::BulletText("%s", gName.c_str());
                            }
                        } else {
                            ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.2f, 1.0f), "Not used in any group");
                        }
                    }

                    ImGui::PopID();
                }
                ImGui::EndChild();
                ImGui::EndTable();
            }

            if (g_mirrorEditorState.imageSourcePickerOpen) {
                ImGui::SetNextWindowSize(ImVec2(900.0f, 620.0f), ImGuiCond_Appearing);
                if (IGFD::FileDialog::Instance()->Display("mirror_image_source_picker",
                                                          ImGuiWindowFlags_NoCollapse,
                                                          ImVec2(720.0f, 480.0f))) {
                    if (IGFD::FileDialog::Instance()->IsOk()) {
                        const int mirrorIndex = g_mirrorEditorState.imageSourcePickerMirrorIndex;
                        if (mirrorIndex >= 0 && mirrorIndex < static_cast<int>(config.mirrors.size())) {
                            std::string selectedPath =
                                IGFD::FileDialog::Instance()->GetFilePathName(IGFD_ResultMode_KeepInputFile);
                            if (!selectedPath.empty()) {
                                auto& selectedMirror = config.mirrors[static_cast<std::size_t>(mirrorIndex)];
                                selectedMirror.source.image = platform::config::NormalizePathForConfig(selectedPath);
                                selectedMirror.source.lastKnownWidth = 0;
                                selectedMirror.source.lastKnownHeight = 0;
                                ForgetMirrorImageSource(selectedMirror.name);
                                AutoSaveConfig(config);
                            }
                        }
                    }

                    IGFD::FileDialog::Instance()->Close();
                    g_mirrorEditorState.imageSourcePickerOpen = false;
                    g_mirrorEditorState.imageSourcePickerMirrorIndex = -1;
                }
            }

            ImGui::EndTabItem();
        }

        const ImGuiTabItemFlags groupsTabFlags =
            (g_mirrorEditorState.mainEditorTabSelectionPending &&
             g_mirrorEditorState.mainEditorTab == MirrorsMainEditorTab::Groups)
                ? ImGuiTabItemFlags_SetSelected
                : 0;
        if (ImGui::BeginTabItem("Mirror Groups", nullptr, groupsTabFlags)) {
            g_mirrorEditorState.mainEditorTab = MirrorsMainEditorTab::Groups;
            g_mirrorEditorState.mainEditorTabSelectionPending = false;
            ImGui::Separator();

            if (!config.mirrorGroups.empty()) {
                s_selectedGroupIndex = std::clamp(s_selectedGroupIndex, 0, static_cast<int>(config.mirrorGroups.size()) - 1);
            }
            if (ImGui::BeginTable("##groups_split_table", 2, splitPaneFlags)) {
                ImGui::TableSetupColumn("Group List", ImGuiTableColumnFlags_WidthFixed, 280.0f);
                ImGui::TableSetupColumn("Group Editor", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                ImGui::BeginChild("##group_list_panel");
                const float groupListFooterHeight = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y + 6.0f;
                ImGui::BeginChild("##group_list_child", ImVec2(0.0f, -groupListFooterHeight), false);
                if (config.mirrorGroups.empty()) {
                    ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "No mirror groups configured.");
                } else {
                    for (size_t i = 0; i < config.mirrorGroups.size(); ++i) {
                        const auto& grp = config.mirrorGroups[i];
                        const bool selected = static_cast<int>(i) == s_selectedGroupIndex;
                        const std::string displayName = grp.name.empty() ? "[unnamed group]" : grp.name;
                        const std::string listLabel = displayName + "###group_list_item_" + std::to_string(i);
                        if (ImGui::Selectable(listLabel.c_str(), selected)) {
                            s_selectedGroupIndex = static_cast<int>(i);
                        }
                        if (i < config.mirrorGroups.size() - 1) {
                            ImGui::Separator();
                        }
                    }
                }
                ImGui::EndChild();
                ImGui::Separator();

                if (AnimatedButton("+##group_sidebar_add", ImVec2(28.0f, 0.0f))) {
                    addNewGroup();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Add");
                }

                const bool hasGroupSelection = !config.mirrorGroups.empty() &&
                                               s_selectedGroupIndex >= 0 &&
                                               s_selectedGroupIndex < static_cast<int>(config.mirrorGroups.size());
                if (!hasGroupSelection) {
                    ImGui::BeginDisabled();
                }

                ImGui::SameLine();
                if (AnimatedButton("-##group_sidebar_delete", ImVec2(28.0f, 0.0f))) {
                    ImGui::OpenPopup("##del_grp");
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                    ImGui::SetTooltip("Delete");
                }

                ImGui::SameLine();
                if (AnimatedButton("x2##group_sidebar_duplicate", ImVec2(28.0f, 0.0f))) {
                    groupToDuplicate = s_selectedGroupIndex;
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                    ImGui::SetTooltip("Duplicate");
                }

                if (!hasGroupSelection) {
                    ImGui::EndDisabled();
                }

                if (ImGui::BeginPopupModal("##del_grp", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                    const char* groupName = "";
                    if (hasGroupSelection) {
                        groupName = config.mirrorGroups[static_cast<size_t>(s_selectedGroupIndex)].name.c_str();
                    }
                    ImGui::Text("Delete group '%s'?", groupName);
                    ImGui::Separator();
                    if (AnimatedButton("Yes")) {
                        groupToRemove = s_selectedGroupIndex;
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::SameLine();
                    if (AnimatedButton("Cancel")) {
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndPopup();
                }
                ImGui::EndChild();

                ImGui::TableSetColumnIndex(1);
                ImGui::BeginChild("##group_editor_child");
                if (config.mirrorGroups.empty()) {
                    ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "No mirror groups configured.");
                    ImGui::Text("Use + in the group list to add one.");
                } else {
                    RenderSharedMirrorGroupEditorSections(config, s_selectedGroupIndex, "main_group_editor");
                }
                ImGui::EndChild();
                ImGui::EndTable();
            }

            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    if (mirrorToDuplicate != -1 && mirrorToDuplicate < static_cast<int>(config.mirrors.size())) {
        const auto sourceIndex = static_cast<size_t>(mirrorToDuplicate);
        platform::config::MirrorConfig duplicate = config.mirrors[sourceIndex];
        duplicate.name = platform::config::MakeUniqueMirrorCopyName(config, duplicate.name);
        config.mirrors.insert(config.mirrors.begin() + sourceIndex + 1, std::move(duplicate));
        s_selectedMirrorIndex = static_cast<int>(sourceIndex + 1);
        AutoSaveConfig(config);
    }

    if (mirrorToRemove != -1) {
        std::string nToRemove = config.mirrors[mirrorToRemove].name;
        ForgetMirrorImageSource(nToRemove);
        config.mirrors.erase(config.mirrors.begin() + mirrorToRemove);

        platform::config::RemoveMirrorReferences(config, nToRemove);
        if (!config.mirrors.empty()) {
            s_selectedMirrorIndex = std::clamp(s_selectedMirrorIndex, 0, static_cast<int>(config.mirrors.size()) - 1);
        } else {
            s_selectedMirrorIndex = 0;
        }
        g_mirrorEditorState.selectedMirrorIndex = -1;
        g_mirrorEditorState.nameBuffer[0] = '\0';
        g_mirrorEditorState.mirrorNameError.clear();
        AutoSaveConfig(config);
    }

    if (groupToDuplicate != -1 && groupToDuplicate < static_cast<int>(config.mirrorGroups.size())) {
        const auto sourceIndex = static_cast<size_t>(groupToDuplicate);
        platform::config::MirrorGroupConfig duplicate = config.mirrorGroups[sourceIndex];
        duplicate.name = platform::config::MakeUniqueGroupCopyName(config, duplicate.name);
        config.mirrorGroups.insert(config.mirrorGroups.begin() + sourceIndex + 1, std::move(duplicate));
        s_selectedGroupIndex = static_cast<int>(sourceIndex + 1);
        AutoSaveConfig(config);
    }

    if (groupToRemove != -1 && groupToRemove < static_cast<int>(config.mirrorGroups.size())) {
        const std::string groupNameToRemove = config.mirrorGroups[static_cast<size_t>(groupToRemove)].name;
        config.mirrorGroups.erase(config.mirrorGroups.begin() + groupToRemove);
        for (auto& mode : config.modes) {
            platform::config::RemoveGroupFromMode(mode, groupNameToRemove);
        }
        if (!config.mirrorGroups.empty()) {
            s_selectedGroupIndex = std::clamp(s_selectedGroupIndex, 0, static_cast<int>(config.mirrorGroups.size()) - 1);
        } else {
            s_selectedGroupIndex = 0;
        }
        g_mirrorEditorState.selectedGroupIndex = -1;
        g_mirrorEditorState.groupNameBuffer[0] = '\0';
        g_mirrorEditorState.groupNameError.clear();
        AutoSaveConfig(config);
    }
}
