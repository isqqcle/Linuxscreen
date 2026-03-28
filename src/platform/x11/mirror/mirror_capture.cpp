bool IsGlxMirrorPipelineEnabledInternal() {
    return true;
}

bool IsPieAnchorInput(const std::string& relativeTo) {
    return relativeTo == "pieLeft" || relativeTo == "pieRight" ||
           relativeTo == "pieLeftViewport" || relativeTo == "pieRightViewport";
}

bool MirrorUsesPieAnchors(const ResolvedMirrorRender& mirrorRender) {
    for (const auto& input : mirrorRender.config.input) {
        if (input.enabled && IsPieAnchorInput(input.relativeTo)) {
            return true;
        }
    }
    return false;
}

bool ActiveModeViewportMatchesLiveViewport(int containerWidth,
                                           int containerHeight,
                                           int viewportTopLeftX,
                                           int viewportTopLeftY,
                                           int viewportWidth,
                                           int viewportHeight) {
    if (containerWidth <= 0 || containerHeight <= 0 || viewportWidth <= 0 || viewportHeight <= 0) {
        return true;
    }

    auto configSnapshot = GetMirrorModeState().GetConfigSnapshot();
    if (!configSnapshot) {
        return true;
    }

    const std::string activeModeName = GetMirrorModeState().GetActiveModeName();
    if (activeModeName.empty()) {
        return true;
    }

    const auto modeIt = std::find_if(configSnapshot->modes.begin(),
                                     configSnapshot->modes.end(),
                                     [&](const auto& mode) { return mode.name == activeModeName; });
    if (modeIt == configSnapshot->modes.end()) {
        return true;
    }
    const auto& activeMode = *modeIt;

    int expectedWidth = 0;
    int expectedHeight = 0;
    MirrorModeState::CalculateModeDimensions(activeMode,
                                             containerWidth,
                                             containerHeight,
                                             expectedWidth,
                                             expectedHeight);
    if (expectedWidth <= 0 || expectedHeight <= 0) {
        return true;
    }

    std::string anchorPreset = activeMode.positionPreset.empty() ? "topLeftScreen" : activeMode.positionPreset;
    if (anchorPreset == "custom") {
        anchorPreset = "topLeftScreen";
    }

    int expectedX = 0;
    int expectedY = 0;
    platform::config::GetRelativeCoords(anchorPreset,
                                        activeMode.x,
                                        activeMode.y,
                                        expectedWidth,
                                        expectedHeight,
                                        containerWidth,
                                        containerHeight,
                                        expectedX,
                                        expectedY);

    constexpr int kViewportTolerancePx = 1;
    return std::abs(viewportTopLeftX - expectedX) <= kViewportTolerancePx &&
           std::abs(viewportTopLeftY - expectedY) <= kViewportTolerancePx &&
           std::abs(viewportWidth - expectedWidth) <= kViewportTolerancePx &&
           std::abs(viewportHeight - expectedHeight) <= kViewportTolerancePx;
}

void RestorePublishedContentForPieMirrors() {
    for (const auto& mirrorRender : g_mirrorConfigs) {
        if (!MirrorUsesPieAnchors(mirrorRender)) {
            continue;
        }

        auto it = g_instances.find(mirrorRender.config.name);
        if (it == g_instances.end()) {
            continue;
        }

        X11MirrorInstance& inst = it->second;
        const int frontIdx = inst.frontIdx.load(std::memory_order_acquire);
        if ((frontIdx == 0 || frontIdx == 1) &&
            inst.finalTexture[frontIdx] != 0 &&
            inst.finalFbo[frontIdx] != 0) {
            inst.hasValidContent = true;
            inst.hasFrameContent = true;
        }
    }
}

void RefreshMirrorConfigsForActiveMode(int width,
                                       int height,
                                       const std::string& activeMode,
                                       const char* debugPrefix) {
    auto config = platform::config::GetConfigSnapshot();
    if (!activeMode.empty() && config) {
        ApplyModeSwitchWithResolvedContainer(activeMode, *config, width, height);
    }

    g_mirrorConfigs = GetMirrorModeState().GetActiveMirrorRenderList();
    ResetAllMirrorInstanceCaptureTimers();
    g_inlineRoundRobinIdx = 0;
    g_currentActiveMode = activeMode;
    if (debugPrefix && IsDebugEnabled()) {
        fprintf(stderr,
                "[Linuxscreen][mirror] %s '%s', %zu mirror(s)\n",
                debugPrefix,
                activeMode.empty() ? "<none>" : activeMode.c_str(),
                g_mirrorConfigs.size());
    }
}

void EnsureMirrorConfigsLoaded(int width, int height) {
    std::lock_guard<std::mutex> lock(g_stateMutex);
    if (g_configsLoaded) {
        return;
    }

    RefreshMirrorConfigsForActiveMode(width, height, GetMirrorModeState().GetActiveModeName(), nullptr);
    g_configsLoaded = true;
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

void SubmitGlxMirrorCaptureInternal(int width, int height) {
    if (!IsGlxMirrorPipelineEnabledInternal()) { return; }
    if (width <= 0 || height <= 0) { return; }

    const bool useInlineMirrorProcessing =
#ifdef __APPLE__
        true;
#else
        GetCurrentGlBackend() != CurrentGlBackend::Glx && HasCurrentGlContext();
#endif

    if (!useInlineMirrorProcessing) {
        std::call_once(g_workerStartOnce, []() {
            if (EnsureGlFunctions() && AreSharedGlxContextsReady()) {
                StartMirrorWorker();
            }
        });

        if (!g_workerStarted.load(std::memory_order_acquire)) {
            return;
        }
    }

    if (!EnsureGlFunctions()) { return; }

    if (!g_configsLoaded) {
        EnsureMirrorConfigsLoaded(width, height);
    }

    // Check for mode switches outside of initial load
    if (g_configsLoaded) {
        std::string activeMode = GetMirrorModeState().GetActiveModeName();
        if (activeMode != g_currentActiveMode) {
            std::lock_guard<std::mutex> lock(g_stateMutex);
            RefreshMirrorConfigsForActiveMode(width,
                                             height,
                                             activeMode,
                                             "Mode switch detected in capture:");
        }
    }

    if (g_mirrorConfigs.empty() && g_currentActiveMode != "EyeZoom") {
        return;
    }

    bool requiresGameFramebuffer = (g_currentActiveMode == "EyeZoom");
    if (!requiresGameFramebuffer) {
        for (const auto& mirror : g_mirrorConfigs) {
            if (!IsWindowCaptureSource(mirror.config.source) &&
                !IsImageSource(mirror.config.source)) {
                requiresGameFramebuffer = true;
                break;
            }
        }
    }
    const std::uint64_t generation = GetSharedGlxContextGeneration();
    if (generation != 0 && generation != g_lastGeneration.load(std::memory_order_acquire)) {
#ifdef __APPLE__
        DrainStaleFenceQueue();
        DestroyAllInstances();
        CleanupMirrorShaders();
        g_workerGeneration.store(generation, std::memory_order_release);
#else
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
#endif
        g_lastGeneration.store(generation, std::memory_order_release);

        if (g_gameFrameTexture) {
            glDeleteTextures(1, &g_gameFrameTexture);
            g_gameFrameTexture = 0;
        }
        g_gameFrameW = 0;
        g_gameFrameH = 0;
        g_inlineRoundRobinIdx = 0;
        g_configsLoaded = false;

        if (IsDebugEnabled()) {
#ifdef __APPLE__
            fprintf(stderr, "[Linuxscreen][mirror] Inline path generation changed (%llu), cleared mirror resources\n",
                    static_cast<unsigned long long>(generation));
#else
            fprintf(stderr, "[Linuxscreen][mirror] Swap thread: generation changed (%llu), posted sentinel\n",
                    static_cast<unsigned long long>(generation));
#endif
        }
        return;
    }

    // Throttle submissions to the fastest configured mirror FPS. The worker
    // thread does its own per-mirror FPS check, so submitting faster than the
    // fastest mirror just wastes CPU and GPU time (texture copy + glFinish on
    // the game thread, wake-up + GL state save/restore on the worker).
    if (g_currentActiveMode != "EyeZoom") {
        static std::chrono::steady_clock::time_point s_lastSubmitTime{};
        int maxFps = 0;
        bool hasUncappedMirror = false;
        for (const auto& mirror : g_mirrorConfigs) {
            if (mirror.config.fps <= 0) {
                hasUncappedMirror = true;
                break;
            }
            maxFps = std::max(maxFps, mirror.config.fps);
        }
        if (!hasUncappedMirror && maxFps > 0) {
            const auto submitNow = std::chrono::steady_clock::now();
            const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                submitNow - s_lastSubmitTime).count();
            if (elapsedMs < (1000 / maxFps)) {
                return;
            }
            s_lastSubmitTime = submitNow;
        }
    }

    const bool overscanActive = IsOverscanActiveInternal();
    if (overscanActive && !g_overscanFboRendered) {
        return;
    }
    const bool overscan = overscanActive;
    const bool macRedirect = IsMacMirrorRedirectActiveInternal() && IsMacMirrorRedirectRenderedInternal();
    OverscanDimensions overscanSnap = overscan ? g_overscanDims : OverscanDimensions{};

    GLint currentViewport[4] = { 0, 0, 0, 0 };
    glGetIntegerv(GL_VIEWPORT, currentViewport);

    int containerWidth = width;
    int containerHeight = height;
    ResolveMirrorConfigContainerSize(width, height, containerWidth, containerHeight);
    if (overscan && overscanSnap.windowWidth > 0 && overscanSnap.windowHeight > 0) {
        containerWidth = overscanSnap.windowWidth;
        containerHeight = overscanSnap.windowHeight;
    }
    if (containerWidth <= 0 || containerHeight <= 0) {
        containerWidth = width;
        containerHeight = height;
    }

    int redirectWidth = 0;
    int redirectHeight = 0;
    if (macRedirect) {
        (void)GetMacMirrorRedirectSizeInternal(redirectWidth, redirectHeight);
    }

    const int captureW = overscan ? g_overscanDims.totalWidth :
                       (macRedirect && redirectWidth > 0 ? redirectWidth : containerWidth);
    const int captureH = overscan ? g_overscanDims.totalHeight :
                       (macRedirect && redirectHeight > 0 ? redirectHeight : containerHeight);

    int copyW = width;
    int copyH = height;
    int copySrcX = 0;
    int copySrcY = 0;
    int copiedH = copyH;

    if (requiresGameFramebuffer) {
        // Ensure game frame texture sized to capture dimensions
        EnsureGameFrameTexture(captureW, captureH);

        copyW = std::min(captureW, g_gameFrameW);
        copyH = std::min(captureH, g_gameFrameH);
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

        // Read from overscan, redirect, or the default framebuffer.
        g_gl.bindFramebuffer(GL_READ_FRAMEBUFFER,
                             overscan ? g_overscanFbo :
                             (macRedirect ? g_macMirrorRedirect.fbo : 0));

        if (overscan) {
            glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, copyW, copyH);
        } else if (macRedirect) {
            glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, copyW, copyH);
        } else {
            // Mirror capture coordinates are resolved in full-container space. Copy the
            // whole drawable so the non-overscan path matches the oversized/OOB path and
            // larger capture rectangles do not sample past the edge of a viewport-sized
            // texture.
            const int viewportX = 0;
            const int viewportY = 0;
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
    }

    GLsync fence = nullptr;
    if (requiresGameFramebuffer && !useInlineMirrorProcessing) {
        glFinish();
    }

    int viewportBottomLeftX = currentViewport[0];
    int viewportBottomLeftY = currentViewport[1];
    if (overscan) {
        viewportBottomLeftX -= overscanSnap.marginLeft;
        viewportBottomLeftY -= overscanSnap.marginBottom;
    }
    const int viewportTopLeftX = viewportBottomLeftX;
    const int viewportTopLeftY = containerHeight - (viewportBottomLeftY + currentViewport[3]);

    bool anyMirrorUsesPieAnchors = false;
    for (const auto& mirrorRender : g_mirrorConfigs) {
        if (MirrorUsesPieAnchors(mirrorRender)) {
            anyMirrorUsesPieAnchors = true;
            break;
        }
    }
    if (anyMirrorUsesPieAnchors &&
        !ActiveModeViewportMatchesLiveViewport(containerWidth,
                                              containerHeight,
                                              viewportTopLeftX,
                                              viewportTopLeftY,
                                              currentViewport[2],
                                              currentViewport[3])) {
        RestorePublishedContentForPieMirrors();
        if (IsDebugEnabled()) {
            fprintf(stderr,
                    "[Linuxscreen][mirror] Skipping pie-anchor capture while live viewport (%d,%d %dx%d) differs from active mode viewport\n",
                    viewportTopLeftX,
                    viewportTopLeftY,
                    currentViewport[2],
                    currentViewport[3]);
        }
        return;
    }

    int textureOriginTopLeftX = 0;
    int textureOriginTopLeftY = 0;
    if (overscan) {
        textureOriginTopLeftX = -overscanSnap.marginLeft;
        textureOriginTopLeftY = -overscanSnap.marginTop;
    } else {
        textureOriginTopLeftX = requiresGameFramebuffer ? copySrcX : 0;
        textureOriginTopLeftY = requiresGameFramebuffer ? (containerHeight - (copySrcY + copiedH)) : 0;
    }

    if (useInlineMirrorProcessing) {
        MirrorFrameSlot slot;
        slot.width = copyW;
        slot.height = copyH;
        slot.generation = generation;
        slot.containerWidth = containerWidth;
        slot.containerHeight = containerHeight;
        slot.viewportTopLeftX = viewportTopLeftX;
        slot.viewportTopLeftY = viewportTopLeftY;
        slot.viewportWidth = currentViewport[2];
        slot.viewportHeight = currentViewport[3];
        slot.textureOriginTopLeftX = textureOriginTopLeftX;
        slot.textureOriginTopLeftY = textureOriginTopLeftY;
        slot.overscanActive = overscan;
        slot.overscanWindowWidth = overscanSnap.windowWidth;
        slot.overscanWindowHeight = overscanSnap.windowHeight;
        slot.overscanMarginLeft = overscanSnap.marginLeft;
        slot.overscanMarginBottom = overscanSnap.marginBottom;
        DrainStaleFenceQueue();
        if (!InitMirrorShaders()) {
            fprintf(stderr, "[Linuxscreen][mirror] Inline path failed to initialize shaders\n");
            return;
        }
        g_workerGeneration.store(generation, std::memory_order_release);
        ProcessAllMirrorsWorker(slot.width, slot.height, slot);
        return;
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
