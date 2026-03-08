bool IsGlxMirrorPipelineEnabledInternal();

static bool ResolveOverscanTargetForActiveMode(int containerWidth,
                                               int containerHeight,
                                               int& outTargetWidth,
                                               int& outTargetHeight) {
    outTargetWidth = 0;
    outTargetHeight = 0;

    if (containerWidth <= 0 || containerHeight <= 0) {
        return false;
    }

    if (!g_modeState.GetActiveModeTargetDimensions(containerWidth, containerHeight, outTargetWidth, outTargetHeight)) {
        return false;
    }

    if (outTargetWidth <= 0 || outTargetHeight <= 0) {
        outTargetWidth = 0;
        outTargetHeight = 0;
        return false;
    }

    return outTargetWidth > containerWidth || outTargetHeight > containerHeight;
}

static void DestroyOverscanFbo() {
    g_overscanFboRendered = false;
    if (g_overscanFbo) {
        g_gl.deleteFramebuffers(1, &g_overscanFbo);
        g_overscanFbo = 0;
    }
    if (g_overscanColorTex) {
        glDeleteTextures(1, &g_overscanColorTex);
        g_overscanColorTex = 0;
    }
    if (g_overscanDepthRb) {
        g_gl.deleteRenderbuffers(1, &g_overscanDepthRb);
        g_overscanDepthRb = 0;
    }
    g_overscanActive.store(false, std::memory_order_release);
    g_overscanDims = {};
}

static bool CreateOverscanFbo(int windowW, int windowH, int mL, int mR, int mT, int mB) {
    if (!EnsureGlFunctions()) { return false; }
    if (!g_gl.genRenderbuffers || !g_gl.bindRenderbuffer || !g_gl.renderbufferStorage ||
        !g_gl.framebufferRenderbuffer || !g_gl.checkFramebufferStatus) {
        if (IsDebugEnabled()) {
            fprintf(stderr, "[Linuxscreen][overscan] Missing renderbuffer GL functions; overscan disabled\n");
        }
        return false;
    }

    // Clamp margins so that total dimensions stay within GL_MAX_TEXTURE_SIZE
    const int maxSz = g_maxTextureSize > 0 ? g_maxTextureSize : 16384;
    {
        const int budgetW = std::max(0, maxSz - windowW);
        const int budgetH = std::max(0, maxSz - windowH);
        if (mL + mR > budgetW) {
            const int oldL = mL, oldR = mR;
            // Scale both sides proportionally
            if (mL + mR > 0) {
                const float ratio = static_cast<float>(budgetW) / static_cast<float>(mL + mR);
                mL = static_cast<int>(static_cast<float>(mL) * ratio);
                mR = budgetW - mL;
            } else {
                mL = 0; mR = 0;
            }
            fprintf(stderr, "[Linuxscreen][overscan] WARNING: Horizontal margins L%d+R%d exceed "
                    "GL_MAX_TEXTURE_SIZE (%d) for window width %d, clamped to L%d+R%d\n",
                    oldL, oldR, maxSz, windowW, mL, mR);
        }
        if (mT + mB > budgetH) {
            const int oldT = mT, oldB = mB;
            if (mT + mB > 0) {
                const float ratio = static_cast<float>(budgetH) / static_cast<float>(mT + mB);
                mT = static_cast<int>(static_cast<float>(mT) * ratio);
                mB = budgetH - mT;
            } else {
                mT = 0; mB = 0;
            }
            fprintf(stderr, "[Linuxscreen][overscan] WARNING: Vertical margins T%d+B%d exceed "
                    "GL_MAX_TEXTURE_SIZE (%d) for window height %d, clamped to T%d+B%d\n",
                    oldT, oldB, maxSz, windowH, mT, mB);
        }
    }

    const int totalW = windowW + mL + mR;
    const int totalH = windowH + mT + mB;

    // Save GL state we'll touch
    GLint prevTex = 0, prevFbo = 0, prevRb = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevFbo);
    glGetIntegerv(GL_RENDERBUFFER_BINDING, &prevRb);

    // Create color texture
    glGenTextures(1, &g_overscanColorTex);
    glBindTexture(GL_TEXTURE_2D, g_overscanColorTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, totalW, totalH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    // Create depth+stencil renderbuffer
    g_gl.genRenderbuffers(1, &g_overscanDepthRb);
    g_gl.bindRenderbuffer(GL_RENDERBUFFER, g_overscanDepthRb);
    g_gl.renderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, totalW, totalH);

    // Create FBO and attach
    g_gl.genFramebuffers(1, &g_overscanFbo);
    g_gl.bindFramebuffer(GL_FRAMEBUFFER, g_overscanFbo);
    g_gl.framebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_overscanColorTex, 0);
    g_gl.framebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, g_overscanDepthRb);

    GLenum status = g_gl.checkFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "[Linuxscreen][overscan] FBO incomplete (status=0x%x); overscan disabled\n", status);
        g_gl.bindFramebuffer(GL_FRAMEBUFFER, prevFbo);
        glBindTexture(GL_TEXTURE_2D, prevTex);
        g_gl.bindRenderbuffer(GL_RENDERBUFFER, prevRb);
        DestroyOverscanFbo();
        return false;
    }

    // Restore GL state
    g_gl.bindFramebuffer(GL_FRAMEBUFFER, prevFbo);
    glBindTexture(GL_TEXTURE_2D, prevTex);
    g_gl.bindRenderbuffer(GL_RENDERBUFFER, prevRb);

    g_overscanDims.totalWidth  = totalW;
    g_overscanDims.totalHeight = totalH;
    g_overscanDims.windowWidth  = windowW;
    g_overscanDims.windowHeight = windowH;
    g_overscanDims.marginLeft   = mL;
    g_overscanDims.marginRight  = mR;
    g_overscanDims.marginTop    = mT;
    g_overscanDims.marginBottom = mB;
    g_overscanFboRendered = false;
    g_overscanActive.store(true, std::memory_order_release);

    if (IsDebugEnabled()) {
        fprintf(stderr, "[Linuxscreen][overscan] Created overscan FBO %u: %dx%d "
                "(window %dx%d + L%d R%d T%d B%d margins)\n",
                g_overscanFbo, totalW, totalH, windowW, windowH, mL, mR, mT, mB);
    }
    return true;
}

GLuint GetOverscanFboIdInternal() {
    return g_overscanActive.load(std::memory_order_acquire) ? g_overscanFbo : 0;
}

bool IsOverscanActiveInternal() {
    return g_overscanActive.load(std::memory_order_acquire);
}

OverscanDimensions GetOverscanDimensionsInternal() {
    return g_overscanDims;
}

bool IsOverscanFboRenderedInternal() {
    return g_overscanFboRendered;
}

void MarkOverscanFboRenderedInternal() {
    g_overscanFboRendered = true;
}

bool UpdateOverscanStateInternal(int windowWidth, int windowHeight) {
    if (!IsGlxMirrorPipelineEnabledInternal()) {
        if (g_overscanActive.load(std::memory_order_acquire)) { DestroyOverscanFbo(); }
        return false;
    }

    int targetWidth = 0;
    int targetHeight = 0;
    const bool needsOverscan = ResolveOverscanTargetForActiveMode(windowWidth,
                                                                  windowHeight,
                                                                  targetWidth,
                                                                  targetHeight);

    if (!needsOverscan) {
        if (g_overscanActive.load(std::memory_order_acquire)) {
            DestroyOverscanFbo();
            if (IsDebugEnabled()) {
                fprintf(stderr, "[Linuxscreen][overscan] Overscan disabled (active mode fits window)\n");
            }
        }
        return false;
    }

    constexpr int marginTop = 0;
    constexpr int marginBottom = 0;
    constexpr int marginLeft = 0;
    constexpr int marginRight = 0;

    if (g_overscanActive.load(std::memory_order_acquire) &&
        g_overscanDims.windowWidth  == targetWidth  &&
        g_overscanDims.windowHeight == targetHeight &&
        g_overscanDims.marginTop    == marginTop    &&
        g_overscanDims.marginBottom == marginBottom &&
        g_overscanDims.marginLeft   == marginLeft   &&
        g_overscanDims.marginRight  == marginRight) {
        return true;
    }

    if (g_overscanActive.load(std::memory_order_acquire)) {
        DestroyOverscanFbo();
    }

    return CreateOverscanFbo(targetWidth, targetHeight, marginLeft, marginRight, marginTop, marginBottom);
}

void BlitOverscanToWindowInternal(int dstX,
                                  int dstY,
                                  int dstWidth,
                                  int dstHeight,
                                  int surfaceWidth,
                                  int surfaceHeight) {
    if (!g_overscanActive.load(std::memory_order_acquire)) { return; }
    if (!g_overscanFboRendered) { return; }
    if (!g_gl.blitFramebuffer || !g_gl.bindFramebuffer) { return; }
    if (dstWidth <= 0 || dstHeight <= 0) { return; }
    if (surfaceWidth <= 0 || surfaceHeight <= 0) { return; }

    const int ml = g_overscanDims.marginLeft;
    const int mb = g_overscanDims.marginBottom;
    const int ww = g_overscanDims.windowWidth;
    const int wh = g_overscanDims.windowHeight;

    int dstL = dstX;
    int dstB = dstY;
    int dstR = dstX + dstWidth;
    int dstT = dstY + dstHeight;

    int clipL = std::max(dstL, 0);
    int clipB = std::max(dstB, 0);
    int clipR = std::min(dstR, surfaceWidth);
    int clipT = std::min(dstT, surfaceHeight);
    if (clipL >= clipR || clipB >= clipT) {
        return;
    }

    const int srcL = ml + (clipL - dstL);
    const int srcB = mb + (clipB - dstB);
    const int srcR = srcL + (clipR - clipL);
    const int srcT = srcB + (clipT - clipB);

    const int srcCenterL = ml;
    const int srcCenterB = mb;
    const int srcCenterR = ml + ww;
    const int srcCenterT = mb + wh;
    if (srcL < srcCenterL || srcB < srcCenterB || srcR > srcCenterR || srcT > srcCenterT) {
        return;
    }

    // Save current FBO bindings
    GLint prevReadFbo = 0, prevDrawFbo = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevReadFbo);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevDrawFbo);

    // Blit from overscan FBO (center game region) to default FBO 0
    g_gl.bindFramebuffer(GL_READ_FRAMEBUFFER, g_overscanFbo);
    g_gl.bindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    g_gl.blitFramebuffer(
        srcL, srcB, srcR, srcT,
        clipL, clipB, clipR, clipT,
        GL_COLOR_BUFFER_BIT, GL_NEAREST
    );

    // Restore FBO bindings
    g_gl.bindFramebuffer(GL_READ_FRAMEBUFFER, prevReadFbo);
    g_gl.bindFramebuffer(GL_DRAW_FRAMEBUFFER, prevDrawFbo);
}
