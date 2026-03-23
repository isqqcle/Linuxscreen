#ifndef __APPLE__

#include "window_capture_linux_internal.h"
#include "mirror/glx_shared_contexts.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>
#include <GL/glx.h>
#include <GL/glxext.h>
#include <gio/gio.h>
#include <gio/gunixfdlist.h>
#include <pipewire/pipewire.h>
#include <spa/buffer/meta.h>
#include <spa/param/buffers.h>
#include <spa/param/video/raw-utils.h>
#include <spa/pod/builder.h>
#include <spa/utils/result.h>
#include <drm/drm_fourcc.h>
#include <sys/mman.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <optional>
#include <sstream>

namespace platform::x11 {

namespace {

// Wayland structs

struct PortalRequestResult {
    std::uint32_t response = 2;
    GVariant* results = nullptr;
    bool completed = false;
};

struct WaylandDmaBufTextureState {
    GLuint importTexture = 0;
    GLuint importFbo = 0;
    GLuint copyTexture = 0;
    GLuint copyFbo = 0;
    int width = 0;
    int height = 0;
};

struct WaylandPipeWireStream {
    std::string key;
    X11SourceRecord* record = nullptr;
    std::mutex mutex;
    std::string errorMessage;
    struct pw_thread_loop* loop = nullptr;
    struct pw_context* context = nullptr;
    struct pw_core* core = nullptr;
    struct pw_stream* stream = nullptr;
    struct spa_hook coreListener{};
    struct spa_hook streamListener{};
    struct spa_video_info_raw videoInfo{};
    WaylandDmaBufTextureState dmaBufTexture;
    std::uint32_t observedFrameCount = 0;
    std::uint32_t lastLoggedDataType = UINT32_MAX;
    bool loggedDmaBufFallback = false;
    bool preferDmaBufOnly = false;
    bool receivedFrame = false;
    bool needsCpuFallback = false;
    bool ready = false;
    bool failed = false;
    bool stopRequested = false;
};

// Wayland globals

std::atomic<std::uint64_t> g_portalTokenCounter{ 1 };
std::mutex g_waylandPortalCapabilityMutex;
std::mutex g_waylandDmaBufMutex;
guint32 g_waylandPortalScreenCastVersion = 0;
bool g_waylandPortalScreenCastAvailable = false;
std::string g_waylandPortalCapabilityMessage;
bool g_waylandPortalCapabilityInitialized = false;
std::chrono::steady_clock::time_point g_waylandPortalCapabilityCheckedAt{};
EGLDisplay g_waylandDmaBufDisplay = EGL_NO_DISPLAY;
void* g_waylandDmaBufNativeDisplay = nullptr;
bool g_waylandDmaBufDisplayInitialized = false;
bool g_waylandDmaBufAvailable = false;
bool g_waylandDmaBufModifierSupported = false;
bool g_waylandDmaBufProbeDisabledForSession = false;

std::once_flag g_glxEglFunctionsResolveOnce;
PFNEGLCREATEIMAGEKHRPROC g_eglCreateImageKHR = nullptr;
PFNEGLDESTROYIMAGEKHRPROC g_eglDestroyImageKHR = nullptr;
PFNGLEGLIMAGETARGETTEXTURE2DOESPROC g_glEGLImageTargetTexture2DOES = nullptr;
PFNGLGENFRAMEBUFFERSPROC g_glGenFramebuffersProc = nullptr;
PFNGLDELETEFRAMEBUFFERSPROC g_glDeleteFramebuffersProc = nullptr;
PFNGLBINDFRAMEBUFFERPROC g_glBindFramebufferProc = nullptr;
PFNGLFRAMEBUFFERTEXTURE2DPROC g_glFramebufferTexture2DProc = nullptr;
PFNGLBLITFRAMEBUFFERPROC g_glBlitFramebufferProc = nullptr;

// Extension helpers

bool HasExtensionToken(const char* extensions, const char* extension) {
    if (!extensions || !extension || extension[0] == '\0') {
        return false;
    }

    const std::string all = extensions;
    const std::string needle = extension;
    std::size_t start = 0;
    while ((start = all.find(needle, start)) != std::string::npos) {
        const bool leftOk = (start == 0) || std::isspace(static_cast<unsigned char>(all[start - 1]));
        const std::size_t end = start + needle.size();
        const bool rightOk = end >= all.size() || std::isspace(static_cast<unsigned char>(all[end]));
        if (leftOk && rightOk) {
            return true;
        }
        start = end;
    }
    return false;
}

void ResolveEglFunctions() {
    std::call_once(g_glxEglFunctionsResolveOnce, []() {
        g_eglCreateImageKHR = reinterpret_cast<PFNEGLCREATEIMAGEKHRPROC>(eglGetProcAddress("eglCreateImageKHR"));
        g_eglDestroyImageKHR = reinterpret_cast<PFNEGLDESTROYIMAGEKHRPROC>(eglGetProcAddress("eglDestroyImageKHR"));
        g_glEGLImageTargetTexture2DOES =
            reinterpret_cast<PFNGLEGLIMAGETARGETTEXTURE2DOESPROC>(eglGetProcAddress("glEGLImageTargetTexture2DOES"));
        g_glGenFramebuffersProc = reinterpret_cast<PFNGLGENFRAMEBUFFERSPROC>(
            glXGetProcAddressARB(reinterpret_cast<const GLubyte*>("glGenFramebuffers")));
        g_glDeleteFramebuffersProc = reinterpret_cast<PFNGLDELETEFRAMEBUFFERSPROC>(
            glXGetProcAddressARB(reinterpret_cast<const GLubyte*>("glDeleteFramebuffers")));
        g_glBindFramebufferProc = reinterpret_cast<PFNGLBINDFRAMEBUFFERPROC>(
            glXGetProcAddressARB(reinterpret_cast<const GLubyte*>("glBindFramebuffer")));
        g_glFramebufferTexture2DProc = reinterpret_cast<PFNGLFRAMEBUFFERTEXTURE2DPROC>(
            glXGetProcAddressARB(reinterpret_cast<const GLubyte*>("glFramebufferTexture2D")));
        g_glBlitFramebufferProc = reinterpret_cast<PFNGLBLITFRAMEBUFFERPROC>(
            glXGetProcAddressARB(reinterpret_cast<const GLubyte*>("glBlitFramebuffer")));
    });
}

// Wayland selection helpers

void RememberWaylandSelectionLocked(const std::string& key,
                                    const WindowCaptureRequest& request,
                                    const WaylandPortalSession& session) {
    WaylandRememberedSelection& remembered = g_waylandRememberedSelections[key];
    remembered.request = request;
    if (!session.restoreToken.empty()) {
        remembered.request.selectionToken = session.restoreToken;
    }
    if (!session.appId.empty()) {
        remembered.request.appId = session.appId;
    }
    if (!session.windowTitle.empty()) {
        remembered.request.windowTitle = session.windowTitle;
    }
    if (session.width > 0) {
        remembered.width = session.width;
    }
    if (session.height > 0) {
        remembered.height = session.height;
    }
}

// SPA format mapping

std::uint32_t MapSpaVideoFormatToDrmFourcc(enum spa_video_format format) {
    switch (format) {
    case SPA_VIDEO_FORMAT_BGRx:
        return DRM_FORMAT_XRGB8888;
    case SPA_VIDEO_FORMAT_BGRA:
        return DRM_FORMAT_ARGB8888;
    case SPA_VIDEO_FORMAT_RGBx:
        return DRM_FORMAT_XBGR8888;
    case SPA_VIDEO_FORMAT_RGBA:
        return DRM_FORMAT_ABGR8888;
    case SPA_VIDEO_FORMAT_xRGB:
        return DRM_FORMAT_BGRX8888;
    case SPA_VIDEO_FORMAT_ARGB:
        return DRM_FORMAT_BGRA8888;
    case SPA_VIDEO_FORMAT_xBGR:
        return DRM_FORMAT_RGBX8888;
    case SPA_VIDEO_FORMAT_ABGR:
        return DRM_FORMAT_RGBA8888;
    default:
        return DRM_FORMAT_INVALID;
    }
}

const char* DescribeSpaDataType(std::uint32_t dataType) {
    switch (dataType) {
    case SPA_DATA_Invalid:
        return "Invalid";
    case SPA_DATA_MemPtr:
        return "MemPtr";
    case SPA_DATA_MemFd:
        return "MemFd";
    case SPA_DATA_DmaBuf:
        return "DmaBuf";
    case SPA_DATA_MemId:
        return "MemId";
#ifdef SPA_DATA_SyncObj
    case SPA_DATA_SyncObj:
        return "SyncObj";
#endif
    default:
        return "Unknown";
    }
}

// DMA-BUF display management

bool EnsureWaylandDmaBufDisplay(bool& outModifierSupported) {
    outModifierSupported = false;
    ResolveEglFunctions();
    if (!g_eglCreateImageKHR || !g_eglDestroyImageKHR || !g_glEGLImageTargetTexture2DOES) {
        return false;
    }
    if (!AreSharedGlxContextsReady()) {
        return false;
    }

    GlxSharedContextHandles handles = GetSharedGlxContextHandles();
    Display* nativeDisplay = reinterpret_cast<Display*>(handles.nativeDisplay);
    if (!nativeDisplay) {
        return false;
    }

    std::lock_guard<std::mutex> lock(g_waylandDmaBufMutex);
    if (!g_waylandDmaBufDisplayInitialized || g_waylandDmaBufNativeDisplay != nativeDisplay) {
        if (g_waylandDmaBufDisplay != EGL_NO_DISPLAY) {
            eglTerminate(g_waylandDmaBufDisplay);
        }

        g_waylandDmaBufDisplay = eglGetDisplay(reinterpret_cast<EGLNativeDisplayType>(nativeDisplay));
        g_waylandDmaBufNativeDisplay = nativeDisplay;
        g_waylandDmaBufDisplayInitialized = true;
        g_waylandDmaBufAvailable = false;
        g_waylandDmaBufModifierSupported = false;
        g_waylandDmaBufProbeDisabledForSession = false;
        if (g_waylandDmaBufDisplay != EGL_NO_DISPLAY) {
            EGLint major = 0;
            EGLint minor = 0;
            if (eglInitialize(g_waylandDmaBufDisplay, &major, &minor) == EGL_TRUE) {
                const char* extensions = eglQueryString(g_waylandDmaBufDisplay, EGL_EXTENSIONS);
                g_waylandDmaBufAvailable =
                    HasExtensionToken(extensions, "EGL_KHR_image_base") &&
                    HasExtensionToken(extensions, "EGL_EXT_image_dma_buf_import");
                g_waylandDmaBufModifierSupported =
                    HasExtensionToken(extensions, "EGL_EXT_image_dma_buf_import_modifiers");
            }
        }
    }

    outModifierSupported = g_waylandDmaBufModifierSupported;
    return g_waylandDmaBufAvailable;
}

void MarkWaylandDmaBufUnavailableForSession(const std::string& reason) {
    std::lock_guard<std::mutex> lock(g_waylandDmaBufMutex);
    if (!g_waylandDmaBufProbeDisabledForSession) {
        DebugWindowCaptureLog("wayland dmabuf session fallback reason=%s",
                              reason.empty() ? "unknown" : SanitizeDebugValue(reason).c_str());
    }
    g_waylandDmaBufProbeDisabledForSession = true;
}

bool ShouldAttemptWaylandDmaBufOnly() {
    const char* overrideValue = std::getenv("LINUXSCREEN_WAYLAND_DISABLE_DMABUF_FIRST");
    if (overrideValue && overrideValue[0] != '\0' && std::strcmp(overrideValue, "0") != 0) {
        return false;
    }

    bool modifierSupported = false;
    if (!EnsureWaylandDmaBufDisplay(modifierSupported)) {
        return false;
    }

    std::lock_guard<std::mutex> lock(g_waylandDmaBufMutex);
    return !g_waylandDmaBufProbeDisabledForSession;
}

// Portal capability

guint32 ReadPortalInterfaceVersion(GDBusConnection* connection, const char* interfaceName) {
    if (!connection || !interfaceName) {
        return 0;
    }

    GError* error = nullptr;
    GVariant* response = g_dbus_connection_call_sync(connection,
                                                     "org.freedesktop.portal.Desktop",
                                                     "/org/freedesktop/portal/desktop",
                                                     "org.freedesktop.DBus.Properties",
                                                     "Get",
                                                     g_variant_new("(ss)", interfaceName, "version"),
                                                     G_VARIANT_TYPE("(v)"),
                                                     G_DBUS_CALL_FLAGS_NONE,
                                                     -1,
                                                     nullptr,
                                                     &error);
    if (!response) {
        if (error) {
            g_error_free(error);
        }
        return 0;
    }

    GVariant* value = nullptr;
    g_variant_get(response, "(@v)", &value);
    guint32 version = 0;
    if (value) {
        GVariant* innerValue = g_variant_get_variant(value);
        if (innerValue) {
            version = g_variant_get_uint32(innerValue);
            g_variant_unref(innerValue);
        }
        g_variant_unref(value);
    }
    g_variant_unref(response);
    return version;
}

// Portal utilities

std::string SanitizeObjectPathElement(const std::string& value) {
    std::string sanitized;
    sanitized.reserve(value.size());
    for (const char ch : value) {
        if ((ch >= 'a' && ch <= 'z') ||
            (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9')) {
            sanitized.push_back(ch);
        } else {
            sanitized.push_back('_');
        }
    }
    if (sanitized.empty()) {
        sanitized = "linuxscreen";
    }
    return sanitized;
}

std::string MakePortalToken(const char* prefix) {
    std::ostringstream stream;
    stream << prefix << "_" << g_portalTokenCounter.fetch_add(1, std::memory_order_relaxed);
    return SanitizeObjectPathElement(stream.str());
}

std::string MakePortalRequestPath(GDBusConnection* connection, const std::string& handleToken) {
    const char* uniqueName = connection ? g_dbus_connection_get_unique_name(connection) : nullptr;
    std::string sender = uniqueName ? uniqueName : std::string("1_0");
    if (!sender.empty() && sender.front() == ':') {
        sender.erase(sender.begin());
    }
    sender = SanitizeObjectPathElement(sender);
    return "/org/freedesktop/portal/desktop/request/" + sender + "/" + handleToken;
}

std::string MakePortalSessionPath(GDBusConnection* connection, const std::string& sessionToken) {
    const char* uniqueName = connection ? g_dbus_connection_get_unique_name(connection) : nullptr;
    std::string sender = uniqueName ? uniqueName : std::string("1_0");
    if (!sender.empty() && sender.front() == ':') {
        sender.erase(sender.begin());
    }
    sender = SanitizeObjectPathElement(sender);
    return "/org/freedesktop/portal/desktop/session/" + sender + "/" + sessionToken;
}

void OnPortalResponseSignal(GDBusConnection* connection,
                            const gchar* senderName,
                            const gchar* objectPath,
                            const gchar* interfaceName,
                            const gchar* signalName,
                            GVariant* parameters,
                            gpointer userData) {
    (void)connection;
    (void)senderName;
    (void)objectPath;
    (void)interfaceName;
    (void)signalName;

    auto* result = static_cast<PortalRequestResult*>(userData);
    if (!result || !parameters) {
        return;
    }

    g_variant_get(parameters, "(u@a{sv})", &result->response, &result->results);
    result->completed = true;
}

bool WaitForPortalResponse(GDBusConnection* connection,
                           const std::string& requestPath,
                           PortalRequestResult& outResult,
                           GError** error) {
    if (!connection || requestPath.empty()) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Invalid portal request path.");
        return false;
    }

    guint subscriptionId = g_dbus_connection_signal_subscribe(connection,
                                                              "org.freedesktop.portal.Desktop",
                                                              "org.freedesktop.portal.Request",
                                                              "Response",
                                                              requestPath.c_str(),
                                                              nullptr,
                                                              G_DBUS_SIGNAL_FLAGS_NO_MATCH_RULE,
                                                              OnPortalResponseSignal,
                                                              &outResult,
                                                              nullptr);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(120);
    while (!outResult.completed && std::chrono::steady_clock::now() < deadline) {
        g_main_context_iteration(nullptr, true);
    }
    g_dbus_connection_signal_unsubscribe(connection, subscriptionId);

    if (!outResult.completed) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT, "Timed out waiting for portal response.");
        return false;
    }
    return true;
}

bool CallPortalRequestMethod(GDBusConnection* connection,
                             const char* methodName,
                             GVariant* parameters,
                             const std::string& expectedRequestPath,
                             PortalRequestResult& outResult,
                             GError** error) {
    if (!connection || !methodName || !parameters) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Invalid portal request call.");
        return false;
    }

    GVariant* reply = g_dbus_connection_call_sync(connection,
                                                  "org.freedesktop.portal.Desktop",
                                                  "/org/freedesktop/portal/desktop",
                                                  "org.freedesktop.portal.ScreenCast",
                                                  methodName,
                                                  parameters,
                                                  G_VARIANT_TYPE("(o)"),
                                                  G_DBUS_CALL_FLAGS_NONE,
                                                  -1,
                                                  nullptr,
                                                  error);
    if (!reply) {
        return false;
    }

    const char* requestPathCString = nullptr;
    g_variant_get(reply, "(&o)", &requestPathCString);
    const std::string requestPath = requestPathCString ? requestPathCString : expectedRequestPath;
    g_variant_unref(reply);
    return WaitForPortalResponse(connection, requestPath, outResult, error);
}

bool ExtractVariantIntPair(GVariant* value, int& outFirst, int& outSecond) {
    if (!value) {
        return false;
    }
    if (g_variant_is_of_type(value, G_VARIANT_TYPE("(ii)"))) {
        g_variant_get(value, "(ii)", &outFirst, &outSecond);
        return true;
    }
    if (g_variant_is_of_type(value, G_VARIANT_TYPE("(uu)"))) {
        guint32 first = 0;
        guint32 second = 0;
        g_variant_get(value, "(uu)", &first, &second);
        outFirst = static_cast<int>(first);
        outSecond = static_cast<int>(second);
        return true;
    }
    return false;
}

void PopulatePortalSessionMetadataFromProps(WaylandPortalSession& ioSession, GVariant* props) {
    if (!props) {
        return;
    }

    GVariant* sizeValue = g_variant_lookup_value(props, "size", nullptr);
    if (sizeValue) {
        ExtractVariantIntPair(sizeValue, ioSession.width, ioSession.height);
        g_variant_unref(sizeValue);
    }
}

void CloseWaylandPortalSession(WaylandPortalSession& ioSession) {
    if (!ioSession.connection || ioSession.sessionPath.empty()) {
        if (ioSession.connection) {
            g_object_unref(ioSession.connection);
            ioSession.connection = nullptr;
        }
        ioSession = {};
        return;
    }

    GError* error = nullptr;
    GVariant* ignored = g_dbus_connection_call_sync(ioSession.connection,
                                                    "org.freedesktop.portal.Desktop",
                                                    ioSession.sessionPath.c_str(),
                                                    "org.freedesktop.portal.Session",
                                                    "Close",
                                                    nullptr,
                                                    nullptr,
                                                    G_DBUS_CALL_FLAGS_NONE,
                                                    -1,
                                                    nullptr,
                                                    &error);
    if (ignored) {
        g_variant_unref(ignored);
    }
    if (error) {
        g_error_free(error);
    }

    g_object_unref(ioSession.connection);
    ioSession.connection = nullptr;
    ioSession.sessionPath.clear();
    ioSession.restoreToken.clear();
    ioSession.appId.clear();
    ioSession.windowTitle.clear();
    ioSession.nodeId = 0;
    ioSession.width = 0;
    ioSession.height = 0;
}

// Portal session creation

bool OpenWaylandPipeWireRemote(WaylandPortalSession& session, int& outFd, std::string& outMessage) {
    outFd = -1;
    if (!session.connection || session.sessionPath.empty()) {
        outMessage = "Wayland portal session is not active.";
        return false;
    }

    GVariantBuilder options;
    g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);

    GError* error = nullptr;
    GUnixFDList* outFdList = nullptr;
    GVariant* response = g_dbus_connection_call_with_unix_fd_list_sync(session.connection,
                                                                       "org.freedesktop.portal.Desktop",
                                                                       "/org/freedesktop/portal/desktop",
                                                                       "org.freedesktop.portal.ScreenCast",
                                                                       "OpenPipeWireRemote",
                                                                       g_variant_new("(oa{sv})", session.sessionPath.c_str(), &options),
                                                                       G_VARIANT_TYPE("(h)"),
                                                                       G_DBUS_CALL_FLAGS_NONE,
                                                                       -1,
                                                                       nullptr,
                                                                       &outFdList,
                                                                       nullptr,
                                                                       &error);
    if (!response) {
        outMessage = error ? error->message : "Failed to open the PipeWire remote.";
        if (error) {
            g_error_free(error);
        }
        if (outFdList) {
            g_object_unref(outFdList);
        }
        return false;
    }

    gint fdIndex = -1;
    g_variant_get(response, "(h)", &fdIndex);
    g_variant_unref(response);
    outFd = g_unix_fd_list_get(outFdList, fdIndex, &error);
    g_object_unref(outFdList);
    if (outFd < 0) {
        outMessage = error ? error->message : "The portal did not return a valid PipeWire file descriptor.";
        if (error) {
            g_error_free(error);
        }
        return false;
    }
    return true;
}

// DMA-BUF texture

void DestroyWaylandDmaBufTextureState(WaylandDmaBufTextureState& state) {
    if (!AreSharedGlxContextsReady()) {
        state = {};
        return;
    }

    GlxContextRestoreState restore;
    if (MakeSharedGlxContextCurrent(GlxSharedContextRole::Render, restore)) {
        if (state.importFbo && g_glDeleteFramebuffersProc) {
            g_glDeleteFramebuffersProc(1, &state.importFbo);
        }
        if (state.copyFbo && g_glDeleteFramebuffersProc) {
            g_glDeleteFramebuffersProc(1, &state.copyFbo);
        }
        if (state.importTexture) {
            glDeleteTextures(1, &state.importTexture);
        }
        if (state.copyTexture) {
            glDeleteTextures(1, &state.copyTexture);
        }
        glFlush();
        RestoreGlxContext(restore);
    }

    state = {};
}

bool CopyWaylandDmaBufToTexture(WaylandPipeWireStream& state,
                                const struct spa_buffer& buffer,
                                WindowCaptureTextureSnapshot& outTexture) {
    outTexture = {};
    if (!buffer.datas || buffer.n_datas == 0) {
        return false;
    }

    bool modifierSupported = false;
    if (!EnsureWaylandDmaBufDisplay(modifierSupported) || !AreSharedGlxContextsReady()) {
        return false;
    }
    if (!g_glGenFramebuffersProc || !g_glBindFramebufferProc || !g_glFramebufferTexture2DProc || !g_glBlitFramebufferProc) {
        return false;
    }

    const auto& data = buffer.datas[0];
    if (data.type != SPA_DATA_DmaBuf || data.fd < 0 || !data.chunk) {
        return false;
    }

    const int width = static_cast<int>(state.videoInfo.size.width);
    const int height = static_cast<int>(state.videoInfo.size.height);
    const int stride = data.chunk->stride > 0 ? data.chunk->stride : width * 4;
    const int offset = static_cast<int>(data.chunk->offset);
    const std::uint32_t drmFormat = MapSpaVideoFormatToDrmFourcc(state.videoInfo.format);
    if (width <= 0 || height <= 0 || stride <= 0 || drmFormat == DRM_FORMAT_INVALID) {
        return false;
    }

    std::uint64_t modifier = DRM_FORMAT_MOD_INVALID;
    if (state.videoInfo.flags & SPA_VIDEO_FLAG_MODIFIER) {
        modifier = state.videoInfo.modifier;
        if (!modifierSupported && modifier != DRM_FORMAT_MOD_INVALID) {
            return false;
        }
    }

    GlxContextRestoreState restore;
    if (!MakeSharedGlxContextCurrent(GlxSharedContextRole::Render, restore)) {
        return false;
    }

    WaylandDmaBufTextureState& textureState = state.dmaBufTexture;
    if (textureState.importTexture == 0) {
        glGenTextures(1, &textureState.importTexture);
        glBindTexture(GL_TEXTURE_2D, textureState.importTexture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    if (textureState.importFbo == 0) {
        g_glGenFramebuffersProc(1, &textureState.importFbo);
    }
    if (textureState.copyFbo == 0) {
        g_glGenFramebuffersProc(1, &textureState.copyFbo);
    }

    if (textureState.copyTexture == 0 ||
        textureState.width != width ||
        textureState.height != height) {
        if (textureState.copyTexture == 0) {
            glGenTextures(1, &textureState.copyTexture);
        }
        glBindTexture(GL_TEXTURE_2D, textureState.copyTexture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        g_glBindFramebufferProc(GL_FRAMEBUFFER, textureState.copyFbo);
        g_glFramebufferTexture2DProc(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, textureState.copyTexture, 0);
        g_glBindFramebufferProc(GL_FRAMEBUFFER, 0);
        textureState.width = width;
        textureState.height = height;
    }

    std::array<EGLint, 32> attribs{};
    std::size_t attrIndex = 0;
    attribs[attrIndex++] = EGL_WIDTH;
    attribs[attrIndex++] = width;
    attribs[attrIndex++] = EGL_HEIGHT;
    attribs[attrIndex++] = height;
    attribs[attrIndex++] = EGL_LINUX_DRM_FOURCC_EXT;
    attribs[attrIndex++] = static_cast<EGLint>(drmFormat);
    attribs[attrIndex++] = EGL_DMA_BUF_PLANE0_FD_EXT;
    attribs[attrIndex++] = data.fd;
    attribs[attrIndex++] = EGL_DMA_BUF_PLANE0_OFFSET_EXT;
    attribs[attrIndex++] = offset;
    attribs[attrIndex++] = EGL_DMA_BUF_PLANE0_PITCH_EXT;
    attribs[attrIndex++] = stride;
    if (modifierSupported && modifier != DRM_FORMAT_MOD_INVALID) {
        attribs[attrIndex++] = EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT;
        attribs[attrIndex++] = static_cast<EGLint>(modifier & 0xffffffffu);
        attribs[attrIndex++] = EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT;
        attribs[attrIndex++] = static_cast<EGLint>((modifier >> 32) & 0xffffffffu);
    }
    attribs[attrIndex++] = EGL_NONE;

    EGLImageKHR image =
        g_eglCreateImageKHR(g_waylandDmaBufDisplay,
                            EGL_NO_CONTEXT,
                            EGL_LINUX_DMA_BUF_EXT,
                            nullptr,
                            attribs.data());
    if (image == EGL_NO_IMAGE_KHR) {
        DebugWindowCaptureLog("wayland dmabuf import failed key=%s format=%u drm=%u stride=%d modifier=0x%llx",
                              state.key.c_str(),
                              static_cast<unsigned int>(state.videoInfo.format),
                              static_cast<unsigned int>(drmFormat),
                              stride,
                              static_cast<unsigned long long>(modifier));
        RestoreGlxContext(restore);
        return false;
    }

    GLint previousReadFbo = 0;
    GLint previousDrawFbo = 0;
    GLint previousTexture = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousReadFbo);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousDrawFbo);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);

    glBindTexture(GL_TEXTURE_2D, textureState.importTexture);
    g_glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, reinterpret_cast<GLeglImageOES>(image));

    g_glBindFramebufferProc(GL_READ_FRAMEBUFFER, textureState.importFbo);
    g_glFramebufferTexture2DProc(GL_READ_FRAMEBUFFER,
                                 GL_COLOR_ATTACHMENT0,
                                 GL_TEXTURE_2D,
                                 textureState.importTexture,
                                 0);
    g_glBindFramebufferProc(GL_DRAW_FRAMEBUFFER, textureState.copyFbo);
    g_glBlitFramebufferProc(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST);

    g_glBindFramebufferProc(GL_READ_FRAMEBUFFER, static_cast<GLuint>(previousReadFbo));
    g_glBindFramebufferProc(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(previousDrawFbo));
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTexture));

    g_eglDestroyImageKHR(g_waylandDmaBufDisplay, image);
    glFlush();

    outTexture.textureId = textureState.copyTexture;
    outTexture.width = width;
    outTexture.height = height;
    outTexture.yInverted = true;
    RestoreGlxContext(restore);
    return outTexture.textureId != 0;
}

// SPA buffer to frame

bool CopyWaylandSpaBufferToFrame(WaylandPipeWireStream& state,
                                 const struct spa_buffer& buffer,
                                 LatestFrameSnapshot& outFrame) {
    if (!buffer.datas || buffer.n_datas == 0) {
        return false;
    }

    const auto& data = buffer.datas[0];
    const int width = static_cast<int>(state.videoInfo.size.width);
    const int height = static_cast<int>(state.videoInfo.size.height);
    if (width <= 0 || height <= 0 || !data.chunk) {
        return false;
    }

    const int srcStride = data.chunk->stride > 0 ? data.chunk->stride : width * 4;
    if (srcStride < width * 4) {
        return false;
    }

    const std::size_t offset = data.chunk->offset;
    const std::uint8_t* src = nullptr;
    void* mapped = nullptr;
    if (data.data) {
        src = static_cast<const std::uint8_t*>(data.data) + offset;
    } else if (data.type == SPA_DATA_MemFd && data.fd >= 0) {
        mapped = mmap(nullptr,
                      static_cast<std::size_t>(data.maxsize),
                      PROT_READ,
                      MAP_PRIVATE,
                      data.fd,
                      data.mapoffset);
        if (mapped == MAP_FAILED) {
            return false;
        }
        src = static_cast<const std::uint8_t*>(mapped) + offset;
    } else {
        return false;
    }

    outFrame = {};
    outFrame.width = width;
    outFrame.height = height;
    outFrame.bytesPerRow = width * 4;
    outFrame.contentWidth = width;
    outFrame.contentHeight = height;
    outFrame.pixels.resize(static_cast<std::size_t>(outFrame.bytesPerRow) * static_cast<std::size_t>(height));

    int cropX = 0;
    int cropY = 0;
    int cropWidth = width;
    int cropHeight = height;
    for (uint32_t metaIndex = 0; metaIndex < buffer.n_metas; ++metaIndex) {
        const spa_meta& meta = buffer.metas[metaIndex];
        if (meta.type != SPA_META_VideoCrop || !meta.data || meta.size < sizeof(spa_meta_region)) {
            continue;
        }
        const auto* region = static_cast<const spa_meta_region*>(meta.data);
        if (!spa_meta_region_is_valid(region)) {
            continue;
        }
        cropX = std::max(0, region->region.position.x);
        cropY = std::max(0, region->region.position.y);
        cropWidth = std::max(1, static_cast<int>(region->region.size.width));
        cropHeight = std::max(1, static_cast<int>(region->region.size.height));
        break;
    }
    outFrame.contentX = std::clamp(cropX, 0, width);
    outFrame.contentY = std::clamp(cropY, 0, height);
    outFrame.contentWidth = std::clamp(cropWidth, 1, width - outFrame.contentX);
    outFrame.contentHeight = std::clamp(cropHeight, 1, height - outFrame.contentY);

    const auto format = state.videoInfo.format;
    for (int row = 0; row < height; ++row) {
        const std::uint8_t* srcRow = src + static_cast<std::size_t>(row) * static_cast<std::size_t>(srcStride);
        std::uint8_t* dstRow = outFrame.pixels.data() + static_cast<std::size_t>(row) * static_cast<std::size_t>(outFrame.bytesPerRow);
        if (format == SPA_VIDEO_FORMAT_BGRx || format == SPA_VIDEO_FORMAT_BGRA) {
            std::memcpy(dstRow, srcRow, static_cast<std::size_t>(width) * 4);
            if (format == SPA_VIDEO_FORMAT_BGRx) {
                for (int x = 0; x < width; ++x) {
                    dstRow[x * 4 + 3] = 255;
                }
            }
            continue;
        }

        for (int x = 0; x < width; ++x) {
            const std::uint8_t* srcPixel = srcRow + x * 4;
            std::uint8_t* dstPixel = dstRow + x * 4;
            switch (format) {
            case SPA_VIDEO_FORMAT_RGBx:
            case SPA_VIDEO_FORMAT_RGBA:
                dstPixel[0] = srcPixel[2];
                dstPixel[1] = srcPixel[1];
                dstPixel[2] = srcPixel[0];
                dstPixel[3] = (format == SPA_VIDEO_FORMAT_RGBA) ? srcPixel[3] : 255;
                break;
            case SPA_VIDEO_FORMAT_xRGB:
            case SPA_VIDEO_FORMAT_ARGB:
                dstPixel[0] = srcPixel[3];
                dstPixel[1] = srcPixel[2];
                dstPixel[2] = srcPixel[1];
                dstPixel[3] = (format == SPA_VIDEO_FORMAT_ARGB) ? srcPixel[0] : 255;
                break;
            case SPA_VIDEO_FORMAT_xBGR:
            case SPA_VIDEO_FORMAT_ABGR:
                dstPixel[0] = srcPixel[1];
                dstPixel[1] = srcPixel[2];
                dstPixel[2] = srcPixel[3];
                dstPixel[3] = (format == SPA_VIDEO_FORMAT_ABGR) ? srcPixel[0] : 255;
                break;
            default:
                if (mapped && mapped != MAP_FAILED) {
                    munmap(mapped, static_cast<std::size_t>(data.maxsize));
                }
                return false;
            }
        }
    }

    if (mapped && mapped != MAP_FAILED) {
        munmap(mapped, static_cast<std::size_t>(data.maxsize));
    }
    return true;
}

// PipeWire callbacks

void OnWaylandCoreError(void* data, uint32_t id, int seq, int res, const char* message) {
    (void)id;
    (void)seq;
    auto* state = static_cast<WaylandPipeWireStream*>(data);
    if (!state || !state->loop) {
        return;
    }

    std::lock_guard<std::mutex> lock(state->mutex);
    state->failed = true;
    state->errorMessage = message ? message : spa_strerror(res);
    pw_thread_loop_signal(state->loop, false);
}

void OnWaylandStreamStateChanged(void* data,
                                 enum pw_stream_state oldState,
                                 enum pw_stream_state newState,
                                 const char* error) {
    auto* state = static_cast<WaylandPipeWireStream*>(data);
    if (!state || !state->loop) {
        return;
    }

    std::lock_guard<std::mutex> lock(state->mutex);
    DebugWindowCaptureLog("wayland stream state key=%s old=%d new=%d error=%s",
                          state->key.c_str(),
                          static_cast<int>(oldState),
                          static_cast<int>(newState),
                          error ? error : "");
    if (newState == PW_STREAM_STATE_ERROR) {
        state->failed = true;
        state->errorMessage = error ? error : "PipeWire stream failed.";
    } else if (newState == PW_STREAM_STATE_PAUSED) {
        const int activeResult = state->stream ? pw_stream_set_active(state->stream, true) : -1;
        if (activeResult < 0) {
            state->failed = true;
            state->errorMessage = std::string("Failed to activate the PipeWire stream: ") + spa_strerror(activeResult);
        } else {
            state->ready = true;
        }
    } else if (newState == PW_STREAM_STATE_STREAMING) {
        state->ready = true;
    }
    pw_thread_loop_signal(state->loop, false);
}

void OnWaylandStreamParamChanged(void* data, std::uint32_t id, const struct spa_pod* param) {
    auto* state = static_cast<WaylandPipeWireStream*>(data);
    if (!state || !state->stream || id != SPA_PARAM_Format || !param) {
        return;
    }

    struct spa_video_info_raw info{};
    if (spa_format_video_raw_parse(param, &info) < 0) {
        return;
    }

    std::lock_guard<std::mutex> lock(state->mutex);
    state->videoInfo = info;
    DebugWindowCaptureLog("wayland stream format key=%s format=%u size=%ux%u",
                          state->key.c_str(),
                          static_cast<unsigned int>(info.format),
                          static_cast<unsigned int>(info.size.width),
                          static_cast<unsigned int>(info.size.height));
    bool canUseDmaBuf = false;
    const std::uint32_t drmFormat = MapSpaVideoFormatToDrmFourcc(info.format);
    if (drmFormat != DRM_FORMAT_INVALID) {
        bool modifierSupported = false;
        const bool eglReady = EnsureWaylandDmaBufDisplay(modifierSupported);
        const bool modifierOk = !(info.flags & SPA_VIDEO_FLAG_MODIFIER) ||
                                modifierSupported ||
                                info.modifier == DRM_FORMAT_MOD_INVALID;
        canUseDmaBuf = eglReady && modifierOk;
    }
    DebugWindowCaptureLog("wayland stream buffers key=%s dmabuf=%d format=%u modifier=0x%llx",
                          state->key.c_str(),
                          canUseDmaBuf ? 1 : 0,
                          static_cast<unsigned int>(info.format),
                          static_cast<unsigned long long>(info.modifier));
    if (state->preferDmaBufOnly && !canUseDmaBuf) {
        state->needsCpuFallback = true;
        state->errorMessage = "PipeWire format negotiation does not support DMA-BUF import for this stream.";
        pw_thread_loop_signal(state->loop, false);
        return;
    }
    std::uint8_t buffer[256];
    struct spa_pod_builder builder = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    const struct spa_pod* params[1];
    const int dataTypeFlags = state->preferDmaBufOnly
        ? (1 << SPA_DATA_DmaBuf)
        : ((1 << SPA_DATA_MemPtr) |
           (1 << SPA_DATA_MemFd) |
           (canUseDmaBuf ? (1 << SPA_DATA_DmaBuf) : 0));
    params[0] = static_cast<const struct spa_pod*>(spa_pod_builder_add_object(
        &builder,
        SPA_TYPE_OBJECT_ParamBuffers,
        SPA_PARAM_Buffers,
        SPA_PARAM_BUFFERS_buffers, SPA_POD_CHOICE_RANGE_Int(8, 2, 16),
        SPA_PARAM_BUFFERS_blocks, SPA_POD_Int(1),
        SPA_PARAM_BUFFERS_size, SPA_POD_Int(static_cast<int>(info.size.height) * std::max(1, static_cast<int>(info.size.width) * 4)),
        SPA_PARAM_BUFFERS_stride, SPA_POD_Int(static_cast<int>(info.size.width) * 4),
        SPA_PARAM_BUFFERS_dataType, SPA_POD_CHOICE_FLAGS_Int(dataTypeFlags)));
    pw_stream_update_params(state->stream, params, 1);

    std::lock_guard<std::mutex> recordLock(g_mutex);
    auto it = g_records.find(state->key);
    if (it == g_records.end()) {
        return;
    }
    auto rememberedIt = g_waylandRememberedSelections.find(state->key);
    if (rememberedIt != g_waylandRememberedSelections.end()) {
        rememberedIt->second.width = static_cast<int>(info.size.width);
        rememberedIt->second.height = static_cast<int>(info.size.height);
    }
    it->second->status.backend = WindowCaptureBackend::Wayland;
    it->second->status.access = WindowCaptureAccessState::Granted;
    it->second->status.width = static_cast<int>(info.size.width);
    it->second->status.height = static_cast<int>(info.size.height);
    if (it->second->status.state == WindowCaptureState::Starting) {
        it->second->status.message = "PipeWire stream connected. Waiting for frames...";
    }
}

void OnWaylandStreamProcess(void* data) {
    auto* state = static_cast<WaylandPipeWireStream*>(data);
    if (!state || !state->stream) {
        return;
    }

    struct pw_buffer* pwBuffer = pw_stream_dequeue_buffer(state->stream);
    if (!pwBuffer) {
        return;
    }

    bool shouldFallbackToCpu = false;
    if (pwBuffer->buffer && pwBuffer->buffer->datas && pwBuffer->buffer->n_datas > 0) {
        const spa_data& primaryData = pwBuffer->buffer->datas[0];
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->observedFrameCount += 1;
            if (state->lastLoggedDataType != primaryData.type || state->observedFrameCount <= 5) {
                DebugWindowCaptureLog("wayland stream data key=%s frame=%u type=%u(%s) nDatas=%u fd=%d max=%u chunkSize=%u",
                                      state->key.c_str(),
                                      static_cast<unsigned int>(state->observedFrameCount),
                                      static_cast<unsigned int>(primaryData.type),
                                      DescribeSpaDataType(primaryData.type),
                                      static_cast<unsigned int>(pwBuffer->buffer->n_datas),
                                      primaryData.fd,
                                      static_cast<unsigned int>(primaryData.maxsize),
                                      primaryData.chunk ? static_cast<unsigned int>(primaryData.chunk->size) : 0u);
                state->lastLoggedDataType = primaryData.type;
            }
            if (state->preferDmaBufOnly && primaryData.type != SPA_DATA_DmaBuf) {
                state->needsCpuFallback = true;
                state->errorMessage = std::string("PipeWire delivered ") +
                                      DescribeSpaDataType(primaryData.type) +
                                      " buffers instead of DMA-BUF.";
                shouldFallbackToCpu = true;
            }
        }
        if (shouldFallbackToCpu) {
            pw_stream_queue_buffer(state->stream, pwBuffer);
            pw_thread_loop_signal(state->loop, false);
            return;
        }
    }

    LatestFrameSnapshot frame;
    WindowCaptureTextureSnapshot textureSnapshot;
    const bool copiedTexture = CopyWaylandDmaBufToTexture(*state, *pwBuffer->buffer, textureSnapshot);
    const bool copiedFrame = copiedTexture ? false : CopyWaylandSpaBufferToFrame(*state, *pwBuffer->buffer, frame);
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (!copiedTexture &&
            pwBuffer->buffer &&
            pwBuffer->buffer->datas &&
            pwBuffer->buffer->n_datas > 0 &&
            pwBuffer->buffer->datas[0].type == SPA_DATA_DmaBuf &&
            !state->loggedDmaBufFallback) {
            const spa_data& primaryData = pwBuffer->buffer->datas[0];
            DebugWindowCaptureLog("wayland stream dmabuf fallback key=%s format=%u modifier=0x%llx stride=%d fd=%d",
                                  state->key.c_str(),
                                  static_cast<unsigned int>(state->videoInfo.format),
                                  static_cast<unsigned long long>(state->videoInfo.modifier),
                                  primaryData.chunk ? primaryData.chunk->stride : 0,
                                  primaryData.fd);
            state->loggedDmaBufFallback = true;
        }
        if (state->preferDmaBufOnly && !copiedTexture) {
            state->needsCpuFallback = true;
            if (state->errorMessage.empty()) {
                state->errorMessage = "DMA-BUF import did not succeed for the selected Wayland stream.";
            }
            shouldFallbackToCpu = true;
        }
        if (copiedTexture || copiedFrame) {
            state->receivedFrame = true;
        }
    }
    if (shouldFallbackToCpu) {
        pw_stream_queue_buffer(state->stream, pwBuffer);
        pw_thread_loop_signal(state->loop, false);
        return;
    }
    pw_stream_queue_buffer(state->stream, pwBuffer);
    if (!copiedTexture && !copiedFrame) {
        return;
    }

    std::lock_guard<std::mutex> recordLock(g_mutex);
    auto it = g_records.find(state->key);
    if (it == g_records.end()) {
        return;
    }

    const std::uint64_t nextFrameNumber =
        std::max(it->second->frame.frameNumber, it->second->textureSnapshot.frameNumber) + 1;
    if (copiedTexture) {
        textureSnapshot.frameNumber = nextFrameNumber;
        it->second->textureSnapshot = textureSnapshot;
        it->second->frame = {};
        DebugWindowCaptureLog("wayland stream frame key=%s size=%dx%d frame=%llu transport=dmabuf",
                              state->key.c_str(),
                              textureSnapshot.width,
                              textureSnapshot.height,
                              static_cast<unsigned long long>(textureSnapshot.frameNumber));
    } else {
        frame.frameNumber = nextFrameNumber;
        it->second->frame = frame;
        it->second->textureSnapshot = {};
        DebugWindowCaptureLog("wayland stream frame key=%s size=%dx%d frame=%llu transport=cpu",
                              state->key.c_str(),
                              frame.contentWidth,
                              frame.contentHeight,
                              static_cast<unsigned long long>(frame.frameNumber));
    }
    it->second->status.backend = WindowCaptureBackend::Wayland;
    it->second->status.access = WindowCaptureAccessState::Granted;
    it->second->status.state = WindowCaptureState::Streaming;
    it->second->status.message = copiedTexture
        ? "Streaming via Wayland portal/PipeWire DMA-BUF"
        : "Streaming via Wayland portal/PipeWire shared-memory fallback";
    it->second->status.width = copiedTexture ? textureSnapshot.width : frame.contentWidth;
    it->second->status.height = copiedTexture ? textureSnapshot.height : frame.contentHeight;
    it->second->status.frameNumber = nextFrameNumber;
    it->second->status.canPersistSelection = true;
    it->second->status.selectionPersistent = !it->second->request.selectionToken.empty();
}

// PipeWire stream lifecycle

bool StartWaylandPipeWireCapture(WaylandPipeWireStream& state,
                                 const WaylandPortalSession& session,
                                 int remoteFd,
                                 std::string& outMessage,
                                 bool preferDmaBufOnly) {
    state.preferDmaBufOnly = preferDmaBufOnly;
    state.receivedFrame = false;
    state.needsCpuFallback = false;
    state.stopRequested = false;
    state.observedFrameCount = 0;
    state.lastLoggedDataType = UINT32_MAX;
    state.loggedDmaBufFallback = false;
    state.ready = false;
    state.failed = false;
    state.errorMessage.clear();
    state.loop = pw_thread_loop_new("linuxscreen-wayland-capture", nullptr);
    if (!state.loop) {
        outMessage = "Failed to allocate the PipeWire thread loop.";
        return false;
    }
    if (pw_thread_loop_start(state.loop) < 0) {
        outMessage = "Failed to start the PipeWire thread loop.";
        return false;
    }

    pw_thread_loop_lock(state.loop);
    state.context = pw_context_new(pw_thread_loop_get_loop(state.loop), nullptr, 0);
    if (!state.context) {
        pw_thread_loop_unlock(state.loop);
        outMessage = "Failed to create the PipeWire context.";
        return false;
    }

    state.core = pw_context_connect_fd(state.context, remoteFd, nullptr, 0);
    if (!state.core) {
        pw_thread_loop_unlock(state.loop);
        outMessage = "Failed to connect to the portal PipeWire remote.";
        return false;
    }

    static const pw_core_events kCoreEvents = {
        PW_VERSION_CORE_EVENTS,
        nullptr,
        nullptr,
        nullptr,
        OnWaylandCoreError,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
    };
    pw_core_add_listener(state.core, &state.coreListener, &kCoreEvents, &state);

    pw_properties* props = pw_properties_new(PW_KEY_MEDIA_TYPE, "Video",
                                             PW_KEY_MEDIA_CATEGORY, "Capture",
                                             PW_KEY_MEDIA_ROLE, "Screen",
                                             nullptr);
    state.stream = pw_stream_new(state.core, "linuxscreen-wayland-window", props);
    if (!state.stream) {
        pw_thread_loop_unlock(state.loop);
        outMessage = "Failed to create the PipeWire capture stream.";
        return false;
    }

    static const pw_stream_events kStreamEvents = {
        PW_VERSION_STREAM_EVENTS,
        nullptr,
        OnWaylandStreamStateChanged,
        nullptr,
        nullptr,
        OnWaylandStreamParamChanged,
        nullptr,
        nullptr,
        OnWaylandStreamProcess,
        nullptr,
        nullptr,
        nullptr,
    };
    pw_stream_add_listener(state.stream, &state.streamListener, &kStreamEvents, &state);

    std::uint8_t paramsBuffer[2048];
    struct spa_pod_builder builder = SPA_POD_BUILDER_INIT(paramsBuffer, sizeof(paramsBuffer));
    const std::array<enum spa_video_format, 6> candidateFormats = {
        SPA_VIDEO_FORMAT_BGRx,
        SPA_VIDEO_FORMAT_BGRA,
        SPA_VIDEO_FORMAT_RGBx,
        SPA_VIDEO_FORMAT_RGBA,
        SPA_VIDEO_FORMAT_xRGB,
        SPA_VIDEO_FORMAT_xBGR,
    };
    const struct spa_pod* params[candidateFormats.size()];
    std::size_t paramCount = 0;
    for (const auto format : candidateFormats) {
        spa_video_info_raw info{};
        info.format = format;
        const struct spa_pod* pod = spa_format_video_raw_build(&builder, SPA_PARAM_EnumFormat, &info);
        if (pod) {
            params[paramCount++] = pod;
        }
    }
    if (paramCount == 0) {
        pw_thread_loop_unlock(state.loop);
        outMessage = "Failed to build PipeWire video format negotiation parameters.";
        return false;
    }

    const int connectResult = pw_stream_connect(state.stream,
                                                PW_DIRECTION_INPUT,
                                                session.nodeId,
                                                static_cast<pw_stream_flags>(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS),
                                                params,
                                                static_cast<std::uint32_t>(paramCount));
    if (connectResult < 0) {
        pw_thread_loop_unlock(state.loop);
        outMessage = std::string("Failed to connect the PipeWire stream: ") + spa_strerror(connectResult);
        return false;
    }

    while (!state.ready &&
           !state.failed &&
           !state.stopRequested &&
           !(state.record && state.record->stopRequested.load(std::memory_order_acquire))) {
        pw_thread_loop_timed_wait(state.loop, 1);
    }
    pw_thread_loop_unlock(state.loop);

    if (state.record && state.record->stopRequested.load(std::memory_order_acquire)) {
        outMessage = "Wayland capture startup was cancelled.";
        return false;
    }

    if (state.failed) {
        outMessage = state.errorMessage.empty() ? "The PipeWire stream failed to start." : state.errorMessage;
        return false;
    }

    return state.ready;
}

void StopWaylandPipeWireCapture(WaylandPipeWireStream& state) {
    state.stopRequested = true;
    if (!state.loop) {
        DestroyWaylandDmaBufTextureState(state.dmaBufTexture);
        return;
    }

    pw_thread_loop_lock(state.loop);
    pw_thread_loop_signal(state.loop, false);
    if (state.stream) {
        pw_stream_destroy(state.stream);
        state.stream = nullptr;
    }
    if (state.core) {
        pw_core_disconnect(state.core);
        state.core = nullptr;
    }
    if (state.context) {
        pw_context_destroy(state.context);
        state.context = nullptr;
    }
    pw_thread_loop_unlock(state.loop);

    pw_thread_loop_stop(state.loop);
    pw_thread_loop_destroy(state.loop);
    state.loop = nullptr;
    DestroyWaylandDmaBufTextureState(state.dmaBufTexture);
}

// Wayland record status helper

void UpdateWaylandRecordStatus(const std::string& key,
                               WindowCaptureState state,
                               const std::string& message,
                               int width = 0,
                               int height = 0,
                               std::uint64_t frameNumber = 0) {
    std::lock_guard<std::mutex> lock(g_mutex);
    auto it = g_records.find(key);
    if (it == g_records.end()) {
        return;
    }

    it->second->status.backend = WindowCaptureBackend::Wayland;
    it->second->status.access = WindowCaptureAccessState::Granted;
    it->second->status.state = state;
    it->second->status.message = message;
    if (width > 0) {
        it->second->status.width = width;
    }
    if (height > 0) {
        it->second->status.height = height;
    }
    it->second->status.frameNumber = frameNumber;
    it->second->status.canPersistSelection = true;
    it->second->status.selectionPersistent = !it->second->request.selectionToken.empty();
    if (state == WindowCaptureState::SelectionRequired ||
        state == WindowCaptureState::NotFound ||
        state == WindowCaptureState::NoAccess ||
        state == WindowCaptureState::Unsupported ||
        state == WindowCaptureState::Error) {
        it->second->textureSnapshot = {};
        it->second->frame = {};
    }
}

} // namespace

// Public Wayland functions

void RefreshWaylandPortalCapability(bool force) {
    std::lock_guard<std::mutex> lock(g_waylandPortalCapabilityMutex);
    const auto now = std::chrono::steady_clock::now();
    if (!force &&
        g_waylandPortalCapabilityInitialized &&
        now - g_waylandPortalCapabilityCheckedAt < std::chrono::seconds(2)) {
        return;
    }

    g_waylandPortalCapabilityInitialized = true;
    g_waylandPortalCapabilityCheckedAt = now;
    g_waylandPortalScreenCastVersion = 0;
    g_waylandPortalScreenCastAvailable = false;
    g_waylandPortalCapabilityMessage.clear();

    GError* error = nullptr;
    GDBusConnection* connection = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
    if (!connection) {
        g_waylandPortalCapabilityMessage = error
            ? std::string(error->message)
            : std::string("Failed to connect to the session D-Bus while checking portal support.");
        if (error) {
            g_error_free(error);
        }
        return;
    }

    g_waylandPortalScreenCastVersion =
        ReadPortalInterfaceVersion(connection, "org.freedesktop.portal.ScreenCast");
    g_object_unref(connection);

    if (g_waylandPortalScreenCastVersion == 0) {
        g_waylandPortalCapabilityMessage =
            "The current desktop portal does not expose ScreenCast support. Install a screencast-capable portal backend"
            " and restart the desktop session.";
        return;
    }

    g_waylandPortalScreenCastAvailable = true;
}

bool HasWaylandPortalScreenCastSupport() {
    RefreshWaylandPortalCapability(false);
    std::lock_guard<std::mutex> lock(g_waylandPortalCapabilityMutex);
    return g_waylandPortalScreenCastAvailable;
}

std::string GetWaylandPortalCapabilityMessage() {
    RefreshWaylandPortalCapability(false);
    std::lock_guard<std::mutex> lock(g_waylandPortalCapabilityMutex);
    if (!g_waylandPortalCapabilityMessage.empty()) {
        return g_waylandPortalCapabilityMessage;
    }
    return "Wayland portal ScreenCast support is unavailable.";
}

bool CreateWaylandPortalSession(const WindowCaptureRequest& request,
                                int fps,
                                bool allowInteractiveSelection,
                                bool keepSessionOpen,
                                WaylandPortalSession& outSession,
                                std::string& outMessage,
                                WindowCaptureSelectionResult* outSelectionResult) {
    (void)fps;

    GError* error = nullptr;
    GDBusConnection* connection = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
    if (!connection) {
        outMessage = error ? error->message : "Failed to connect to the session bus.";
        if (error) {
            g_error_free(error);
        }
        if (outSelectionResult) {
            *outSelectionResult = WindowCaptureSelectionResult::Failed;
        }
        return false;
    }

    const guint32 portalVersion = ReadPortalInterfaceVersion(connection, "org.freedesktop.portal.ScreenCast");
    if (portalVersion == 0) {
        outMessage = "The current desktop portal does not expose ScreenCast support.";
        g_object_unref(connection);
        if (outSelectionResult) {
            *outSelectionResult = WindowCaptureSelectionResult::Unsupported;
        }
        return false;
    }

    const std::string createHandleToken = MakePortalToken("linuxscreen_create");
    const std::string sessionToken = MakePortalToken("linuxscreen_session");
    const std::string expectedCreatePath = MakePortalRequestPath(connection, createHandleToken);

    GVariantBuilder createOptions;
    g_variant_builder_init(&createOptions, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&createOptions, "{sv}", "handle_token", g_variant_new_string(createHandleToken.c_str()));
    g_variant_builder_add(&createOptions, "{sv}", "session_handle_token", g_variant_new_string(sessionToken.c_str()));

    PortalRequestResult createResult;
    if (!CallPortalRequestMethod(connection,
                                 "CreateSession",
                                 g_variant_new("(a{sv})", &createOptions),
                                 expectedCreatePath,
                                 createResult,
                                 &error)) {
        outMessage = error ? error->message : "Failed to create a portal session.";
        if (error) {
            g_error_free(error);
        }
        g_object_unref(connection);
        if (outSelectionResult) {
            *outSelectionResult = WindowCaptureSelectionResult::Failed;
        }
        return false;
    }

    if (createResult.response != 0 || !createResult.results) {
        outMessage = createResult.response == 1
            ? "Window selection was cancelled."
            : "The desktop portal rejected the screen cast session.";
        if (createResult.results) {
            g_variant_unref(createResult.results);
        }
        g_object_unref(connection);
        if (outSelectionResult) {
            *outSelectionResult = createResult.response == 1
                ? WindowCaptureSelectionResult::Cancelled
                : WindowCaptureSelectionResult::Failed;
        }
        return false;
    }

    GVariant* sessionHandleValue = g_variant_lookup_value(createResult.results, "session_handle", G_VARIANT_TYPE_OBJECT_PATH);
    std::string sessionPath = sessionHandleValue
        ? std::string(g_variant_get_string(sessionHandleValue, nullptr))
        : MakePortalSessionPath(connection, sessionToken);
    if (sessionHandleValue) {
        g_variant_unref(sessionHandleValue);
    }
    g_variant_unref(createResult.results);

    const std::string selectHandleToken = MakePortalToken("linuxscreen_select");
    const std::string expectedSelectPath = MakePortalRequestPath(connection, selectHandleToken);
    GVariantBuilder selectOptions;
    g_variant_builder_init(&selectOptions, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&selectOptions, "{sv}", "handle_token", g_variant_new_string(selectHandleToken.c_str()));
    g_variant_builder_add(&selectOptions, "{sv}", "types", g_variant_new_uint32(2));
    g_variant_builder_add(&selectOptions, "{sv}", "multiple", g_variant_new_boolean(FALSE));
    if (portalVersion >= 4) {
        g_variant_builder_add(&selectOptions, "{sv}", "persist_mode", g_variant_new_uint32(2));
        if (!request.selectionToken.empty()) {
            g_variant_builder_add(&selectOptions, "{sv}", "restore_token", g_variant_new_string(request.selectionToken.c_str()));
        }
    }

    PortalRequestResult selectResult;
    if (!CallPortalRequestMethod(connection,
                                 "SelectSources",
                                 g_variant_new("(oa{sv})", sessionPath.c_str(), &selectOptions),
                                 expectedSelectPath,
                                 selectResult,
                                 &error)) {
        outMessage = error ? error->message : "Failed to request window selection from the portal.";
        if (error) {
            g_error_free(error);
        }
        WaylandPortalSession cleanup;
        cleanup.connection = connection;
        cleanup.sessionPath = sessionPath;
        CloseWaylandPortalSession(cleanup);
        if (outSelectionResult) {
            *outSelectionResult = WindowCaptureSelectionResult::Failed;
        }
        return false;
    }

    if (selectResult.results) {
        g_variant_unref(selectResult.results);
    }
    if (selectResult.response != 0) {
        outMessage = selectResult.response == 1
            ? "Window selection was cancelled."
            : "The desktop portal rejected the selected sources.";
        WaylandPortalSession cleanup;
        cleanup.connection = connection;
        cleanup.sessionPath = sessionPath;
        CloseWaylandPortalSession(cleanup);
        if (outSelectionResult) {
            *outSelectionResult = selectResult.response == 1
                ? WindowCaptureSelectionResult::Cancelled
                : WindowCaptureSelectionResult::Failed;
        }
        return false;
    }

    if (!allowInteractiveSelection && request.selectionToken.empty()) {
        outMessage = "Pick a Wayland window via the portal before capture can start.";
        WaylandPortalSession cleanup;
        cleanup.connection = connection;
        cleanup.sessionPath = sessionPath;
        CloseWaylandPortalSession(cleanup);
        if (outSelectionResult) {
            *outSelectionResult = WindowCaptureSelectionResult::Unsupported;
        }
        return false;
    }

    const std::string startHandleToken = MakePortalToken("linuxscreen_start");
    const std::string expectedStartPath = MakePortalRequestPath(connection, startHandleToken);
    GVariantBuilder startOptions;
    g_variant_builder_init(&startOptions, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&startOptions, "{sv}", "handle_token", g_variant_new_string(startHandleToken.c_str()));

    PortalRequestResult startResult;
    if (!CallPortalRequestMethod(connection,
                                 "Start",
                                 g_variant_new("(osa{sv})", sessionPath.c_str(), "", &startOptions),
                                 expectedStartPath,
                                 startResult,
                                 &error)) {
        outMessage = error ? error->message : "Failed to start the selected portal session.";
        if (error) {
            g_error_free(error);
        }
        WaylandPortalSession cleanup;
        cleanup.connection = connection;
        cleanup.sessionPath = sessionPath;
        CloseWaylandPortalSession(cleanup);
        if (outSelectionResult) {
            *outSelectionResult = WindowCaptureSelectionResult::Failed;
        }
        return false;
    }

    if (startResult.response != 0 || !startResult.results) {
        outMessage = startResult.response == 1
            ? "Window selection was cancelled."
            : "The desktop portal did not start a window capture session.";
        if (startResult.results) {
            g_variant_unref(startResult.results);
        }
        WaylandPortalSession cleanup;
        cleanup.connection = connection;
        cleanup.sessionPath = sessionPath;
        CloseWaylandPortalSession(cleanup);
        if (outSelectionResult) {
            *outSelectionResult = startResult.response == 1
                ? WindowCaptureSelectionResult::Cancelled
                : WindowCaptureSelectionResult::Failed;
        }
        return false;
    }

    WaylandPortalSession session;
    session.connection = connection;
    session.sessionPath = sessionPath;

    GVariant* restoreTokenValue = g_variant_lookup_value(startResult.results, "restore_token", G_VARIANT_TYPE_STRING);
    if (restoreTokenValue) {
        session.restoreToken = g_variant_get_string(restoreTokenValue, nullptr);
        g_variant_unref(restoreTokenValue);
    }

    GVariant* streamsValue = g_variant_lookup_value(startResult.results, "streams", G_VARIANT_TYPE("a(ua{sv})"));
    if (streamsValue) {
        GVariantIter iter;
        g_variant_iter_init(&iter, streamsValue);
        guint32 nodeId = 0;
        GVariant* props = nullptr;
        if (g_variant_iter_next(&iter, "(u@a{sv})", &nodeId, &props)) {
            session.nodeId = nodeId;
            PopulatePortalSessionMetadataFromProps(session, props);
            g_variant_unref(props);
        }
        g_variant_unref(streamsValue);
    }
    g_variant_unref(startResult.results);

    if (session.nodeId == 0) {
        outMessage = "The desktop portal started without providing a PipeWire stream.";
        CloseWaylandPortalSession(session);
        if (outSelectionResult) {
            *outSelectionResult = WindowCaptureSelectionResult::Failed;
        }
        return false;
    }

    outSession = session;
    if (!keepSessionOpen) {
        CloseWaylandPortalSession(session);
        outSession.connection = nullptr;
        outSession.sessionPath.clear();
    }
    if (outSelectionResult) {
        *outSelectionResult = WindowCaptureSelectionResult::Selected;
    }
    if (outMessage.empty()) {
        outMessage = "Wayland window selection completed.";
    }
    return true;
}

// Wayland capture thread

void RunWaylandCaptureThread(const std::string& key, X11SourceRecord* record) {
    std::string lastSelectionToken;
    bool waitingForManualRepick = false;

    while (!record->stopRequested.load(std::memory_order_acquire)) {
        const WindowCaptureRequest request = record->request;
        if (request.selectionToken != lastSelectionToken) {
            lastSelectionToken = request.selectionToken;
            waitingForManualRepick = false;
        }
        if (request.selectionToken.empty()) {
            UpdateWaylandRecordStatus(key,
                                      WindowCaptureState::SelectionRequired,
                                      "Pick a Wayland window via the portal before capture can start.");
            SleepInterruptible(record->stopRequested, std::chrono::seconds(1));
            continue;
        }
        if (waitingForManualRepick) {
            UpdateWaylandRecordStatus(key,
                                      WindowCaptureState::NotFound,
                                      kWaylandManualRepickMessage);
            SleepInterruptible(record->stopRequested, std::chrono::seconds(1));
            continue;
        }

        WaylandPortalSession session;
        std::string message;
        if (!CreateWaylandPortalSession(request,
                                        request.fps,
                                        false,
                                        true,
                                        session,
                                        message,
                                        nullptr)) {
            {
                std::lock_guard<std::mutex> lock(g_mutex);
                g_waylandManualRepickMessages[key] =
                    message.empty() ? std::string(kWaylandManualRepickMessage) : message;
            }
            waitingForManualRepick = true;
            UpdateWaylandRecordStatus(key,
                                      WindowCaptureState::NotFound,
                                      message.empty() ? kWaylandManualRepickMessage : message);
            SleepInterruptible(record->stopRequested, std::chrono::seconds(1));
            continue;
        }

        {
            std::lock_guard<std::mutex> lock(g_mutex);
            RememberWaylandSelectionLocked(key, request, session);
            ApplyRememberedWaylandSelectionLocked(key, record->request);
            g_waylandManualRepickMessages.erase(key);
            g_availableWindows = { MakeSelectedWindowSnapshot(record->request,
                                                              session.nodeId,
                                                              session.width,
                                                              session.height) };
        }

        UpdateWaylandRecordStatus(key,
                                  WindowCaptureState::Starting,
                                  "Opening the Wayland PipeWire stream...",
                                  session.width,
                                  session.height);

        const bool tryDmaBufFirst = ShouldAttemptWaylandDmaBufOnly();
        bool streamStoppedForFallback = false;
        bool reachedStableStreaming = false;
        for (int streamAttempt = 0; streamAttempt < (tryDmaBufFirst ? 2 : 1); ++streamAttempt) {
            if (record->stopRequested.load(std::memory_order_acquire)) {
                break;
            }

            const bool preferDmaBufOnly = tryDmaBufFirst && streamAttempt == 0;
            int remoteFd = -1;
            if (!OpenWaylandPipeWireRemote(session, remoteFd, message)) {
                {
                    std::lock_guard<std::mutex> lock(g_mutex);
                    g_waylandManualRepickMessages[key] =
                        message.empty() ? std::string(kWaylandManualRepickMessage) : message;
                }
                waitingForManualRepick = true;
                UpdateWaylandRecordStatus(key,
                                          WindowCaptureState::NotFound,
                                          message.empty() ? kWaylandManualRepickMessage : message,
                                          session.width,
                                          session.height);
                break;
            }

            WaylandPipeWireStream stream;
            stream.key = key;
            stream.record = record;
            if (!StartWaylandPipeWireCapture(stream, session, remoteFd, message, preferDmaBufOnly)) {
                const bool cpuFallbackEligible = preferDmaBufOnly && !record->stopRequested.load(std::memory_order_acquire);
                StopWaylandPipeWireCapture(stream);
                if (cpuFallbackEligible) {
                    MarkWaylandDmaBufUnavailableForSession(message);
                    message = "DMA-BUF-only Wayland transport did not start; retrying with shared-memory buffers.";
                    DebugWindowCaptureLog("wayland stream retry key=%s reason=dmabuf-start-failed", key.c_str());
                    UpdateWaylandRecordStatus(key,
                                              WindowCaptureState::Starting,
                                              message,
                                              session.width,
                                              session.height);
                    continue;
                }

                {
                    std::lock_guard<std::mutex> lock(g_mutex);
                    g_waylandManualRepickMessages[key] =
                        message.empty() ? std::string(kWaylandManualRepickMessage) : message;
                }
                waitingForManualRepick = true;
                UpdateWaylandRecordStatus(key,
                                          WindowCaptureState::NotFound,
                                          message.empty() ? kWaylandManualRepickMessage : message,
                                          session.width,
                                          session.height);
                break;
            }

            UpdateWaylandRecordStatus(key,
                                      WindowCaptureState::Starting,
                                      preferDmaBufOnly
                                          ? "Waiting for the first PipeWire DMA-BUF frame..."
                                          : "Waiting for the first PipeWire frame...",
                                      session.width,
                                      session.height);

            const auto firstFrameDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1500);
            bool needsCpuFallback = false;
            bool sawFrame = false;
            while (!record->stopRequested.load(std::memory_order_acquire)) {
                {
                    std::lock_guard<std::mutex> lock(stream.mutex);
                    if (stream.failed) {
                        message = stream.errorMessage;
                        if (preferDmaBufOnly && !sawFrame) {
                            if (message.empty()) {
                                message = "PipeWire DMA-BUF startup failed before the first frame.";
                            }
                            needsCpuFallback = true;
                        }
                        break;
                    }
                    if (stream.needsCpuFallback) {
                        message = stream.errorMessage;
                        needsCpuFallback = true;
                        break;
                    }
                    if (stream.receivedFrame) {
                        sawFrame = true;
                    }
                }
                if (preferDmaBufOnly &&
                    !sawFrame &&
                    std::chrono::steady_clock::now() >= firstFrameDeadline) {
                    message = "PipeWire did not deliver a DMA-BUF frame in time; retrying with shared-memory buffers.";
                    needsCpuFallback = true;
                    break;
                }
                SleepInterruptible(record->stopRequested, std::chrono::milliseconds(100));
            }

            const bool stopRequested = record->stopRequested.load(std::memory_order_acquire);
            StopWaylandPipeWireCapture(stream);
            if (stopRequested) {
                streamStoppedForFallback = false;
                reachedStableStreaming = false;
                break;
            }
            if (needsCpuFallback && preferDmaBufOnly) {
                MarkWaylandDmaBufUnavailableForSession(message);
                streamStoppedForFallback = true;
                DebugWindowCaptureLog("wayland stream retry key=%s reason=%s",
                                      key.c_str(),
                                      message.empty() ? "dmabuf-fallback" : SanitizeDebugValue(message).c_str());
                UpdateWaylandRecordStatus(key,
                                          WindowCaptureState::Starting,
                                          message.empty()
                                              ? "Retrying Wayland capture with shared-memory buffers..."
                                              : message,
                                          session.width,
                                          session.height);
                continue;
            }

            reachedStableStreaming = true;
            streamStoppedForFallback = false;
            break;
        }

        CloseWaylandPortalSession(session);
        if (record->stopRequested.load(std::memory_order_acquire)) {
            break;
        }
        if (streamStoppedForFallback) {
            SleepInterruptible(record->stopRequested, std::chrono::milliseconds(100));
            continue;
        }
        if (reachedStableStreaming) {
            {
                std::lock_guard<std::mutex> lock(g_mutex);
                g_waylandManualRepickMessages[key] =
                    message.empty() ? std::string(kWaylandManualRepickMessage) : message;
            }
            waitingForManualRepick = true;
            UpdateWaylandRecordStatus(key,
                                      WindowCaptureState::NotFound,
                                      message.empty() ? kWaylandManualRepickMessage : message);
            SleepInterruptible(record->stopRequested, std::chrono::milliseconds(500));
            continue;
        }

        SleepInterruptible(record->stopRequested, std::chrono::milliseconds(500));
    }
}

// Shutdown helper for DMA-BUF EGL state

void ShutdownWaylandDmaBufDisplay() {
    std::lock_guard<std::mutex> dmaLock(g_waylandDmaBufMutex);
    if (g_waylandDmaBufDisplay != EGL_NO_DISPLAY) {
        eglTerminate(g_waylandDmaBufDisplay);
    }
    g_waylandDmaBufDisplay = EGL_NO_DISPLAY;
    g_waylandDmaBufNativeDisplay = nullptr;
    g_waylandDmaBufDisplayInitialized = false;
    g_waylandDmaBufAvailable = false;
    g_waylandDmaBufModifierSupported = false;
    g_waylandDmaBufProbeDisabledForSession = false;
}

} // namespace platform::x11

#endif
