// Unified window capture source editor.
// #include'd from tab_mirrors_editor.cpp — expects `config`, `mirror`, and
// `s_selectedMirrorIndex` to be in scope.

{
    const WindowCaptureBackend captureBackend = GetWindowCaptureBackend();
    const bool backendUsesPickerOnly = captureBackend == WindowCaptureBackend::Wayland;
    const char* pickerPopupId = captureBackend == WindowCaptureBackend::MacOS
                                    ? "##macos_window_picker"
                                    : "##linux_window_picker";

    auto invalidateSourceCache = [&]() {
        g_mirrorEditorState.windowSourceCacheKey.clear();
        g_mirrorEditorState.windowSourceCacheExpiresAt = 0.0;
    };
    auto invalidateTransientState = [&]() {
        InvalidateWindowCaptureTransientState();
        invalidateSourceCache();
    };

    auto refreshSourceUiState = [&]() {
        const bool pickerOpen = !backendUsesPickerOnly &&
                                ImGui::IsPopupOpen(pickerPopupId);
        std::string cacheKey = std::to_string(s_selectedMirrorIndex) + "|" +
                               std::to_string(static_cast<int>(mirror.source.type)) + "|" +
                               mirror.source.appId + "|" + mirror.source.windowTitle + "|";
        if (captureBackend != WindowCaptureBackend::MacOS) {
            cacheKey += mirror.source.selectionToken + "|";
        }
        cacheKey += std::to_string(static_cast<int>(mirror.source.titleMatchMode)) + "|" +
                    std::to_string(static_cast<int>(mirror.source.fallbackMode)) + "|" +
                    std::to_string(mirror.source.lastKnownWidth) + "x" +
                    std::to_string(mirror.source.lastKnownHeight);
        const double nowSeconds = ImGui::GetTime();
        const double refreshIntervalSeconds = pickerOpen ? 0.20 : 0.75;
        if (g_mirrorEditorState.windowSourceCacheKey != cacheKey ||
            nowSeconds >= g_mirrorEditorState.windowSourceCacheExpiresAt) {
            g_mirrorEditorState.windowSourceCacheKey = cacheKey;
            g_mirrorEditorState.windowSourceStatus = GetWindowCaptureStatus(mirror.source);
            if (backendUsesPickerOnly || pickerOpen || !mirror.source.appId.empty()) {
                g_mirrorEditorState.availableWindows = GetAvailableWindowsSnapshot();
            } else {
                g_mirrorEditorState.availableWindows.clear();
            }
            g_mirrorEditorState.windowSourceCacheExpiresAt = nowSeconds + refreshIntervalSeconds;
        }
    };
    refreshSourceUiState();

    const WindowCaptureStatus& status = g_mirrorEditorState.windowSourceStatus;
    const auto& availableWindows = g_mirrorEditorState.availableWindows;

    const std::string titlePatternBufferKey = std::to_string(s_selectedMirrorIndex) + "|" +
                                              mirror.source.appId + "|" +
                                              mirror.source.windowTitle;
    if (g_mirrorEditorState.titlePatternBufferKey != titlePatternBufferKey) {
        g_mirrorEditorState.titlePatternBufferKey = titlePatternBufferKey;
        std::strncpy(g_mirrorEditorState.titlePatternBuffer,
                     mirror.source.windowTitle.c_str(),
                     sizeof(g_mirrorEditorState.titlePatternBuffer) - 1);
        g_mirrorEditorState.titlePatternBuffer[sizeof(g_mirrorEditorState.titlePatternBuffer) - 1] = '\0';
    }

    auto updateWindowSelectionHints = [&](const AvailableWindow& window) {
        mirror.source.lastKnownWidth = window.width;
        mirror.source.lastKnownHeight = window.height;
    };

    auto syncCaptureSizeToWindow = [&](const AvailableWindow& window) {
        updateWindowSelectionHints(window);
        if (window.width > 0) {
            mirror.captureWidth = window.width;
        }
        if (window.height > 0) {
            mirror.captureHeight = window.height;
        }
    };

    const int selectedWindowIndex = FindBestMatchingWindowIndex(availableWindows,
                                                                mirror.source.appId,
                                                                mirror.source.windowTitle,
                                                                mirror.source.titleMatchMode,
                                                                mirror.source.fallbackMode,
                                                                0,
                                                                mirror.source.lastKnownWidth,
                                                                mirror.source.lastKnownHeight);
    const AvailableWindow* selectedWindow =
        selectedWindowIndex >= 0 ? &availableWindows[static_cast<std::size_t>(selectedWindowIndex)] : nullptr;
    if (!selectedWindow && backendUsesPickerOnly && !mirror.source.selectionToken.empty()) {
        auto tokenIt = std::find_if(availableWindows.begin(), availableWindows.end(), [&](const auto& window) {
            return window.selectionToken == mirror.source.selectionToken;
        });
        if (tokenIt != availableWindows.end()) {
            selectedWindow = &(*tokenIt);
        }
    }

    // Title match / fallback / pattern controls (not shown for Wayland portal-driven selection)
    bool sourceSelectionChanged = false;
    if (!backendUsesPickerOnly) {
        const char* titleMatchModes[] = {
            "Exact",
            "Starts with",
            "Ends with",
            "Contains",
            "Disabled",
        };
        int currentTitleMatchMode = static_cast<int>(mirror.source.titleMatchMode);
        if (ImGui::Combo("Title Match Mode",
                         &currentTitleMatchMode,
                         titleMatchModes,
                         IM_ARRAYSIZE(titleMatchModes))) {
            mirror.source.titleMatchMode =
                static_cast<platform::config::MirrorSourceTitleMatchMode>(currentTitleMatchMode);
            invalidateTransientState();
            sourceSelectionChanged = true;
            AutoSaveConfig(config);
        }

        const char* fallbackModes[] = {
            "None",
            "Same app",
        };
        int currentFallbackMode = static_cast<int>(mirror.source.fallbackMode);
        if (ImGui::Combo("Fallback",
                         &currentFallbackMode,
                         fallbackModes,
                         IM_ARRAYSIZE(fallbackModes))) {
            mirror.source.fallbackMode =
                static_cast<platform::config::MirrorSourceFallbackMode>(currentFallbackMode);
            invalidateTransientState();
            sourceSelectionChanged = true;
            AutoSaveConfig(config);
        }

        if (ImGui::InputText("Title Pattern",
                             g_mirrorEditorState.titlePatternBuffer,
                             sizeof(g_mirrorEditorState.titlePatternBuffer))) {
            mirror.source.windowTitle = g_mirrorEditorState.titlePatternBuffer;
            g_mirrorEditorState.titlePatternBufferKey = std::to_string(s_selectedMirrorIndex) + "|" +
                                                         mirror.source.appId + "|" +
                                                         mirror.source.windowTitle;
            invalidateTransientState();
            sourceSelectionChanged = true;
            AutoSaveConfig(config);
        }
    }

    if (sourceSelectionChanged) {
        refreshSourceUiState();
    }

    // App / match display
    if (!backendUsesPickerOnly) {
        ImGui::TextWrapped("App: %s",
                           mirror.source.appId.empty() ? "[No app selected]" : mirror.source.appId.c_str());
        if (selectedWindow) {
            ImGui::TextWrapped("Match: %s", selectedWindow->windowTitle.c_str());
        } else if (!mirror.source.windowTitle.empty()) {
            ImGui::TextWrapped("Match: %s", mirror.source.windowTitle.c_str());
        } else {
            ImGui::TextDisabled("Match: unavailable");
        }
    }

    int liveWidth = 0;
    int liveHeight = 0;
    if (status.width > 0 && status.height > 0) {
        liveWidth = status.width;
        liveHeight = status.height;
        ImGui::Text("Current Size: %dx%d", liveWidth, liveHeight);
    } else if (selectedWindow && selectedWindow->width > 0 && selectedWindow->height > 0) {
        liveWidth = selectedWindow->width;
        liveHeight = selectedWindow->height;
        ImGui::Text("Current Size: %dx%d", liveWidth, liveHeight);
    } else {
        ImGui::TextDisabled("Current Size: unavailable");
    }
    if (ImGui::Checkbox("Use selected window size", &mirror.source.useWindowSize)) {
        if (mirror.source.useWindowSize && selectedWindow) {
            syncCaptureSizeToWindow(*selectedWindow);
        }
        AutoSaveConfig(config);
    }
    // Keep capture dimensions in sync with the live window size
    if (mirror.source.useWindowSize && liveWidth > 0 && liveHeight > 0) {
        if (mirror.captureWidth != liveWidth || mirror.captureHeight != liveHeight) {
            mirror.captureWidth = liveWidth;
            mirror.captureHeight = liveHeight;
            mirror.source.lastKnownWidth = liveWidth;
            mirror.source.lastKnownHeight = liveHeight;
            AutoSaveConfig(config);
        }
    }

    // Refresh / Pick / Clear buttons
    if (captureBackend == WindowCaptureBackend::MacOS ||
        captureBackend == WindowCaptureBackend::X11) {
        if (ImGui::Button("Refresh List")) {
            RefreshAvailableWindows();
            invalidateSourceCache();
        }
        ImGui::SameLine();

#ifdef __APPLE__
        const auto permissionState = GetWindowCaptureAccessState();
        if (permissionState != WindowCaptureAccessState::Granted) {
            ImGui::TextDisabled("Grant Screen Recording permission, then refresh.");
        } else {
#endif
            if (ImGui::Button("Pick Window")) {
                RefreshAvailableWindows();
                invalidateSourceCache();
                ImGui::OpenPopup(pickerPopupId);
            }
#ifdef __APPLE__
        }
#endif
    } else if (captureBackend == WindowCaptureBackend::Wayland) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.4f, 1.0f));
        ImGui::TextWrapped("Window capture on Wayland has some limitations:");
        ImGui::BulletText("Some compositors may not support the window chooser.");
        ImGui::BulletText("Windows must be re-picked after they are closed and reopened.");
        ImGui::BulletText("Limit window source mirrors to one per mode.");
        ImGui::BulletText("Prefer image source where possible.");
        ImGui::BulletText("For best results, run under X11.");
        ImGui::PopStyleColor();
        ImGui::Spacing();

        bool selectionTaskInFlight = false;
        {
            std::lock_guard<std::mutex> lock(g_mirrorEditorState.waylandSelectionMutex);
            selectionTaskInFlight = g_mirrorEditorState.waylandSelectionTask.inFlight &&
                                    g_mirrorEditorState.waylandSelectionTask.mirrorName == mirror.name;
        }
        const bool canOpenPortalPicker = status.state != WindowCaptureState::Unsupported &&
                                         status.access != WindowCaptureAccessState::Unsupported;
        ImGui::BeginDisabled(!canOpenPortalPicker || selectionTaskInFlight);
        const char* pickerLabel = selectionTaskInFlight
            ? "Picking Window..."
            : (mirror.source.selectionToken.empty() ? "Pick Window" : "Re-pick Window");
        if (ImGui::Button(pickerLabel)) {
            const bool forceRepick = !mirror.source.selectionToken.empty();
            {
                std::lock_guard<std::mutex> lock(g_mirrorEditorState.waylandSelectionMutex);
                g_mirrorEditorState.waylandSelectionTask.inFlight = true;
                g_mirrorEditorState.waylandSelectionTask.completed = false;
                g_mirrorEditorState.waylandSelectionTask.mirrorName = mirror.name;
                g_mirrorEditorState.waylandSelectionTask.source = mirror.source;
                g_mirrorEditorState.waylandSelectionTask.result = WindowCaptureSelectionResult::Unsupported;
                g_mirrorEditorState.waylandSelectionTask.message.clear();
            }
            const std::string mirrorName = mirror.name;
            platform::config::MirrorSourceConfig sourceSnapshot = mirror.source;
            const int mirrorFps = mirror.fps;
            std::thread([mirrorName, sourceSnapshot, mirrorFps, forceRepick]() mutable {
                std::string selectionMessage;
                const WindowCaptureSelectionResult result =
                    RequestWindowCaptureSelection(sourceSnapshot,
                                                 mirrorFps,
                                                 forceRepick,
                                                 &selectionMessage);
                std::lock_guard<std::mutex> lock(g_mirrorEditorState.waylandSelectionMutex);
                g_mirrorEditorState.waylandSelectionTask.inFlight = false;
                g_mirrorEditorState.waylandSelectionTask.completed = true;
                g_mirrorEditorState.waylandSelectionTask.mirrorName = mirrorName;
                g_mirrorEditorState.waylandSelectionTask.source = sourceSnapshot;
                g_mirrorEditorState.waylandSelectionTask.result = result;
                g_mirrorEditorState.waylandSelectionTask.message = selectionMessage;
            }).detach();
            invalidateSourceCache();
        }
        ImGui::EndDisabled();
    } else {
        ImGui::TextDisabled("No window capture backend is active for this session.");
    }

    ImGui::SameLine();
    if (ImGui::Button("Clear")) {
        const platform::config::MirrorSourceConfig previousSource = mirror.source;
        mirror.source.appId.clear();
        mirror.source.windowTitle.clear();
        mirror.source.selectionToken.clear();
        mirror.source.lastKnownWidth = 0;
        mirror.source.lastKnownHeight = 0;
        g_mirrorEditorState.titlePatternBuffer[0] = '\0';
        g_mirrorEditorState.titlePatternBufferKey.clear();
        if (HasConfiguredWindowCaptureSource(previousSource)) {
            ForgetWindowCaptureSource(previousSource);
        }
        invalidateTransientState();
        AutoSaveConfig(config);
    }

    // Window picker popup (macOS and X11 only)
    if (!backendUsesPickerOnly &&
        ImGui::BeginPopupModal(pickerPopupId, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const char* pickerTitle = captureBackend == WindowCaptureBackend::MacOS
                                      ? "Choose a capturable macOS window"
                                      : "Choose a capturable X11 window";
        ImGui::TextUnformatted(pickerTitle);
        ImGui::Separator();

        if (ImGui::Button("Refresh##picker")) {
            RefreshAvailableWindows();
            invalidateSourceCache();
        }

        if (availableWindows.empty()) {
            ImGui::TextDisabled("No shareable windows are currently available.");
        } else if (ImGui::BeginTable("window_picker_table",
                                     4,
                                     ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp,
                                     ImVec2(720.0f, 320.0f))) {
            ImGui::TableSetupColumn("App");
            ImGui::TableSetupColumn("Window");
            ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 90.0f);
            ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 90.0f);
            ImGui::TableHeadersRow();

            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(availableWindows.size()));
            while (clipper.Step()) {
                for (int rowIndex = clipper.DisplayStart; rowIndex < clipper.DisplayEnd; ++rowIndex) {
                    const auto& window = availableWindows[static_cast<std::size_t>(rowIndex)];
                    ImGui::PushID(static_cast<int>(window.windowId));
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    const bool selected = (mirror.source.appId == window.appId &&
                                           mirror.source.windowTitle == window.windowTitle);
                    if (ImGui::Selectable(window.appName.empty() ? window.appId.c_str() : window.appName.c_str(),
                                          selected,
                                          ImGuiSelectableFlags_SpanAllColumns)) {
                        mirror.source.appId = window.appId;
                        mirror.source.windowTitle = window.windowTitle;
                        mirror.source.selectionToken.clear();
                        std::strncpy(g_mirrorEditorState.titlePatternBuffer,
                                     window.windowTitle.c_str(),
                                     sizeof(g_mirrorEditorState.titlePatternBuffer) - 1);
                        g_mirrorEditorState.titlePatternBuffer[sizeof(g_mirrorEditorState.titlePatternBuffer) - 1] = '\0';
                        g_mirrorEditorState.titlePatternBufferKey = std::to_string(s_selectedMirrorIndex) + "|" +
                                                                         mirror.source.appId + "|" +
                                                                         mirror.source.windowTitle;
                        updateWindowSelectionHints(window);
                        if (mirror.source.useWindowSize) {
                            syncCaptureSizeToWindow(window);
                        }
                        invalidateTransientState();
                        AutoSaveConfig(config);
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(window.windowTitle.c_str());
                    ImGui::TableSetColumnIndex(2);
                    ImGui::Text("%dx%d", window.width, window.height);
                    ImGui::TableSetColumnIndex(3);
                    ImGui::TextUnformatted(window.onScreen ? "On Screen" : "Hidden");
                    ImGui::PopID();
                }
            }

            ImGui::EndTable();
        }

        if (AnimatedButton("Close")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}
