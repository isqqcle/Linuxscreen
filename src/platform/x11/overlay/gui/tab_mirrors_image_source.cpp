// Unified image source editor.
// #include'd from tab_mirrors_editor.cpp — expects `config`, `mirror`, and
// `s_selectedMirrorIndex` to be in scope.

{
    const bool isCalcOverlaySource = IsCalcOverlaySource(mirror.source);
    if (isCalcOverlaySource) {
        const std::string calcOverlayImagePath = GetCalcOverlayImagePath();
        if (mirror.source.image != calcOverlayImagePath) {
            mirror.source.image = calcOverlayImagePath;
            mirror.source.lastKnownWidth = 0;
            mirror.source.lastKnownHeight = 0;
            ForgetMirrorImageSource(mirror.name);
            AutoSaveConfig(config);
        }
    }

    g_mirrorEditorState.imageSourceStatus = GetMirrorImageSourceStatus(mirror.name, mirror.source);
    const MirrorImageSourceStatus& status = g_mirrorEditorState.imageSourceStatus;

    char imagePathBuffer[1024];
    std::snprintf(imagePathBuffer, sizeof(imagePathBuffer), "%s", mirror.source.image.c_str());
    ImGui::BeginDisabled(isCalcOverlaySource);
    if (ImGui::InputText("Image Path", imagePathBuffer, sizeof(imagePathBuffer))) {
        mirror.source.image = imagePathBuffer;
        mirror.source.lastKnownWidth = 0;
        mirror.source.lastKnownHeight = 0;
        ForgetMirrorImageSource(mirror.name);
        AutoSaveConfig(config);
    }
    ImGui::EndDisabled();
    if (isCalcOverlaySource) {
        ImGui::TextDisabled("Managed automatically by Calc Overlay.");
    } else {
        ImGui::SameLine();
        if (AnimatedButton("Browse##mirror_image_source_browse")) {
            IGFD::FileDialogConfig dialogConfig;
            dialogConfig.path = platform::config::GetConfigDirectoryPath();
            IGFD::FileDialog::Instance()->OpenDialog("mirror_image_source_picker",
                                                     "Select Mirror Image",
                                                     "Image Files{.png,.jpg,.jpeg,.bmp,.gif},.*",
                                                     dialogConfig);
            g_mirrorEditorState.imageSourcePickerMirrorIndex = s_selectedMirrorIndex;
            g_mirrorEditorState.imageSourcePickerOpen = true;
        }
    }

    if (!status.message.empty()) {
        const ImVec4 color = status.decodeFailed
            ? ImVec4(1.0f, 0.5f, 0.5f, 1.0f)
            : ImVec4(0.7f, 0.7f, 0.7f, 1.0f);
        ImGui::TextColored(color, "%s", status.message.c_str());
    }

    if (status.width > 0 && status.height > 0 &&
        (mirror.source.lastKnownWidth != status.width || mirror.source.lastKnownHeight != status.height)) {
        mirror.source.lastKnownWidth = status.width;
        mirror.source.lastKnownHeight = status.height;
        if (mirror.source.useImageSize) {
            mirror.captureWidth = status.width;
            mirror.captureHeight = status.height;
        }
        AutoSaveConfig(config);
    }

    const bool hasKnownSize = status.width > 0 && status.height > 0;
    if (hasKnownSize) {
        ImGui::Text("Current Size: %dx%d", status.width, status.height);
    } else {
        ImGui::TextDisabled("Current Size: unavailable");
    }
    if (ImGui::Checkbox("Use image size", &mirror.source.useImageSize)) {
        if (mirror.source.useImageSize && hasKnownSize) {
            mirror.captureWidth = status.width;
            mirror.captureHeight = status.height;
        }
        AutoSaveConfig(config);
    }

    int imageReloadPollMs = mirror.source.imageReloadPollMs;
    if (ImGui::InputInt("Reload poll (ms)", &imageReloadPollMs)) {
        imageReloadPollMs = std::clamp(imageReloadPollMs, 1, 10000);
        if (mirror.source.imageReloadPollMs != imageReloadPollMs) {
            mirror.source.imageReloadPollMs = imageReloadPollMs;
            AutoSaveConfig(config);
        }
    }
    if (mirror.source.imageReloadPollMs < 50) {
        ImGui::SameLine();
        RenderImageReloadPollWarningMarker();
    }

    if (mirror.source.useImageSize && hasKnownSize &&
        (mirror.captureWidth != status.width || mirror.captureHeight != status.height)) {
        mirror.captureWidth = status.width;
        mirror.captureHeight = status.height;
        AutoSaveConfig(config);
    }

    if (!isCalcOverlaySource && ImGui::Button("Clear##mirror_image_source_clear")) {
        mirror.source.image.clear();
        mirror.source.useImageSize = false;
        mirror.source.imageReloadPollMs = platform::config::MirrorSourceConfig::kDefaultImageReloadPollMs;
        mirror.source.lastKnownWidth = 0;
        mirror.source.lastKnownHeight = 0;
        ForgetMirrorImageSource(mirror.name);
        AutoSaveConfig(config);
    }
}
