static bool IsMacMirrorRedirectEnabledInternal() {
#ifdef __APPLE__
    return true;
#else
    return false;
#endif
}

static void DestroyMacMirrorRedirectTargetInternal() {
    g_macMirrorRedirect.renderedThisFrame = false;
    g_macMirrorRedirect.active = false;
    if (g_macMirrorRedirect.fbo != 0) {
        g_gl.deleteFramebuffers(1, &g_macMirrorRedirect.fbo);
        g_macMirrorRedirect.fbo = 0;
    }
    if (g_macMirrorRedirect.colorTexture != 0) {
        glDeleteTextures(1, &g_macMirrorRedirect.colorTexture);
        g_macMirrorRedirect.colorTexture = 0;
    }
    if (g_macMirrorRedirect.depthStencilRb != 0) {
        g_gl.deleteRenderbuffers(1, &g_macMirrorRedirect.depthStencilRb);
        g_macMirrorRedirect.depthStencilRb = 0;
    }
    g_macMirrorRedirect.width = 0;
    g_macMirrorRedirect.height = 0;
}

static bool CreateMacMirrorRedirectTargetInternal(int width, int height) {
    if (!EnsureGlFunctions()) {
        return false;
    }
    if (!g_gl.genRenderbuffers || !g_gl.bindRenderbuffer || !g_gl.renderbufferStorage ||
        !g_gl.framebufferRenderbuffer || !g_gl.checkFramebufferStatus) {
        return false;
    }
    if (width <= 0 || height <= 0) {
        return false;
    }

    GLint prevTex = 0;
    GLint prevFbo = 0;
    GLint prevRb = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevFbo);
    glGetIntegerv(GL_RENDERBUFFER_BINDING, &prevRb);

    glGenTextures(1, &g_macMirrorRedirect.colorTexture);
    glBindTexture(GL_TEXTURE_2D, g_macMirrorRedirect.colorTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    g_gl.genRenderbuffers(1, &g_macMirrorRedirect.depthStencilRb);
    g_gl.bindRenderbuffer(GL_RENDERBUFFER, g_macMirrorRedirect.depthStencilRb);
    g_gl.renderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);

    g_gl.genFramebuffers(1, &g_macMirrorRedirect.fbo);
    g_gl.bindFramebuffer(GL_FRAMEBUFFER, g_macMirrorRedirect.fbo);
    g_gl.framebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_macMirrorRedirect.colorTexture, 0);
    g_gl.framebufferRenderbuffer(GL_FRAMEBUFFER,
                                 GL_DEPTH_STENCIL_ATTACHMENT,
                                 GL_RENDERBUFFER,
                                 g_macMirrorRedirect.depthStencilRb);

    const GLenum status = g_gl.checkFramebufferStatus(GL_FRAMEBUFFER);

    g_gl.bindFramebuffer(GL_FRAMEBUFFER, prevFbo);
    glBindTexture(GL_TEXTURE_2D, prevTex);
    g_gl.bindRenderbuffer(GL_RENDERBUFFER, prevRb);

    if (status != GL_FRAMEBUFFER_COMPLETE) {
        DestroyMacMirrorRedirectTargetInternal();
        return false;
    }

    g_macMirrorRedirect.width = width;
    g_macMirrorRedirect.height = height;
    g_macMirrorRedirect.active = true;
    g_macMirrorRedirect.renderedThisFrame = false;
    return true;
}

static bool EnsureMacMirrorRedirectTargetInternal(int width, int height) {
    if (!IsMacMirrorRedirectEnabledInternal()) {
        return false;
    }
    if (width <= 0 || height <= 0) {
        return false;
    }
    if (g_macMirrorRedirect.fbo != 0 &&
        g_macMirrorRedirect.colorTexture != 0 &&
        g_macMirrorRedirect.depthStencilRb != 0 &&
        g_macMirrorRedirect.width == width &&
        g_macMirrorRedirect.height == height) {
        g_macMirrorRedirect.active = true;
        return true;
    }

    DestroyMacMirrorRedirectTargetInternal();
    return CreateMacMirrorRedirectTargetInternal(width, height);
}

static bool ShouldActivateMacMirrorRedirectInternal(int windowWidth, int windowHeight) {
#ifdef __APPLE__
    if (!IsMacMirrorRedirectEnabledInternal()) {
        return false;
    }
    if (IsOverscanActiveInternal()) {
        return false;
    }
    if (windowWidth <= 0 || windowHeight <= 0) {
        return false;
    }
    return !g_modeState.GetActiveMirrorRenderList().empty();
#else
    (void)windowWidth;
    (void)windowHeight;
    return false;
#endif
}

bool UpdateMacMirrorRedirectStateInternal(int windowWidth, int windowHeight) {
    if (!ShouldActivateMacMirrorRedirectInternal(windowWidth, windowHeight)) {
        if (g_macMirrorRedirect.fbo != 0 || g_macMirrorRedirect.active) {
            DestroyMacMirrorRedirectTargetInternal();
        }
        return false;
    }

    if (!EnsureMacMirrorRedirectTargetInternal(windowWidth, windowHeight)) {
        return false;
    }
    return true;
}

GLuint GetMacMirrorRedirectFboIdInternal() {
    return g_macMirrorRedirect.active ? g_macMirrorRedirect.fbo : 0;
}

bool GetMacMirrorRedirectSizeInternal(int& width, int& height) {
    width = g_macMirrorRedirect.width;
    height = g_macMirrorRedirect.height;
    return g_macMirrorRedirect.active &&
           g_macMirrorRedirect.fbo != 0 &&
           width > 0 &&
           height > 0;
}

bool IsMacMirrorRedirectActiveInternal() {
    return g_macMirrorRedirect.active && g_macMirrorRedirect.fbo != 0;
}

bool IsMacMirrorRedirectRenderedInternal() {
    return g_macMirrorRedirect.renderedThisFrame;
}

void MarkMacMirrorRedirectRenderedInternal() {
    g_macMirrorRedirect.renderedThisFrame = true;
}

void BlitMacMirrorRedirectToWindowInternal(int dstX,
                                           int dstY,
                                           int dstWidth,
                                           int dstHeight,
                                           int surfaceWidth,
                                           int surfaceHeight) {
    if (!IsMacMirrorRedirectActiveInternal()) {
        return;
    }
    if (!g_macMirrorRedirect.renderedThisFrame) {
        return;
    }
    if (!g_gl.bindFramebuffer || !g_gl.blitFramebuffer) {
        return;
    }
    if (dstWidth <= 0 || dstHeight <= 0 || surfaceWidth <= 0 || surfaceHeight <= 0) {
        return;
    }

    g_gl.bindFramebuffer(GL_READ_FRAMEBUFFER, g_macMirrorRedirect.fbo);
    g_gl.bindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    g_gl.blitFramebuffer(0,
                         0,
                         g_macMirrorRedirect.width,
                         g_macMirrorRedirect.height,
                         dstX,
                         dstY,
                         std::min(surfaceWidth, dstX + dstWidth),
                         std::min(surfaceHeight, dstY + dstHeight),
                         GL_COLOR_BUFFER_BIT,
                         GL_NEAREST);
    // Leave the window framebuffer bound for the following GUI pass.
    g_gl.bindFramebuffer(GL_FRAMEBUFFER, 0);
}
