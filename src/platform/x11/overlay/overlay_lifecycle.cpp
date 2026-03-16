void ShutdownImGuiOverlayLocked(bool processExit) {
    if (!processExit) {
        FlushPendingConfigSave(true);
    }

    if (!g_imguiOverlayState.context) {
        g_imguiOverlayState = ImGuiOverlayState{};
        std::lock_guard<std::mutex> captureLock(g_imguiOverlayCaptureMutex);
        ResetOverlayCaptureStateLocked();
        return;
    }

    ImGuiContext* previousContext = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(g_imguiOverlayState.context);

#ifdef __APPLE__
    const void* currentGlContext = CGLGetCurrentContext();
#else
    const void* currentGlContext = reinterpret_cast<void*>(glXGetCurrentContext());
#endif
    const bool canShutdownOpenGlBackend = !processExit && currentGlContext != nullptr;

    if (g_imguiOverlayState.openglBackendInitialized && canShutdownOpenGlBackend) {
        ImGui_ImplOpenGL3_Shutdown();
    } else if (g_imguiOverlayState.openglBackendInitialized) {
        ImGuiIO& io = ImGui::GetIO();
        io.BackendRendererUserData = nullptr;
        io.BackendRendererName = nullptr;
        io.BackendFlags &= ~(ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures);
    }
    g_imguiOverlayState.openglBackendInitialized = false;

    if (!processExit) {
        ShutdownX11Clipboard();
    }

    ImGuiContext* destroyedContext = g_imguiOverlayState.context;
    ImGui::DestroyContext(destroyedContext);
    g_imguiOverlayState = ImGuiOverlayState{};
    g_overlayUiAnimationState = OverlayUiAnimationState{};
    g_buttonRippleStates.clear();
    g_headerRippleStates.clear();
    g_rebindKeyCyclePulseStates.clear();
    g_headerContentRevealStates.clear();
    g_pendingHeaderRevealProgress = 1.0f;
    g_pendingHeaderRevealValid = false;
    g_pendingHeaderRevealClosing = false;
    g_pendingHeaderRevealId = 0;
    g_rebindLayoutState.keyboardLayoutCloseRequested = false;
    g_rebindLayoutState.keyboardLayoutOpenSequence = 0;
    g_rebindLayoutState.contextPopupOpenSequence = 0;

    if (!processExit) {
        iam_clip_shutdown();
    }

    {
        std::lock_guard<std::mutex> captureLock(g_imguiOverlayCaptureMutex);
        ResetOverlayCaptureStateLocked();
    }

    ImGui::SetCurrentContext(previousContext == destroyedContext ? nullptr : previousContext);
}

bool EnsureImGuiOverlayInitializedLocked(void* glContext) {
    if (g_imguiOverlayState.context && g_imguiOverlayState.glContext == glContext) { return true; }

    if (g_imguiOverlayState.context) { ShutdownImGuiOverlayLocked(false); }

    if (!glContext) { return false; }

    ImGuiContext* previousContext = ImGui::GetCurrentContext();
    ImGuiContext* context = ImGui::CreateContext();
    if (!context) {
        ImGui::SetCurrentContext(previousContext);
        return false;
    }

    ImGui::SetCurrentContext(context);
        auto config = platform::config::GetConfigSnapshot();
    if (config) {
        ApplyThemePreset(config->guiTheme);
        if (!config->guiCustomColors.empty()) {
            ApplyCustomColorsOverlay(config->guiCustomColors);
        }
        ImGui::GetStyle().Alpha = config->guiOpacity;
    } else {
        ImGui::StyleColorsDark();
    }

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    io.BackendPlatformName = "linuxscreen_x11_input_queue";

    ImGuiPlatformIO& platformIo = ImGui::GetPlatformIO();
    platformIo.Platform_GetClipboardTextFn = X11GetClipboardText;

    ImFontConfig fontConfig;
    fontConfig.OversampleH = 1;
    fontConfig.OversampleV = 1;
    fontConfig.PixelSnapH = true;

    io.Fonts->Clear();
    
    bool fontLoaded = false;
    if (config && !config->guiFontPath.empty()) {
        std::string fontPath = config->guiFontPath;
        // If it's a font name from scanner (no slashes), resolve to absolute path
        if (fontPath.find('/') == std::string::npos) {
            if (!g_fontsScanned) {
                g_discoveredFonts = platform::common::ScanForFonts();
                g_fontsScanned = true;
            }
            auto it = g_discoveredFonts.find(fontPath);
            if (it != g_discoveredFonts.end()) {
                fontPath = it->second;
            }
        }

        if (FILE* f = fopen(fontPath.c_str(), "r")) {
            fclose(f);
            if (io.Fonts->AddFontFromFileTTF(fontPath.c_str(), (float)config->guiFontSize, &fontConfig)) {
                fontLoaded = true;
            }
        }
    }
    
    if (!fontLoaded) {
        io.Fonts->AddFontDefault(&fontConfig);
    }

#ifdef __APPLE__
    if (!ImGui_ImplOpenGL3_Init("#version 120")) {
#else
    if (!ImGui_ImplOpenGL3_Init(nullptr)) {
#endif
        ImGui::DestroyContext(context);
        ImGui::SetCurrentContext(previousContext);
        return false;
    }

    g_imguiOverlayState.context = context;
    g_imguiOverlayState.glContext = glContext;
    g_imguiOverlayState.openglBackendInitialized = true;
    g_imguiOverlayState.hasLastFrameTime = false;
    g_imguiOverlayState.frameCount = 0;

    ImGui::SetCurrentContext(previousContext);
    return true;
}

bool AreImGuiTextureHandlesValidLocked() {
    ImGuiPlatformIO& platformIo = ImGui::GetPlatformIO();
    for (int i = 0; i < platformIo.Textures.Size; ++i) {
        ImTextureData* texture = platformIo.Textures[i];
        if (!texture) { continue; }
        if (texture->Status != ImTextureStatus_OK) { continue; }

        const ImTextureID textureId = texture->GetTexID();
        if (textureId == ImTextureID_Invalid) { continue; }

        const GLuint glTextureId = static_cast<GLuint>(textureId);
        if (glTextureId == 0) { continue; }
        if (glIsTexture(glTextureId) == GL_FALSE) { return false; }
    }
    return true;
}

bool EnsureImGuiDeviceObjectsHealthyLocked() {
    if (!g_imguiOverlayState.openglBackendInitialized) { return false; }
    if (AreImGuiTextureHandlesValidLocked()) { return true; }

    while (glGetError() != GL_NO_ERROR) {}

    ImGui_ImplOpenGL3_DestroyDeviceObjects();
    
    ImGuiIO& io = ImGui::GetIO();
    if (io.Fonts) {
        io.Fonts->TexIsBuilt = false;
    }
    
    if (!ImGui_ImplOpenGL3_CreateDeviceObjects()) {
        g_imguiOverlayState.openglBackendInitialized = false;
        return false;
    }
    
    while (glGetError() != GL_NO_ERROR) {}
    
    return true;
}

ImGuiInputDrainResult DrainInputQueueToCurrentContext() {
    constexpr std::size_t kDrainBatchSize = 512;
    constexpr std::size_t kDrainPassLimit = 8;

    ImGuiInputDrainResult total{};
    for (std::size_t pass = 0; pass < kDrainPassLimit; ++pass) {
        const ImGuiInputDrainResult result = DrainImGuiInputEventsToCurrentContext(kDrainBatchSize);
        total.drained += result.drained;
        total.applied += result.applied;
        total.hasImGuiSupport = total.hasImGuiSupport || result.hasImGuiSupport;
        total.hadCurrentContext = total.hadCurrentContext || result.hadCurrentContext;
        if (result.drained < kDrainBatchSize) { break; }
    }

    return total;
}

} // namespace
