bool EnsureOverlayProgramsReady() {
    if (!g_overlayProgramReady) {
        static const char* overlayFrag = R"(#version 330 core
out vec4 FragColor;
in vec2 TexCoord;
uniform sampler2D screenTexture;
uniform vec4 u_sourceRect;
uniform float u_opacity;
void main() {
    vec2 srcCoord = u_sourceRect.xy + TexCoord * u_sourceRect.zw;
    FragColor = texture(screenTexture, srcCoord) * u_opacity;
})";

        GLuint vs = CompileShader(GL_VERTEX_SHADER, kVertShader);
        GLuint fs = CompileShader(GL_FRAGMENT_SHADER, overlayFrag);
        g_overlayProgram = CreateProgram(vs, fs);
        if (vs) { g_gl.deleteShader(vs); }
        if (fs) { g_gl.deleteShader(fs); }
        if (!g_overlayProgram) {
            return false;
        }
        g_overlayLocScreenTexture = g_gl.getUniformLocation(g_overlayProgram, "screenTexture");
        g_overlayLocSourceRect = g_gl.getUniformLocation(g_overlayProgram, "u_sourceRect");
        g_overlayLocOpacity = g_gl.getUniformLocation(g_overlayProgram, "u_opacity");
        g_overlayProgramReady = true;
    }

    if (!g_solidColorProgramReady) {
        static const char* solidVert = R"(#version 330 core
layout(location = 0) in vec2 aPos;
void main() { gl_Position = vec4(aPos, 0.0, 1.0); })";
        static const char* solidFrag = R"(#version 330 core
out vec4 FragColor;
uniform vec4 u_color;
void main() { FragColor = u_color; })";

        GLuint vs = CompileShader(GL_VERTEX_SHADER, solidVert);
        GLuint fs = CompileShader(GL_FRAGMENT_SHADER, solidFrag);
        g_solidColorProgram = CreateProgram(vs, fs);
        if (vs) { g_gl.deleteShader(vs); }
        if (fs) { g_gl.deleteShader(fs); }
        if (!g_solidColorProgram) {
            return false;
        }
        g_solidColorLocColor = g_gl.getUniformLocation(g_solidColorProgram, "u_color");
        g_solidColorProgramReady = true;
    }

    if (!g_gradientProgramReady) {
        GLuint vs = CompileShader(GL_VERTEX_SHADER, kVertShader);
        GLuint fs = CompileShader(GL_FRAGMENT_SHADER, kGradientFragShader);
        g_gradientProgram = CreateProgram(vs, fs);
        if (vs) { g_gl.deleteShader(vs); }
        if (fs) { g_gl.deleteShader(fs); }
        if (!g_gradientProgram) {
            return false;
        }
        g_gradientLocs.numStops = g_gl.getUniformLocation(g_gradientProgram, "u_numStops");
        g_gradientLocs.stopColors = g_gl.getUniformLocation(g_gradientProgram, "u_stopColors");
        g_gradientLocs.stopPositions = g_gl.getUniformLocation(g_gradientProgram, "u_stopPositions");
        g_gradientLocs.angle = g_gl.getUniformLocation(g_gradientProgram, "u_angle");
        g_gradientLocs.time = g_gl.getUniformLocation(g_gradientProgram, "u_time");
        g_gradientLocs.animationType = g_gl.getUniformLocation(g_gradientProgram, "u_animationType");
        g_gradientLocs.animationSpeed = g_gl.getUniformLocation(g_gradientProgram, "u_animationSpeed");
        g_gradientLocs.colorFade = g_gl.getUniformLocation(g_gradientProgram, "u_colorFade");
        g_gradientProgramReady = true;
    }

    if (!g_overlayStaticBorderProgramReady) {
        GLuint vs = CompileShader(GL_VERTEX_SHADER, kVertShader);
        GLuint fs = CompileShader(GL_FRAGMENT_SHADER, kStaticBorderFragShader);
        g_overlayStaticBorderProgram = CreateProgram(vs, fs);
        if (vs) { g_gl.deleteShader(vs); }
        if (fs) { g_gl.deleteShader(fs); }
        if (!g_overlayStaticBorderProgram) {
            return false;
        }
        g_overlayStaticBorderLocs.shape = g_gl.getUniformLocation(g_overlayStaticBorderProgram, "u_shape");
        g_overlayStaticBorderLocs.borderColor = g_gl.getUniformLocation(g_overlayStaticBorderProgram, "u_borderColor");
        g_overlayStaticBorderLocs.thickness = g_gl.getUniformLocation(g_overlayStaticBorderProgram, "u_thickness");
        g_overlayStaticBorderLocs.radius = g_gl.getUniformLocation(g_overlayStaticBorderProgram, "u_radius");
        g_overlayStaticBorderLocs.size = g_gl.getUniformLocation(g_overlayStaticBorderProgram, "u_size");
        g_overlayStaticBorderLocs.quadSize = g_gl.getUniformLocation(g_overlayStaticBorderProgram, "u_quadSize");
        g_overlayStaticBorderProgramReady = true;
    }

    return true;
}

struct PendingStaticMirrorBorder {
    const platform::config::MirrorConfig* config = nullptr;
    int screenX = 0;
    int screenY = 0;
    int screenW = 0;
    int screenH = 0;
    bool hasFrameContent = false;
};

struct LetterboxRegion {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 1.0f;
    float v1 = 1.0f;
};

int BuildLetterboxRegions(int viewportWidth,
                          int viewportHeight,
                          const ModeViewportRect& modeViewport,
                          LetterboxRegion outRegions[4]) {
    if (viewportWidth <= 0 || viewportHeight <= 0 || !modeViewport.valid) {
        return 0;
    }

    const int vpLeft = std::clamp(modeViewport.x, 0, viewportWidth);
    const int vpRight = std::clamp(modeViewport.x + modeViewport.width, 0, viewportWidth);
    const int vpTopScreen = std::clamp(modeViewport.y, 0, viewportHeight);
    const int vpBottomScreen = std::clamp(modeViewport.y + modeViewport.height, 0, viewportHeight);

    const int vpBottomGl = std::clamp(viewportHeight - vpBottomScreen, 0, viewportHeight);
    const int vpTopGl = std::clamp(viewportHeight - vpTopScreen, 0, viewportHeight);

    int count = 0;
    auto pushRegion = [&](int x, int y, int w, int h) {
        if (w <= 0 || h <= 0 || count >= 4) {
            return;
        }
        LetterboxRegion region;
        region.x = x;
        region.y = y;
        region.width = w;
        region.height = h;
        region.u0 = static_cast<float>(x) / static_cast<float>(viewportWidth);
        region.v0 = static_cast<float>(y) / static_cast<float>(viewportHeight);
        region.u1 = static_cast<float>(x + w) / static_cast<float>(viewportWidth);
        region.v1 = static_cast<float>(y + h) / static_cast<float>(viewportHeight);
        outRegions[count++] = region;
    };

    pushRegion(0, 0, viewportWidth, vpBottomGl);
    pushRegion(0, vpTopGl, viewportWidth, viewportHeight - vpTopGl);
    pushRegion(0, vpBottomGl, vpLeft, vpTopGl - vpBottomGl);
    pushRegion(vpRight, vpBottomGl, viewportWidth - vpRight, vpTopGl - vpBottomGl);

    return count;
}

void DrawRegionQuad(int viewportWidth,
                    int viewportHeight,
                    const LetterboxRegion& region,
                    bool useCustomUv) {
    if (region.width <= 0 || region.height <= 0) {
        return;
    }

    const float ndcL = (2.0f * static_cast<float>(region.x) / static_cast<float>(viewportWidth)) - 1.0f;
    const float ndcR = (2.0f * static_cast<float>(region.x + region.width) / static_cast<float>(viewportWidth)) - 1.0f;
    const float ndcB = (2.0f * static_cast<float>(region.y) / static_cast<float>(viewportHeight)) - 1.0f;
    const float ndcT = (2.0f * static_cast<float>(region.y + region.height) / static_cast<float>(viewportHeight)) - 1.0f;

    const float u0 = useCustomUv ? region.u0 : 0.0f;
    const float v0 = useCustomUv ? region.v0 : 0.0f;
    const float u1 = useCustomUv ? region.u1 : 1.0f;
    const float v1 = useCustomUv ? region.v1 : 1.0f;

    const float quadVerts[] = {
        ndcL, ndcB, u0, v0,
        ndcR, ndcB, u1, v0,
        ndcR, ndcT, u1, v1,
        ndcL, ndcB, u0, v0,
        ndcR, ndcT, u1, v1,
        ndcL, ndcT, u0, v1,
    };

    g_gl.bindVertexArray(g_overlayVao);
    g_gl.bindBuffer(GL_ARRAY_BUFFER, g_overlayVbo);
    g_gl.bufferSubData(GL_ARRAY_BUFFER, 0, sizeof(quadVerts), quadVerts);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

void DrawFullscreenQuad(int viewportWidth, int viewportHeight) {
    LetterboxRegion fullRegion;
    fullRegion.x = 0;
    fullRegion.y = 0;
    fullRegion.width = viewportWidth;
    fullRegion.height = viewportHeight;
    fullRegion.u0 = 0.0f;
    fullRegion.v0 = 0.0f;
    fullRegion.u1 = 1.0f;
    fullRegion.v1 = 1.0f;
    DrawRegionQuad(viewportWidth, viewportHeight, fullRegion, false);
}

void RenderStaticMirrorBorderOverlay(int viewportWidth,
                                     int viewportHeight,
                                     const PendingStaticMirrorBorder& pending) {
    if (!pending.config) {
        return;
    }

    const auto& border = pending.config->border;
    if (border.type != platform::config::MirrorBorderType::Static || border.staticThickness <= 0) {
        return;
    }
    if (pending.screenW <= 0 || pending.screenH <= 0) {
        return;
    }

    int baseW = (border.staticWidth > 0) ? border.staticWidth : pending.screenW;
    int baseH = (border.staticHeight > 0) ? border.staticHeight : pending.screenH;
    baseW = std::max(baseW, 2);
    baseH = std::max(baseH, 2);

    const int borderExtension = border.staticThickness + 1;
    const int quadW = baseW + borderExtension * 2;
    const int quadH = baseH + borderExtension * 2;

    const int centerOffsetX = (baseW - pending.screenW) / 2;
    const int centerOffsetY = (baseH - pending.screenH) / 2;

    const int quadX = pending.screenX - centerOffsetX + border.staticOffsetX - borderExtension;
    const int quadY = pending.screenY - centerOffsetY + border.staticOffsetY - borderExtension;
    const int quadYGl = viewportHeight - (quadY + quadH);

    const float ndcL = (2.0f * static_cast<float>(quadX) / static_cast<float>(viewportWidth)) - 1.0f;
    const float ndcR = (2.0f * static_cast<float>(quadX + quadW) / static_cast<float>(viewportWidth)) - 1.0f;
    const float ndcB = (2.0f * static_cast<float>(quadYGl) / static_cast<float>(viewportHeight)) - 1.0f;
    const float ndcT = (2.0f * static_cast<float>(quadYGl + quadH) / static_cast<float>(viewportHeight)) - 1.0f;

    const float quadVerts[] = {
        ndcL, ndcB,  0.0f, 0.0f,
        ndcR, ndcB,  1.0f, 0.0f,
        ndcR, ndcT,  1.0f, 1.0f,
        ndcL, ndcB,  0.0f, 0.0f,
        ndcR, ndcT,  1.0f, 1.0f,
        ndcL, ndcT,  0.0f, 1.0f,
    };

    g_gl.bindVertexArray(g_overlayVao);
    g_gl.bindBuffer(GL_ARRAY_BUFFER, g_overlayVbo);
    g_gl.bufferSubData(GL_ARRAY_BUFFER, 0, sizeof(quadVerts), quadVerts);

    g_gl.useProgram(g_overlayStaticBorderProgram);
    g_gl.uniform1i(g_overlayStaticBorderLocs.shape, static_cast<int>(border.staticShape));
    g_gl.uniform4f(g_overlayStaticBorderLocs.borderColor,
                   border.staticColor.r,
                   border.staticColor.g,
                   border.staticColor.b,
                   border.staticColor.a * pending.config->opacity);
    g_gl.uniform1f(g_overlayStaticBorderLocs.thickness, static_cast<float>(border.staticThickness));
    g_gl.uniform1f(g_overlayStaticBorderLocs.radius, static_cast<float>(border.staticRadius));
    g_gl.uniform2f(g_overlayStaticBorderLocs.size, static_cast<float>(baseW), static_cast<float>(baseH));
    g_gl.uniform2f(g_overlayStaticBorderLocs.quadSize, static_cast<float>(quadW), static_cast<float>(quadH));
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

void RenderRectBorderOverlay(int viewportWidth,
                             int viewportHeight,
                             const platform::config::BorderConfig& border,
                             int rectX,
                             int rectY,
                             int rectW,
                             int rectH) {
    if (border.width <= 0) {
        return;
    }
    if (rectW <= 0 || rectH <= 0) {
        return;
    }

    const int borderThickness = std::max(border.width, 1);
    const int baseW = std::max(rectW, 2);
    const int baseH = std::max(rectH, 2);
    const int borderExtension = borderThickness + 1;
    const int quadW = baseW + borderExtension * 2;
    const int quadH = baseH + borderExtension * 2;
    const int quadX = rectX - borderExtension;
    const int quadY = rectY - borderExtension;
    const int quadYGl = viewportHeight - (quadY + quadH);

    const float ndcL = (2.0f * static_cast<float>(quadX) / static_cast<float>(viewportWidth)) - 1.0f;
    const float ndcR = (2.0f * static_cast<float>(quadX + quadW) / static_cast<float>(viewportWidth)) - 1.0f;
    const float ndcB = (2.0f * static_cast<float>(quadYGl) / static_cast<float>(viewportHeight)) - 1.0f;
    const float ndcT = (2.0f * static_cast<float>(quadYGl + quadH) / static_cast<float>(viewportHeight)) - 1.0f;

    const float quadVerts[] = {
        ndcL, ndcB,  0.0f, 0.0f,
        ndcR, ndcB,  1.0f, 0.0f,
        ndcR, ndcT,  1.0f, 1.0f,
        ndcL, ndcB,  0.0f, 0.0f,
        ndcR, ndcT,  1.0f, 1.0f,
        ndcL, ndcT,  0.0f, 1.0f,
    };

    g_gl.bindVertexArray(g_overlayVao);
    g_gl.bindBuffer(GL_ARRAY_BUFFER, g_overlayVbo);
    g_gl.bufferSubData(GL_ARRAY_BUFFER, 0, sizeof(quadVerts), quadVerts);

    g_gl.useProgram(g_overlayStaticBorderProgram);
    g_gl.uniform1i(g_overlayStaticBorderLocs.shape, 0);
    g_gl.uniform4f(g_overlayStaticBorderLocs.borderColor,
                   border.color.r,
                   border.color.g,
                   border.color.b,
                   border.color.a);
    g_gl.uniform1f(g_overlayStaticBorderLocs.thickness, static_cast<float>(borderThickness));
    g_gl.uniform1f(g_overlayStaticBorderLocs.radius, static_cast<float>(std::max(border.radius, 0)));
    g_gl.uniform2f(g_overlayStaticBorderLocs.size, static_cast<float>(baseW), static_cast<float>(baseH));
    g_gl.uniform2f(g_overlayStaticBorderLocs.quadSize, static_cast<float>(quadW), static_cast<float>(quadH));
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

void RenderModeViewportBorderOverlay(int viewportWidth,
                                     int viewportHeight,
                                     const platform::config::BorderConfig& border,
                                     const ModeViewportRect& modeViewport) {
    if (!modeViewport.valid) {
        return;
    }

    RenderRectBorderOverlay(viewportWidth,
                            viewportHeight,
                            border,
                            modeViewport.x,
                            modeViewport.y,
                            modeViewport.width,
                            modeViewport.height);
}

void RenderBackgroundColorLetterbox(const platform::config::Color& color,
                                    int viewportWidth,
                                    int viewportHeight,
                                    const ModeViewportRect& modeViewport) {
    LetterboxRegion regions[4];
    const int regionCount = BuildLetterboxRegions(viewportWidth, viewportHeight, modeViewport, regions);
    if (regionCount == 0) {
        return;
    }

    const float alpha = std::clamp(color.a, 0.0f, 1.0f);
    if (alpha < 1.0f) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    } else {
        glDisable(GL_BLEND);
    }

    g_gl.useProgram(g_solidColorProgram);
    g_gl.uniform4f(g_solidColorLocColor, color.r, color.g, color.b, alpha);
    for (int i = 0; i < regionCount; ++i) {
        DrawRegionQuad(viewportWidth, viewportHeight, regions[i], false);
    }
    glDisable(GL_BLEND);
}

void RenderBackgroundImageLetterbox(GLuint texture,
                                    int viewportWidth,
                                    int viewportHeight,
                                    const ModeViewportRect& modeViewport) {
    if (texture == 0) {
        return;
    }

    LetterboxRegion regions[4];
    const int regionCount = BuildLetterboxRegions(viewportWidth, viewportHeight, modeViewport, regions);
    if (regionCount == 0) {
        return;
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    g_gl.activeTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);

    g_gl.useProgram(g_overlayProgram);
    g_gl.uniform1i(g_overlayLocScreenTexture, 0);
    g_gl.uniform4f(g_overlayLocSourceRect, 0.0f, 0.0f, 1.0f, 1.0f);
    g_gl.uniform1f(g_overlayLocOpacity, 1.0f);

    glEnable(GL_SCISSOR_TEST);
    for (int i = 0; i < regionCount; ++i) {
        glScissor(regions[i].x, regions[i].y, regions[i].width, regions[i].height);
        DrawFullscreenQuad(viewportWidth, viewportHeight);
    }
    glDisable(GL_SCISSOR_TEST);

    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_BLEND);
}

void RenderBackgroundGradientLetterbox(const platform::config::ModeBackgroundConfig& background,
                                       int viewportWidth,
                                       int viewportHeight,
                                       const ModeViewportRect& modeViewport) {
    if (background.gradientStops.size() < 2) {
        return;
    }

    LetterboxRegion regions[4];
    const int regionCount = BuildLetterboxRegions(viewportWidth, viewportHeight, modeViewport, regions);
    if (regionCount == 0) {
        return;
    }

    const int numStops = std::min(static_cast<int>(background.gradientStops.size()), 8);
    float colors[8 * 4] = {};
    float positions[8] = {};
    for (int i = 0; i < numStops; ++i) {
        const auto& stop = background.gradientStops[static_cast<std::size_t>(i)];
        colors[i * 4 + 0] = stop.color.r;
        colors[i * 4 + 1] = stop.color.g;
        colors[i * 4 + 2] = stop.color.b;
        colors[i * 4 + 3] = stop.color.a;
        positions[i] = std::clamp(stop.position, 0.0f, 1.0f);
    }

    static const auto kGradientStartTime = std::chrono::steady_clock::now();
    const auto now = std::chrono::steady_clock::now();
    const float timeSeconds = std::chrono::duration<float>(now - kGradientStartTime).count();

    glDisable(GL_BLEND);
    g_gl.useProgram(g_gradientProgram);
    g_gl.uniform1i(g_gradientLocs.numStops, numStops);
    g_gl.uniform4fv(g_gradientLocs.stopColors, numStops, colors);
    g_gl.uniform1fv(g_gradientLocs.stopPositions, numStops, positions);
    g_gl.uniform1f(g_gradientLocs.angle, background.gradientAngle * 3.14159265f / 180.0f);
    g_gl.uniform1f(g_gradientLocs.time, timeSeconds);
    g_gl.uniform1i(g_gradientLocs.animationType, static_cast<int>(background.gradientAnimation));
    g_gl.uniform1f(g_gradientLocs.animationSpeed, background.gradientAnimationSpeed);
    g_gl.uniform1i(g_gradientLocs.colorFade, background.gradientColorFade ? 1 : 0);

    glEnable(GL_SCISSOR_TEST);
    for (int i = 0; i < regionCount; ++i) {
        glScissor(regions[i].x, regions[i].y, regions[i].width, regions[i].height);
        DrawFullscreenQuad(viewportWidth, viewportHeight);
    }
    glDisable(GL_SCISSOR_TEST);
}

} // namespace
void RenderGlxMirrorOverlay(int viewportWidth, int viewportHeight) {
    if (!IsGlxMirrorPipelineEnabled()) { return; }
    if (viewportWidth <= 0 || viewportHeight <= 0) { return; }
    if (!EnsureGlFunctions()) { return; }

    std::lock_guard<std::mutex> lock(g_stateMutex);

    auto configSnapshot = platform::config::GetConfigSnapshot();

    // Check for config changes and refresh if needed
    const uint64_t currentVersion = platform::config::GetConfigSnapshotVersion();
    const uint64_t lastVersion = g_lastConfigVersion.load(std::memory_order_relaxed);
    if (currentVersion > lastVersion) {
        std::lock_guard<std::mutex> refreshLock(g_configRefreshMutex);
        g_lastConfigVersion.store(currentVersion, std::memory_order_relaxed);

        // Re-apply current mode to pick up mirror definition changes
        const std::string refreshMode = g_modeState.GetActiveModeName();
        if (!refreshMode.empty() && configSnapshot) {
            ApplyModeSwitchWithResolvedContainer(refreshMode, *configSnapshot, viewportWidth, viewportHeight);
        }

        // Refresh mirror configs from mode state
        g_mirrorConfigs = g_modeState.GetActiveMirrorRenderList();
        g_currentActiveMode = refreshMode;

        if (IsDebugEnabled()) {
            fprintf(stderr, "[Linuxscreen][mirror] Config refreshed (version %llu -> %llu), %zu mirror(s)\n",
                    static_cast<unsigned long long>(lastVersion),
                    static_cast<unsigned long long>(currentVersion),
                    g_mirrorConfigs.size());
        }
    }

    // Check for mode changes and refresh mirror configs
    std::string activeMode = g_modeState.GetActiveModeName();
    if (activeMode != g_currentActiveMode) {
        g_currentActiveMode = activeMode;
        if (!activeMode.empty() && configSnapshot) {
            ApplyModeSwitchWithResolvedContainer(activeMode, *configSnapshot, viewportWidth, viewportHeight);
        }
        g_mirrorConfigs = g_modeState.GetActiveMirrorRenderList();
        if (IsDebugEnabled()) {
            fprintf(stderr, "[Linuxscreen][mirror] Mode changed to '%s', loaded %zu mirror(s)\n",
                    activeMode.empty() ? "<none>" : activeMode.c_str(), g_mirrorConfigs.size());
        }
    }

    const bool viewportSizeChanged = (viewportWidth != g_lastOverlayViewportWidth) || (viewportHeight != g_lastOverlayViewportHeight);
    if (viewportSizeChanged) {
        g_lastOverlayViewportWidth = viewportWidth;
        g_lastOverlayViewportHeight = viewportHeight;
        if (!activeMode.empty() && configSnapshot) {
            ApplyModeSwitchWithResolvedContainer(activeMode, *configSnapshot, viewportWidth, viewportHeight);
            g_mirrorConfigs = g_modeState.GetActiveMirrorRenderList();
            if (IsDebugEnabled()) {
                fprintf(stderr,
                        "[Linuxscreen][mirror] Viewport size changed to %dx%d, refreshed %zu mirror(s)\n",
                        viewportWidth,
                        viewportHeight,
                        g_mirrorConfigs.size());
            }
        }
    }

    if (activeMode != g_lastRenderedBackgroundActiveMode) {
        const std::string previousRenderedMode = g_lastRenderedBackgroundActiveMode;
        std::string selectedSourceMode = activeMode;
        bool usePrevious = false;
        if (!previousRenderedMode.empty() && configSnapshot) {
            const auto* previousMode = FindModeConfigByName(*configSnapshot, previousRenderedMode);
            const auto* nextMode = FindModeConfigByName(*configSnapshot, activeMode);
            if (previousMode && nextMode && ShouldUsePreviousModeBackground(*previousMode, *nextMode)) {
                selectedSourceMode = previousMode->name;
                usePrevious = true;
            }
        }

        g_stickyBackgroundModeName = selectedSourceMode;
        g_stickyBackgroundPending = !selectedSourceMode.empty();
        g_lastRenderedBackgroundActiveMode = activeMode;

        if (IsDebugEnabled()) {
            fprintf(stderr,
                    "[Linuxscreen][mirror] Background source on switch '%s' -> '%s': %s\n",
                    previousRenderedMode.empty() ? "<none>" : previousRenderedMode.c_str(),
                    activeMode.empty() ? "<none>" : activeMode.c_str(),
                    usePrevious ? "previous" : "active");
        }
    }

    GlStateSnapshot savedState;
    SaveGlState(savedState);

    if (!g_overlayGeomReady) {
        g_gl.genVertexArrays(1, &g_overlayVao);
        g_gl.genBuffers(1, &g_overlayVbo);
        g_gl.bindVertexArray(g_overlayVao);
        g_gl.bindBuffer(GL_ARRAY_BUFFER, g_overlayVbo);
        g_gl.bufferData(GL_ARRAY_BUFFER, 6 * 4 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
        g_gl.enableVertexAttribArray(0);
        g_gl.vertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
        g_gl.enableVertexAttribArray(1);
        g_gl.vertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
        g_gl.bindVertexArray(0);
        g_overlayGeomReady = true;
    }

    if (!EnsureOverlayProgramsReady()) {
        RestoreGlState(savedState);
        return;
    }

    if (!g_overlayVao || !g_overlayProgram) {
        RestoreGlState(savedState);
        return;
    }

    ModeViewportRect modeViewportRect;
    (void)ResolveModeViewportRect(viewportWidth, viewportHeight, modeViewportRect);

    g_gl.bindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, viewportWidth, viewportHeight);

    DrainDecodedModeBackgroundImages();

    const platform::config::ModeConfig* activeModeConfig = nullptr;
    const platform::config::ModeConfig* sourceModeConfig = nullptr;
    if (configSnapshot) {
        activeModeConfig = FindModeConfigByName(*configSnapshot, activeMode);

        std::string sourceModeName = activeMode;
        if (g_stickyBackgroundPending && !g_stickyBackgroundModeName.empty()) {
            sourceModeName = g_stickyBackgroundModeName;
        }
        sourceModeConfig = FindModeConfigByName(*configSnapshot, sourceModeName);
        if (!sourceModeConfig) {
            sourceModeConfig = activeModeConfig;
        }
    }

    if (activeModeConfig && activeModeConfig->background.selectedMode == "image") {
        EnsureModeBackgroundImageRequested(activeModeConfig->name, activeModeConfig->background);
    }
    if (sourceModeConfig &&
        sourceModeConfig != activeModeConfig &&
        sourceModeConfig->background.selectedMode == "image") {
        EnsureModeBackgroundImageRequested(sourceModeConfig->name, sourceModeConfig->background);
    }

    // Draw mode background in letterbox regions before mirrors.
    if (sourceModeConfig) {
        const auto& background = sourceModeConfig->background;
        const std::string selectedMode = background.selectedMode;

        bool renderedBackground = false;
        if (selectedMode == "image") {
            auto imageIt = g_modeBackgroundImages.find(sourceModeConfig->name);
            if (imageIt != g_modeBackgroundImages.end()) {
                const GLuint texture = GetModeBackgroundTexture(imageIt->second);
                if (texture != 0) {
                    RenderBackgroundImageLetterbox(texture, viewportWidth, viewportHeight, modeViewportRect);
                    renderedBackground = true;
                }
            }
            if (!renderedBackground && background.gradientStops.size() >= 2) {
                RenderBackgroundGradientLetterbox(background, viewportWidth, viewportHeight, modeViewportRect);
                renderedBackground = true;
            }
            if (!renderedBackground) {
                RenderBackgroundColorLetterbox(background.color, viewportWidth, viewportHeight, modeViewportRect);
            }
        } else if (selectedMode == "gradient") {
            if (background.gradientStops.size() >= 2) {
                RenderBackgroundGradientLetterbox(background, viewportWidth, viewportHeight, modeViewportRect);
                renderedBackground = true;
            }
            if (!renderedBackground) {
                RenderBackgroundColorLetterbox(background.color, viewportWidth, viewportHeight, modeViewportRect);
            }
        } else {
            RenderBackgroundColorLetterbox(background.color, viewportWidth, viewportHeight, modeViewportRect);
        }
    }

    if (activeModeConfig &&
        modeViewportRect.valid &&
        activeModeConfig->border.enabled &&
        activeModeConfig->border.width > 0) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        RenderModeViewportBorderOverlay(viewportWidth, viewportHeight, activeModeConfig->border, modeViewportRect);
        g_gl.bindVertexArray(0);
        glDisable(GL_BLEND);
    }

    g_stickyBackgroundPending = false;

    // On Linux, the worker thread uses a separate GL context. Insert a GPU-side
    // wait on the worker's publish fence so the GPU won't read mirror textures
    // until the worker's rendering is complete. glWaitSync does NOT block the
    // CPU — it only inserts a dependency in the GPU command queue.
#ifndef __APPLE__
    {
        std::lock_guard<std::mutex> lock(g_publishFenceMutex);
        if (g_publishFence && g_gl.waitSync) {
            g_gl.waitSync(g_publishFence, 0, GL_TIMEOUT_IGNORED);
        }
    }
#endif

    int renderedCount = 0;
    std::vector<PendingStaticMirrorBorder> pendingStaticBorders;
    pendingStaticBorders.reserve(g_mirrorConfigs.size());

    // Set up common GL state once before the mirror loop.
    g_gl.bindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, viewportWidth, viewportHeight);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    g_gl.activeTexture(GL_TEXTURE0);
    g_gl.useProgram(g_overlayProgram);
    g_gl.uniform1i(g_overlayLocScreenTexture, 0);
    g_gl.uniform4f(g_overlayLocSourceRect, 0.0f, 0.0f, 1.0f, 1.0f);
    g_gl.bindVertexArray(g_overlayVao);
    g_gl.bindBuffer(GL_ARRAY_BUFFER, g_overlayVbo);

    for (const auto& mirrorRender : g_mirrorConfigs) {
        const auto& config = mirrorRender.config;
        auto it = g_instances.find(config.name);
        if (it == g_instances.end()) { continue; }
        const X11MirrorInstance& inst = it->second;
        if (!inst.hasValidContent || !inst.finalFbo[0] || !inst.finalTexture[0]) { continue; }

        const int outW = inst.finalW;
        const int outH = inst.finalH;
        if (outW <= 0 || outH <= 0) { continue; }

        int screenX, screenY;
        ResolveMirrorAnchorCoords(
            config.output.relativeTo,
            config.output.x,
            config.output.y,
            outW,
            outH,
            viewportWidth,
            viewportHeight,
            modeViewportRect,
            screenX,
            screenY
        );
        const int destX = screenX;
        const int destY = viewportHeight - screenY - outH;

        int f = inst.frontIdx.load(std::memory_order_acquire);

        glBindTexture(GL_TEXTURE_2D, inst.finalTexture[f]);
        g_gl.uniform1f(g_overlayLocOpacity, config.opacity);

        float ndcL = (2.0f * static_cast<float>(destX) / static_cast<float>(viewportWidth)) - 1.0f;
        float ndcR = (2.0f * static_cast<float>(destX + outW) / static_cast<float>(viewportWidth)) - 1.0f;
        float ndcB = (2.0f * static_cast<float>(destY) / static_cast<float>(viewportHeight)) - 1.0f;
        float ndcT = (2.0f * static_cast<float>(destY + outH) / static_cast<float>(viewportHeight)) - 1.0f;

        float quadVerts[] = {
            ndcL, ndcB,  0.0f, 0.0f,
            ndcR, ndcB,  1.0f, 0.0f,
            ndcR, ndcT,  1.0f, 1.0f,

            ndcL, ndcB,  0.0f, 0.0f,
            ndcR, ndcT,  1.0f, 1.0f,
            ndcL, ndcT,  0.0f, 1.0f,
        };

        g_gl.bufferSubData(GL_ARRAY_BUFFER, 0, sizeof(quadVerts), quadVerts);
        glDrawArrays(GL_TRIANGLES, 0, 6);

        if (config.border.type == platform::config::MirrorBorderType::Static &&
            config.border.staticThickness > 0) {
            pendingStaticBorders.push_back(PendingStaticMirrorBorder{ &config, screenX, screenY, outW, outH, inst.hasFrameContent });
        }
        renderedCount++;
    }
    g_gl.bindVertexArray(0);
    glDisable(GL_BLEND);

    if (!pendingStaticBorders.empty()) {
        g_gl.bindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, viewportWidth, viewportHeight);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        for (const PendingStaticMirrorBorder& pending : pendingStaticBorders) {
            if (!pending.hasFrameContent) {
                continue;
            }
            RenderStaticMirrorBorderOverlay(viewportWidth, viewportHeight, pending);
        }
        g_gl.bindVertexArray(0);
        glDisable(GL_BLEND);
    }

    if (IsDebugEnabled()) {
        static int renderCount = 0;
        if ((++renderCount % 60) == 0) {
            fprintf(stderr, "[Linuxscreen][mirror] Render overlay: viewport=%dx%d, rendered %d mirrors\n",
                    viewportWidth, viewportHeight, renderedCount);
        }
    }

    RestoreGlState(savedState);
}

void ShutdownGlxMirrorPipeline() {
    StopMirrorWorker();
    StopBackgroundDecodeWorker();

    std::lock_guard<std::mutex> lock(g_stateMutex);
#ifdef __APPLE__
    if (g_glReady.load(std::memory_order_acquire) && CGLGetCurrentContext() != nullptr) {
#else
    if (g_glReady.load(std::memory_order_acquire) && glXGetCurrentContext() != nullptr) {
#endif
        DestroyAllInstances();
        CleanupMirrorShaders();
        ClearAllModeBackgroundGpuTextures();
        if (g_gameFrameFbo) { g_gl.deleteFramebuffers(1, &g_gameFrameFbo); g_gameFrameFbo = 0; }
        if (g_gameFrameTexture) { glDeleteTextures(1, &g_gameFrameTexture); g_gameFrameTexture = 0; }
        if (g_overlayVao) { g_gl.deleteVertexArrays(1, &g_overlayVao); g_overlayVao = 0; }
        if (g_overlayVbo) { g_gl.deleteBuffers(1, &g_overlayVbo); g_overlayVbo = 0; }
        if (g_overlayProgram) { g_gl.deleteProgram(g_overlayProgram); g_overlayProgram = 0; }
        if (g_solidColorProgram) { g_gl.deleteProgram(g_solidColorProgram); g_solidColorProgram = 0; }
        if (g_gradientProgram) { g_gl.deleteProgram(g_gradientProgram); g_gradientProgram = 0; }
        if (g_overlayStaticBorderProgram) { g_gl.deleteProgram(g_overlayStaticBorderProgram); g_overlayStaticBorderProgram = 0; }
    } else {
        g_instances.clear();
        g_modeBackgroundImages.clear();
        g_gameFrameTexture = 0;
        g_gameFrameFbo = 0;
    }
    g_gameFrameW = 0;
    g_gameFrameH = 0;
    g_mirrorConfigs.clear();
    g_configsLoaded = false;
    g_lastOverlayViewportWidth = 0;
    g_lastOverlayViewportHeight = 0;
    g_workerStarted.store(false, std::memory_order_release);
    g_overlayGeomReady = false;
    g_overlayProgramReady = false;
    g_solidColorProgramReady = false;
    g_gradientProgramReady = false;
    g_overlayStaticBorderProgramReady = false;
    g_solidColorLocColor = -1;
    g_gradientLocs = GradientShaderLocs{};
    g_overlayStaticBorderLocs = StaticBorderShaderLocs{};
    g_stickyBackgroundModeName.clear();
    g_stickyBackgroundPending = false;
    g_lastRenderedBackgroundActiveMode.clear();
}

void ShutdownGlxMirrorPipelineForProcessExit() {
    g_stopWorker.store(true, std::memory_order_release);
    g_slotCV.notify_all();

    if (g_workerThread.joinable()) {
        // Wait briefly for the worker to exit cleanly before detaching.
        // Without this, the thread may still be accessing global mutexes
        // when static destructors run, causing EINVAL on mutex lock.
        bool workerExited = false;
        {
            std::unique_lock<std::mutex> lock(g_workerExitMutex);
            workerExited = g_workerExitCV.wait_for(lock, std::chrono::seconds(2),
                                                   []() { return g_workerExited; });
        }
        if (workerExited) {
            g_workerThread.join();
        } else {
            std::fprintf(stderr,
                         "[Linuxscreen][mirror][WARNING] worker thread did not exit within process shutdown timeout; detaching\n");
            g_workerThread.detach();
        }
    }

    g_backgroundDecodeStop.store(true, std::memory_order_release);
    g_backgroundDecodeCv.notify_all();
    if (g_backgroundDecodeThread.joinable()) {
        // Join (not detach) so the thread fully exits before static destructors
        // destroy g_backgroundDecodeMutex, preventing EINVAL on mutex lock.
        g_backgroundDecodeThread.join();
    }
    g_backgroundDecodeStarted.store(false, std::memory_order_release);
    {
        std::lock_guard<std::mutex> decodeLock(g_backgroundDecodeMutex);
        g_backgroundDecodeRequests.clear();
        g_decodedModeBackgroundImages.clear();
    }

    std::lock_guard<std::mutex> lock(g_stateMutex);
    g_instances.clear();
    g_modeBackgroundImages.clear();
    g_gameFrameTexture = 0;
    g_gameFrameFbo = 0;
    g_gameFrameW = 0;
    g_gameFrameH = 0;
    g_mirrorConfigs.clear();
    g_configsLoaded = false;
    g_lastOverlayViewportWidth = 0;
    g_lastOverlayViewportHeight = 0;
    g_shaders = MirrorShaderPrograms{};
    g_workerStarted.store(false, std::memory_order_release);
    g_overlayVao = 0;
    g_overlayVbo = 0;
    g_overlayProgram = 0;
    g_solidColorProgram = 0;
    g_gradientProgram = 0;
    g_overlayStaticBorderProgram = 0;
    g_overlayGeomReady = false;
    g_overlayProgramReady = false;
    g_solidColorProgramReady = false;
    g_gradientProgramReady = false;
    g_overlayStaticBorderProgramReady = false;
    g_solidColorLocColor = -1;
    g_gradientLocs = GradientShaderLocs{};
    g_overlayStaticBorderLocs = StaticBorderShaderLocs{};
    g_stickyBackgroundModeName.clear();
    g_stickyBackgroundPending = false;
    g_lastRenderedBackgroundActiveMode.clear();
}

MirrorModeState& GetMirrorModeState() {
    return g_modeState;
}

void RenderGlxEyeZoomOverlay(int viewportWidth, int viewportHeight) {
    if (!IsGlxMirrorPipelineEnabled()) { return; }
    if (viewportWidth <= 0 || viewportHeight <= 0) { return; }
    if (!EnsureGlFunctions()) { return; }

    std::lock_guard<std::mutex> lock(g_stateMutex);
    
    std::string activeMode = g_modeState.GetActiveModeName();

    if (activeMode != "EyeZoom") {
        return;
    }

    auto configSnap = platform::config::GetConfigSnapshot();
    if (!configSnap) return;
    const auto& zoomConfig = configSnap->eyezoom;
    const platform::config::ModeConfig* activeModeConfig = FindModeConfigByName(*configSnap, activeMode);
    int cloneWidth = zoomConfig.cloneWidth;
    if (cloneWidth < 2) cloneWidth = 2;
    if (cloneWidth % 2 != 0) cloneWidth = (cloneWidth / 2) * 2;

    if (g_gameFrameTexture == 0 || g_gameFrameW <= 0 || g_gameFrameH <= 0) {
        return;
    }

    GlStateSnapshot savedState;
    SaveGlState(savedState);

    if (!g_overlayGeomReady) {
        g_gl.genVertexArrays(1, &g_overlayVao);
        g_gl.genBuffers(1, &g_overlayVbo);
        g_gl.bindVertexArray(g_overlayVao);
        g_gl.bindBuffer(GL_ARRAY_BUFFER, g_overlayVbo);
        g_gl.bufferData(GL_ARRAY_BUFFER, 6 * 4 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
        g_gl.enableVertexAttribArray(0);
        g_gl.vertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
        g_gl.enableVertexAttribArray(1);
        g_gl.vertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
        g_gl.bindVertexArray(0);
        g_overlayGeomReady = true;
    }

    if (!EnsureOverlayProgramsReady()) {
        RestoreGlState(savedState);
        return;
    }

    if (!g_overlayVao || !g_overlayProgram || !g_solidColorProgram) {
        RestoreGlState(savedState);
        return;
    }

    ModeViewportRect modeViewportRect;
    (void)ResolveModeViewportRect(viewportWidth, viewportHeight, modeViewportRect);

    const std::string outputRelativeTo = zoomConfig.outputRelativeTo.empty() ? "middleLeftScreen" : zoomConfig.outputRelativeTo;
    int outputPositionContainerWidth = viewportWidth;
    int outputPositionContainerHeight = viewportHeight;
    if (modeViewportRect.valid && ShouldUseViewportAnchor(outputRelativeTo)) {
        outputPositionContainerWidth = modeViewportRect.width;
        outputPositionContainerHeight = modeViewportRect.height;
    }

    int outputX = zoomConfig.outputX;
    int outputY = zoomConfig.outputY;
    if (zoomConfig.outputUseRelativePosition) {
        outputX = static_cast<int>(zoomConfig.outputRelativeX * static_cast<float>(outputPositionContainerWidth));
        outputY = static_cast<int>(zoomConfig.outputRelativeY * static_cast<float>(outputPositionContainerHeight));
    }

    int zoomOutputWidth = ResolveEyeZoomOutputWidth(zoomConfig, viewportWidth, viewportHeight, modeViewportRect, outputX);
    if (zoomOutputWidth <= 1) {
        RestoreGlState(savedState);
        return;
    }

    int zoomOutputHeight = ResolveEyeZoomOutputHeight(zoomConfig, viewportWidth, viewportHeight, modeViewportRect);
    if (zoomOutputHeight <= 1) {
        RestoreGlState(savedState);
        return;
    }

    int zoomX = zoomConfig.horizontalMargin;
    int zoomYTop = zoomConfig.verticalMargin;
    if (!zoomConfig.outputRelativeTo.empty()) {
        ResolveMirrorAnchorCoords(zoomConfig.outputRelativeTo,
                                  outputX,
                                  outputY,
                                  zoomOutputWidth,
                                  zoomOutputHeight,
                                  viewportWidth,
                                  viewportHeight,
                                  modeViewportRect,
                                  zoomX,
                                  zoomYTop);
    }
    int zoomY_gl = viewportHeight - zoomYTop - zoomOutputHeight;
    const int maxSnapshotSize = g_maxTextureSize > 0 ? g_maxTextureSize : 16384;
    const int snapshotAllocW = std::min(zoomOutputWidth, maxSnapshotSize);
    const int snapshotAllocH = std::min(zoomOutputHeight, maxSnapshotSize);
    if (snapshotAllocW != zoomOutputWidth || snapshotAllocH != zoomOutputHeight) {
        fprintf(stderr, "[Linuxscreen][mirror] WARNING: EyeZoom snapshot allocation %dx%d exceeds GL_MAX_TEXTURE_SIZE (%d), clamping to %dx%d\n",
                zoomOutputWidth, zoomOutputHeight, maxSnapshotSize, snapshotAllocW, snapshotAllocH);
    }

    g_gl.bindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, viewportWidth, viewportHeight);

    int srcCenterX = g_gameFrameW / 2;
    int srcLeft = srcCenterX - cloneWidth / 2;
    int srcRight = srcCenterX + cloneWidth / 2;
    int srcCenterY = g_gameFrameH / 2;

    if (IsOverscanActive()) {
        const auto dims = GetOverscanDimensions();
        if (dims.totalWidth == g_gameFrameW && dims.totalHeight == g_gameFrameH &&
            dims.windowWidth > 0 && dims.windowHeight > 0) {
            srcCenterX = dims.marginLeft + (dims.windowWidth / 2);
            srcCenterY = dims.marginBottom + (dims.windowHeight / 2);
        }
    }

    int srcBottom = srcCenterY - zoomConfig.cloneHeight / 2;
    int srcTop = srcCenterY + zoomConfig.cloneHeight / 2;

    srcLeft = std::max(0, srcLeft);
    srcBottom = std::max(0, srcBottom);
    srcRight = std::min(g_gameFrameW, srcRight);
    srcTop = std::min(g_gameFrameH, srcTop);
    if (srcRight <= srcLeft || srcTop <= srcBottom) {
        RestoreGlState(savedState);
        return;
    }

    glDisable(GL_BLEND);
    g_gl.activeTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_gameFrameTexture);

    g_gl.useProgram(g_overlayProgram);
    g_gl.uniform1i(g_overlayLocScreenTexture, 0);
    g_gl.uniform1f(g_overlayLocOpacity, 1.0f);

    float sx = (float)srcLeft / g_gameFrameW;
    float sy = (float)srcBottom / g_gameFrameH;
    float sw = (float)(srcRight - srcLeft) / g_gameFrameW;
    float sh = (float)(srcTop - srcBottom) / g_gameFrameH;
    g_gl.uniform4f(g_overlayLocSourceRect, sx, sy, sw, sh);

    float ndcL = (2.0f * (float)zoomX / viewportWidth) - 1.0f;
    float ndcR = (2.0f * (float)(zoomX + zoomOutputWidth) / viewportWidth) - 1.0f;
    float ndcB = (2.0f * (float)zoomY_gl / viewportHeight) - 1.0f;
    float ndcT = (2.0f * (float)(zoomY_gl + zoomOutputHeight) / viewportHeight) - 1.0f;

    float quadVerts[] = {
        ndcL, ndcB, 0.0f, 0.0f,
        ndcR, ndcB, 1.0f, 0.0f,
        ndcR, ndcT, 1.0f, 1.0f,
        ndcL, ndcB, 0.0f, 0.0f,
        ndcR, ndcT, 1.0f, 1.0f,
        ndcL, ndcT, 0.0f, 1.0f,
    };

    g_gl.bindVertexArray(g_overlayVao);
    g_gl.bindBuffer(GL_ARRAY_BUFFER, g_overlayVbo);
    g_gl.bufferSubData(GL_ARRAY_BUFFER, 0, sizeof(quadVerts), quadVerts);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    g_gl.useProgram(g_solidColorProgram);
    g_gl.bindVertexArray(g_overlayVao);
    g_gl.bindBuffer(GL_ARRAY_BUFFER, g_overlayVbo);

    float pixelWidthOnScreen = zoomOutputWidth / (float)cloneWidth;
    int labelsPerSide = cloneWidth / 2;
    int overlayLabelsPerSide = zoomConfig.overlayWidth;
    if (overlayLabelsPerSide < 0) overlayLabelsPerSide = labelsPerSide;
    if (overlayLabelsPerSide > labelsPerSide) overlayLabelsPerSide = labelsPerSide;

    float centerY = zoomY_gl + zoomOutputHeight / 2.0f;
    float boxHeight = (float)zoomConfig.rectHeight;
    if (zoomConfig.linkRectToFont) {
        float linkedFontSize = (float)zoomConfig.textFontSize;
        if (zoomConfig.autoFontSize) {
            linkedFontSize = pixelWidthOnScreen * 0.90f;
            if (linkedFontSize < 6.0f) linkedFontSize = 6.0f;
        } else if (linkedFontSize < 1.0f) {
            linkedFontSize = 1.0f;
        }
        boxHeight = linkedFontSize * 1.2f;
    }

    for (int xOffset = -overlayLabelsPerSide; xOffset <= overlayLabelsPerSide; xOffset++) {
        if (xOffset == 0) continue;

        int boxIndex = xOffset + labelsPerSide - (xOffset > 0 ? 1 : 0);
        float boxLeft = zoomX + (boxIndex * pixelWidthOnScreen);
        float boxRight = boxLeft + pixelWidthOnScreen;
        float boxBottom = centerY - boxHeight / 2.0f;
        float boxTop = centerY + boxHeight / 2.0f;

        auto boxColor = (boxIndex % 2 == 0) ? zoomConfig.gridColor1 : zoomConfig.gridColor2;
        float boxOpacity = (boxIndex % 2 == 0) ? zoomConfig.gridColor1Opacity : zoomConfig.gridColor2Opacity;
        g_gl.uniform4f(g_solidColorLocColor, boxColor.r, boxColor.g, boxColor.b, boxColor.a * boxOpacity);

        float boxNdcL = (boxLeft / (float)viewportWidth) * 2.0f - 1.0f;
        float boxNdcR = (boxRight / (float)viewportWidth) * 2.0f - 1.0f;
        float boxNdcB = (boxBottom / (float)viewportHeight) * 2.0f - 1.0f;
        float boxNdcT = (boxTop / (float)viewportHeight) * 2.0f - 1.0f;

        float boxVerts[] = {
            boxNdcL, boxNdcB, 0, 0, boxNdcR, boxNdcB, 0, 0, boxNdcR, boxNdcT, 0, 0,
            boxNdcL, boxNdcB, 0, 0, boxNdcR, boxNdcT, 0, 0, boxNdcL, boxNdcT, 0, 0,
        };
        g_gl.bufferSubData(GL_ARRAY_BUFFER, 0, sizeof(boxVerts), boxVerts);
        glDrawArrays(GL_TRIANGLES, 0, 6);
    }

    float centerX = zoomX + zoomOutputWidth / 2.0f;
    float centerLineWidth = 2.0f;
    float lineLeft = centerX - centerLineWidth / 2.0f;
    float lineRight = centerX + centerLineWidth / 2.0f;

    float lineNdcL = (lineLeft / (float)viewportWidth) * 2.0f - 1.0f;
    float lineNdcR = (lineRight / (float)viewportWidth) * 2.0f - 1.0f;
    float lineNdcB = ((float)zoomY_gl / (float)viewportHeight) * 2.0f - 1.0f;
    float lineNdcT = ((float)(zoomY_gl + zoomOutputHeight) / (float)viewportHeight) * 2.0f - 1.0f;

    g_gl.uniform4f(g_solidColorLocColor, zoomConfig.centerLineColor.r, zoomConfig.centerLineColor.g, zoomConfig.centerLineColor.b, zoomConfig.centerLineColor.a * zoomConfig.centerLineColorOpacity);
    float centerLineVerts[] = {
        lineNdcL, lineNdcB, 0, 0, lineNdcR, lineNdcB, 0, 0, lineNdcR, lineNdcT, 0, 0,
        lineNdcL, lineNdcB, 0, 0, lineNdcR, lineNdcT, 0, 0, lineNdcL, lineNdcT, 0, 0,
    };
    g_gl.bufferSubData(GL_ARRAY_BUFFER, 0, sizeof(centerLineVerts), centerLineVerts);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    if (activeModeConfig &&
        activeModeConfig->border.enabled &&
        activeModeConfig->border.width > 0) {
        RenderRectBorderOverlay(viewportWidth,
                                viewportHeight,
                                activeModeConfig->border,
                                zoomX,
                                zoomYTop,
                                zoomOutputWidth,
                                zoomOutputHeight);
    }

    glDisable(GL_BLEND);
    RestoreGlState(savedState);
}

} // namespace platform::x11
