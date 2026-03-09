#include "glx_shared_contexts.h"

#ifdef __APPLE__

#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

namespace platform::x11 {

namespace {

struct SharedCglState {
    CGLContextObj gameContext = nullptr;
    CGLContextObj renderContext = nullptr;
    CGLContextObj mirrorContext = nullptr;
    bool ready = false;
};

struct SharedCglGlobals {
    std::mutex sharedMutex;
    SharedCglState sharedState;
    std::mutex errorMutex;
    std::string lastError;
    std::mutex infoMutex;
    std::string lastInitInfo;
    std::atomic<std::uint64_t> sharedGeneration{ 0 };
};

SharedCglGlobals& SharedGlobals() {
    // Keep teardown state alive through process exit. macOS can invoke the
    // preload destructor after namespace-scope mutex destructors have already
    // run, which turns any later lock attempt into EINVAL/std::system_error.
    static SharedCglGlobals* globals = new SharedCglGlobals();
    return *globals;
}

void SetLastError(const std::string& msg) {
    auto& globals = SharedGlobals();
    std::lock_guard<std::mutex> lk(globals.errorMutex);
    globals.lastError = msg;
}

void ClearLastError() {
    auto& globals = SharedGlobals();
    std::lock_guard<std::mutex> lk(globals.errorMutex);
    globals.lastError.clear();
}

void SetLastInitInfo(const std::string& msg) {
    auto& globals = SharedGlobals();
    std::lock_guard<std::mutex> lk(globals.infoMutex);
    globals.lastInitInfo = msg;
}

void ClearLastInitInfo() {
    auto& globals = SharedGlobals();
    std::lock_guard<std::mutex> lk(globals.infoMutex);
    globals.lastInitInfo.clear();
}

void ShutdownSharedGlxContextsLocked(SharedCglGlobals& globals, bool processExit) {
    if (!processExit) {
        if (globals.sharedState.renderContext) { CGLDestroyContext(globals.sharedState.renderContext); }
        if (globals.sharedState.mirrorContext) { CGLDestroyContext(globals.sharedState.mirrorContext); }
    }
    globals.sharedState = SharedCglState{};
    globals.sharedGeneration.fetch_add(1, std::memory_order_acq_rel);
    ClearLastInitInfo();
}

} // namespace

bool EnsureSharedGlxContexts(void* /*nativeDisplay*/, std::uint64_t /*drawable*/, void* gameContext) {
    CGLContextObj cglCtx = reinterpret_cast<CGLContextObj>(gameContext);
    if (!cglCtx) {
        SetLastError("missing CGL game context");
        return false;
    }

    auto& globals = SharedGlobals();
    std::lock_guard<std::mutex> lock(globals.sharedMutex);

    std::string recreateReason;
    if (globals.sharedState.ready && globals.sharedState.gameContext != cglCtx) { recreateReason = "context changed"; }

    if (globals.sharedState.ready && recreateReason.empty()) { return true; }
    if (globals.sharedState.ready) { ShutdownSharedGlxContextsLocked(globals, false); }

    ClearLastError();
    ClearLastInitInfo();

    CGLPixelFormatObj pix = CGLGetPixelFormat(cglCtx);
    if (!pix) { SetLastError("CGLGetPixelFormat returned null"); return false; }

    CGLContextObj renderCtx = nullptr;
    CGLContextObj mirrorCtx = nullptr;
    if (CGLCreateContext(pix, cglCtx, &renderCtx) != kCGLNoError || !renderCtx) {
        SetLastError("CGLCreateContext failed for render context");
        return false;
    }
    if (CGLCreateContext(pix, cglCtx, &mirrorCtx) != kCGLNoError || !mirrorCtx) {
        CGLDestroyContext(renderCtx);
        SetLastError("CGLCreateContext failed for mirror context");
        return false;
    }

    // Verify texture sharing: create a texture in the game context and check
    // visibility from the shared contexts.
    {
        GlxContextRestoreState preVerifyState;
        preVerifyState.context = CGLGetCurrentContext();
        preVerifyState.valid = true;

        CGLSetCurrentContext(cglCtx);

        GLint prevActiveTexUnit = 0;
        glGetIntegerv(GL_ACTIVE_TEXTURE, &prevActiveTexUnit);
        glActiveTexture(GL_TEXTURE0);
        GLint prevBinding = 0;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevBinding);

        GLuint testTexture = 0;
        glGenTextures(1, &testTexture);
        glBindTexture(GL_TEXTURE_2D, testTexture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        const std::uint32_t pixel = 0xFF00FFFFu;
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &pixel);
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prevBinding));
        glActiveTexture(static_cast<GLenum>(prevActiveTexUnit));

        bool renderOk = false;
        bool mirrorOk = false;

        CGLSetCurrentContext(renderCtx);
        renderOk = (glIsTexture(testTexture) == GL_TRUE);

        CGLSetCurrentContext(mirrorCtx);
        mirrorOk = (glIsTexture(testTexture) == GL_TRUE);

        // Restore game context and clean up test texture
        CGLSetCurrentContext(cglCtx);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prevBinding));
        glActiveTexture(static_cast<GLenum>(prevActiveTexUnit));
        if (testTexture != 0) { glDeleteTextures(1, &testTexture); }

        // Restore original context
        CGLSetCurrentContext(reinterpret_cast<CGLContextObj>(preVerifyState.context));

        if (!renderOk || !mirrorOk) {
            CGLDestroyContext(renderCtx);
            CGLDestroyContext(mirrorCtx);
            SetLastError("shared CGL context texture probe failed");
            return false;
        }
    }

    globals.sharedState.gameContext = cglCtx;
    globals.sharedState.renderContext = renderCtx;
    globals.sharedState.mirrorContext = mirrorCtx;
    globals.sharedState.ready = true;
    globals.sharedGeneration.fetch_add(1, std::memory_order_acq_rel);

    std::string info = "CGL shared contexts initialized";
    if (!recreateReason.empty()) { info.append(" (recreated: "); info.append(recreateReason); info.append(")"); }
    SetLastInitInfo(info);
    return true;
}

bool AreSharedGlxContextsReady() {
    auto& globals = SharedGlobals();
    std::lock_guard<std::mutex> lock(globals.sharedMutex);
    return globals.sharedState.ready;
}

GlxSharedContextHandles GetSharedGlxContextHandles() {
    auto& globals = SharedGlobals();
    std::lock_guard<std::mutex> lock(globals.sharedMutex);
    GlxSharedContextHandles handles;
    handles.nativeDisplay = nullptr;
    handles.renderContext = globals.sharedState.renderContext;
    handles.renderDrawable = 0;
    handles.mirrorContext = globals.sharedState.mirrorContext;
    handles.mirrorDrawable = 0;
    return handles;
}

std::string GetSharedGlxContextLastError() {
    auto& globals = SharedGlobals();
    std::lock_guard<std::mutex> lock(globals.errorMutex);
    return globals.lastError;
}

std::string GetSharedGlxContextLastInitInfo() {
    auto& globals = SharedGlobals();
    std::lock_guard<std::mutex> lock(globals.infoMutex);
    return globals.lastInitInfo;
}

std::uint64_t GetSharedGlxContextGeneration() {
    return SharedGlobals().sharedGeneration.load(std::memory_order_acquire);
}

bool MakeSharedGlxContextCurrent(GlxSharedContextRole role, GlxContextRestoreState& outRestore) {
    auto& globals = SharedGlobals();
    std::lock_guard<std::mutex> lock(globals.sharedMutex);
    if (!globals.sharedState.ready) { return false; }

    CGLContextObj targetContext =
        (role == GlxSharedContextRole::Mirror) ? globals.sharedState.mirrorContext : globals.sharedState.renderContext;
    if (!targetContext) { return false; }

    outRestore.display = nullptr;
    outRestore.drawDrawable = 0;
    outRestore.readDrawable = 0;
    outRestore.context = CGLGetCurrentContext();
    outRestore.valid = true;

    if (CGLSetCurrentContext(targetContext) != kCGLNoError) { outRestore.valid = false; return false; }
    return true;
}

bool RestoreGlxContext(const GlxContextRestoreState& restore) {
    if (!restore.valid) { return false; }
    CGLContextObj ctx = reinterpret_cast<CGLContextObj>(restore.context);
    return CGLSetCurrentContext(ctx) == kCGLNoError;
}

void ShutdownSharedGlxContexts() {
    auto& globals = SharedGlobals();
    std::lock_guard<std::mutex> lock(globals.sharedMutex);
    ShutdownSharedGlxContextsLocked(globals, false);
}

void ShutdownSharedGlxContextsForProcessExit() {
    auto& globals = SharedGlobals();
    std::lock_guard<std::mutex> lock(globals.sharedMutex);
    ShutdownSharedGlxContextsLocked(globals, true);
}

} // namespace platform::x11

#else // !__APPLE__

#include <GL/gl.h>
#include <GL/glx.h>
#include <X11/Xlib.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace platform::x11 {

namespace {

struct SharedGlxState {
    Display* display = nullptr;
    GLXContext gameContext = nullptr;
    GLXDrawable sourceDrawable = 0;
    int fbConfigId = 0;
    GLXFBConfig fbConfig = nullptr;
    GLXContext renderContext = nullptr;
    GLXContext mirrorContext = nullptr;
    GLXDrawable renderDrawable = 0;
    GLXDrawable mirrorDrawable = 0;
    bool ready = false;
};

std::mutex g_sharedMutex;
SharedGlxState g_sharedState;
std::mutex g_errorMutex;
std::string g_lastError;
std::mutex g_infoMutex;
std::string g_lastInitInfo;
std::atomic<std::uint64_t> g_sharedGeneration{ 0 };

void SetLastError(const std::string& message) {
    std::lock_guard<std::mutex> lock(g_errorMutex);
    g_lastError = message;
}

void ClearLastError() {
    std::lock_guard<std::mutex> lock(g_errorMutex);
    g_lastError.clear();
}

void SetLastInitInfo(const std::string& message) {
    std::lock_guard<std::mutex> lock(g_infoMutex);
    g_lastInitInfo = message;
}

void ClearLastInitInfo() {
    std::lock_guard<std::mutex> lock(g_infoMutex);
    g_lastInitInfo.clear();
}

void AppendReason(std::string& reason, const char* label) {
    if (!label || label[0] == '\0') { return; }
    if (!reason.empty()) { reason.append(", "); }
    reason.append(label);
}

std::string FormatPointer(const void* ptr) {
    return std::to_string(reinterpret_cast<std::uintptr_t>(ptr));
}

std::string DescribeFingerprint(const SharedGlxState& state) {
    std::string info = "display=";
    info.append(FormatPointer(state.display));
    info.append(" context=");
    info.append(FormatPointer(state.gameContext));
    info.append(" drawable=");
    info.append(std::to_string(static_cast<unsigned long long>(state.sourceDrawable)));
    if (state.fbConfigId != 0) {
        info.append(" fbConfigId=");
        info.append(std::to_string(state.fbConfigId));
    }
    return info;
}

bool QueryFbConfigIdFromDrawable(Display* display, GLXDrawable drawable, int& outId) {
#ifdef GLX_FBCONFIG_ID
    if (!display || !drawable) { return false; }
    unsigned int value = 0;
    glXQueryDrawable(display, drawable, GLX_FBCONFIG_ID, &value);
    if (value != 0) {
        outId = static_cast<int>(value);
        return true;
    }
#endif
    return false;
}

bool QueryFbConfigIdFromContext(Display* display, GLXContext context, int& outId) {
#ifdef GLX_FBCONFIG_ID
    if (!display || !context) { return false; }
    int value = 0;
    if (glXQueryContext(display, context, GLX_FBCONFIG_ID, &value) == 0 && value != 0) {
        outId = value;
        return true;
    }
#endif
    return false;
}

GLXFBConfig FindMatchingFbConfig(Display* display, int fbConfigId) {
    if (!display || fbConfigId == 0) { return nullptr; }

    int screen = DefaultScreen(display);
    int count = 0;
    GLXFBConfig* configs = glXGetFBConfigs(display, screen, &count);
    if (!configs || count == 0) { return nullptr; }

    GLXFBConfig matched = nullptr;
    for (int i = 0; i < count; ++i) {
        int id = 0;
        if (glXGetFBConfigAttrib(display, configs[i], GLX_FBCONFIG_ID, &id) == 0 && id == fbConfigId) {
            matched = configs[i];
            break;
        }
    }

    XFree(configs);
    return matched;
}

GLXFBConfig ChooseFallbackFbConfig(Display* display) {
    if (!display) { return nullptr; }

    int screen = DefaultScreen(display);
    const int attribs[] = { GLX_RENDER_TYPE, GLX_RGBA_BIT, GLX_DRAWABLE_TYPE, GLX_PBUFFER_BIT | GLX_WINDOW_BIT, GLX_DOUBLEBUFFER,
                            True, 0 };
    int count = 0;
    GLXFBConfig* configs = glXChooseFBConfig(display, screen, attribs, &count);
    if (!configs || count == 0) { return nullptr; }

    GLXFBConfig fallback = configs[0];
    XFree(configs);
    return fallback;
}

GLXContext CreateSharedContext(Display* display, GLXFBConfig fbConfig, GLXContext shareContext) {
    if (!display || !fbConfig) { return nullptr; }

    using CreateContextAttribsFn = GLXContext (*)(Display*, GLXFBConfig, GLXContext, Bool, const int*);
    static CreateContextAttribsFn createAttribs = nullptr;
    static std::once_flag resolveOnce;

    std::call_once(resolveOnce, []() {
        createAttribs = reinterpret_cast<CreateContextAttribsFn>(glXGetProcAddressARB(
            reinterpret_cast<const GLubyte*>("glXCreateContextAttribsARB")));
    });

    if (createAttribs) {
        int major = 3;
        int minor = 3;
        glGetIntegerv(GL_MAJOR_VERSION, &major);
        glGetIntegerv(GL_MINOR_VERSION, &minor);
        while (glGetError() != GL_NO_ERROR) {}

#ifdef GLX_CONTEXT_MAJOR_VERSION_ARB
        std::vector<int> attribs;
        attribs.reserve(9);
        attribs.push_back(GLX_CONTEXT_MAJOR_VERSION_ARB);
        attribs.push_back(major);
        attribs.push_back(GLX_CONTEXT_MINOR_VERSION_ARB);
        attribs.push_back(minor);

#ifdef GLX_CONTEXT_PROFILE_MASK_ARB
        int profileMask = 0;
        glGetIntegerv(GL_CONTEXT_PROFILE_MASK, &profileMask);
        while (glGetError() != GL_NO_ERROR) {}
        if (profileMask & GL_CONTEXT_CORE_PROFILE_BIT) {
            attribs.push_back(GLX_CONTEXT_PROFILE_MASK_ARB);
            attribs.push_back(GLX_CONTEXT_CORE_PROFILE_BIT_ARB);
        } else {
            attribs.push_back(GLX_CONTEXT_PROFILE_MASK_ARB);
            attribs.push_back(GLX_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB);
        }
#endif

#ifdef GLX_CONTEXT_FLAGS_ARB
        int flags = 0;
        glGetIntegerv(GL_CONTEXT_FLAGS, &flags);
        while (glGetError() != GL_NO_ERROR) {}
        attribs.push_back(GLX_CONTEXT_FLAGS_ARB);
        attribs.push_back(flags);
#endif

        attribs.push_back(0);

        GLXContext ctx = createAttribs(display, fbConfig, shareContext, True, attribs.data());
        if (ctx) { return ctx; }
#endif
    }

    return glXCreateNewContext(display, fbConfig, GLX_RGBA_TYPE, shareContext, True);
}

GLXDrawable CreateSharedPbuffer(Display* display, GLXFBConfig fbConfig) {
    if (!display || !fbConfig) { return 0; }
    const int attribs[] = { GLX_PBUFFER_WIDTH, 16, GLX_PBUFFER_HEIGHT, 16, GLX_PRESERVED_CONTENTS, True, 0 };
    return glXCreatePbuffer(display, fbConfig, attribs);
}

bool MakeContextCurrent(Display* display, GLXDrawable draw, GLXDrawable read, GLXContext context) {
    if (!display) { return false; }
    return glXMakeContextCurrent(display, draw, read, context) == True;
}

bool EnsureGameContextCurrent(Display* display, GLXDrawable drawable, GLXContext context) {
    if (!display || !drawable || !context) { return false; }
    if (glXGetCurrentContext() == context && glXGetCurrentDisplay() == display) { return true; }
    return MakeContextCurrent(display, drawable, drawable, context);
}

bool VerifySharedContextTextures(Display* display,
                                 GLXDrawable gameDrawable,
                                 GLXContext gameContext,
                                 GLXContext renderContext,
                                 GLXDrawable renderDrawable,
                                 GLXContext mirrorContext,
                                 GLXDrawable mirrorDrawable,
                                 std::string& outFailureReason) {
    outFailureReason.clear();

    if (!display || !gameDrawable || !gameContext || !renderContext || !mirrorContext || !renderDrawable || !mirrorDrawable) {
        outFailureReason = "missing contexts or drawables for sharing probe";
        return false;
    }

    GlxContextRestoreState restore;
    restore.display = glXGetCurrentDisplay();
    restore.context = glXGetCurrentContext();
    restore.drawDrawable = static_cast<std::uint64_t>(glXGetCurrentDrawable());
    restore.readDrawable = static_cast<std::uint64_t>(glXGetCurrentReadDrawable());
    restore.valid = true;

    if (!EnsureGameContextCurrent(display, gameDrawable, gameContext)) {
        outFailureReason = "failed to make game context current for sharing probe";
        RestoreGlxContext(restore);
        return false;
    }

    GLint prevActiveTexUnit = 0;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &prevActiveTexUnit);
    glActiveTexture(GL_TEXTURE0);
    GLint prevBinding = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevBinding);

    GLuint testTexture = 0;
    glGenTextures(1, &testTexture);
    glBindTexture(GL_TEXTURE_2D, testTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    const std::uint32_t pixel = 0xFF00FFFFu;
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &pixel);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prevBinding));
    glActiveTexture(static_cast<GLenum>(prevActiveTexUnit));

    bool renderOk = false;
    bool mirrorOk = false;

    if (!MakeContextCurrent(display, renderDrawable, renderDrawable, renderContext)) {
        outFailureReason = "failed to make render shared context current for sharing probe";
    } else {
        renderOk = (glIsTexture(testTexture) == GL_TRUE);
    }

    if (!MakeContextCurrent(display, mirrorDrawable, mirrorDrawable, mirrorContext)) {
        if (outFailureReason.empty()) {
            outFailureReason = "failed to make mirror shared context current for sharing probe";
        }
    } else {
        mirrorOk = (glIsTexture(testTexture) == GL_TRUE);
    }

    if (!EnsureGameContextCurrent(display, gameDrawable, gameContext)) {
        outFailureReason = "failed to restore game context after sharing probe";
        RestoreGlxContext(restore);
        return false;
    }

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prevBinding));
    glActiveTexture(static_cast<GLenum>(prevActiveTexUnit));
    if (testTexture != 0) { glDeleteTextures(1, &testTexture); }

    RestoreGlxContext(restore);

    if (!renderOk || !mirrorOk) {
        if (outFailureReason.empty()) {
            outFailureReason = "shared context texture probe failed";
        }
        return false;
    }

    return true;
}

void ShutdownSharedGlxContextsLocked(bool processExit) {
    if (!processExit && g_sharedState.display) {
        if (g_sharedState.renderContext) { glXDestroyContext(g_sharedState.display, g_sharedState.renderContext); }
        if (g_sharedState.mirrorContext) { glXDestroyContext(g_sharedState.display, g_sharedState.mirrorContext); }
        if (g_sharedState.renderDrawable) { glXDestroyPbuffer(g_sharedState.display, g_sharedState.renderDrawable); }
        if (g_sharedState.mirrorDrawable) { glXDestroyPbuffer(g_sharedState.display, g_sharedState.mirrorDrawable); }
    }

    g_sharedState = SharedGlxState{};
    g_sharedGeneration.fetch_add(1, std::memory_order_acq_rel);
    ClearLastInitInfo();
}

} // namespace

bool EnsureSharedGlxContexts(void* nativeDisplay, std::uint64_t drawable, void* gameContext) {
    Display* display = reinterpret_cast<Display*>(nativeDisplay);
    GLXDrawable glxDrawable = static_cast<GLXDrawable>(drawable);
    GLXContext context = reinterpret_cast<GLXContext>(gameContext);

    if (!display || !glxDrawable || !context) {
        SetLastError("missing display/drawable/context for shared GLX initialization");
        return false;
    }

    std::lock_guard<std::mutex> lock(g_sharedMutex);

    std::string recreateReason;
    if (g_sharedState.ready) {
        if (g_sharedState.display != display) { AppendReason(recreateReason, "display changed"); }
        if (g_sharedState.gameContext != context) { AppendReason(recreateReason, "context changed"); }
        if (g_sharedState.sourceDrawable != glxDrawable) { AppendReason(recreateReason, "drawable changed"); }
    }

    int fbConfigId = 0;
    bool hasId = QueryFbConfigIdFromDrawable(display, glxDrawable, fbConfigId) ||
                 QueryFbConfigIdFromContext(display, context, fbConfigId);

    if (g_sharedState.ready && fbConfigId != 0 && g_sharedState.fbConfigId != 0 && g_sharedState.fbConfigId != fbConfigId) {
        AppendReason(recreateReason, "FBConfig changed");
    }

    if (g_sharedState.ready && recreateReason.empty()) {
        return true;
    }

    if (g_sharedState.ready && !recreateReason.empty()) {
        ShutdownSharedGlxContextsLocked(false);
    }

    ClearLastError();
    ClearLastInitInfo();

    GLXFBConfig fbConfig = nullptr;
    if (hasId) { fbConfig = FindMatchingFbConfig(display, fbConfigId); }
    if (!fbConfig) { fbConfig = ChooseFallbackFbConfig(display); }
    if (!fbConfig) {
        SetLastError("failed to resolve GLXFBConfig for shared contexts");
        return false;
    }

    GLXContext renderContext = CreateSharedContext(display, fbConfig, context);
    GLXContext mirrorContext = CreateSharedContext(display, fbConfig, context);
    if (!renderContext || !mirrorContext) {
        if (renderContext) { glXDestroyContext(display, renderContext); }
        if (mirrorContext) { glXDestroyContext(display, mirrorContext); }
        SetLastError("failed to create shared GLX contexts");
        return false;
    }

    GLXDrawable renderDrawable = CreateSharedPbuffer(display, fbConfig);
    GLXDrawable mirrorDrawable = CreateSharedPbuffer(display, fbConfig);
    if (!renderDrawable || !mirrorDrawable) {
        if (renderDrawable) { glXDestroyPbuffer(display, renderDrawable); }
        if (mirrorDrawable) { glXDestroyPbuffer(display, mirrorDrawable); }
        glXDestroyContext(display, renderContext);
        glXDestroyContext(display, mirrorContext);
        SetLastError("failed to create GLX pbuffers for shared contexts");
        return false;
    }

    std::string shareFailure;
    if (!VerifySharedContextTextures(display, glxDrawable, context, renderContext, renderDrawable, mirrorContext, mirrorDrawable,
                                     shareFailure)) {
        if (renderDrawable) { glXDestroyPbuffer(display, renderDrawable); }
        if (mirrorDrawable) { glXDestroyPbuffer(display, mirrorDrawable); }
        glXDestroyContext(display, renderContext);
        glXDestroyContext(display, mirrorContext);
        SetLastError(shareFailure.empty() ? "shared context texture probe failed" : shareFailure);
        return false;
    }

    g_sharedState.display = display;
    g_sharedState.gameContext = context;
    g_sharedState.sourceDrawable = glxDrawable;
    g_sharedState.fbConfigId = fbConfigId;
    g_sharedState.fbConfig = fbConfig;
    g_sharedState.renderContext = renderContext;
    g_sharedState.mirrorContext = mirrorContext;
    g_sharedState.renderDrawable = renderDrawable;
    g_sharedState.mirrorDrawable = mirrorDrawable;
    g_sharedState.ready = true;
    g_sharedGeneration.fetch_add(1, std::memory_order_acq_rel);

    std::string info = "shared GLX contexts initialized";
    if (!recreateReason.empty()) {
        info.append(" (recreated: ");
        info.append(recreateReason);
        info.append(")");
    }
    info.append(" ");
    info.append(DescribeFingerprint(g_sharedState));
    SetLastInitInfo(info);
    return true;
}

bool AreSharedGlxContextsReady() {
    std::lock_guard<std::mutex> lock(g_sharedMutex);
    return g_sharedState.ready;
}

GlxSharedContextHandles GetSharedGlxContextHandles() {
    std::lock_guard<std::mutex> lock(g_sharedMutex);
    GlxSharedContextHandles handles;
    handles.nativeDisplay = g_sharedState.display;
    handles.renderContext = g_sharedState.renderContext;
    handles.renderDrawable = static_cast<std::uint64_t>(g_sharedState.renderDrawable);
    handles.mirrorContext = g_sharedState.mirrorContext;
    handles.mirrorDrawable = static_cast<std::uint64_t>(g_sharedState.mirrorDrawable);
    return handles;
}

std::string GetSharedGlxContextLastError() {
    std::lock_guard<std::mutex> lock(g_errorMutex);
    return g_lastError;
}

std::string GetSharedGlxContextLastInitInfo() {
    std::lock_guard<std::mutex> lock(g_infoMutex);
    return g_lastInitInfo;
}

std::uint64_t GetSharedGlxContextGeneration() {
    return g_sharedGeneration.load(std::memory_order_acquire);
}

bool MakeSharedGlxContextCurrent(GlxSharedContextRole role, GlxContextRestoreState& outRestore) {
    std::lock_guard<std::mutex> lock(g_sharedMutex);
    if (!g_sharedState.ready || !g_sharedState.display) { return false; }

    Display* display = g_sharedState.display;
    GLXContext targetContext = (role == GlxSharedContextRole::Mirror) ? g_sharedState.mirrorContext : g_sharedState.renderContext;
    GLXDrawable targetDrawable = (role == GlxSharedContextRole::Mirror) ? g_sharedState.mirrorDrawable : g_sharedState.renderDrawable;
    if (!targetContext || !targetDrawable) { return false; }

    outRestore.display = glXGetCurrentDisplay();
    outRestore.context = glXGetCurrentContext();
    outRestore.drawDrawable = static_cast<std::uint64_t>(glXGetCurrentDrawable());
    outRestore.readDrawable = static_cast<std::uint64_t>(glXGetCurrentReadDrawable());
    outRestore.valid = true;

    if (!glXMakeContextCurrent(display, targetDrawable, targetDrawable, targetContext)) {
        outRestore.valid = false;
        return false;
    }

    return true;
}

bool RestoreGlxContext(const GlxContextRestoreState& restore) {
    if (!restore.valid) { return false; }
    Display* display = reinterpret_cast<Display*>(restore.display);
    GLXContext context = reinterpret_cast<GLXContext>(restore.context);
    GLXDrawable draw = static_cast<GLXDrawable>(restore.drawDrawable);
    GLXDrawable read = static_cast<GLXDrawable>(restore.readDrawable);
    if (!display) { return false; }

    if (!context) {
        return glXMakeContextCurrent(display, 0, 0, nullptr) == True;
    }

    return glXMakeContextCurrent(display, draw, read, context) == True;
}

void ShutdownSharedGlxContexts() {
    std::lock_guard<std::mutex> lock(g_sharedMutex);
    ShutdownSharedGlxContextsLocked(false);
}

void ShutdownSharedGlxContextsForProcessExit() {
    std::lock_guard<std::mutex> lock(g_sharedMutex);
    ShutdownSharedGlxContextsLocked(true);
}

} // namespace platform::x11

#endif // !__APPLE__
