void RenderRebindsTab(platform::config::LinuxscreenConfig& config, bool isCapturing) {
    ImGui::Text("Key Rebinds");
    ImGui::Separator();

    const platform::config::CaptureTarget captureTarget = platform::config::GetCaptureTarget();
    const int captureTargetIndex = platform::config::GetCaptureTargetIndex();

    bool rebindsEnabled = config.keyRebinds.enabled;
    if (ImGui::Checkbox("Enable key rebinds", &rebindsEnabled)) {
        config.keyRebinds.enabled = rebindsEnabled;
        platform::x11::ShowRebindToggleIndicator(rebindsEnabled);
        AutoSaveConfig(config);
    }

    if (AnimatedButton("Open Keyboard Layout")) {
        g_rebindLayoutState.keyboardLayoutOpen = true;
        g_rebindLayoutState.keyboardLayoutCloseRequested = false;
        ++g_rebindLayoutState.keyboardLayoutOpenSequence;
        ImGui::OpenPopup("Keyboard Layout");
    }

    const ImVec2 center = ImGui::GetMainViewport()
                              ? ImVec2(ImGui::GetMainViewport()->WorkPos.x + ImGui::GetMainViewport()->WorkSize.x * 0.5f,
                                       ImGui::GetMainViewport()->WorkPos.y + ImGui::GetMainViewport()->WorkSize.y * 0.5f)
                              : ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f);
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImVec2 popupSize = ImVec2(1180.0f, 690.0f);
    if (ImGui::GetMainViewport()) {
        const ImVec2 workSize = ImGui::GetMainViewport()->WorkSize;
        popupSize.x = std::max(980.0f, std::min(1500.0f, workSize.x * 0.93f));
        popupSize.y = std::max(620.0f, std::min(980.0f, workSize.y * 0.90f));
    }
    ImGui::SetNextWindowSize(popupSize, ImGuiCond_Appearing);

    if (ImGui::BeginPopupModal("Keyboard Layout", &g_rebindLayoutState.keyboardLayoutOpen, ImGuiWindowFlags_NoScrollbar)) {
        if (!g_rebindLayoutState.keyboardLayoutOpen && !g_rebindLayoutState.keyboardLayoutCloseRequested) {
            // Keep the popup alive while animating close after titlebar X click.
            g_rebindLayoutState.keyboardLayoutCloseRequested = true;
            g_rebindLayoutState.keyboardLayoutOpen = true;
        }

        if (isCapturing && IsRebindCaptureTarget(captureTarget)) {
            RenderHotkeyCaptureModal();
        }

        const bool bindingActive = isCapturing;
        if (ImGui::IsKeyPressed(ImGuiKey_Escape) &&
            !bindingActive &&
            g_rebindLayoutState.unicodeEditIndex == -1) {
            g_rebindLayoutState.keyboardLayoutCloseRequested = true;
        }

        const float dt = GetOverlayAnimationDeltaTime();
        constexpr ImGuiID kLayoutModalAnimChannelId = static_cast<ImGuiID>(0xD54219A1u);
        const ImGuiID layoutModalAnimId = ImHashData(&g_rebindLayoutState.keyboardLayoutOpenSequence,
                                                     sizeof(g_rebindLayoutState.keyboardLayoutOpenSequence),
                                                     static_cast<ImGuiID>(0xD54219A0u));
        const bool closingLayout = g_rebindLayoutState.keyboardLayoutCloseRequested;
        const float layoutTarget = closingLayout ? 0.0f : 1.0f;
        const float layoutDuration = closingLayout ? 0.09f : 0.18f;
        const float layoutProgress = iam_tween_float(layoutModalAnimId,
                                                     kLayoutModalAnimChannelId,
                                                     layoutTarget,
                                                     layoutDuration,
                                                     iam_ease_preset(iam_ease_out_cubic),
                                                     iam_policy_crossfade,
                                                     dt);

        if (closingLayout && layoutProgress <= 0.02f) {
            g_rebindLayoutState.keyboardLayoutCloseRequested = false;
            g_rebindLayoutState.keyboardLayoutOpen = false;
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return;
        }

        const float layoutAlphaScale = std::clamp(0.22f + (0.78f * layoutProgress), 0.0f, 1.0f);
        const float layoutYOffset = (1.0f - layoutProgress) * 12.0f;
        if (layoutYOffset > 0.001f) {
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + layoutYOffset);
            SubmitCursorBoundaryItem();
        }
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * layoutAlphaScale);

        float scalePercent = g_rebindLayoutState.keyboardLayoutScale * 100.0f;
        ImGui::TextUnformatted("Scale:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(220.0f);
        if (ImGui::SliderFloat("##rebind_layout_scale", &scalePercent, 70.0f, 160.0f, "%.0f%%", ImGuiSliderFlags_AlwaysClamp)) {
            g_rebindLayoutState.keyboardLayoutScale = scalePercent / 100.0f;
        }
        ImGui::Separator();

        struct KeyCell {
            uint32_t vk = 0;
            const char* label = nullptr;
            float widthUnits = 1.0f;
        };

        auto spacer = [](float widthUnits) -> KeyCell { return KeyCell{ 0, nullptr, widthUnits }; };
        auto keyCell = [](uint32_t vk, float widthUnits = 1.0f, const char* label = nullptr) -> KeyCell { return KeyCell{ vk, label, widthUnits }; };
        const std::array<uint32_t, 5> mouseButtons = { platform::input::VK_LBUTTON, platform::input::VK_RBUTTON, platform::input::VK_MBUTTON,
                                                        platform::input::VK_XBUTTON1, platform::input::VK_XBUTTON2 };
        const std::array<const char*, 5> mouseLabels = { "M1", "M2", "M3", "M4", "M5" };

        const std::vector<std::vector<KeyCell>> rows = {
            { keyCell(platform::input::VK_ESCAPE, 1.0f, "ESC"), spacer(0.6f), keyCell(platform::input::VK_F1, 1.0f, "F1"),
              keyCell(static_cast<uint32_t>(platform::input::VK_F1 + 1), 1.0f, "F2"),
              keyCell(static_cast<uint32_t>(platform::input::VK_F1 + 2), 1.0f, "F3"),
              keyCell(static_cast<uint32_t>(platform::input::VK_F1 + 3), 1.0f, "F4"), spacer(0.35f),
              keyCell(static_cast<uint32_t>(platform::input::VK_F1 + 4), 1.0f, "F5"),
              keyCell(static_cast<uint32_t>(platform::input::VK_F1 + 5), 1.0f, "F6"),
              keyCell(static_cast<uint32_t>(platform::input::VK_F1 + 6), 1.0f, "F7"),
              keyCell(static_cast<uint32_t>(platform::input::VK_F1 + 7), 1.0f, "F8"), spacer(0.35f),
              keyCell(static_cast<uint32_t>(platform::input::VK_F1 + 8), 1.0f, "F9"),
              keyCell(static_cast<uint32_t>(platform::input::VK_F1 + 9), 1.0f, "F10"),
              keyCell(static_cast<uint32_t>(platform::input::VK_F1 + 10), 1.0f, "F11"),
              keyCell(static_cast<uint32_t>(platform::input::VK_F1 + 11), 1.0f, "F12") },
            { keyCell(platform::input::VK_OEM_3, 1.0f, "`"), keyCell('1'), keyCell('2'), keyCell('3'), keyCell('4'), keyCell('5'), keyCell('6'),
              keyCell('7'), keyCell('8'), keyCell('9'), keyCell('0'), keyCell(platform::input::VK_OEM_MINUS, 1.0f, "-"),
              keyCell(platform::input::VK_OEM_PLUS, 1.0f, "="), keyCell(platform::input::VK_BACK, 2.0f, "BACK") },
            { keyCell(platform::input::VK_TAB, 1.5f, "TAB"), keyCell('Q'), keyCell('W'), keyCell('E'), keyCell('R'), keyCell('T'), keyCell('Y'),
              keyCell('U'), keyCell('I'), keyCell('O'), keyCell('P'), keyCell(platform::input::VK_OEM_4, 1.0f, "["),
              keyCell(platform::input::VK_OEM_6, 1.0f, "]"), keyCell(platform::input::VK_OEM_5, 1.5f, "\\") },
            { keyCell(platform::input::VK_CAPITAL, 1.75f, "CAPS"), keyCell('A'), keyCell('S'), keyCell('D'), keyCell('F'), keyCell('G'), keyCell('H'),
              keyCell('J'), keyCell('K'), keyCell('L'), keyCell(platform::input::VK_OEM_1, 1.0f, ";"), keyCell(platform::input::VK_OEM_7, 1.0f, "'"),
              keyCell(platform::input::VK_RETURN, 2.25f, "ENTER") },
            { keyCell(platform::input::VK_LSHIFT, 2.25f, "LSHIFT"), keyCell('Z'), keyCell('X'), keyCell('C'), keyCell('V'), keyCell('B'), keyCell('N'),
              keyCell('M'), keyCell(platform::input::VK_OEM_COMMA, 1.0f, ","), keyCell(platform::input::VK_OEM_PERIOD, 1.0f, "."),
              keyCell(platform::input::VK_OEM_2, 1.0f, "/"), keyCell(platform::input::VK_RSHIFT, 2.75f, "RSHIFT") },
            { keyCell(platform::input::VK_LCONTROL, 1.4f, "LCTRL"), keyCell(platform::input::VK_LWIN, 1.4f, "LWIN"),
              keyCell(platform::input::VK_LMENU, 1.4f, "LALT"), keyCell(platform::input::VK_SPACE, 6.2f, "SPACE"),
              keyCell(platform::input::VK_RMENU, 1.4f, "RALT"), keyCell(platform::input::VK_RWIN, 1.4f, "RWIN"),
              keyCell(platform::input::VK_APPS, 1.4f, "APPS"), keyCell(platform::input::VK_RCONTROL, 1.4f, "RCTRL") },
        };

        std::set<uint32_t> presetLayoutVks;
        for (const auto& row : rows) {
            for (const auto& cell : row) {
                if (cell.vk != 0) {
                    presetLayoutVks.insert(cell.vk);
                }
            }
        }
        for (uint32_t mouseVk : mouseButtons) {
            presetLayoutVks.insert(mouseVk);
        }
        const auto isPresetLayoutVk = [&](uint32_t vk) { return presetLayoutVks.find(vk) != presetLayoutVks.end(); };

        if (!isCapturing && g_rebindLayoutState.unicodeEditIndex == -1 &&
            g_rebindLayoutState.customSourceCaptureIndex >= 0 &&
            g_rebindLayoutState.customSourceCaptureIndex < static_cast<int>(config.keyRebinds.rebinds.size())) {
            platform::config::BindingInputEvent capturedEvent;
            if (platform::config::ConsumeBindingInputEventSince(g_rebindLayoutState.customSourceCaptureSequence, capturedEvent) &&
                capturedEvent.action == platform::input::InputAction::Press && capturedEvent.vk != platform::input::VK_NONE) {
                const int rebindIndex = g_rebindLayoutState.customSourceCaptureIndex;
                g_rebindLayoutState.customSourceCaptureIndex = -1;
                g_rebindLayoutState.customSourceCaptureSequence = 0;

                if (capturedEvent.vk != platform::input::VK_LWIN && capturedEvent.vk != platform::input::VK_RWIN &&
                    capturedEvent.vk != platform::input::VK_ESCAPE && rebindIndex >= 0 &&
                    rebindIndex < static_cast<int>(config.keyRebinds.rebinds.size())) {
                    auto& editedRebind = config.keyRebinds.rebinds[static_cast<std::size_t>(rebindIndex)];
                    editedRebind.fromKey = capturedEvent.vk;
                    if (editedRebind.toKey == 0) {
                        editedRebind.toKey = capturedEvent.vk;
                    }
                    if (TrimAsciiWhitespace(editedRebind.name).empty() && !isPresetLayoutVk(capturedEvent.vk)) {
                        editedRebind.name = FormatSingleVk(capturedEvent.vk);
                    }
                    g_rebindLayoutState.contextVk = editedRebind.fromKey;
                    g_rebindLayoutState.contextPreferredIndex = rebindIndex;
                    AutoSaveConfig(config);
                }
            }
        } else {
            g_rebindLayoutState.customSourceCaptureIndex = -1;
            g_rebindLayoutState.customSourceCaptureSequence = 0;
        }

        const float footerReserve = ImGui::GetTextLineHeightWithSpacing() * 2.2f;
        const float childHeight = std::max(260.0f, ImGui::GetContentRegionAvail().y - footerReserve);
        ImGui::BeginChild("##rebind_layout_child", ImVec2(0, childHeight), true,
                          ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_AlwaysVerticalScrollbar);

        const float unitWidth = ImGui::GetFrameHeight() * 1.95f * g_rebindLayoutState.keyboardLayoutScale;
        const float keyHeight = ImGui::GetFrameHeight() * 1.90f * g_rebindLayoutState.keyboardLayoutScale;
        const float gap = 5.0f * g_rebindLayoutState.keyboardLayoutScale;

        bool openRebindContextPopup = false;
        bool openRebindContextPopupAtStoredPos = false;
        auto toggleRebindEnabled = [&](uint32_t sourceVk, int preferredIndex) {
            int idx = preferredIndex;
            if (idx < 0 || idx >= static_cast<int>(config.keyRebinds.rebinds.size()) ||
                config.keyRebinds.rebinds[static_cast<std::size_t>(idx)].fromKey != sourceVk) {
                idx = FindBestRebindIndexForKey(config, sourceVk);
            }
            if (idx < 0 || idx >= static_cast<int>(config.keyRebinds.rebinds.size())) {
                platform::config::KeyRebind added;
                added.fromKey = sourceVk;
                added.toKey = sourceVk;
                SetRebindInputState(added, RebindInputState::BlockInput);
                config.keyRebinds.rebinds.push_back(added);
                idx = static_cast<int>(config.keyRebinds.rebinds.size()) - 1;
                g_rebindLayoutState.contextVk = sourceVk;
                g_rebindLayoutState.contextPreferredIndex = idx;
                AutoSaveConfig(config);
                return;
            }

            auto& edit = config.keyRebinds.rebinds[static_cast<std::size_t>(idx)];
            const RebindInputState currentState = GetRebindInputState(edit);
            const bool hasCustomRemap = !IsNoOpRebindForKey(edit, sourceVk);

            if (!hasCustomRemap) {
                if (currentState == RebindInputState::BlockInput) {
                    EraseRebindAdjustingLayoutState(config, idx);
                    AutoSaveConfig(config);
                    return;
                }
                SetRebindInputState(edit, RebindInputState::BlockInput);
                g_rebindLayoutState.contextVk = sourceVk;
                g_rebindLayoutState.contextPreferredIndex = idx;
                AutoSaveConfig(config);
                return;
            }

            const RebindInputState nextState = NextRebindInputState(currentState);
            SetRebindInputState(edit, nextState);
            g_rebindLayoutState.contextVk = sourceVk;
            g_rebindLayoutState.contextPreferredIndex = idx;
            AutoSaveConfig(config);
        };
        for (std::size_t rowIndex = 0; rowIndex < rows.size(); ++rowIndex) {
            if (rowIndex > 0) {
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + gap);
            }

            for (std::size_t colIndex = 0; colIndex < rows[rowIndex].size(); ++colIndex) {
                const KeyCell& cell = rows[rowIndex][colIndex];
                if (colIndex > 0) {
                    ImGui::SameLine(0.0f, gap);
                }

                const float width = cell.widthUnits * unitWidth;
                if (cell.vk == 0) {
                    ImGui::Dummy(ImVec2(width, keyHeight));
                    continue;
                }

                const int rebindIndex = FindBestRebindIndexForKey(config, cell.vk);
                const platform::config::KeyRebind* rebind =
                    (rebindIndex >= 0 && rebindIndex < static_cast<int>(config.keyRebinds.rebinds.size()))
                        ? &config.keyRebinds.rebinds[static_cast<std::size_t>(rebindIndex)]
                        : nullptr;
                const RebindInputState rebindState = rebind ? GetRebindInputState(*rebind) : RebindInputState::EnabledRemap;
                const bool hasCustomRemap = rebind && !IsNoOpRebindForKey(*rebind, cell.vk);
                const bool activeRebind = rebind && rebind->fromKey != 0 &&
                                          (rebindState == RebindInputState::BlockInput || hasCustomRemap);
                const RebindButtonPalette palette = GetRebindButtonPalette(activeRebind, rebindState);
                ImGui::PushStyleColor(ImGuiCol_Button, palette.normal);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, palette.hovered);
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, palette.active);

                std::string keyLabel = cell.label ? std::string(cell.label) : FormatSingleVk(cell.vk);
                if (activeRebind) {
                    const std::string compactLine = BuildCompactRebindLine(rebind, cell.vk, width);
                    if (!compactLine.empty()) {
                        keyLabel += "\n" + compactLine;
                    }
                }

                ImGui::PushID(static_cast<int>(rowIndex * 100 + colIndex));
                ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.5f, 0.5f));
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0f, 2.0f));
                AnimatedButton(keyLabel.c_str(), ImVec2(width, keyHeight));
                ImGui::PopStyleVar();
                ImGui::PopStyleVar();
                const ImGuiID keyItemId = ImGui::GetItemID();
                DrawRebindKeyButtonEffects(keyItemId);
                if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
                    g_rebindLayoutState.contextVk = cell.vk;
                    g_rebindLayoutState.contextPreferredIndex = rebindIndex;
                    openRebindContextPopup = true;
                }
                if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                    toggleRebindEnabled(cell.vk, rebindIndex);
                    TriggerRebindKeyCyclePulse(keyItemId);
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::Text("%s (%u)", FormatSingleVk(cell.vk).c_str(), static_cast<unsigned>(cell.vk));
                    if (activeRebind) {
                        ImGui::Text("State: %s", RebindInputStateLabel(rebindState));
                        if (rebindState == RebindInputState::BlockInput) {
                            ImGui::TextDisabled("No output: source input is consumed.");
                        }
                        ImGui::Text("Chat/Text: %s", TypesValueForRebind(rebind, cell.vk).c_str());
                        ImGui::Text("Game: %s", TriggersValueForRebind(rebind, cell.vk).c_str());
                        ImGui::TextUnformatted("Left-click: configure, Right-click: cycle state");
                    } else {
                        ImGui::TextUnformatted("Left-click: configure. Right-click: create/cycle state.");
                    }
                    ImGui::EndTooltip();
                }
                ImGui::PopID();
                ImGui::PopStyleColor(3);
            }
        }

        ImGui::Spacing();
        ImGui::SeparatorText("Mouse");
        for (std::size_t i = 0; i < mouseButtons.size(); ++i) {
            if (i > 0) {
                ImGui::SameLine(0.0f, gap);
            }
            const uint32_t vk = mouseButtons[i];
            const int rebindIndex = FindBestRebindIndexForKey(config, vk);
            const platform::config::KeyRebind* rebind =
                (rebindIndex >= 0 && rebindIndex < static_cast<int>(config.keyRebinds.rebinds.size()))
                    ? &config.keyRebinds.rebinds[static_cast<std::size_t>(rebindIndex)]
                    : nullptr;
            const RebindInputState rebindState = rebind ? GetRebindInputState(*rebind) : RebindInputState::EnabledRemap;
            const bool hasCustomRemap = rebind && !IsNoOpRebindForKey(*rebind, vk);
            const bool activeRebind = rebind && rebind->fromKey != 0 &&
                                      (rebindState == RebindInputState::BlockInput || hasCustomRemap);
            const RebindButtonPalette palette = GetRebindButtonPalette(activeRebind, rebindState);
            ImGui::PushStyleColor(ImGuiCol_Button, palette.normal);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, palette.hovered);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, palette.active);
            ImGui::PushID(5000 + static_cast<int>(i));

            std::string mouseLabel = mouseLabels[i];
            if (activeRebind) {
                const std::string compactLine = BuildCompactRebindLine(rebind, vk, 168.0f * g_rebindLayoutState.keyboardLayoutScale);
                if (!compactLine.empty()) {
                    mouseLabel += "\n" + compactLine;
                }
            }

            ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.5f, 0.5f));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0f, 2.0f));
            AnimatedButton(mouseLabel.c_str(), ImVec2(168.0f * g_rebindLayoutState.keyboardLayoutScale, keyHeight));
            ImGui::PopStyleVar();
            ImGui::PopStyleVar();
            const ImGuiID mouseItemId = ImGui::GetItemID();
            DrawRebindKeyButtonEffects(mouseItemId);
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
                g_rebindLayoutState.contextVk = vk;
                g_rebindLayoutState.contextPreferredIndex = rebindIndex;
                openRebindContextPopup = true;
            }
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                toggleRebindEnabled(vk, rebindIndex);
                TriggerRebindKeyCyclePulse(mouseItemId);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::Text("%s (%u)", FormatSingleVk(vk).c_str(), static_cast<unsigned>(vk));
                if (activeRebind) {
                    ImGui::Text("State: %s", RebindInputStateLabel(rebindState));
                    if (rebindState == RebindInputState::BlockInput) {
                        ImGui::TextDisabled("No output: source input is consumed.");
                    }
                    ImGui::Text("Chat/Text: %s", TypesValueForRebind(rebind, vk).c_str());
                    ImGui::Text("Game: %s", TriggersValueForRebind(rebind, vk).c_str());
                    ImGui::TextUnformatted("Left-click: configure, Right-click: cycle state");
                } else {
                    ImGui::TextUnformatted("Left-click: configure. Right-click: create/cycle state.");
                }
                ImGui::EndTooltip();
            }
            ImGui::PopID();
            ImGui::PopStyleColor(3);
        }

        ImGui::Spacing();
        ImGui::SeparatorText("Custom Rebinds");
        ImGui::TextDisabled("Add non-layout keys or mouse inputs, then configure Types/Triggers like any key.");

        char customDraftNameBuffer[96] = {};
        std::snprintf(customDraftNameBuffer, sizeof(customDraftNameBuffer), "%s", g_rebindLayoutState.customDraftName.c_str());
        ImGui::SetNextItemWidth(220.0f * g_rebindLayoutState.keyboardLayoutScale);
        if (ImGui::InputTextWithHint("##custom_rebind_name", "Name (optional)", customDraftNameBuffer, sizeof(customDraftNameBuffer))) {
            g_rebindLayoutState.customDraftName = customDraftNameBuffer;
        }

        ImGui::SameLine();
        const bool draftInputCapturing = isCapturing && captureTarget == platform::config::CaptureTarget::RebindDraftInput;
        const std::string draftInputLabel = draftInputCapturing
                                                ? "[Press input...]"
                                                : (g_rebindLayoutState.customDraftInputVk != 0 ? FormatSingleVk(g_rebindLayoutState.customDraftInputVk)
                                                                                                : std::string("Select Input"));
        if (AnimatedButton((draftInputLabel + "##custom_rebind_capture").c_str(),
                          ImVec2(180.0f * g_rebindLayoutState.keyboardLayoutScale, 0.0f))) {
            g_rebindLayoutState.reopenContextPopupAfterCapture = false;
            g_rebindLayoutState.customSourceCaptureIndex = -1;
            g_rebindLayoutState.customSourceCaptureSequence = 0;
            platform::config::StartRebindDraftInputCapture();
        }

        ImGui::SameLine();
        ImGui::BeginDisabled(g_rebindLayoutState.customDraftInputVk == 0);
        if (AnimatedButton("Add / Upsert##custom_rebind_add", ImVec2(130.0f * g_rebindLayoutState.keyboardLayoutScale, 0.0f))) {
            const uint32_t inputVk = g_rebindLayoutState.customDraftInputVk;
            const std::string trimmedName = TrimAsciiWhitespace(g_rebindLayoutState.customDraftName);

            int rebindIndex = FindAnyRebindIndexForKey(config, inputVk);
            if (rebindIndex < 0) {
                platform::config::KeyRebind added;
                added.fromKey = inputVk;
                added.toKey = inputVk;
                added.enabled = true;
                added.consumeSourceInput = false;
                added.name = trimmedName;
                config.keyRebinds.rebinds.push_back(added);
                rebindIndex = static_cast<int>(config.keyRebinds.rebinds.size()) - 1;
            } else {
                auto& existing = config.keyRebinds.rebinds[static_cast<std::size_t>(rebindIndex)];
                existing.fromKey = inputVk;
                if (existing.toKey == 0) {
                    existing.toKey = inputVk;
                }
                existing.enabled = true;
                existing.consumeSourceInput = false;
                existing.name = trimmedName;
            }

            g_rebindLayoutState.contextVk = inputVk;
            g_rebindLayoutState.contextPreferredIndex = rebindIndex;
            g_rebindLayoutState.customDraftInputVk = 0;
            g_rebindLayoutState.customDraftName.clear();
            openRebindContextPopup = true;
            AutoSaveConfig(config);
        }
        ImGui::EndDisabled();

        if (g_rebindLayoutState.customDraftInputVk != 0) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Clear Input##custom_rebind_clear")) {
                g_rebindLayoutState.customDraftInputVk = 0;
            }
        }

        std::vector<int> customRebindIndices;
        customRebindIndices.reserve(config.keyRebinds.rebinds.size());
        for (int i = 0; i < static_cast<int>(config.keyRebinds.rebinds.size()); ++i) {
            const auto& rebind = config.keyRebinds.rebinds[static_cast<std::size_t>(i)];
            if (rebind.fromKey == 0 || isPresetLayoutVk(rebind.fromKey)) {
                continue;
            }
            customRebindIndices.push_back(i);
        }

        if (customRebindIndices.empty()) {
            ImGui::TextDisabled("(No custom rebinds)");
        } else {
            const float customButtonWidth = 230.0f * g_rebindLayoutState.keyboardLayoutScale;
            const float customButtonHeight = keyHeight;
            const int customColumns =
                std::max(1, static_cast<int>((ImGui::GetContentRegionAvail().x + gap) / (customButtonWidth + gap)));

            for (std::size_t customIdx = 0; customIdx < customRebindIndices.size(); ++customIdx) {
                const int rebindIndex = customRebindIndices[customIdx];
                if (rebindIndex < 0 || rebindIndex >= static_cast<int>(config.keyRebinds.rebinds.size())) {
                    continue;
                }

                auto& rebind = config.keyRebinds.rebinds[static_cast<std::size_t>(rebindIndex)];
                const RebindInputState rebindState = GetRebindInputState(rebind);
                const bool hasCustomRemap = !IsNoOpRebindForKey(rebind, rebind.fromKey);
                const bool activeRebind = rebind.fromKey != 0 &&
                                          (rebindState == RebindInputState::BlockInput || hasCustomRemap);
                const RebindButtonPalette palette = GetRebindButtonPalette(activeRebind, rebindState);

                if (customIdx > 0 && static_cast<int>(customIdx % static_cast<std::size_t>(customColumns)) != 0) {
                    ImGui::SameLine(0.0f, gap);
                }

                ImGui::PushID(8000 + rebindIndex);
                ImGui::PushStyleColor(ImGuiCol_Button, palette.normal);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, palette.hovered);
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, palette.active);

                std::string displayName = RebindDisplayName(rebind);
                if (displayName.empty()) {
                    displayName = FormatSingleVk(rebind.fromKey);
                }
                displayName = ShortenForButton(displayName, 12);
                const std::string compactLine = BuildCompactRebindLine(&rebind, rebind.fromKey, customButtonWidth);
                const std::string lowerLine = compactLine.empty() ? ShortenForButton(FormatSingleVk(rebind.fromKey), 10) : compactLine;
                const std::string customLabel = displayName + "\n" + lowerLine;

                ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.5f, 0.5f));
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0f, 2.0f));
                AnimatedButton(customLabel.c_str(), ImVec2(customButtonWidth, customButtonHeight));
                ImGui::PopStyleVar();
                ImGui::PopStyleVar();
                const ImGuiID customItemId = ImGui::GetItemID();
                DrawRebindKeyButtonEffects(customItemId);
                if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
                    g_rebindLayoutState.contextVk = rebind.fromKey;
                    g_rebindLayoutState.contextPreferredIndex = rebindIndex;
                    openRebindContextPopup = true;
                }
                if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                    toggleRebindEnabled(rebind.fromKey, rebindIndex);
                    TriggerRebindKeyCyclePulse(customItemId);
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::Text("%s (%u)", FormatSingleVk(rebind.fromKey).c_str(), static_cast<unsigned>(rebind.fromKey));
                    ImGui::Text("State: %s", RebindInputStateLabel(rebindState));
                    if (rebindState == RebindInputState::BlockInput) {
                        ImGui::TextDisabled("No output: source input is consumed.");
                    }
                    ImGui::Text("Chat/Text: %s", TypesValueForRebind(&rebind, rebind.fromKey).c_str());
                    ImGui::Text("Game: %s", TriggersValueForRebind(&rebind, rebind.fromKey).c_str());
                    ImGui::TextUnformatted("Left-click: configure, Right-click: cycle state");
                    ImGui::EndTooltip();
                }

                ImGui::PopStyleColor(3);
                ImGui::PopID();
            }
        }

        ImGui::EndChild();

        if (!isCapturing &&
            g_rebindLayoutState.unicodeEditIndex == -1 &&
            g_rebindLayoutState.reopenContextPopupAfterCapture) {
            openRebindContextPopup = true;
            openRebindContextPopupAtStoredPos = true;
            g_rebindLayoutState.reopenContextPopupAfterCapture = false;
        }

        if (openRebindContextPopup) {
            ++g_rebindLayoutState.contextPopupOpenSequence;
            ImGui::OpenPopup("Rebind Config##layout");
        }

        if (g_rebindLayoutState.unicodeEditIndex != -1) {
            ImGui::OpenPopup("Custom Unicode##layout");
        }

        if (ImGui::BeginPopupModal("Custom Unicode##layout", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
            static char unicodeInputBuffer[64] = {};
            static char unicodeShiftInputBuffer[64] = {};
            if (ImGui::IsWindowAppearing()) {
                std::snprintf(unicodeInputBuffer, sizeof(unicodeInputBuffer), "%s", g_rebindLayoutState.unicodeEditText.c_str());
                std::snprintf(unicodeShiftInputBuffer,
                              sizeof(unicodeShiftInputBuffer),
                              "%s",
                              g_rebindLayoutState.unicodeShiftEditText.c_str());
            }

            ImGui::TextUnformatted("Enter Unicode characters or codepoints:");
            ImGui::TextDisabled("Examples: ø   U+00F8   0x00F8");
            ImGui::Separator();
            ImGui::TextUnformatted("Base:");
            ImGui::SetNextItemWidth(260.0f);
            if (ImGui::IsWindowAppearing()) {
                ImGui::SetKeyboardFocusHere();
            }
            ImGui::InputTextWithHint("##unicode_input_layout", "ø or U+00F8", unicodeInputBuffer, sizeof(unicodeInputBuffer));
            ImGui::TextUnformatted("Shift:");
            ImGui::SetNextItemWidth(260.0f);
            ImGui::InputTextWithHint("##unicode_shift_input_layout", "+ or U+002B", unicodeShiftInputBuffer, sizeof(unicodeShiftInputBuffer));
            ImGui::Spacing();

            if (AnimatedButton("Apply", ImVec2(120, 0))) {
                if (g_rebindLayoutState.unicodeEditIndex >= 0 &&
                    g_rebindLayoutState.unicodeEditIndex < static_cast<int>(config.keyRebinds.rebinds.size())) {
                    auto& rebind = config.keyRebinds.rebinds[static_cast<std::size_t>(g_rebindLayoutState.unicodeEditIndex)];
                    bool changed = false;
                    const std::string unicodeInput = TrimAsciiWhitespace(std::string(unicodeInputBuffer));
                    const std::string unicodeShiftInput = TrimAsciiWhitespace(std::string(unicodeShiftInputBuffer));
                    std::uint32_t codepoint = 0;
                    std::uint32_t shiftCodepoint = 0;
                    const bool baseValid = unicodeInput.empty() || TryParseUnicodeInputString(unicodeInput, codepoint);
                    const bool shiftValid = unicodeShiftInput.empty() || TryParseUnicodeInputString(unicodeShiftInput, shiftCodepoint);

                    if (baseValid && shiftValid) {
                        rebind.customOutputVK = 0;
                        rebind.customOutputUnicode = codepoint;
                        rebind.customOutputShiftUnicode = shiftCodepoint;
                        rebind.useCustomOutput =
                            rebind.customOutputUnicode != 0 ||
                            rebind.customOutputShiftUnicode != 0 ||
                            rebind.customOutputScanCode != 0;
                        changed = true;
                    }

                    if (changed) {
                        const int idx = g_rebindLayoutState.unicodeEditIndex;
                        if (IsNoOpRebindForKey(rebind, rebind.fromKey)) {
                            EraseRebindAdjustingLayoutState(config, idx);
                        }
                        AutoSaveConfig(config);
                    }
                }

                g_rebindLayoutState.unicodeEditIndex = -1;
                g_rebindLayoutState.unicodeEditText.clear();
                g_rebindLayoutState.unicodeShiftEditText.clear();
                unicodeInputBuffer[0] = '\0';
                unicodeShiftInputBuffer[0] = '\0';
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (AnimatedButton("Cancel", ImVec2(120, 0))) {
                g_rebindLayoutState.unicodeEditIndex = -1;
                g_rebindLayoutState.unicodeEditText.clear();
                g_rebindLayoutState.unicodeShiftEditText.clear();
                unicodeInputBuffer[0] = '\0';
                unicodeShiftInputBuffer[0] = '\0';
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        if (openRebindContextPopupAtStoredPos && g_rebindLayoutState.hasContextPopupPos) {
            ImGui::SetNextWindowPos(g_rebindLayoutState.contextPopupPos, ImGuiCond_Appearing);
        } else {
            ImGui::SetNextWindowPos(ImGui::GetMousePos(), ImGuiCond_Appearing);
        }
        if (ImGui::BeginPopup("Rebind Config##layout")) {
            constexpr ImGuiID kContextPopupAnimChannelId = static_cast<ImGuiID>(0xD54219B1u);
            const ImGuiID contextPopupAnimId = ImHashData(&g_rebindLayoutState.contextPopupOpenSequence,
                                                          sizeof(g_rebindLayoutState.contextPopupOpenSequence),
                                                          static_cast<ImGuiID>(0xD54219B0u));
            const float contextPopupProgress = iam_tween_float(contextPopupAnimId,
                                                               kContextPopupAnimChannelId,
                                                               1.0f,
                                                               0.11f,
                                                               iam_ease_preset(iam_ease_out_cubic),
                                                               iam_policy_crossfade,
                                                               GetOverlayAnimationDeltaTime());
            const float contextPopupYOffset = (1.0f - contextPopupProgress) * 6.0f;
            if (contextPopupYOffset > 0.001f) {
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + contextPopupYOffset);
                SubmitCursorBoundaryItem();
            }
            const float contextPopupAlphaScale = std::clamp(0.25f + (0.75f * contextPopupProgress), 0.0f, 1.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * contextPopupAlphaScale);

            g_rebindLayoutState.contextPopupPos = ImGui::GetWindowPos();
            g_rebindLayoutState.hasContextPopupPos = true;
            int idx = g_rebindLayoutState.contextPreferredIndex;
            if (idx < 0 || idx >= static_cast<int>(config.keyRebinds.rebinds.size()) ||
                config.keyRebinds.rebinds[static_cast<std::size_t>(idx)].fromKey != g_rebindLayoutState.contextVk) {
                idx = FindBestRebindIndexForKey(config, g_rebindLayoutState.contextVk);
                g_rebindLayoutState.contextPreferredIndex = idx;
            }

            auto getRebindPtr = [&](int index) -> platform::config::KeyRebind* {
                if (index < 0 || index >= static_cast<int>(config.keyRebinds.rebinds.size())) {
                    return nullptr;
                }
                return &config.keyRebinds.rebinds[static_cast<std::size_t>(index)];
            };

            platform::config::KeyRebind* rebind = getRebindPtr(idx);
            const std::string typesValue = TypesValueForRebind(rebind, g_rebindLayoutState.contextVk);
            const std::string triggersValue = TriggersValueForRebind(rebind, g_rebindLayoutState.contextVk);
            const bool contextIsCustom = !isPresetLayoutVk(g_rebindLayoutState.contextVk);

            ImGui::Text("Source: %s", FormatSingleVk(g_rebindLayoutState.contextVk).c_str());

            if (contextIsCustom && rebind != nullptr) {
                char customNameBuffer[96] = {};
                std::snprintf(customNameBuffer, sizeof(customNameBuffer), "%s", rebind->name.c_str());
                ImGui::SetNextItemWidth(220.0f);
                if (ImGui::InputTextWithHint("##custom_popup_name", "Custom name", customNameBuffer, sizeof(customNameBuffer))) {
                    rebind->name = TrimAsciiWhitespace(std::string(customNameBuffer));
                }
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    AutoSaveConfig(config);
                }

                ImGui::SameLine();
                const bool sourceCapturing = isCapturing &&
                                             captureTarget == platform::config::CaptureTarget::RebindFrom &&
                                             captureTargetIndex == idx;
                const std::string sourceCaptureLabel = sourceCapturing ? std::string("[Press key...]")
                                                                       : FormatSingleVk(rebind->fromKey);
                if (AnimatedButton((sourceCaptureLabel + "##custom_popup_source").c_str(), ImVec2(170, 0))) {
                    g_rebindLayoutState.reopenContextPopupAfterCapture = true;
                    g_rebindLayoutState.customSourceCaptureIndex = -1;
                    g_rebindLayoutState.customSourceCaptureSequence = 0;
                    platform::config::StartRebindFromCapture(idx);
                }
            }

            if (AnimatedCollapsingHeader("State & Repeat##layout_section_state", ImGuiTreeNodeFlags_DefaultOpen)) {
                HeaderRevealScope headerRevealScope = BeginAnimatedHeaderContentReveal();
                ImGui::Indent();

                RebindInputState popupState = rebind ? GetRebindInputState(*rebind) : RebindInputState::EnabledRemap;
                const bool popupHasCustomRemap = rebind ? !IsNoOpRebindForKey(*rebind, g_rebindLayoutState.contextVk) : false;
                const char* popupStateLabel = RebindInputStateLabel(popupState);
                if (ImGui::BeginCombo("State##layout_rebind_state", popupStateLabel)) {
                    const RebindInputState options[] = { RebindInputState::EnabledRemap, RebindInputState::BlockInput,
                                                         RebindInputState::DisabledPassThrough };
                    for (RebindInputState option : options) {
                        if (option == RebindInputState::DisabledPassThrough && !popupHasCustomRemap &&
                            popupState != RebindInputState::DisabledPassThrough) {
                            continue;
                        }
                        const bool selected = option == popupState;
                        if (ImGui::Selectable(RebindInputStateLabel(option), selected)) {
                            idx = EnsureRebindForKey(config, g_rebindLayoutState.contextVk);
                            g_rebindLayoutState.contextPreferredIndex = idx;
                            if (idx >= 0 && idx < static_cast<int>(config.keyRebinds.rebinds.size())) {
                                auto& edit = config.keyRebinds.rebinds[static_cast<std::size_t>(idx)];
                                if (option == RebindInputState::DisabledPassThrough &&
                                    IsNoOpRebindForKey(edit, g_rebindLayoutState.contextVk)) {
                                    if (popupState == RebindInputState::DisabledPassThrough) {
                                        EraseRebindAdjustingLayoutState(config, idx);
                                    }
                                    AutoSaveConfig(config);
                                    continue;
                                }
                                SetRebindInputState(edit, option);
                                if (option == RebindInputState::EnabledRemap &&
                                    IsNoOpRebindForKey(edit, g_rebindLayoutState.contextVk)) {
                                    EraseRebindAdjustingLayoutState(config, idx);
                                    AutoSaveConfig(config);
                                    continue;
                                }
                                AutoSaveConfig(config);
                            }
                        }
                        if (selected) {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }
                ImGui::SameLine();
                ImGui::TextDisabled("Right-click any key to cycle this state");
                ImGui::Separator();

                {
                    bool perRepeatDisabled = false;
                    int perStartDelay = 0;
                    int perRepeatDelay = 0;
                    bool hasExisting = idx >= 0 && idx < static_cast<int>(config.keyRebinds.rebinds.size());
                    if (hasExisting) {
                        const auto& edit = config.keyRebinds.rebinds[static_cast<std::size_t>(idx)];
                        perRepeatDisabled = edit.keyRepeatDisabled;
                        perStartDelay = edit.keyRepeatStartDelay;
                        perRepeatDelay = edit.keyRepeatDelay;
                    }

                    const bool repeatControlsLocked = g_rebindLayoutState.contextVk == platform::input::VK_LBUTTON ||
                                                      g_rebindLayoutState.contextVk == platform::input::VK_RBUTTON;
                    if (repeatControlsLocked) {
                        bool repeatAlwaysDisabled = true;
                        ImGui::BeginDisabled();
                        ImGui::Checkbox("Disable Repeat##layout_key_repeat_disable", &repeatAlwaysDisabled);
                        ImGui::SliderInt("Start Delay##layout_key_repeat_start",
                                         &perStartDelay,
                                         0,
                                         500,
                                         perStartDelay == 0 ? "Inherit" : "%d ms");
                        const std::string perRepeatDelayFormat = BuildRepeatDelaySliderFormat(perRepeatDelay, "Inherit");
                        ImGui::SliderInt("Repeat Delay##layout_key_repeat_delay",
                                         &perRepeatDelay,
                                         0,
                                         500,
                                         perRepeatDelayFormat.c_str());
                        ImGui::EndDisabled();
                        ImGui::TextDisabled("MB1/MB2 repeat is always disabled. Delay overrides are unavailable.");
                    } else {
                        const bool repeatDisabledChanged = ImGui::Checkbox("Disable Repeat##layout_key_repeat_disable", &perRepeatDisabled);
                        ImGui::BeginDisabled(perRepeatDisabled);
                        const bool startDelayChanged = ImGui::SliderInt("Start Delay##layout_key_repeat_start",
                                                                         &perStartDelay,
                                                                         0,
                                                                         500,
                                                                         perStartDelay == 0 ? "Inherit" : "%d ms");
                        const std::string perRepeatDelayFormat = BuildRepeatDelaySliderFormat(perRepeatDelay, "Inherit");
                        const bool repeatDelayChanged = ImGui::SliderInt("Repeat Delay##layout_key_repeat_delay",
                                                                          &perRepeatDelay,
                                                                          0,
                                                                          500,
                                                                          perRepeatDelayFormat.c_str());
                        const int effectiveRepeatDelayMs =
                            ResolveEffectivePerInputRepeatDelayMs(config, g_rebindLayoutState.contextVk, perRepeatDelay);
                        if (!perRepeatDisabled && ShouldWarnAboutHotkeySlotRepeatRate(effectiveRepeatDelayMs)) {
                            ImGui::SameLine();
                            RenderHotkeySlotRepeatRateWarningMarker();
                        }
                        ImGui::EndDisabled();

                        if (repeatDisabledChanged || startDelayChanged || repeatDelayChanged) {
                            const std::size_t previousSize = config.keyRebinds.rebinds.size();
                            if (!hasExisting) {
                                idx = EnsureRebindForKey(config, g_rebindLayoutState.contextVk);
                                g_rebindLayoutState.contextPreferredIndex = idx;
                                hasExisting = idx >= 0 && idx < static_cast<int>(config.keyRebinds.rebinds.size());
                            }

                            if (hasExisting) {
                                auto& edit = config.keyRebinds.rebinds[static_cast<std::size_t>(idx)];
                                if (repeatDisabledChanged) {
                                    edit.keyRepeatDisabled = perRepeatDisabled;
                                }
                                if (startDelayChanged) {
                                    edit.keyRepeatStartDelay = perStartDelay;
                                }
                                if (repeatDelayChanged) {
                                    edit.keyRepeatDelay = perRepeatDelay;
                                }
                                if (IsNoOpRebindForKey(edit, edit.fromKey)) {
                                    EraseRebindAdjustingLayoutState(config, idx);
                                }
                            }

                            AutoSaveConfig(config);
                            if (config.keyRebinds.rebinds.size() != previousSize &&
                                g_rebindLayoutState.contextPreferredIndex >= 0 &&
                                g_rebindLayoutState.contextPreferredIndex < static_cast<int>(config.keyRebinds.rebinds.size())) {
                                idx = g_rebindLayoutState.contextPreferredIndex;
                            }
                        }
                    }

                    if (platform::input::IsMouseVk(g_rebindLayoutState.contextVk) && !repeatControlsLocked) {
                        ImGui::TextDisabled("Mouse note: per-key repeat overrides apply even when global mouse repeat is off.");
                    }
                }

                ImGui::Unindent();
            }

            if (AnimatedCollapsingHeader("Output Mapping##layout_section_output", ImGuiTreeNodeFlags_DefaultOpen)) {
                HeaderRevealScope headerRevealScope = BeginAnimatedHeaderContentReveal();
                ImGui::Indent();
                ImGui::Spacing();

            ImGui::TextUnformatted("Types (Chat/Text):");
            ImGui::SameLine();
            {
                const bool typesCapturing = isCapturing &&
                                            captureTarget == platform::config::CaptureTarget::RebindTypes &&
                                            captureTargetIndex == idx;
                std::string label = typesCapturing ? "[Press key...]" : typesValue;
                if (AnimatedButton((label + "##types_bind").c_str(), ImVec2(160, 0))) {
                    const std::size_t previousSize = config.keyRebinds.rebinds.size();
                    idx = EnsureRebindForKey(config, g_rebindLayoutState.contextVk);
                    g_rebindLayoutState.contextPreferredIndex = idx;
                    if (config.keyRebinds.rebinds.size() != previousSize) {
                        AutoSaveConfig(config);
                    }
                    if (idx >= 0) {
                        g_rebindLayoutState.reopenContextPopupAfterCapture = true;
                        g_rebindLayoutState.customSourceCaptureIndex = -1;
                        g_rebindLayoutState.customSourceCaptureSequence = 0;
                        platform::config::StartRebindTypesCapture(idx);
                    }
                }
            }
            ImGui::SameLine();
            if (AnimatedButton("Custom##types_custom", ImVec2(110, 0))) {
                const std::size_t previousSize = config.keyRebinds.rebinds.size();
                idx = EnsureRebindForKey(config, g_rebindLayoutState.contextVk);
                g_rebindLayoutState.contextPreferredIndex = idx;
                if (config.keyRebinds.rebinds.size() != previousSize) {
                    AutoSaveConfig(config);
                }
                if (idx >= 0) {
                    g_rebindLayoutState.unicodeEditIndex = idx;
                    g_rebindLayoutState.reopenContextPopupAfterCapture = true;
                    g_rebindLayoutState.customSourceCaptureIndex = -1;
                    g_rebindLayoutState.customSourceCaptureSequence = 0;
                    const auto& selected = config.keyRebinds.rebinds[static_cast<std::size_t>(idx)];
                    g_rebindLayoutState.unicodeEditText =
                        selected.customOutputUnicode != 0 ? FormatCodepointUPlus(selected.customOutputUnicode) : std::string();
                    g_rebindLayoutState.unicodeShiftEditText =
                        selected.customOutputShiftUnicode != 0 ? FormatCodepointUPlus(selected.customOutputShiftUnicode) : std::string();
                }
            }

            ImGui::Spacing();
            ImGui::TextUnformatted("Triggers (Game):");
            ImGui::SameLine();
            {
                const bool triggerCapturing = isCapturing &&
                                              captureTarget == platform::config::CaptureTarget::RebindTo &&
                                              captureTargetIndex == idx;
                std::string label = triggerCapturing ? "[Press key...]" : triggersValue;
                if (AnimatedButton((label + "##triggers_bind").c_str(), ImVec2(160, 0))) {
                    const std::size_t previousSize = config.keyRebinds.rebinds.size();
                    idx = EnsureRebindForKey(config, g_rebindLayoutState.contextVk);
                    g_rebindLayoutState.contextPreferredIndex = idx;
                    if (config.keyRebinds.rebinds.size() != previousSize) {
                        AutoSaveConfig(config);
                    }
                    if (idx >= 0) {
                        g_rebindLayoutState.reopenContextPopupAfterCapture = true;
                        g_rebindLayoutState.customSourceCaptureIndex = -1;
                        g_rebindLayoutState.customSourceCaptureSequence = 0;
                        platform::config::StartRebindToCapture(idx);
                    }
                }
            }

            ImGui::SameLine();
            {
                idx = (idx >= 0) ? idx : FindBestRebindIndexForKey(config, g_rebindLayoutState.contextVk);
                platform::config::KeyRebind* selected = getRebindPtr(idx);

                uint32_t triggerVk = selected ? selected->toKey : g_rebindLayoutState.contextVk;
                if (triggerVk == 0) {
                    triggerVk = g_rebindLayoutState.contextVk;
                }

                const int selectedCustomScanCode = (selected && selected->useCustomOutput && selected->customOutputScanCode != 0)
                                                       ? static_cast<int>(selected->customOutputScanCode)
                                                       : 0;
                const int previewScanCode =
                    selectedCustomScanCode > 0 ? selectedCustomScanCode : GetDerivedX11ScanCodeForVk(triggerVk);

                std::map<int, uint32_t> scanOptions = BuildKnownScanOptions(triggerVk);
                auto addScanOption = [&](int scanCode, uint32_t vkHint) {
                    if (scanCode <= 0) {
                        return;
                    }

                    auto it = scanOptions.find(scanCode);
                    if (it == scanOptions.end()) {
                        scanOptions.emplace(scanCode, vkHint);
                    } else if (it->second == 0 && vkHint != 0) {
                        it->second = vkHint;
                    }
                };

                addScanOption(selectedCustomScanCode, triggerVk);
                for (const auto& rebindEntry : config.keyRebinds.rebinds) {
                    if (rebindEntry.customOutputScanCode != 0) {
                        const uint32_t hintVk = rebindEntry.toKey != 0 ? rebindEntry.toKey : triggerVk;
                        addScanOption(static_cast<int>(rebindEntry.customOutputScanCode), hintVk);
                    }
                }
                platform::config::BindingInputEvent latestBindingEvent;
                std::uint64_t latestSeq = platform::config::GetLatestBindingInputSequence();
                if (latestSeq != 0) {
                    std::uint64_t probeSequence = latestSeq - 1;
                    if (platform::config::ConsumeBindingInputEventSince(probeSequence, latestBindingEvent) &&
                        latestBindingEvent.nativeScanCode > 0) {
                        const uint32_t latestHintVk =
                            latestBindingEvent.vk != platform::input::VK_NONE ? latestBindingEvent.vk : triggerVk;
                        addScanOption(latestBindingEvent.nativeScanCode, latestHintVk);
                    }
                }

                const std::string preview = FormatScanDisplay(previewScanCode, triggerVk);
                ImGui::SetNextItemWidth(255.0f);
                if (ImGui::BeginCombo("##triggers_scan_combo", preview.c_str())) {
                    const bool defaultSelected = selectedCustomScanCode == 0;
                    if (ImGui::Selectable("Default (Derived from Trigger Key)", defaultSelected)) {
                        const std::size_t previousSize = config.keyRebinds.rebinds.size();
                        idx = EnsureRebindForKey(config, g_rebindLayoutState.contextVk);
                        g_rebindLayoutState.contextPreferredIndex = idx;
                        if (config.keyRebinds.rebinds.size() != previousSize) {
                            AutoSaveConfig(config);
                        }
                        if (idx >= 0) {
                            auto& edit = config.keyRebinds.rebinds[static_cast<std::size_t>(idx)];
                            edit.customOutputScanCode = 0;
                            if (edit.customOutputVK == 0 &&
                                edit.customOutputUnicode == 0 &&
                                edit.customOutputShiftUnicode == 0) {
                                edit.useCustomOutput = false;
                            }
                            if (IsNoOpRebindForKey(edit, edit.fromKey)) {
                                EraseRebindAdjustingLayoutState(config, idx);
                            }
                            AutoSaveConfig(config);
                        }
                    }
                    ImGui::Separator();

                    for (const auto& scanOption : scanOptions) {
                        const int scanCode = scanOption.first;
                        if (scanCode <= 0) {
                            continue;
                        }

                        const uint32_t displayVk = scanOption.second != 0 ? scanOption.second : triggerVk;
                        const std::string itemLabel =
                            FormatScanDisplay(scanCode, displayVk) + "##scan_" + std::to_string(scanCode);
                        const bool selectedItem = selectedCustomScanCode == scanCode;
                        if (ImGui::Selectable(itemLabel.c_str(), selectedItem)) {
                            const std::size_t previousSize = config.keyRebinds.rebinds.size();
                            idx = EnsureRebindForKey(config, g_rebindLayoutState.contextVk);
                            g_rebindLayoutState.contextPreferredIndex = idx;
                            if (config.keyRebinds.rebinds.size() != previousSize) {
                                AutoSaveConfig(config);
                            }
                            if (idx >= 0) {
                                auto& edit = config.keyRebinds.rebinds[static_cast<std::size_t>(idx)];
                                edit.useCustomOutput = true;
                                edit.customOutputScanCode = static_cast<uint32_t>(scanCode);
                                if (scanOption.second != 0 && platform::input::IsKeyboardVk(scanOption.second)) {
                                    edit.toKey = platform::input::NormalizeModifierVkFromConfig(scanOption.second, scanCode);
                                }
                                AutoSaveConfig(config);
                            }
                        }
                    }

                    ImGui::EndCombo();
                }
            }

            ImGui::Spacing();
            if (AnimatedButton("Reset##layout_reset", ImVec2(170, 0))) {
                if (idx >= 0 && idx < static_cast<int>(config.keyRebinds.rebinds.size())) {
                    auto& reset = config.keyRebinds.rebinds[static_cast<std::size_t>(idx)];
                    reset.toKey = reset.fromKey;
                    reset.customOutputVK = 0;
                    reset.customOutputUnicode = 0;
                    reset.customOutputShiftUnicode = 0;
                    reset.customOutputScanCode = 0;
                    reset.useCustomOutput = false;
                    reset.enabled = true;
                    reset.consumeSourceInput = false;
                    reset.keyRepeatDisabled = false;
                    reset.keyRepeatStartDelay = 0;
                    reset.keyRepeatDelay = 0;
                    if (IsNoOpRebindForKey(reset, reset.fromKey)) {
                        EraseRebindAdjustingLayoutState(config, idx);
                    }
                    AutoSaveConfig(config);
                }
            }

                ImGui::Unindent();
            }

            ImGui::PopStyleVar();
            ImGui::EndPopup();
        }

        ImGui::Spacing();
        ImGui::TextDisabled("Green = rebind enabled, red = no output, gray = rebind disabled. Hover inputs for details.");

        ImGui::PopStyleVar();
        ImGui::EndPopup();
    } else if (!ImGui::IsPopupOpen("Keyboard Layout")) {
        g_rebindLayoutState.keyboardLayoutCloseRequested = false;
    }
}
