bool IsGlxMirrorPipelineEnabledInternal() {
    return true;
}

void SubmitGlxMirrorCaptureInternal(int width, int height) {
    if (!IsGlxMirrorPipelineEnabledInternal()) { return; }
    if (width <= 0 || height <= 0) { return; }

    std::call_once(g_workerStartOnce, []() {
        if (EnsureGlFunctions() && AreSharedGlxContextsReady()) {
            StartMirrorWorker();
        }
    });

    if (!g_workerStarted.load(std::memory_order_acquire)) {
        return;
    }

    if (!EnsureGlFunctions()) { return; }

    if (!g_configsLoaded) {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        if (!g_configsLoaded) {
            std::string activeMode = g_modeState.GetActiveModeName();
            if (!activeMode.empty()) {
                auto config = platform::config::GetConfigSnapshot();
                if (config) {
                    g_modeState.ApplyModeSwitch(activeMode, *config, width, height);
                }
            }

            // Get mirrors from the current mode state (which is initialized from config)
            g_mirrorConfigs = g_modeState.GetActiveMirrorRenderList();
            g_configsLoaded = true;
            g_currentActiveMode = g_modeState.GetActiveModeName();
            if (IsDebugEnabled()) {
                fprintf(stderr, "[Linuxscreen][mirror] Loaded %zu mirror config(s)\n", g_mirrorConfigs.size());
                for (const auto& c : g_mirrorConfigs) {
                    fprintf(stderr, "[Linuxscreen][mirror]   '%s' capture=%dx%d output=(%d,%d,%.1f) colors=%zu sensitivity=%.3f\n",
                            c.config.name.c_str(), c.config.captureWidth, c.config.captureHeight,
                            c.config.output.x, c.config.output.y, c.config.output.scale,
                            c.config.colors.targetColors.size(), c.config.colorSensitivity);
                }
            }
        }
    }

    // Check for mode switches outside of initial load
    if (g_configsLoaded) {
        std::string activeMode = g_modeState.GetActiveModeName();
        if (activeMode != g_currentActiveMode) {
            std::lock_guard<std::mutex> lock(g_stateMutex);
            g_currentActiveMode = activeMode;
            g_mirrorConfigs = g_modeState.GetActiveMirrorRenderList();
            if (IsDebugEnabled()) {
                fprintf(stderr, "[Linuxscreen][mirror] Mode switch detected in capture: '%s', %zu mirror(s)\n",
                        activeMode.empty() ? "<none>" : activeMode.c_str(), g_mirrorConfigs.size());
            }
        }
    }

    if (g_mirrorConfigs.empty() && g_currentActiveMode != "EyeZoom") { return; }

    const std::uint64_t generation = GetSharedGlxContextGeneration();
    if (generation != 0 && generation != g_lastGeneration.load(std::memory_order_acquire)) {
        PostFrameSlot(width,
                      height,
                      nullptr,
                      generation,
                      false,
                      OverscanDimensions{},
                      width,
                      height,
                      0,
                      0,
                      width,
                      height,
                      0,
                      0);
        g_lastGeneration.store(generation, std::memory_order_release);

        if (g_gameFrameTexture) {
            glDeleteTextures(1, &g_gameFrameTexture);
            g_gameFrameTexture = 0;
        }
        g_gameFrameW = 0;
        g_gameFrameH = 0;
        g_configsLoaded = false;

        if (IsDebugEnabled()) {
            fprintf(stderr, "[Linuxscreen][mirror] Swap thread: generation changed (%llu), posted sentinel\n",
                    static_cast<unsigned long long>(generation));
        }
        return;
    }

    const bool overscan = IsOverscanActiveInternal() && g_overscanFboRendered;
    OverscanDimensions overscanSnap = overscan ? g_overscanDims : OverscanDimensions{};
    const int captureW = overscan ? g_overscanDims.totalWidth : width;
    const int captureH = overscan ? g_overscanDims.totalHeight : height;

    GLint currentViewport[4] = { 0, 0, 0, 0 };
    glGetIntegerv(GL_VIEWPORT, currentViewport);

    int containerWidth = width;
    int containerHeight = height;
    auto handles = platform::x11::GetRuntimeHandles();
    if (handles.nativeDisplay && handles.nativeWindow) {
        unsigned int physicalW = 0;
        unsigned int physicalH = 0;
        glXQueryDrawable(reinterpret_cast<Display*>(handles.nativeDisplay),
                         handles.nativeWindow,
                         GLX_WIDTH,
                         &physicalW);
        glXQueryDrawable(reinterpret_cast<Display*>(handles.nativeDisplay),
                         handles.nativeWindow,
                         GLX_HEIGHT,
                         &physicalH);
        if (physicalW > 0 && physicalH > 0) {
            containerWidth = static_cast<int>(physicalW);
            containerHeight = static_cast<int>(physicalH);
        }
    }
    if (overscan && overscanSnap.windowWidth > 0 && overscanSnap.windowHeight > 0) {
        containerWidth = overscanSnap.windowWidth;
        containerHeight = overscanSnap.windowHeight;
    }
    if (containerWidth <= 0 || containerHeight <= 0) {
        (void)GetGameWindowSize(containerWidth, containerHeight);
    }
    if (containerWidth <= 0 || containerHeight <= 0) {
        containerWidth = width;
        containerHeight = height;
    }

    // Ensure game frame texture sized to capture dimensions
    EnsureGameFrameTexture(captureW, captureH);
    const int copyW = std::min(captureW, g_gameFrameW);
    const int copyH = std::min(captureH, g_gameFrameH);
    if (copyW != captureW || copyH != captureH) {
        fprintf(stderr, "[Linuxscreen][mirror] WARNING: Capture copy %dx%d exceeds game frame texture %dx%d, clamping to %dx%d\n",
                captureW, captureH, g_gameFrameW, g_gameFrameH, copyW, copyH);
    }
    if (copyW <= 0 || copyH <= 0) { return; }

    // Capture game frame on the game context
    GLint prevActiveUnit = 0;
    GLint prevReadFbo = 0;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &prevActiveUnit);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevReadFbo);
    // Switch to TEXTURE0 and save whatever the game had bound there, then restore it exactly.
    glActiveTexture(GL_TEXTURE0);
    GLint prevTex0Binding = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex0Binding);
    glBindTexture(GL_TEXTURE_2D, g_gameFrameTexture);

    // Read from overscan FBO if active, otherwise from default framebuffer (FBO 0)
    g_gl.bindFramebuffer(GL_READ_FRAMEBUFFER, overscan ? g_overscanFbo : 0);

    int copySrcX = 0;
    int copySrcY = 0;
    int copiedH = copyH;
    if (overscan) {
        glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, copyW, copyH);
    } else {
        // Capture from the current viewport origin so modes that intentionally offset
        // the game viewport (e.g. Thin/Preemptive layouts) still sample the correct
        // game region.
        const int viewportX = std::max(0, currentViewport[0]);
        const int viewportY = std::max(0, currentViewport[1]);
        int safeW = copyW;
        int safeH = copyH;
        if (containerWidth > 0 && containerHeight > 0) {
            safeW = std::min(safeW, std::max(0, containerWidth - viewportX));
            safeH = std::min(safeH, std::max(0, containerHeight - viewportY));
        }
        copySrcX = viewportX;
        copySrcY = viewportY;
        copiedH = safeH;
        if (safeW > 0 && safeH > 0) {
            glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, viewportX, viewportY, safeW, safeH);
        }
    }
    
    g_gl.bindFramebuffer(GL_READ_FRAMEBUFFER, prevReadFbo);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prevTex0Binding));
    glActiveTexture(prevActiveUnit);

    GLsync fence = nullptr;
    if (g_gl.fenceSync) {
        fence = g_gl.fenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    }

    int viewportBottomLeftX = currentViewport[0];
    int viewportBottomLeftY = currentViewport[1];
    if (overscan) {
        viewportBottomLeftX -= overscanSnap.marginLeft;
        viewportBottomLeftY -= overscanSnap.marginBottom;
    }
    const int viewportTopLeftX = viewportBottomLeftX;
    const int viewportTopLeftY = containerHeight - (viewportBottomLeftY + currentViewport[3]);

    int textureOriginTopLeftX = 0;
    int textureOriginTopLeftY = 0;
    if (overscan) {
        textureOriginTopLeftX = -overscanSnap.marginLeft;
        textureOriginTopLeftY = -overscanSnap.marginTop;
    } else {
        textureOriginTopLeftX = copySrcX;
        textureOriginTopLeftY = containerHeight - (copySrcY + copiedH);
    }

    PostFrameSlot(copyW,
                  copyH,
                  fence,
                  generation,
                  overscan,
                  overscanSnap,
                  containerWidth,
                  containerHeight,
                  viewportTopLeftX,
                  viewportTopLeftY,
                  currentViewport[2],
                  currentViewport[3],
                  textureOriginTopLeftX,
                  textureOriginTopLeftY);
}
