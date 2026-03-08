void RenderMirrorsTab(platform::config::LinuxscreenConfig& config) {
    float displayWidth = 0.0f;
    float displayHeight = 0.0f;
    float framebufferScaleX = 1.0f;
    float framebufferScaleY = 1.0f;
    const bool hasDisplayMetrics = GetOverlayDisplayMetrics(displayWidth, displayHeight, framebufferScaleX, framebufferScaleY);
    (void)framebufferScaleX;
    (void)framebufferScaleY;
    const bool hasValidDisplaySize = hasDisplayMetrics && displayWidth > 0.0f && displayHeight > 0.0f;

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
        bool changed = false;
        const int currentIndex = FindMirrorRelativeToOptionIndex(relativeTo);
        const char* preview = (currentIndex >= 0) ? kMirrorRelativeToOptions[currentIndex].label : "Unknown";
        if (ImGui::BeginCombo(label, preview)) {
            for (const auto& option : kMirrorRelativeToOptions) {
                const bool selected = (relativeTo == option.value);
                if (ImGui::Selectable(option.label, selected)) {
                    relativeTo = option.value;
                    changed = true;
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        return changed;
    };

    int activeModeViewportWidth = static_cast<int>(displayWidth);
    int activeModeViewportHeight = static_cast<int>(displayHeight);
    bool hasActiveModeViewport = false;
    if (hasValidDisplaySize) {
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
                hasActiveModeViewport = true;
                activeModeViewportWidth = modeWidth;
                activeModeViewportHeight = modeHeight;
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

    auto updateGroupRelativeSizeFromScale = [&](platform::config::MirrorGroupConfig& group) {
        float containerWidth = 0.0f;
        float containerHeight = 0.0f;
        if (!resolveOutputContainerSize(group.output, containerWidth, containerHeight)) {
            return;
        }

        const platform::config::MirrorGroupItem* firstEnabledItem = nullptr;
        const platform::config::MirrorConfig* itemMirror = nullptr;
        for (const auto& item : group.mirrors) {
            if (!item.enabled) {
                continue;
            }
            const platform::config::MirrorConfig* resolvedMirror = nullptr;
            for (const auto& mirror : config.mirrors) {
                if (mirror.name == item.mirrorId) {
                    resolvedMirror = &mirror;
                    break;
                }
            }
            if (!resolvedMirror) {
                continue;
            }
            firstEnabledItem = &item;
            itemMirror = resolvedMirror;
            break;
        }
        if (!firstEnabledItem || !itemMirror) {
            return;
        }

        const float mirrorScaleX = itemMirror->output.separateScale ? itemMirror->output.scaleX : itemMirror->output.scale;
        const float mirrorScaleY = itemMirror->output.separateScale ? itemMirror->output.scaleY : itemMirror->output.scale;
        const int border = platform::config::GetMirrorDynamicBorderPadding(itemMirror->border);
        const float baseWidth = static_cast<float>(itemMirror->captureWidth + (2 * border));
        const float baseHeight = static_cast<float>(itemMirror->captureHeight + (2 * border));
        if (!(baseWidth > 0.0f) || !(baseHeight > 0.0f)) {
            return;
        }

        const float outputWidth = baseWidth * mirrorScaleX * group.output.scale * firstEnabledItem->widthPercent;
        const float outputHeight = baseHeight * mirrorScaleY * group.output.scale * firstEnabledItem->heightPercent;
        group.output.relativeWidth = std::clamp(outputWidth / containerWidth, 0.01f, 20.0f);
        group.output.relativeHeight = std::clamp(outputHeight / containerHeight, 0.01f, 20.0f);
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

    auto resolveGroupScaleReference = [&](const platform::config::MirrorGroupConfig& group,
                                          float& outContainerWidth,
                                          float& outContainerHeight,
                                          float& outBaseWidth,
                                          float& outBaseHeight,
                                          float& outItemWidthPercent,
                                          float& outItemHeightPercent) {
        outContainerWidth = 0.0f;
        outContainerHeight = 0.0f;
        outBaseWidth = 0.0f;
        outBaseHeight = 0.0f;
        outItemWidthPercent = 1.0f;
        outItemHeightPercent = 1.0f;
        if (!resolveOutputContainerSize(group.output, outContainerWidth, outContainerHeight)) {
            return false;
        }

        const platform::config::MirrorGroupItem* firstEnabledItem = nullptr;
        const platform::config::MirrorConfig* itemMirror = nullptr;
        for (const auto& item : group.mirrors) {
            if (!item.enabled) {
                continue;
            }
            const platform::config::MirrorConfig* resolvedMirror = nullptr;
            for (const auto& mirror : config.mirrors) {
                if (mirror.name == item.mirrorId) {
                    resolvedMirror = &mirror;
                    break;
                }
            }
            if (!resolvedMirror) {
                continue;
            }
            firstEnabledItem = &item;
            itemMirror = resolvedMirror;
            break;
        }
        if (!firstEnabledItem || !itemMirror) {
            return false;
        }

        const int border = platform::config::GetMirrorDynamicBorderPadding(itemMirror->border);
        outBaseWidth = static_cast<float>(itemMirror->captureWidth + (2 * border));
        outBaseHeight = static_cast<float>(itemMirror->captureHeight + (2 * border));
        outItemWidthPercent = std::max(0.0001f, firstEnabledItem->widthPercent);
        outItemHeightPercent = std::max(0.0001f, firstEnabledItem->heightPercent);
        return outBaseWidth > 0.0f && outBaseHeight > 0.0f;
    };

    auto getGroupUniformRelativeScale = [&](const platform::config::MirrorGroupConfig& group, float& outScale) {
        float containerWidth = 0.0f;
        float containerHeight = 0.0f;
        float baseWidth = 0.0f;
        float baseHeight = 0.0f;
        float itemWidthPercent = 1.0f;
        float itemHeightPercent = 1.0f;
        if (!resolveGroupScaleReference(group,
                                        containerWidth,
                                        containerHeight,
                                        baseWidth,
                                        baseHeight,
                                        itemWidthPercent,
                                        itemHeightPercent)) {
            return false;
        }

        const float scaleX = (containerWidth * group.output.relativeWidth * itemWidthPercent) / baseWidth;
        const float scaleY = (containerHeight * group.output.relativeHeight * itemHeightPercent) / baseHeight;
        outScale = std::clamp(ResolveUniformScaleByFitMode(scaleX,
                                                           scaleY,
                                                           NormalizeAspectFitMode(group.output.aspectFitMode)),
                              0.01f,
                              20.0f);
        return true;
    };

    auto setGroupRelativeSizeFromUniformScale = [&](platform::config::MirrorGroupConfig& group, float uniformScale) {
        uniformScale = std::clamp(uniformScale, 0.01f, 20.0f);

        float containerWidth = 0.0f;
        float containerHeight = 0.0f;
        float baseWidth = 0.0f;
        float baseHeight = 0.0f;
        float itemWidthPercent = 1.0f;
        float itemHeightPercent = 1.0f;
        if (!resolveGroupScaleReference(group,
                                        containerWidth,
                                        containerHeight,
                                        baseWidth,
                                        baseHeight,
                                        itemWidthPercent,
                                        itemHeightPercent)) {
            group.output.relativeWidth = uniformScale;
            group.output.relativeHeight = uniformScale;
            return;
        }

        group.output.relativeWidth = std::clamp((uniformScale * baseWidth) / (containerWidth * itemWidthPercent), 0.01f, 20.0f);
        group.output.relativeHeight = std::clamp((uniformScale * baseHeight) / (containerHeight * itemHeightPercent), 0.01f, 20.0f);
    };

    auto makeUniqueMirrorCopyName = [&](const std::string& sourceName) {
        const std::string stem = sourceName.empty() ? "Mirror" : sourceName;
        const std::string baseCopyName = stem + " (Copy)";
        std::string candidate = baseCopyName;
        int suffix = 2;
        auto exists = [&](const std::string& name) {
            for (const auto& mirror : config.mirrors) {
                if (mirror.name == name) {
                    return true;
                }
            }
            return false;
        };
        while (exists(candidate)) {
            candidate = stem + " (Copy " + std::to_string(suffix) + ")";
            ++suffix;
        }
        return candidate;
    };

    auto makeUniqueGroupCopyName = [&](const std::string& sourceName) {
        const std::string stem = sourceName.empty() ? "Group" : sourceName;
        const std::string baseCopyName = stem + " (Copy)";
        std::string candidate = baseCopyName;
        int suffix = 2;
        auto exists = [&](const std::string& name) {
            for (const auto& group : config.mirrorGroups) {
                if (group.name == name) {
                    return true;
                }
            }
            return false;
        };
        while (exists(candidate)) {
            candidate = stem + " (Copy " + std::to_string(suffix) + ")";
            ++suffix;
        }
        return candidate;
    };

    int mirrorToRemove = -1;
    int mirrorToDuplicate = -1;
    int groupToRemove = -1;
    int groupToDuplicate = -1;
    static int s_selectedMirrorIndex = 0;
    static int s_selectedGroupIndex = 0;

    auto addNewMirror = [&]() {
        platform::config::MirrorConfig newMirror;
        newMirror.name = "New Mirror " + std::to_string(config.mirrors.size() + 1);
        newMirror.output.relativeTo = "centerViewport";
        newMirror.colorSensitivity = 1.0f;
        newMirror.rawOutput = true;
        platform::config::MirrorCaptureConfig newZone;
        newZone.relativeTo = "centerViewport";
        newMirror.input.push_back(newZone);
        config.mirrors.push_back(std::move(newMirror));
        s_selectedMirrorIndex = static_cast<int>(config.mirrors.size()) - 1;
        g_mirrorEditorState.selectedMirrorIndex = -1;
        g_mirrorEditorState.nameBuffer[0] = '\0';
        g_mirrorEditorState.mirrorNameError.clear();
        AutoSaveConfig(config);
    };

    auto addNewGroup = [&]() {
        platform::config::MirrorGroupConfig grp;
        grp.name = "Group" + std::to_string(config.mirrorGroups.size());
        grp.output.relativeTo = "centerViewport";
        config.mirrorGroups.push_back(grp);
        s_selectedGroupIndex = static_cast<int>(config.mirrorGroups.size()) - 1;
        g_mirrorEditorState.selectedGroupIndex = -1;
        g_mirrorEditorState.groupNameBuffer[0] = '\0';
        g_mirrorEditorState.groupNameError.clear();
        AutoSaveConfig(config);
    };

    const ImGuiTableFlags splitPaneFlags = ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV;

    if (ImGui::BeginTabBar("##mirrors_split_panes")) {
        if (ImGui::BeginTabItem("Mirrors")) {
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
                    }

                    if (AnimatedCollapsingHeader("Identity")) {


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

                        int captureWidth = mirror.captureWidth;
                        int captureHeight = mirror.captureHeight;
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
                        ImGui::Unindent();
                    }

                    if (AnimatedCollapsingHeader("Border Settings")) {


                        HeaderRevealScope headerRevealScope = BeginAnimatedHeaderContentReveal();
                        ImGui::Indent();
                        const char* borderTypes[] = { "Dynamic (around content)", "Static (shape overlay)" };
                        int currentBorderType = static_cast<int>(mirror.border.type);
                        ImGui::PushItemWidth(200);
                        if (ImGui::Combo("Border Type", &currentBorderType, borderTypes, IM_ARRAYSIZE(borderTypes))) {
                            mirror.border.type = static_cast<platform::config::MirrorBorderType>(currentBorderType);
                            AutoSaveConfig(config);
                        }
                        ImGui::PopItemWidth();

                        if (mirror.border.type == platform::config::MirrorBorderType::Dynamic) {
                            if (ImGui::DragInt("Dynamic Thickness", &mirror.border.dynamicThickness, 1, 0, 32)) {
                                if (mirror.border.dynamicThickness < 0) mirror.border.dynamicThickness = 0;
                                AutoSaveConfig(config);
                            }

                            float dynColor[4] = { mirror.colors.border.r, mirror.colors.border.g, mirror.colors.border.b, mirror.colors.border.a };
                            if (ImGui::ColorEdit4("Border Color##dyn", dynColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar)) {
                                mirror.colors.border = { dynColor[0], dynColor[1], dynColor[2], dynColor[3] };
                                AutoSaveConfig(config);
                            }
                        } else {
                            ImGui::TextDisabled("Set Thickness to 0 to disable");

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

                    if (AnimatedCollapsingHeader("Output Scale")) {


                        HeaderRevealScope headerRevealScope = BeginAnimatedHeaderContentReveal();
                        ImGui::Indent();
                        if (ImGui::Checkbox("Relative size to container##mirror_output_size", &mirror.output.useRelativeSize)) {
                            if (mirror.output.useRelativeSize) {
                                updateMirrorRelativeSizeFromScale(mirror);
                            }
                            AutoSaveConfig(config);
                        }
                        ImGui::SameLine();
                        HelpMarker("When enabled, output width/height are stored as percentages of the anchor container.\n"
                                   "Container is screen for *Screen anchors and mode viewport for *Viewport/Pie anchors.");

                        if (ImGui::Checkbox("Preserve aspect ratio##mirror_output_size", &mirror.output.preserveAspectRatio)) {
                            if (mirror.output.preserveAspectRatio) {
                                float uniformScale = 1.0f;
                                if (getMirrorUniformRelativeScale(mirror, uniformScale)) {
                                    setMirrorRelativeSizeFromUniformScale(mirror, uniformScale);
                                }
                            }
                            AutoSaveConfig(config);
                        }

                        if (mirror.output.preserveAspectRatio) {
                            if (DrawAspectFitModeCombo("Fit Mode##mirror_output_size", mirror.output.aspectFitMode)) {
                                mirror.output.aspectFitMode = NormalizeAspectFitMode(mirror.output.aspectFitMode);
                                AutoSaveConfig(config);
                            }
                            float uniformScale = 1.0f;
                            if (!getMirrorUniformRelativeScale(mirror, uniformScale)) {
                                uniformScale = ResolveUniformScaleByFitMode(std::clamp(mirror.output.relativeWidth, 0.01f, 20.0f),
                                                                             std::clamp(mirror.output.relativeHeight, 0.01f, 20.0f),
                                                                             NormalizeAspectFitMode(mirror.output.aspectFitMode));
                            }
                            float scalePercent = uniformScale * 100.0f;
                            if (ImGui::SliderFloat("Scale %%##mirror_output_size", &scalePercent, 1.0f, 2000.0f, "%.1f%%")) {
                                setMirrorRelativeSizeFromUniformScale(mirror, scalePercent / 100.0f);
                                mirror.output.useRelativeSize = true;
                                AutoSaveConfig(config);
                            }
                        } else {
                            float widthPercent = mirror.output.relativeWidth * 100.0f;
                            if (ImGui::SliderFloat("Width %%##mirror_output_size", &widthPercent, 1.0f, 2000.0f, "%.1f%%")) {
                                mirror.output.relativeWidth = std::clamp(widthPercent / 100.0f, 0.01f, 20.0f);
                                mirror.output.useRelativeSize = true;
                                AutoSaveConfig(config);
                            }
                            float heightPercent = mirror.output.relativeHeight * 100.0f;
                            if (ImGui::SliderFloat("Height %%##mirror_output_size", &heightPercent, 1.0f, 2000.0f, "%.1f%%")) {
                                mirror.output.relativeHeight = std::clamp(heightPercent / 100.0f, 0.01f, 20.0f);
                                mirror.output.useRelativeSize = true;
                                AutoSaveConfig(config);
                            }
                        }
                        ImGui::Unindent();
                    }

                    if (AnimatedCollapsingHeader("Output Position")) {


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
                            if (ImGui::SliderFloat("X %%##mirror_output", &xPercent, -100.0f, 200.0f, "%.1f%%")) {
                                mirror.output.relativeX = xPercent / 100.0f;
                                updatePixelsFromRelative(mirror.output);
                                AutoSaveConfig(config);
                            }

                            float yPercent = mirror.output.relativeY * 100.0f;
                            if (ImGui::SliderFloat("Y %%##mirror_output", &yPercent, -100.0f, 200.0f, "%.1f%%")) {
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

                        ImGui::Text("Target Colors (Match):");
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

                    if (AnimatedCollapsingHeader("Input/Capture Zones")) {


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
                                if (drawRelativeToCombo("##zone_relative_to", zone.relativeTo)) {
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
                            nZone.relativeTo = "centerViewport";
                            mirror.input.push_back(nZone);
                            AutoSaveConfig(config);
                        }
                        ImGui::Unindent();
                    }

                    if (AnimatedCollapsingHeader("References")) {


                        HeaderRevealScope headerRevealScope = BeginAnimatedHeaderContentReveal();
                        ImGui::Indent();
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
                        ImGui::Unindent();
                    }

                    ImGui::PopID();
                }
                ImGui::EndChild();
                ImGui::EndTable();
            }

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Mirror Groups")) {
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
                    auto& grp = config.mirrorGroups[static_cast<size_t>(s_selectedGroupIndex)];
                    ImGui::PushID(10000 + s_selectedGroupIndex);

                    if (g_mirrorEditorState.selectedGroupIndex != s_selectedGroupIndex) {
                        g_mirrorEditorState.selectedGroupIndex = s_selectedGroupIndex;
                        CopyEditorNameToBuffer(g_mirrorEditorState.groupNameBuffer,
                                               sizeof(g_mirrorEditorState.groupNameBuffer),
                                               grp.name);
                        g_mirrorEditorState.groupNameError.clear();
                    }

                    if (AnimatedCollapsingHeader("Identity")) {


                        HeaderRevealScope headerRevealScope = BeginAnimatedHeaderContentReveal();
                        ImGui::Indent();
                        if (ImGui::InputText("Group Name",
                                             g_mirrorEditorState.groupNameBuffer,
                                             sizeof(g_mirrorEditorState.groupNameBuffer))) {
                            g_mirrorEditorState.groupNameError.clear();
                        }
                        if (ImGui::IsItemDeactivatedAfterEdit()) {
                            const std::string newName = TrimEditorName(g_mirrorEditorState.groupNameBuffer);
                            if (newName.empty()) {
                                g_mirrorEditorState.groupNameError = "Group name cannot be empty.";
                            } else if (HasDuplicateGroupName(config, newName, s_selectedGroupIndex)) {
                                g_mirrorEditorState.groupNameError = "Group name must be unique.";
                            } else {
                                g_mirrorEditorState.groupNameError.clear();
                                if (newName != grp.name) {
                                    platform::config::RenameGroup(config, grp.name, newName);
                                }
                                CopyEditorNameToBuffer(g_mirrorEditorState.groupNameBuffer,
                                                       sizeof(g_mirrorEditorState.groupNameBuffer),
                                                       newName);
                                AutoSaveConfig(config);
                            }
                        }
                        if (!g_mirrorEditorState.groupNameError.empty()) {
                            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "%s", g_mirrorEditorState.groupNameError.c_str());
                        }
                        ImGui::Unindent();
                    }

                    if (AnimatedCollapsingHeader("Group Output Scale")) {


                        HeaderRevealScope headerRevealScope = BeginAnimatedHeaderContentReveal();
                        ImGui::Indent();
                        if (ImGui::Checkbox("Relative size to container##grpout_size", &grp.output.useRelativeSize)) {
                            if (grp.output.useRelativeSize) {
                                updateGroupRelativeSizeFromScale(grp);
                            }
                            AutoSaveConfig(config);
                        }
                        ImGui::SameLine();
                        HelpMarker("When enabled, group output width/height are stored as percentages of the anchor container.\n"
                                   "Container is screen for *Screen anchors and mode viewport for *Viewport/Pie anchors.");

                        if (ImGui::Checkbox("Preserve aspect ratio##grpout_size", &grp.output.preserveAspectRatio)) {
                            if (grp.output.preserveAspectRatio) {
                                float uniformScale = 1.0f;
                                if (getGroupUniformRelativeScale(grp, uniformScale)) {
                                    setGroupRelativeSizeFromUniformScale(grp, uniformScale);
                                }
                            }
                            AutoSaveConfig(config);
                        }

                        if (grp.output.preserveAspectRatio) {
                            if (DrawAspectFitModeCombo("Fit Mode##grpout_size", grp.output.aspectFitMode)) {
                                grp.output.aspectFitMode = NormalizeAspectFitMode(grp.output.aspectFitMode);
                                AutoSaveConfig(config);
                            }
                            float uniformScale = 1.0f;
                            if (!getGroupUniformRelativeScale(grp, uniformScale)) {
                                uniformScale = ResolveUniformScaleByFitMode(std::clamp(grp.output.relativeWidth, 0.01f, 20.0f),
                                                                             std::clamp(grp.output.relativeHeight, 0.01f, 20.0f),
                                                                             NormalizeAspectFitMode(grp.output.aspectFitMode));
                            }
                            float scalePercent = uniformScale * 100.0f;
                            if (ImGui::SliderFloat("Scale %%##grpout_size", &scalePercent, 1.0f, 2000.0f, "%.1f%%")) {
                                setGroupRelativeSizeFromUniformScale(grp, scalePercent / 100.0f);
                                grp.output.useRelativeSize = true;
                                AutoSaveConfig(config);
                            }
                        } else {
                            float widthPercent = grp.output.relativeWidth * 100.0f;
                            if (ImGui::SliderFloat("Width %%##grpout_size", &widthPercent, 1.0f, 2000.0f, "%.1f%%")) {
                                grp.output.relativeWidth = std::clamp(widthPercent / 100.0f, 0.01f, 20.0f);
                                grp.output.useRelativeSize = true;
                                AutoSaveConfig(config);
                            }

                            float heightPercent = grp.output.relativeHeight * 100.0f;
                            if (ImGui::SliderFloat("Height %%##grpout_size", &heightPercent, 1.0f, 2000.0f, "%.1f%%")) {
                                grp.output.relativeHeight = std::clamp(heightPercent / 100.0f, 0.01f, 20.0f);
                                grp.output.useRelativeSize = true;
                                AutoSaveConfig(config);
                            }
                        }
                        ImGui::Unindent();
                    }

                    if (AnimatedCollapsingHeader("Group Output Position")) {


                        HeaderRevealScope headerRevealScope = BeginAnimatedHeaderContentReveal();
                        ImGui::Indent();
                        if (ImGui::Checkbox("Relative to screen##grpout", &grp.output.useRelativePosition)) {
                            if (grp.output.useRelativePosition) {
                                updateRelativeFromPixels(grp.output);
                            } else {
                                updatePixelsFromRelative(grp.output);
                            }
                            AutoSaveConfig(config);
                        }

                        ImGui::SameLine();
                        HelpMarker("When enabled, position is stored as percentages of screen size.\n"
                                   "This makes configs portable across different screen resolutions.");

                        if (drawRelativeToCombo("Relative To##grpout", grp.output.relativeTo)) {
                            AutoSaveConfig(config);
                        }

                        if (grp.output.useRelativePosition) {
                            float xPercent = grp.output.relativeX * 100.0f;
                            if (ImGui::SliderFloat("X %%##grpout", &xPercent, -100.0f, 200.0f, "%.1f%%")) {
                                grp.output.relativeX = xPercent / 100.0f;
                                updatePixelsFromRelative(grp.output);
                                AutoSaveConfig(config);
                            }

                            float yPercent = grp.output.relativeY * 100.0f;
                            if (ImGui::SliderFloat("Y %%##grpout", &yPercent, -100.0f, 200.0f, "%.1f%%")) {
                                grp.output.relativeY = yPercent / 100.0f;
                                updatePixelsFromRelative(grp.output);
                                AutoSaveConfig(config);
                            }
                        } else {
                            if (ImGui::DragInt("X Offset##grpout", &grp.output.x, 1)) {
                                AutoSaveConfig(config);
                            }
                            if (ImGui::DragInt("Y Offset##grpout", &grp.output.y, 1)) {
                                AutoSaveConfig(config);
                            }
                        }
                        ImGui::Unindent();
                    }

                    if (AnimatedCollapsingHeader("Group Mirrors (Per-Item Sizing)")) {


                        HeaderRevealScope headerRevealScope = BeginAnimatedHeaderContentReveal();
                        ImGui::Indent();
                        int gm_remove = -1;
                        int gm_drag_source = -1;
                        int gm_drag_target = -1;
                        bool gm_drop_after = false;
                        int gm_preview_row = -1;
                        bool gm_preview_after = false;
                        ImGui::TextDisabled("Drag to reorder z-index (bottom -> top).");
                        if (ImGui::BeginTable("group_mirror_items",
                                              8,
                                              ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV)) {
                            ImGuiTable* groupMirrorTable = ImGui::GetCurrentTable();
                            ImGui::TableSetupColumn("###grp_mir_col_drag",
                                                    ImGuiTableColumnFlags_WidthFixed,
                                                    ImGui::GetFrameHeight() + 6.0f);
                            ImGui::TableSetupColumn("###grp_mir_col_on",
                                                    ImGuiTableColumnFlags_WidthFixed,
                                                    44.0f);
                            ImGui::TableSetupColumn("Mirror###grp_mir_col_mirror",
                                                    ImGuiTableColumnFlags_WidthStretch,
                                                    1.6f);
                            ImGui::TableSetupColumn("W%###grp_mir_col_w",
                                                    ImGuiTableColumnFlags_WidthFixed,
                                                    80.0f);
                            ImGui::TableSetupColumn("H%###grp_mir_col_h",
                                                    ImGuiTableColumnFlags_WidthFixed,
                                                    80.0f);
                            ImGui::TableSetupColumn("OX###grp_mir_col_ox",
                                                    ImGuiTableColumnFlags_WidthFixed,
                                                    72.0f);
                            ImGui::TableSetupColumn("OY###grp_mir_col_oy",
                                                    ImGuiTableColumnFlags_WidthFixed,
                                                    72.0f);
                            ImGui::TableSetupColumn("###grp_mir_col_delete",
                                                    ImGuiTableColumnFlags_WidthFixed,
                                                    ImGui::GetFrameHeight() + 6.0f);
                            ImGui::TableHeadersRow();

                            for (int j = 0; j < static_cast<int>(grp.mirrors.size()); ++j) {
                                auto& gi = grp.mirrors[j];
                                ImGui::PushID(j);
                                ImGui::TableNextRow();

                                ImGui::TableSetColumnIndex(0);
                                ImGui::SetNextItemWidth(-1.0f);
                                (void)ImGui::SmallButton("::##gm_drag");
                                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoDisableHover)) {
                                    const int payloadIndex = j;
                                    ImGui::SetDragDropPayload("LINUXSCREEN_GROUP_MIRROR_REORDER", &payloadIndex, sizeof(payloadIndex));
                                    ImGui::TextUnformatted(gi.mirrorId.empty() ? "[unnamed]" : gi.mirrorId.c_str());
                                    ImGui::EndDragDropSource();
                                }

                                ImGui::TableSetColumnIndex(1);
                                if (ImGui::Checkbox("##gmen", &gi.enabled)) {
                                    AutoSaveConfig(config);
                                }

                                ImGui::TableSetColumnIndex(2);
                                const std::string mirrorPreview = gi.mirrorId.empty() ? "[Select Mirror]" : gi.mirrorId;
                                ImGui::SetNextItemWidth(-1.0f);
                                if (ImGui::BeginCombo("##gmid", mirrorPreview.c_str())) {
                                    for (const auto& mirrorConf : config.mirrors) {
                                        const bool selected = (mirrorConf.name == gi.mirrorId);
                                        if (ImGui::Selectable(mirrorConf.name.c_str(), selected)) {
                                            gi.mirrorId = mirrorConf.name;
                                            AutoSaveConfig(config);
                                        }
                                        if (selected) {
                                            ImGui::SetItemDefaultFocus();
                                        }
                                    }
                                    ImGui::EndCombo();
                                }

                                bool mirrorExists = false;
                                for (const auto& mirrorConf : config.mirrors) {
                                    if (mirrorConf.name == gi.mirrorId) {
                                        mirrorExists = true;
                                        break;
                                    }
                                }
                                if (!mirrorExists && !gi.mirrorId.empty()) {
                                    ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.45f, 1.0f), "Missing");
                                }

                                ImGui::TableSetColumnIndex(3);
                                ImGui::SetNextItemWidth(-1.0f);
                                float widthPct = gi.widthPercent * 100.0f;
                                if (ImGui::SliderFloat("##gm_w", &widthPct, 10.0f, 200.0f, "%.0f%%")) {
                                    gi.widthPercent = widthPct / 100.0f;
                                    AutoSaveConfig(config);
                                }

                                ImGui::TableSetColumnIndex(4);
                                ImGui::SetNextItemWidth(-1.0f);
                                float heightPct = gi.heightPercent * 100.0f;
                                if (ImGui::SliderFloat("##gm_h", &heightPct, 10.0f, 200.0f, "%.0f%%")) {
                                    gi.heightPercent = heightPct / 100.0f;
                                    AutoSaveConfig(config);
                                }

                                ImGui::TableSetColumnIndex(5);
                                ImGui::SetNextItemWidth(-1.0f);
                                if (ImGui::DragInt("##gm_ox", &gi.offsetX, 1)) {
                                    AutoSaveConfig(config);
                                }

                                ImGui::TableSetColumnIndex(6);
                                ImGui::SetNextItemWidth(-1.0f);
                                if (ImGui::DragInt("##gm_oy", &gi.offsetY, 1)) {
                                    AutoSaveConfig(config);
                                }

                                ImGui::TableSetColumnIndex(7);
                                if (ImGui::SmallButton("X##gm")) {
                                    gm_remove = j;
                                }
                                ImRect rowRect = ImGui::TableGetCellBgRect(groupMirrorTable, 0);
                                const ImRect rightRect = ImGui::TableGetCellBgRect(groupMirrorTable, 7);
                                rowRect.Max.x = rightRect.Max.x;
                                const float midY = (rowRect.Min.y + rowRect.Max.y) * 0.5f;
                                constexpr int kGroupMirrorTableColumnCount = 8;
                                for (int col = 0; col < kGroupMirrorTableColumnCount; ++col) {
                                    ImGui::TableSetColumnIndex(col);
                                    const ImRect cellRect = ImGui::TableGetCellBgRect(groupMirrorTable, col);
                                    ImGui::PushID(col);
                                    if (ImGui::BeginDragDropTargetCustom(cellRect, ImGui::GetID("##gm_row_drop_target"))) {
                                        const bool dropAfter = ImGui::GetIO().MousePos.y > midY;
                                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("LINUXSCREEN_GROUP_MIRROR_REORDER",
                                                                                                        ImGuiDragDropFlags_AcceptBeforeDelivery)) {
                                            if (payload->DataSize == sizeof(int)) {
                                                gm_preview_row = j;
                                                gm_preview_after = dropAfter;
                                                if (payload->IsDelivery()) {
                                                    gm_drag_source = *static_cast<const int*>(payload->Data);
                                                    gm_drag_target = j;
                                                    gm_drop_after = dropAfter;
                                                }
                                            }
                                        }
                                        ImGui::EndDragDropTarget();
                                    }
                                    ImGui::PopID();
                                }
                                ImGui::TableSetColumnIndex(7);
                                if (gm_preview_row == j && ImGui::GetDragDropPayload() != nullptr) {
                                    ImDrawList* dl = ImGui::GetWindowDrawList();
                                    dl->AddRectFilled(rowRect.Min, rowRect.Max, IM_COL32(88, 166, 236, 34));
                                    const float lineY = gm_preview_after ? rowRect.Max.y : rowRect.Min.y;
                                    dl->AddLine(ImVec2(rowRect.Min.x, lineY),
                                                ImVec2(rowRect.Max.x, lineY),
                                                IM_COL32(72, 190, 255, 255),
                                                2.0f);
                                }

                                ImGui::PopID();
                            }
                            ImGui::EndTable();
                        }
                        if (gm_drag_source >= 0 &&
                            gm_drag_target >= 0 &&
                            gm_drag_source < static_cast<int>(grp.mirrors.size()) &&
                            gm_drag_target < static_cast<int>(grp.mirrors.size()) &&
                            gm_drag_source != gm_drag_target) {
                            const int source = gm_drag_source;
                            const int target = gm_drag_target;
                            int insertIndex = target + (gm_drop_after ? 1 : 0);
                            if (source < insertIndex) {
                                insertIndex -= 1;
                            }
                            auto movedItem = std::move(grp.mirrors[static_cast<size_t>(source)]);
                            grp.mirrors.erase(grp.mirrors.begin() + source);
                            grp.mirrors.insert(grp.mirrors.begin() + insertIndex, std::move(movedItem));
                            AutoSaveConfig(config);
                        }
                        if (gm_remove >= 0) {
                            grp.mirrors.erase(grp.mirrors.begin() + gm_remove);
                            AutoSaveConfig(config);
                        }

                        if (ImGui::BeginCombo("Add Mirror##add_mirror_to_group", "[Select Mirror]")) {
                            for (const auto& mir : config.mirrors) {
                                bool already = false;
                                for (const auto& gmi : grp.mirrors) {
                                    if (gmi.mirrorId == mir.name) { already = true; break; }
                                }
                                if (!already && ImGui::Selectable(mir.name.c_str())) {
                                    platform::config::MirrorGroupItem item;
                                    item.mirrorId = mir.name;
                                    grp.mirrors.push_back(item);
                                    AutoSaveConfig(config);
                                }
                            }
                            ImGui::EndCombo();
                        }
                        ImGui::Unindent();
                    }

                    if (AnimatedCollapsingHeader("References")) {


                        HeaderRevealScope headerRevealScope = BeginAnimatedHeaderContentReveal();
                        ImGui::Indent();
                        std::vector<std::string> containingModes;
                        containingModes.reserve(config.modes.size());
                        for (const auto& modeEntry : config.modes) {
                            if (modeEntry.name.empty()) {
                                continue;
                            }
                            if (platform::config::IsGroupInMode(modeEntry, grp.name)) {
                                containingModes.push_back(modeEntry.name);
                            }
                        }

                        std::string addToModesPreview = "[Select modes]";
                        if (containingModes.size() == 1) {
                            addToModesPreview = containingModes.front();
                        } else if (!containingModes.empty()) {
                            addToModesPreview = std::to_string(containingModes.size()) + " modes selected";
                        }

                        if (ImGui::BeginCombo("Add to Modes##group_refs_add_to_modes", addToModesPreview.c_str())) {
                            for (auto& candidateMode : config.modes) {
                                if (candidateMode.name.empty()) {
                                    continue;
                                }

                                const bool isInMode = platform::config::IsGroupInMode(candidateMode, grp.name);
                                if (ImGui::Selectable(candidateMode.name.c_str(),
                                                      isInMode,
                                                      ImGuiSelectableFlags_DontClosePopups)) {
                                    if (isInMode) {
                                        platform::config::RemoveGroupFromMode(candidateMode, grp.name);
                                    } else {
                                        platform::config::AddGroupToMode(candidateMode, grp.name);
                                    }
                                    AutoSaveConfig(config);
                                }
                            }
                            ImGui::EndCombo();
                        }

                        containingModes.clear();
                        for (const auto& modeEntry : config.modes) {
                            if (modeEntry.name.empty()) {
                                continue;
                            }
                            if (platform::config::IsGroupInMode(modeEntry, grp.name)) {
                                containingModes.push_back(modeEntry.name);
                            }
                        }

                        if (!containingModes.empty()) {
                            ImGui::Text("Used in modes:");
                            for (const auto& mName : containingModes) {
                                ImGui::BulletText("%s", mName.c_str());
                            }
                        } else {
                            ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.2f, 1.0f), "Not used in any mode");
                        }
                        ImGui::Unindent();
                    }

                    ImGui::PopID();
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
        duplicate.name = makeUniqueMirrorCopyName(duplicate.name);
        config.mirrors.insert(config.mirrors.begin() + sourceIndex + 1, std::move(duplicate));
        s_selectedMirrorIndex = static_cast<int>(sourceIndex + 1);
        AutoSaveConfig(config);
    }

    if (mirrorToRemove != -1) {
        std::string nToRemove = config.mirrors[mirrorToRemove].name;
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
        duplicate.name = makeUniqueGroupCopyName(duplicate.name);
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
