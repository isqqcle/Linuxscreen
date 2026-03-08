void DestroyMirrorInstance(X11MirrorInstance& inst) {
    if (inst.filterFbo) { g_gl.deleteFramebuffers(1, &inst.filterFbo); inst.filterFbo = 0; }
    if (inst.filterTexture) { glDeleteTextures(1, &inst.filterTexture); inst.filterTexture = 0; }
    if (inst.finalFbo[0]) { g_gl.deleteFramebuffers(1, &inst.finalFbo[0]); inst.finalFbo[0] = 0; }
    if (inst.finalFbo[1]) { g_gl.deleteFramebuffers(1, &inst.finalFbo[1]); inst.finalFbo[1] = 0; }
    if (inst.finalTexture[0]) { glDeleteTextures(1, &inst.finalTexture[0]); inst.finalTexture[0] = 0; }
    if (inst.finalTexture[1]) { glDeleteTextures(1, &inst.finalTexture[1]); inst.finalTexture[1] = 0; }
    if (inst.contentDownsampleFbo) { g_gl.deleteFramebuffers(1, &inst.contentDownsampleFbo); inst.contentDownsampleFbo = 0; }
    if (inst.contentDownsampleTex) { glDeleteTextures(1, &inst.contentDownsampleTex); inst.contentDownsampleTex = 0; }
    if (inst.contentDetectionPbo) { g_gl.deleteBuffers(1, &inst.contentDetectionPbo); inst.contentDetectionPbo = 0; }
    inst.filterW = 0;
    inst.filterH = 0;
    inst.finalW = 0;
    inst.finalH = 0;
    inst.contentDownW = 0;
    inst.contentDownH = 0;
    inst.contentPboW = 0;
    inst.contentPboH = 0;
    inst.hasValidContent = false;
    inst.hasFrameContent = false;
}

void DestroyAllInstances() {
    for (auto& kv : g_instances) {
        DestroyMirrorInstance(kv.second);
    }
    g_instances.clear();
}

void EnsureGameFrameTexture(int w, int h) {
    // Clamp to GPU max texture size to prevent driver crashes
    const int maxSz = g_maxTextureSize > 0 ? g_maxTextureSize : 16384;
    if (w > maxSz || h > maxSz) {
        fprintf(stderr, "[Linuxscreen][mirror] WARNING: Game frame texture %dx%d exceeds "
                "GL_MAX_TEXTURE_SIZE (%d), clamping\n", w, h, maxSz);
        w = std::min(w, maxSz);
        h = std::min(h, maxSz);
    }

    if (g_gameFrameTexture != 0 && g_gameFrameW == w && g_gameFrameH == h) { return; }

    if (g_gameFrameFbo) { g_gl.deleteFramebuffers(1, &g_gameFrameFbo); g_gameFrameFbo = 0; }
    if (g_gameFrameTexture) { glDeleteTextures(1, &g_gameFrameTexture); g_gameFrameTexture = 0; }

    // Save game context state before touching it (called on the game's render thread)
    GLint prevActiveUnit = 0;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &prevActiveUnit);
    glActiveTexture(GL_TEXTURE0);
    GLint prevTex2d = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex2d);

    glGenTextures(1, &g_gameFrameTexture);
    glBindTexture(GL_TEXTURE_2D, g_gameFrameTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prevTex2d));
    glActiveTexture(prevActiveUnit);

    g_gameFrameW = w;
    g_gameFrameH = h;

    if (IsDebugEnabled()) {
        fprintf(stderr, "[Linuxscreen][mirror] Game frame texture created/resized: %dx%d\n", w, h);
    }
}

void EnsureMirrorResources(const ResolvedMirrorRender& resolved, X11MirrorInstance& inst) {
    const auto& config = resolved.config;
    const int border = platform::config::GetMirrorDynamicBorderPadding(config.border);
    const int fboW = config.captureWidth + 2 * border;
    const int fboH = config.captureHeight + 2 * border;
    const float scaleX = config.output.separateScale ? config.output.scaleX : config.output.scale;
    const float scaleY = config.output.separateScale ? config.output.scaleY : config.output.scale;
    const int finalW = std::max(1, static_cast<int>(static_cast<float>(fboW) * scaleX));
    const int finalH = std::max(1, static_cast<int>(static_cast<float>(fboH) * scaleY));

    if (inst.filterTexture != 0 && inst.filterW == fboW && inst.filterH == fboH &&
        inst.finalW == finalW && inst.finalH == finalH) {
        return;
    }

    DestroyMirrorInstance(inst);

    // Create filter texture + FBO
    glGenTextures(1, &inst.filterTexture);
    glBindTexture(GL_TEXTURE_2D, inst.filterTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, fboW, fboH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);

    g_gl.genFramebuffers(1, &inst.filterFbo);
    g_gl.bindFramebuffer(GL_FRAMEBUFFER, inst.filterFbo);
    g_gl.framebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, inst.filterTexture, 0);
    g_gl.bindFramebuffer(GL_FRAMEBUFFER, 0);

    // Create double-buffered final textures + FBOs (M2)
    for (int i = 0; i < 2; ++i) {
        glGenTextures(1, &inst.finalTexture[i]);
        glBindTexture(GL_TEXTURE_2D, inst.finalTexture[i]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, finalW, finalH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glBindTexture(GL_TEXTURE_2D, 0);

        g_gl.genFramebuffers(1, &inst.finalFbo[i]);
        g_gl.bindFramebuffer(GL_FRAMEBUFFER, inst.finalFbo[i]);
        g_gl.framebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, inst.finalTexture[i], 0);
        g_gl.bindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    inst.filterW = fboW;
    inst.filterH = fboH;
    inst.finalW = finalW;
    inst.finalH = finalH;

    if (IsDebugEnabled()) {
        fprintf(stderr, "[Linuxscreen][mirror] Instance '%s' FBO created/resized: filter=%dx%d final=%dx%d\n",
                config.name.c_str(), fboW, fboH, finalW, finalH);
    }
}

bool DetectMirrorFrameContent(X11MirrorInstance& inst, int sourceFbo, int sourceW, int sourceH) {
    constexpr int kDetectMax = 64;
    constexpr int kDetectStep = 4;

    const int detW = std::min(sourceW, kDetectMax);
    const int detH = std::min(sourceH, kDetectMax);
    if (detW <= 0 || detH <= 0) {
        return false;
    }

    if (inst.contentDownsampleFbo == 0 || inst.contentDownsampleTex == 0 ||
        inst.contentDownW != detW || inst.contentDownH != detH) {
        if (inst.contentDownsampleFbo == 0) {
            g_gl.genFramebuffers(1, &inst.contentDownsampleFbo);
        }
        if (inst.contentDownsampleTex == 0) {
            glGenTextures(1, &inst.contentDownsampleTex);
        }

        glBindTexture(GL_TEXTURE_2D, inst.contentDownsampleTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, detW, detH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);

        g_gl.bindFramebuffer(GL_FRAMEBUFFER, inst.contentDownsampleFbo);
        g_gl.framebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, inst.contentDownsampleTex, 0);
        g_gl.bindFramebuffer(GL_FRAMEBUFFER, 0);

        inst.contentDownW = detW;
        inst.contentDownH = detH;
    }

    if (inst.contentDetectionPbo == 0 || inst.contentPboW != detW || inst.contentPboH != detH) {
        if (inst.contentDetectionPbo == 0) {
            g_gl.genBuffers(1, &inst.contentDetectionPbo);
        }
        g_gl.bindBuffer(GL_PIXEL_PACK_BUFFER, inst.contentDetectionPbo);
        g_gl.bufferData(GL_PIXEL_PACK_BUFFER, detW * detH * 4, nullptr, GL_STREAM_READ);
        g_gl.bindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        inst.contentPboW = detW;
        inst.contentPboH = detH;
    }

    g_gl.bindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(sourceFbo));
    g_gl.bindFramebuffer(GL_DRAW_FRAMEBUFFER, inst.contentDownsampleFbo);
    g_gl.blitFramebuffer(0, 0, sourceW, sourceH, 0, 0, detW, detH, GL_COLOR_BUFFER_BIT, GL_LINEAR);

    g_gl.bindFramebuffer(GL_READ_FRAMEBUFFER, inst.contentDownsampleFbo);
    g_gl.bindBuffer(GL_PIXEL_PACK_BUFFER, inst.contentDetectionPbo);
    glReadPixels(0, 0, detW, detH, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    bool hasContent = false;
    const unsigned char* mapped = static_cast<const unsigned char*>(
        g_gl.mapBufferRange(GL_PIXEL_PACK_BUFFER, 0, detW * detH * 4, GL_MAP_READ_BIT));
    if (mapped) {
        for (int y = 0; y < detH && !hasContent; y += kDetectStep) {
            const unsigned char* row = mapped + (static_cast<std::size_t>(y) * detW * 4);
            for (int x = 0; x < detW; x += kDetectStep) {
                if (row[(static_cast<std::size_t>(x) * 4) + 3] > 0) {
                    hasContent = true;
                    break;
                }
            }
        }
        g_gl.unmapBuffer(GL_PIXEL_PACK_BUFFER);
    }

    g_gl.bindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    g_gl.bindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    g_gl.bindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

    return hasContent;
}

struct GlStateSnapshot {
    GLint program = 0;
    GLint vao = 0;
    GLint arrayBuffer = 0;
    GLint readFbo = 0;
    GLint drawFbo = 0;
    GLint viewport[4] = {};
    GLboolean blend = GL_FALSE;
    GLint blendSrcRgb = 0, blendDstRgb = 0;
    GLint blendSrcAlpha = 0, blendDstAlpha = 0;
    GLboolean depthTest = GL_FALSE;
    GLboolean scissorTest = GL_FALSE;
    GLint activeTexUnit = 0;
    GLint tex2d = 0;
    GLboolean colorMask[4] = {};
    GLfloat clearColor[4] = {};
};

void SaveGlState(GlStateSnapshot& s) {
    glGetIntegerv(GL_CURRENT_PROGRAM, &s.program);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &s.vao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &s.arrayBuffer);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &s.readFbo);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &s.drawFbo);
    glGetIntegerv(GL_VIEWPORT, s.viewport);
    s.blend = glIsEnabled(GL_BLEND);
    glGetIntegerv(GL_BLEND_SRC_RGB, &s.blendSrcRgb);
    glGetIntegerv(GL_BLEND_DST_RGB, &s.blendDstRgb);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &s.blendSrcAlpha);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &s.blendDstAlpha);
    s.depthTest = glIsEnabled(GL_DEPTH_TEST);
    s.scissorTest = glIsEnabled(GL_SCISSOR_TEST);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &s.activeTexUnit);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &s.tex2d);
    glGetBooleanv(GL_COLOR_WRITEMASK, s.colorMask);
    glGetFloatv(GL_COLOR_CLEAR_VALUE, s.clearColor);
}

void RestoreGlState(const GlStateSnapshot& s) {
    g_gl.useProgram(s.program);
    g_gl.bindVertexArray(s.vao);
    g_gl.bindBuffer(GL_ARRAY_BUFFER, s.arrayBuffer);
    g_gl.bindFramebuffer(GL_READ_FRAMEBUFFER, s.readFbo);
    g_gl.bindFramebuffer(GL_DRAW_FRAMEBUFFER, s.drawFbo);
    glViewport(s.viewport[0], s.viewport[1], s.viewport[2], s.viewport[3]);
    if (s.blend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    g_gl.blendFuncSeparate(s.blendSrcRgb, s.blendDstRgb, s.blendSrcAlpha, s.blendDstAlpha);
    if (s.depthTest) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (s.scissorTest) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
    g_gl.activeTexture(s.activeTexUnit);
    glBindTexture(GL_TEXTURE_2D, s.tex2d);
    glColorMask(s.colorMask[0], s.colorMask[1], s.colorMask[2], s.colorMask[3]);
    glClearColor(s.clearColor[0], s.clearColor[1], s.clearColor[2], s.clearColor[3]);
}

const platform::config::ModeConfig* FindModeConfigByName(const platform::config::LinuxscreenConfig& config,
                                                         const std::string& modeName) {
    for (const auto& mode : config.modes) {
        if (mode.name == modeName) {
            return &mode;
        }
    }
    return nullptr;
}

void EnqueueStaleFence(GLsync fence) {
    if (!fence) { return; }
    StaleFenceNode* node = new StaleFenceNode{ fence, nullptr };
    std::lock_guard<std::mutex> lock(g_staleFenceMutex);
    node->next = g_staleFenceHead;
    g_staleFenceHead = node;
}

void DrainStaleFenceQueue() {
    std::lock_guard<std::mutex> lock(g_staleFenceMutex);
    std::uint64_t count = 0;
    while (g_staleFenceHead) {
        StaleFenceNode* node = g_staleFenceHead;
        g_staleFenceHead = node->next;
        if (node->fence && g_gl.deleteSync) {
            g_gl.deleteSync(node->fence);
        }
        delete node;
        ++count;
    }
    if (count > 0) {
        g_staleFenceDrainCount.fetch_add(count, std::memory_order_relaxed);
    }
}

void PostFrameSlot(int width,
                   int height,
                   GLsync fence,
                   std::uint64_t generation,
                   bool overscanActive,
                   const OverscanDimensions& overscanSnap,
                   int containerWidth,
                   int containerHeight,
                   int viewportTopLeftX,
                   int viewportTopLeftY,
                   int viewportWidth,
                   int viewportHeight,
                   int textureOriginTopLeftX,
                   int textureOriginTopLeftY) {
    // Determine which slot to write (alternate between 0 and 1)
    static int writeIdx = 0;
    int nextIdx = 1 - writeIdx;

    // Before overwriting, move any existing fence to stale queue
    if (g_slotPool[nextIdx].fence) {
        EnqueueStaleFence(g_slotPool[nextIdx].fence);
        g_slotPool[nextIdx].fence = nullptr;
    }

    // Write new slot data
    g_slotPool[nextIdx].width = width;
    g_slotPool[nextIdx].height = height;
    g_slotPool[nextIdx].fence = fence;
    g_slotPool[nextIdx].generation = generation;
    g_slotPool[nextIdx].containerWidth = containerWidth;
    g_slotPool[nextIdx].containerHeight = containerHeight;
    g_slotPool[nextIdx].viewportTopLeftX = viewportTopLeftX;
    g_slotPool[nextIdx].viewportTopLeftY = viewportTopLeftY;
    g_slotPool[nextIdx].viewportWidth = viewportWidth;
    g_slotPool[nextIdx].viewportHeight = viewportHeight;
    g_slotPool[nextIdx].textureOriginTopLeftX = textureOriginTopLeftX;
    g_slotPool[nextIdx].textureOriginTopLeftY = textureOriginTopLeftY;
    g_slotPool[nextIdx].overscanActive = overscanActive;
    g_slotPool[nextIdx].overscanWindowWidth = overscanSnap.windowWidth;
    g_slotPool[nextIdx].overscanWindowHeight = overscanSnap.windowHeight;
    g_slotPool[nextIdx].overscanMarginLeft = overscanSnap.marginLeft;
    g_slotPool[nextIdx].overscanMarginBottom = overscanSnap.marginBottom;

    // Publish slot index
    int prevIdx = g_pendingSlotIdx.exchange(nextIdx, std::memory_order_release);
    if (prevIdx >= 0) {
        // Previous slot was overwritten without being consumed
        g_slotOverwriteCount.fetch_add(1, std::memory_order_relaxed);
    }

    writeIdx = nextIdx;

    // Notify worker
    {
        std::lock_guard<std::mutex> lock(g_slotMutex);
        g_slotCV.notify_one();
    }
}

void ProcessAllMirrorsWorker(int width, int height, const MirrorFrameSlot& slot) {
    if (width <= 0 || height <= 0) { return; }
    if (!g_configsLoaded) { return; }

    std::vector<ResolvedMirrorRender> localConfigs;
    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        if (g_mirrorConfigs.empty()) { return; }
        localConfigs = g_mirrorConfigs;
    }

    // Save GL state
    GlStateSnapshot savedState;
    SaveGlState(savedState);

    // Disable depth test and scissor for our rendering
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

    // Bind quad VAO
    g_gl.bindVertexArray(g_shaders.quadVao);
    g_gl.bindBuffer(GL_ARRAY_BUFFER, g_shaders.quadVbo);

    const auto now = std::chrono::steady_clock::now();
    struct PendingMirrorPublish {
        X11MirrorInstance* instance = nullptr;
        int backIdx = 0;
        bool hasFrameContent = false;
    };
    std::vector<PendingMirrorPublish> pendingPublishes;
    pendingPublishes.reserve(localConfigs.size());

    for (auto& mirrorRender : localConfigs) {
        const auto& config = mirrorRender.config;
        auto& inst = g_instances[config.name];
        if (config.fps > 0) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - inst.lastCaptureTime).count();
            if (elapsed < (1000 / config.fps)) {
                continue;
            }
        }

        // Ensure FBO resources
        EnsureMirrorResources(mirrorRender, inst);
        if (!inst.filterFbo || !inst.finalFbo[0]) { continue; }

        const int border = platform::config::GetMirrorDynamicBorderPadding(config.border);
        const int fboW = inst.filterW;
        const int fboH = inst.filterH;
        const int finalW = inst.finalW;
        const int finalH = inst.finalH;

        // ===== PASS 1: Filter pass =====
        g_gl.bindFramebuffer(GL_FRAMEBUFFER, inst.filterFbo);

        // Clear the full FBO with transparent black before rendering inputs
        glViewport(0, 0, fboW, fboH);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        // Select shader based on mode
        const bool useRaw = config.rawOutput;
        const bool useColorPt = !useRaw && config.colorPassthrough;
        const bool useFilter = !useRaw && !useColorPt;
        (void)useFilter;

        // Bind game frame texture on unit 0
        g_gl.activeTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, g_gameFrameTexture);

        // Multiple input regions: additive blend after first
        bool firstInput = true;

        for (const auto& region : config.input) {
            if (!region.enabled) {
                continue;
            }

            // Compute source rect in capture-texture-local top-left coordinates.
            int capX = 0;
            int capY = 0;
            ResolveMirrorCaptureInputCoords(region.relativeTo,
                                            region.x,
                                            region.y,
                                            config.captureWidth,
                                            config.captureHeight,
                                            slot,
                                            width,
                                            height,
                                            capX,
                                            capY);

            // Convert top-left texture coords to GL's bottom-left texture coords.
            int capY_gl = height - capY - config.captureHeight;

            // Set up viewport for this input region (border padding around capture area)
            glViewport(border, border, config.captureWidth, config.captureHeight);

            // Compute normalized texture coordinates relative to the full texture size
            float sx = static_cast<float>(capX) / static_cast<float>(width);
            float sy = static_cast<float>(capY_gl) / static_cast<float>(height);
            float sw = static_cast<float>(config.captureWidth) / static_cast<float>(width);
            float sh = static_cast<float>(config.captureHeight) / static_cast<float>(height);

            // Enable additive blend for second+ inputs (accumulate color contributions)
            if (!firstInput) {
                glEnable(GL_BLEND);
                glBlendFunc(GL_ONE, GL_ONE);
            } else {
                glDisable(GL_BLEND);
            }
            firstInput = false;

            if (useRaw) {
                g_gl.useProgram(g_shaders.passthroughProgram);
                g_gl.uniform1i(g_shaders.passthroughLocs.screenTexture, 0);
                g_gl.uniform4f(g_shaders.passthroughLocs.sourceRect, sx, sy, sw, sh);
            } else if (useColorPt) {
                g_gl.useProgram(g_shaders.filterPassthroughProgram);
                g_gl.uniform1i(g_shaders.filterPassthroughLocs.screenTexture, 0);
                g_gl.uniform4f(g_shaders.filterPassthroughLocs.sourceRect, sx, sy, sw, sh);
                g_gl.uniform1i(g_shaders.filterPassthroughLocs.gammaMode, 1); // AssumeSRGB
                const int nColors = static_cast<int>(config.colors.targetColors.size());
                g_gl.uniform1i(g_shaders.filterPassthroughLocs.targetColorCount, nColors);
                if (nColors > 0) {
                    // Pack colors into float array for glUniform3fv
                    float colorData[8 * 3] = {};
                    for (int ci = 0; ci < nColors && ci < 8; ++ci) {
                        colorData[ci * 3 + 0] = config.colors.targetColors[ci].r;
                        colorData[ci * 3 + 1] = config.colors.targetColors[ci].g;
                        colorData[ci * 3 + 2] = config.colors.targetColors[ci].b;
                    }
                    g_gl.uniform3fv(g_shaders.filterPassthroughLocs.targetColors, nColors, colorData);
                }
                g_gl.uniform1f(g_shaders.filterPassthroughLocs.sensitivity, config.colorSensitivity);
            } else {
                // useFilter
                g_gl.useProgram(g_shaders.filterProgram);
                g_gl.uniform1i(g_shaders.filterLocs.screenTexture, 0);
                g_gl.uniform4f(g_shaders.filterLocs.sourceRect, sx, sy, sw, sh);
                g_gl.uniform1i(g_shaders.filterLocs.gammaMode, 1); // AssumeSRGB
                const int nColors = static_cast<int>(config.colors.targetColors.size());
                g_gl.uniform1i(g_shaders.filterLocs.targetColorCount, nColors);
                if (nColors > 0) {
                    float colorData[8 * 3] = {};
                    for (int ci = 0; ci < nColors && ci < 8; ++ci) {
                        colorData[ci * 3 + 0] = config.colors.targetColors[ci].r;
                        colorData[ci * 3 + 1] = config.colors.targetColors[ci].g;
                        colorData[ci * 3 + 2] = config.colors.targetColors[ci].b;
                    }
                    g_gl.uniform3fv(g_shaders.filterLocs.targetColors, nColors, colorData);
                }
                g_gl.uniform4f(g_shaders.filterLocs.outputColor,
                               config.colors.output.r, config.colors.output.g,
                               config.colors.output.b, config.colors.output.a);
                g_gl.uniform1f(g_shaders.filterLocs.sensitivity, config.colorSensitivity);
            }

            glDrawArrays(GL_TRIANGLES, 0, 6);
        }

        glDisable(GL_BLEND);

        const bool needsFrameContentDetection =
            !useRaw &&
            config.border.type == platform::config::MirrorBorderType::Static &&
            config.border.staticThickness > 0;
        const bool hasFrameContent = needsFrameContentDetection
            ? DetectMirrorFrameContent(inst, inst.filterFbo, fboW, fboH)
            : true;

        // ===== PASS 2: Border/render pass =====
        // Write to back buffer (will be flipped after processing)
        int backIdx = 1 - inst.frontIdx.load(std::memory_order_relaxed);
        g_gl.bindFramebuffer(GL_FRAMEBUFFER, inst.finalFbo[backIdx]);
        glViewport(0, 0, finalW, finalH);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glBindTexture(GL_TEXTURE_2D, inst.filterTexture);

        if (useRaw) {
            // Raw: passthrough blit, force alpha=1 (opaque)
            g_gl.useProgram(g_shaders.passthroughProgram);
            g_gl.uniform1i(g_shaders.passthroughLocs.screenTexture, 0);
            // Full-texture source rect
            g_gl.uniform4f(g_shaders.passthroughLocs.sourceRect, 0.0f, 0.0f, 1.0f, 1.0f);
        } else if (config.border.type == platform::config::MirrorBorderType::Dynamic && useColorPt) {
            // Render passthrough: dynamic border, preserves pixel color
            g_gl.useProgram(g_shaders.renderPassthroughProgram);
            g_gl.uniform1i(g_shaders.renderPassthroughLocs.filterTexture, 0);
            g_gl.uniform1i(g_shaders.renderPassthroughLocs.borderWidth, border);
            g_gl.uniform4f(g_shaders.renderPassthroughLocs.borderColor,
                           config.colors.border.r, config.colors.border.g,
                           config.colors.border.b, config.colors.border.a);
            g_gl.uniform2f(g_shaders.renderPassthroughLocs.screenPixel,
                           1.0f / static_cast<float>(finalW),
                           1.0f / static_cast<float>(finalH));
        } else if (config.border.type == platform::config::MirrorBorderType::Dynamic) {
            // Render: dynamic border, replaces pixel color
            g_gl.useProgram(g_shaders.renderProgram);
            g_gl.uniform1i(g_shaders.renderLocs.filterTexture, 0);
            g_gl.uniform1i(g_shaders.renderLocs.borderWidth, border);
            g_gl.uniform4f(g_shaders.renderLocs.outputColor,
                           config.colors.output.r, config.colors.output.g,
                           config.colors.output.b, config.colors.output.a);
            g_gl.uniform4f(g_shaders.renderLocs.borderColor,
                           config.colors.border.r, config.colors.border.g,
                           config.colors.border.b, config.colors.border.a);
            g_gl.uniform2f(g_shaders.renderLocs.screenPixel,
                           1.0f / static_cast<float>(finalW),
                           1.0f / static_cast<float>(finalH));
        } else if (useColorPt) {
            // Static-border mirrors keep only the filtered content in the final texture.
            g_gl.useProgram(g_shaders.renderPassthroughProgram);
            g_gl.uniform1i(g_shaders.renderPassthroughLocs.filterTexture, 0);
            g_gl.uniform1i(g_shaders.renderPassthroughLocs.borderWidth, 0);
            g_gl.uniform4f(g_shaders.renderPassthroughLocs.borderColor, 0.0f, 0.0f, 0.0f, 0.0f);
            g_gl.uniform2f(g_shaders.renderPassthroughLocs.screenPixel,
                           1.0f / static_cast<float>(finalW),
                           1.0f / static_cast<float>(finalH));
        } else {
            g_gl.useProgram(g_shaders.renderProgram);
            g_gl.uniform1i(g_shaders.renderLocs.filterTexture, 0);
            g_gl.uniform1i(g_shaders.renderLocs.borderWidth, 0);
            g_gl.uniform4f(g_shaders.renderLocs.outputColor,
                           config.colors.output.r, config.colors.output.g,
                           config.colors.output.b, config.colors.output.a);
            g_gl.uniform4f(g_shaders.renderLocs.borderColor, 0.0f, 0.0f, 0.0f, 0.0f);
            g_gl.uniform2f(g_shaders.renderLocs.screenPixel,
                           1.0f / static_cast<float>(finalW),
                           1.0f / static_cast<float>(finalH));
        }

        glDrawArrays(GL_TRIANGLES, 0, 6);

        inst.lastCaptureTime = now;
        pendingPublishes.push_back(PendingMirrorPublish{ &inst, backIdx, hasFrameContent });
    }

    if (!pendingPublishes.empty()) {
        // Finish the batch once before publishing new front buffers so readers never
        // sample partially rendered textures from a shared context.
        glFinish();

        for (const PendingMirrorPublish& pending : pendingPublishes) {
            pending.instance->frontIdx.store(pending.backIdx, std::memory_order_release);
            pending.instance->hasValidContent = true;
            pending.instance->hasFrameContent = pending.hasFrameContent;
        }
    }

    RestoreGlState(savedState);
}

void WorkerThreadMain() {
    struct WorkerExitSignal {
        ~WorkerExitSignal() {
            std::lock_guard<std::mutex> lock(g_workerExitMutex);
            g_workerExited = true;
            g_workerExitCV.notify_all();
        }
    } workerExitSignal;

    SetViewportPlacementBypass(true);

    if (IsDebugEnabled()) {
        fprintf(stderr, "[Linuxscreen][mirror] Worker thread started (tid=%zu)\n",
                static_cast<std::size_t>(std::hash<std::thread::id>{}(std::this_thread::get_id())));
    }

    // Make mirror context current once at start
    GlxContextRestoreState restore;
    if (!MakeSharedGlxContextCurrent(GlxSharedContextRole::Mirror, restore)) {
        fprintf(stderr, "[Linuxscreen][mirror] Worker failed to make mirror context current\n");
        return;
    }

    std::uint64_t lastGeneration = 0;
    bool shadersInitialized = false;

    while (!g_stopWorker.load(std::memory_order_acquire)) {
        // Wait for slot with timeout to periodically check stop flag
        std::unique_lock<std::mutex> lock(g_slotMutex);
        bool gotSlot = g_slotCV.wait_for(lock, std::chrono::milliseconds(100), [&]() {
            return g_pendingSlotIdx.load(std::memory_order_acquire) >= 0 || g_stopWorker.load(std::memory_order_acquire);
        });

        if (g_stopWorker.load(std::memory_order_acquire)) {
            break;
        }

        if (!gotSlot) {
            continue;
        }

        int idx = g_pendingSlotIdx.exchange(-1, std::memory_order_acquire);
        if (idx < 0) {
            continue;
        }

        MirrorFrameSlot slot = g_slotPool[idx];
        lock.unlock();

        DrainStaleFenceQueue();

        if (slot.generation != lastGeneration) {
            if (IsDebugEnabled()) {
                fprintf(stderr, "[Linuxscreen][mirror] Worker generation changed (%llu -> %llu), resetting\n",
                        static_cast<unsigned long long>(lastGeneration),
                        static_cast<unsigned long long>(slot.generation));
            }

            DestroyAllInstances();
            CleanupMirrorShaders();
            shadersInitialized = false;
            lastGeneration = slot.generation;
            g_workerGeneration.store(slot.generation, std::memory_order_release);

            if (slot.fence == nullptr) {
                continue;
            }
        }

        // Wait on fence for texture data to be ready
        if (slot.fence && g_gl.clientWaitSync) {
            GLenum waitResult = g_gl.clientWaitSync(slot.fence, GL_SYNC_FLUSH_COMMANDS_BIT, 16'000'000); // 16ms
            g_gl.deleteSync(slot.fence);
            slot.fence = nullptr;

            if (waitResult == GL_TIMEOUT_EXPIRED) {
                g_fenceTimeoutCount.fetch_add(1, std::memory_order_relaxed);
                if (IsDebugEnabled()) {
                    static std::atomic<std::uint64_t> lastLog{ 0 };
                    std::uint64_t count = g_fenceTimeoutCount.load(std::memory_order_relaxed);
                    if (count - lastLog.load() >= 1) {
                        lastLog.store(count);
                        fprintf(stderr, "[Linuxscreen][mirror] Fence wait timeout (total=%llu)\n",
                                static_cast<unsigned long long>(count));
                    }
                }
                continue; // Skip this frame
            }
        }

        if (!shadersInitialized) {
            if (!InitMirrorShaders()) {
                fprintf(stderr, "[Linuxscreen][mirror] Worker failed to initialize shaders\n");
                continue;
            }
            shadersInitialized = true;
        }

        ProcessAllMirrorsWorker(slot.width, slot.height, slot);
    }

    DrainStaleFenceQueue();
    DestroyAllInstances();
    CleanupMirrorShaders();

    if (IsDebugEnabled()) {
        fprintf(stderr, "[Linuxscreen][mirror] Worker thread stopped\n");
    }
}

void StartMirrorWorker() {
    bool expected = false;
    if (!g_workerStarted.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return;
    }

    g_stopWorker.store(false, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(g_workerExitMutex);
        g_workerExited = false;
    }
    g_workerThread = std::thread(WorkerThreadMain);
}

void StopMirrorWorker() {
    g_stopWorker.store(true, std::memory_order_release);
    g_slotCV.notify_all();

    if (g_workerThread.joinable()) {
        std::unique_lock<std::mutex> lock(g_workerExitMutex);
        const bool stopped = g_workerExitCV.wait_for(lock, std::chrono::seconds(2), []() { return g_workerExited; });
        lock.unlock();

        if (stopped) {
            g_workerThread.join();
        } else {
            g_workerThread.detach();
            if (IsDebugEnabled()) {
                fprintf(stderr, "[Linuxscreen][mirror] Worker thread join timeout, detached\n");
            }
        }
    }

    g_workerStarted.store(false, std::memory_order_release);
}
