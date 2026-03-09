#ifdef __APPLE__
static std::string ConvertShaderToGLSL120(GLenum type, const char* src) {
    if (!src) { return {}; }
    std::string s(src);

    // Version line
    {
        auto pos = s.find("#version 330 core");
        if (pos != std::string::npos) { s.replace(pos, 17, "#version 120"); }
        else {
            pos = s.find("#version 330");
            if (pos != std::string::npos) { s.replace(pos, 12, "#version 120"); }
        }
    }

    // layout(location = N) in vecN name → attribute vecN name
    for (;;) {
        auto pos = s.find("layout(location");
        if (pos == std::string::npos) break;
        auto endParen = s.find(')', pos);
        if (endParen == std::string::npos) break;
        size_t afterParen = endParen + 1;
        while (afterParen < s.size() && (s[afterParen] == ' ' || s[afterParen] == '\t')) afterParen++;
        if (s.compare(afterParen, 3, "in ") == 0) {
            s.replace(pos, afterParen + 3 - pos, "attribute ");
        } else {
            s.erase(pos, afterParen - pos);
        }
    }

    if (type == GL_VERTEX_SHADER) {
        // out vecN name → varying vecN name (vertex shader outputs)
        for (size_t pos = 0;;) {
            pos = s.find("\nout ", pos);
            if (pos == std::string::npos) break;
            s.replace(pos + 1, 3, "varying");
            pos += 8;
        }
        if (s.compare(0, 4, "out ") == 0) { s.replace(0, 3, "varying"); }
    } else {
        // Fragment shader: in vecN name → varying vecN name
        for (size_t pos = 0;;) {
            pos = s.find("\nin ", pos);
            if (pos == std::string::npos) break;
            s.replace(pos + 1, 2, "varying");
            pos += 8;
        }
        if (s.compare(0, 3, "in ") == 0) { s.replace(0, 2, "varying"); }

        // Remove "out vec4 FragColor;" declaration
        {
            auto pos = s.find("out vec4 FragColor;");
            if (pos != std::string::npos) {
                auto lineEnd = s.find('\n', pos);
                if (lineEnd != std::string::npos) { s.erase(pos, lineEnd - pos + 1); }
                else { s.erase(pos); }
            }
        }

        // Replace FragColor with gl_FragColor
        for (size_t pos = 0;;) {
            pos = s.find("FragColor", pos);
            if (pos == std::string::npos) break;
            if (pos > 0 && (std::isalnum(static_cast<unsigned char>(s[pos - 1])) || s[pos - 1] == '_')) { pos += 9; continue; }
            size_t end = pos + 9;
            if (end < s.size() && (std::isalnum(static_cast<unsigned char>(s[end])) || s[end] == '_')) { pos += 9; continue; }
            s.replace(pos, 9, "gl_FragColor");
            pos += 12;
        }
    }

    // texture( → texture2D(
    for (size_t pos = 0;;) {
        pos = s.find("texture(", pos);
        if (pos == std::string::npos) break;
        if (pos > 0 && (std::isalnum(static_cast<unsigned char>(s[pos - 1])) || s[pos - 1] == '_')) { pos += 8; continue; }
        s.replace(pos, 8, "texture2D(");
        pos += 10;
    }

    return s;
}
#endif

GLuint CompileShader(GLenum type, const char* source) {
    GLuint shader = g_gl.createShader(type);
    if (!shader) { return 0; }
#ifdef __APPLE__
    std::string converted = ConvertShaderToGLSL120(type, source);
    source = converted.c_str();
#endif
    g_gl.shaderSource(shader, 1, &source, nullptr);
    g_gl.compileShader(shader);

    GLint status = 0;
    g_gl.getShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (!status) {
        char log[512] = {};
        g_gl.getShaderInfoLog(shader, sizeof(log), nullptr, log);
        fprintf(stderr, "[Linuxscreen][mirror] Shader compile error: %s\n", log);
        g_gl.deleteShader(shader);
        return 0;
    }
    return shader;
}

GLuint CreateProgram(GLuint vert, GLuint frag) {
    if (!vert || !frag) { return 0; }
    GLuint prog = g_gl.createProgram();
    if (!prog) { return 0; }
    g_gl.attachShader(prog, vert);
    g_gl.attachShader(prog, frag);
#ifdef __APPLE__
    if (g_gl.bindAttribLocation) {
        g_gl.bindAttribLocation(prog, 0, "aPos");
        g_gl.bindAttribLocation(prog, 1, "aTexCoord");
    }
#endif
    g_gl.linkProgram(prog);

    GLint status = 0;
    g_gl.getProgramiv(prog, GL_LINK_STATUS, &status);
    if (!status) {
        char log[512] = {};
        g_gl.getProgramInfoLog(prog, sizeof(log), nullptr, log);
        fprintf(stderr, "[Linuxscreen][mirror] Program link error: %s\n", log);
        g_gl.deleteProgram(prog);
        return 0;
    }
    return prog;
}

bool InitMirrorShaders() {
    if (g_shaders.ready) { return true; }

    GLuint vert = CompileShader(GL_VERTEX_SHADER, kVertShader);
    if (!vert) { return false; }

    GLuint filterFrag = CompileShader(GL_FRAGMENT_SHADER, kFilterFragShader);
    GLuint filterPtFrag = CompileShader(GL_FRAGMENT_SHADER, kFilterPassthroughFragShader);
    GLuint ptFrag = CompileShader(GL_FRAGMENT_SHADER, kPassthroughFragShader);
    GLuint renderFrag = CompileShader(GL_FRAGMENT_SHADER, kRenderFragShader);
    GLuint renderPtFrag = CompileShader(GL_FRAGMENT_SHADER, kRenderPassthroughFragShader);
    GLuint staticBorderFrag = CompileShader(GL_FRAGMENT_SHADER, kStaticBorderFragShader);

    g_shaders.filterProgram = CreateProgram(vert, filterFrag);
    g_shaders.filterPassthroughProgram = CreateProgram(vert, filterPtFrag);
    g_shaders.passthroughProgram = CreateProgram(vert, ptFrag);
    g_shaders.renderProgram = CreateProgram(vert, renderFrag);
    g_shaders.renderPassthroughProgram = CreateProgram(vert, renderPtFrag);
    g_shaders.staticBorderProgram = CreateProgram(vert, staticBorderFrag);

    g_gl.deleteShader(vert);
    if (filterFrag) g_gl.deleteShader(filterFrag);
    if (filterPtFrag) g_gl.deleteShader(filterPtFrag);
    if (ptFrag) g_gl.deleteShader(ptFrag);
    if (renderFrag) g_gl.deleteShader(renderFrag);
    if (renderPtFrag) g_gl.deleteShader(renderPtFrag);
    if (staticBorderFrag) g_gl.deleteShader(staticBorderFrag);

    if (!g_shaders.filterProgram || !g_shaders.filterPassthroughProgram ||
        !g_shaders.passthroughProgram || !g_shaders.renderProgram ||
        !g_shaders.renderPassthroughProgram || !g_shaders.staticBorderProgram) {
        return false;
    }

    // Query filter shader uniforms
    auto& fl = g_shaders.filterLocs;
    fl.screenTexture = g_gl.getUniformLocation(g_shaders.filterProgram, "screenTexture");
    fl.sourceRect = g_gl.getUniformLocation(g_shaders.filterProgram, "u_sourceRect");
    fl.gammaMode = g_gl.getUniformLocation(g_shaders.filterProgram, "u_gammaMode");
    fl.targetColors = g_gl.getUniformLocation(g_shaders.filterProgram, "u_targetColors");
    fl.targetColorCount = g_gl.getUniformLocation(g_shaders.filterProgram, "u_targetColorCount");
    fl.outputColor = g_gl.getUniformLocation(g_shaders.filterProgram, "outputColor");
    fl.sensitivity = g_gl.getUniformLocation(g_shaders.filterProgram, "u_sensitivity");

    // Query filter passthrough shader uniforms
    auto& fpl = g_shaders.filterPassthroughLocs;
    fpl.screenTexture = g_gl.getUniformLocation(g_shaders.filterPassthroughProgram, "screenTexture");
    fpl.sourceRect = g_gl.getUniformLocation(g_shaders.filterPassthroughProgram, "u_sourceRect");
    fpl.gammaMode = g_gl.getUniformLocation(g_shaders.filterPassthroughProgram, "u_gammaMode");
    fpl.targetColors = g_gl.getUniformLocation(g_shaders.filterPassthroughProgram, "u_targetColors");
    fpl.targetColorCount = g_gl.getUniformLocation(g_shaders.filterPassthroughProgram, "u_targetColorCount");
    fpl.sensitivity = g_gl.getUniformLocation(g_shaders.filterPassthroughProgram, "u_sensitivity");

    // Query passthrough shader uniforms
    auto& pl = g_shaders.passthroughLocs;
    pl.screenTexture = g_gl.getUniformLocation(g_shaders.passthroughProgram, "screenTexture");
    pl.sourceRect = g_gl.getUniformLocation(g_shaders.passthroughProgram, "u_sourceRect");

    // Query render shader uniforms
    auto& rl = g_shaders.renderLocs;
    rl.filterTexture = g_gl.getUniformLocation(g_shaders.renderProgram, "filterTexture");
    rl.borderWidth = g_gl.getUniformLocation(g_shaders.renderProgram, "u_borderWidth");
    rl.outputColor = g_gl.getUniformLocation(g_shaders.renderProgram, "u_outputColor");
    rl.borderColor = g_gl.getUniformLocation(g_shaders.renderProgram, "u_borderColor");
    rl.screenPixel = g_gl.getUniformLocation(g_shaders.renderProgram, "u_screenPixel");

    // Query render passthrough shader uniforms
    auto& rpl = g_shaders.renderPassthroughLocs;
    rpl.filterTexture = g_gl.getUniformLocation(g_shaders.renderPassthroughProgram, "filterTexture");
    rpl.borderWidth = g_gl.getUniformLocation(g_shaders.renderPassthroughProgram, "u_borderWidth");
    rpl.borderColor = g_gl.getUniformLocation(g_shaders.renderPassthroughProgram, "u_borderColor");
    rpl.screenPixel = g_gl.getUniformLocation(g_shaders.renderPassthroughProgram, "u_screenPixel");

    // Query static border shader uniforms
    auto& sbl = g_shaders.staticBorderLocs;
    sbl.shape = g_gl.getUniformLocation(g_shaders.staticBorderProgram, "u_shape");
    sbl.borderColor = g_gl.getUniformLocation(g_shaders.staticBorderProgram, "u_borderColor");
    sbl.thickness = g_gl.getUniformLocation(g_shaders.staticBorderProgram, "u_thickness");
    sbl.radius = g_gl.getUniformLocation(g_shaders.staticBorderProgram, "u_radius");
    sbl.size = g_gl.getUniformLocation(g_shaders.staticBorderProgram, "u_size");
    sbl.quadSize = g_gl.getUniformLocation(g_shaders.staticBorderProgram, "u_quadSize");

    // Create fullscreen quad VAO/VBO
    // Vertices: {x, y, u, v}
    static const float kQuadVerts[] = {
        -1.0f, -1.0f,  0.0f, 0.0f,
         1.0f, -1.0f,  1.0f, 0.0f,
         1.0f,  1.0f,  1.0f, 1.0f,
        -1.0f, -1.0f,  0.0f, 0.0f,
         1.0f,  1.0f,  1.0f, 1.0f,
        -1.0f,  1.0f,  0.0f, 1.0f,
    };

    g_gl.genVertexArrays(1, &g_shaders.quadVao);
    g_gl.genBuffers(1, &g_shaders.quadVbo);
    g_gl.bindVertexArray(g_shaders.quadVao);
    g_gl.bindBuffer(GL_ARRAY_BUFFER, g_shaders.quadVbo);
    g_gl.bufferData(GL_ARRAY_BUFFER, sizeof(kQuadVerts), kQuadVerts, GL_DYNAMIC_DRAW);
    g_gl.vertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), reinterpret_cast<void*>(0));
    g_gl.enableVertexAttribArray(0);
    g_gl.vertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), reinterpret_cast<void*>(2 * sizeof(float)));
    g_gl.enableVertexAttribArray(1);
    g_gl.bindVertexArray(0);
    g_gl.bindBuffer(GL_ARRAY_BUFFER, 0);

    g_shaders.ready = true;
    if (IsDebugEnabled()) {
        fprintf(stderr, "[Linuxscreen][mirror] Shaders compiled successfully\n");
        fprintf(stderr, "[Linuxscreen][mirror]   filterProgram=%u passthroughProgram=%u renderProgram=%u\n",
                g_shaders.filterProgram, g_shaders.passthroughProgram, g_shaders.renderProgram);
    }
    return true;
}

void CleanupMirrorShaders() {
    if (!g_shaders.ready) { return; }

    if (g_shaders.filterProgram) { g_gl.deleteProgram(g_shaders.filterProgram); g_shaders.filterProgram = 0; }
    if (g_shaders.filterPassthroughProgram) { g_gl.deleteProgram(g_shaders.filterPassthroughProgram); g_shaders.filterPassthroughProgram = 0; }
    if (g_shaders.passthroughProgram) { g_gl.deleteProgram(g_shaders.passthroughProgram); g_shaders.passthroughProgram = 0; }
    if (g_shaders.renderProgram) { g_gl.deleteProgram(g_shaders.renderProgram); g_shaders.renderProgram = 0; }
    if (g_shaders.renderPassthroughProgram) { g_gl.deleteProgram(g_shaders.renderPassthroughProgram); g_shaders.renderPassthroughProgram = 0; }
    if (g_shaders.staticBorderProgram) { g_gl.deleteProgram(g_shaders.staticBorderProgram); g_shaders.staticBorderProgram = 0; }

    if (g_shaders.quadVao) { g_gl.deleteVertexArrays(1, &g_shaders.quadVao); g_shaders.quadVao = 0; }
    if (g_shaders.quadVbo) { g_gl.deleteBuffers(1, &g_shaders.quadVbo); g_shaders.quadVbo = 0; }

    g_shaders.ready = false;
}
