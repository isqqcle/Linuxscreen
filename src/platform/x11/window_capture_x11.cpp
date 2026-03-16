#ifndef __APPLE__

#include "window_capture_linux_internal.h"
#include "mirror/glx_shared_contexts.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>
#include <GL/glx.h>
#include <GL/glxext.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XShm.h>
#include <X11/extensions/Xcomposite.h>
#if __has_include(<X11/extensions/Xdamage.h>)
#include <X11/extensions/Xdamage.h>
#else
using Damage = XID;
using XserverRegion = XID;
enum {
    XDamageNotify = 0,
    XDamageReportRawRectangles = 0,
    XDamageReportDeltaRectangles = 1,
    XDamageReportBoundingBox = 2,
    XDamageReportNonEmpty = 3,
};
typedef struct {
    int type;
    unsigned long serial;
    Bool send_event;
    Display* display;
    Damage damage;
    Drawable drawable;
    int level;
    Bool more;
    Time timestamp;
    XRectangle area;
    XRectangle geometry;
} XDamageNotifyEvent;
extern "C" Bool XDamageQueryExtension(Display* display, int* eventBase, int* errorBase);
extern "C" Damage XDamageCreate(Display* display, Drawable drawable, int level);
extern "C" void XDamageDestroy(Display* display, Damage damage);
extern "C" void XDamageSubtract(Display* display, Damage damage, XserverRegion repair, XserverRegion parts);
#endif
#include <sys/ipc.h>
#include <sys/shm.h>

#include <algorithm>
#include <cstring>

namespace platform::x11 {

namespace {

// X11 structs

struct X11SharedImage {
    XImage* image = nullptr;
    XShmSegmentInfo shmInfo{};
    bool attached = false;
    int width = 0;
    int height = 0;
};

struct X11DamageWatch {
    int eventBase = 0;
    ::Window redirectedWindow = None;
    Damage damage = None;
    Pixmap pixmap = None;
    int pixmapWidth = 0;
    int pixmapHeight = 0;
};

struct X11TextureFromPixmapState {
    Display* display = nullptr;
    GLXFBConfig fbConfig = nullptr;
    GLXPixmap glxPixmap = 0;
    GLuint texture = 0;
    int width = 0;
    int height = 0;
    bool yInverted = false;
    bool textureBound = false;
};

// X11 globals

std::mutex g_x11ErrorTrapMutex;
std::atomic<int> g_lastX11ErrorCode{ 0 };
std::once_flag g_glxTextureFromPixmapResolveOnce;
PFNGLXBINDTEXIMAGEEXTPROC g_glXBindTexImageEXT = nullptr;
PFNGLXRELEASETEXIMAGEEXTPROC g_glXReleaseTexImageEXT = nullptr;

// Environment checks

bool PreferX11CompositeCapture() {
    const char* value = std::getenv("LINUXSCREEN_X11_ENABLE_COMPOSITE");
    if (!value || value[0] == '\0') {
        return true;
    }
    return std::strcmp(value, "0") != 0;
}

bool ShouldRebindTextureFromPixmapEachPoll() {
    const char* value = std::getenv("LINUXSCREEN_X11_REBIND_TFP");
    return value && value[0] != '\0' && std::strcmp(value, "0") != 0;
}

// X11 error trapping

int TrapX11ErrorHandler(Display*, XErrorEvent* errorEvent) {
    if (errorEvent) {
        g_lastX11ErrorCode.store(static_cast<int>(errorEvent->error_code), std::memory_order_release);
    } else {
        g_lastX11ErrorCode.store(1, std::memory_order_release);
    }
    return 0;
}

template <typename Fn>
bool CallX11WithErrorTrap(Display* display, Fn&& fn) {
    if (!display) {
        return false;
    }

    std::lock_guard<std::mutex> trapLock(g_x11ErrorTrapMutex);
    g_lastX11ErrorCode.store(0, std::memory_order_release);
    int (*previousHandler)(Display*, XErrorEvent*) = XSetErrorHandler(TrapX11ErrorHandler);

    fn();
    XSync(display, False);

    XSetErrorHandler(previousHandler);
    return g_lastX11ErrorCode.load(std::memory_order_acquire) == 0;
}

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

void ResolveTextureFromPixmapFunctions() {
    std::call_once(g_glxTextureFromPixmapResolveOnce, []() {
        g_glXBindTexImageEXT = reinterpret_cast<PFNGLXBINDTEXIMAGEEXTPROC>(
            glXGetProcAddressARB(reinterpret_cast<const GLubyte*>("glXBindTexImageEXT")));
        g_glXReleaseTexImageEXT = reinterpret_cast<PFNGLXRELEASETEXIMAGEEXTPROC>(
            glXGetProcAddressARB(reinterpret_cast<const GLubyte*>("glXReleaseTexImageEXT")));
    });
}

bool HasGlxExtension(Display* display, const char* extension) {
    if (!display || !extension || extension[0] == '\0') {
        return false;
    }

    const int screen = DefaultScreen(display);
    const char* extensions = glXQueryExtensionsString(display, screen);
    return HasExtensionToken(extensions, extension);
}

// Window property reading

std::string ReadTextProperty(Display* display, ::Window window, Atom property, Atom type) {
    if (!display || property == None) {
        return {};
    }

    Atom actualType = None;
    int actualFormat = 0;
    unsigned long itemCount = 0;
    unsigned long bytesAfter = 0;
    unsigned char* raw = nullptr;
    int status = Success;
    if (!CallX11WithErrorTrap(display, [&]() {
            status = XGetWindowProperty(display,
                                        window,
                                        property,
                                        0,
                                        4096,
                                        False,
                                        type,
                                        &actualType,
                                        &actualFormat,
                                        &itemCount,
                                        &bytesAfter,
                                        &raw);
        }) ||
        status != Success) {
        return {};
    }

    std::string value;
    if (raw && itemCount > 0) {
        value.assign(reinterpret_cast<const char*>(raw),
                     strnlen(reinterpret_cast<const char*>(raw), itemCount));
    }
    if (raw) {
        XFree(raw);
    }
    return value;
}

std::string ReadWindowTitle(Display* display, ::Window window) {
    const Atom utf8String = XInternAtom(display, "UTF8_STRING", True);
    const Atom netWmName = XInternAtom(display, "_NET_WM_NAME", True);
    std::string title = ReadTextProperty(display, window, netWmName, utf8String);
    if (!title.empty()) {
        return title;
    }

    char* fallbackName = nullptr;
    int fetched = 0;
    if (CallX11WithErrorTrap(display, [&]() {
            fetched = XFetchName(display, window, &fallbackName);
        }) &&
        fetched != 0 &&
        fallbackName) {
        title.assign(fallbackName);
        XFree(fallbackName);
    }
    return title;
}

std::string ReadWindowAppId(Display* display, ::Window window) {
    XClassHint classHint{};
    int fetched = 0;
    if (!CallX11WithErrorTrap(display, [&]() {
            fetched = XGetClassHint(display, window, &classHint);
        }) ||
        fetched == 0) {
        return {};
    }

    std::string value;
    if (classHint.res_class && *classHint.res_class) {
        value = classHint.res_class;
    } else if (classHint.res_name && *classHint.res_name) {
        value = classHint.res_name;
    }

    if (classHint.res_name) {
        XFree(classHint.res_name);
    }
    if (classHint.res_class) {
        XFree(classHint.res_class);
    }
    return value;
}

bool GetWindowAttributesSafe(Display* display, ::Window window, XWindowAttributes& outAttrs) {
    std::memset(&outAttrs, 0, sizeof(outAttrs));
    int attrStatus = 0;
    return CallX11WithErrorTrap(display, [&]() {
               attrStatus = XGetWindowAttributes(display, window, &outAttrs);
           }) &&
           attrStatus != 0;
}

// Window enumeration

void CollectWindows(Display* display,
                    ::Window window,
                    ::Window activeWindow,
                    std::vector<AvailableWindow>& outWindows) {
    if (!display || window == None) {
        return;
    }

    XWindowAttributes attrs{};
    if (!GetWindowAttributesSafe(display, window, attrs)) {
        return;
    }

    if (attrs.map_state == IsViewable &&
        attrs.c_class == InputOutput &&
        attrs.width > 0 &&
        attrs.height > 0) {
        AvailableWindow available;
        available.windowId = static_cast<std::uint64_t>(window);
        available.appId = ReadWindowAppId(display, window);
        available.windowTitle = ReadWindowTitle(display, window);
        available.appName = available.appId;
        available.width = attrs.width;
        available.height = attrs.height;
        available.onScreen = true;
        available.active = (window == activeWindow);
        if (!available.appId.empty()) {
            outWindows.push_back(std::move(available));
        }
    }

    ::Window root = None;
    ::Window parent = None;
    ::Window* children = nullptr;
    unsigned int childCount = 0;
    int queryStatus = 0;
    if (!CallX11WithErrorTrap(display, [&]() {
            queryStatus = XQueryTree(display, window, &root, &parent, &children, &childCount);
        }) ||
        queryStatus == 0) {
        return;
    }

    for (unsigned int index = 0; index < childCount; ++index) {
        CollectWindows(display, children[index], activeWindow, outWindows);
    }
    if (children) {
        XFree(children);
    }
}

bool EnumerateManagedWindows(Display* display, std::vector<::Window>& outWindows) {
    outWindows.clear();
    if (!display) {
        return false;
    }

    const int screen = DefaultScreen(display);
    const ::Window root = RootWindow(display, screen);
    const Atom clientListAtom = XInternAtom(display, "_NET_CLIENT_LIST", True);
    if (clientListAtom == None) {
        return false;
    }

    Atom actualType = None;
    int actualFormat = 0;
    unsigned long itemCount = 0;
    unsigned long bytesAfter = 0;
    unsigned char* raw = nullptr;
    int status = Success;
    const bool ok = CallX11WithErrorTrap(display, [&]() {
        status = XGetWindowProperty(display,
                                    root,
                                    clientListAtom,
                                    0,
                                    ~0L,
                                    False,
                                    XA_WINDOW,
                                    &actualType,
                                    &actualFormat,
                                    &itemCount,
                                    &bytesAfter,
                                    &raw);
    });
    if (!ok || status != Success || !raw || actualType != XA_WINDOW || actualFormat != 32 || itemCount == 0) {
        if (raw) {
            XFree(raw);
        }
        return false;
    }

    const ::Window* windows = reinterpret_cast<const ::Window*>(raw);
    outWindows.assign(windows, windows + itemCount);
    XFree(raw);
    return !outWindows.empty();
}

} // namespace

std::vector<AvailableWindow> EnumerateX11Windows(Display* display) {
    std::vector<AvailableWindow> windows;
    if (!display) {
        return windows;
    }

    const int screen = DefaultScreen(display);
    const ::Window root = RootWindow(display, screen);
    const Atom activeAtom = XInternAtom(display, "_NET_ACTIVE_WINDOW", True);
    ::Window activeWindow = None;
    if (activeAtom != None) {
        Atom actualType = None;
        int actualFormat = 0;
        unsigned long itemCount = 0;
        unsigned long bytesAfter = 0;
        unsigned char* raw = nullptr;
        int status = Success;
        if (CallX11WithErrorTrap(display, [&]() {
                status = XGetWindowProperty(display,
                                            root,
                                            activeAtom,
                                            0,
                                            1,
                                            False,
                                            XA_WINDOW,
                                            &actualType,
                                            &actualFormat,
                                            &itemCount,
                                            &bytesAfter,
                                            &raw);
            }) &&
            status == Success &&
            raw &&
            itemCount > 0) {
            activeWindow = *reinterpret_cast<::Window*>(raw);
        }
        if (raw) {
            XFree(raw);
        }
    }

    std::vector<::Window> managedWindows;
    if (EnumerateManagedWindows(display, managedWindows)) {
        windows.reserve(managedWindows.size());
        for (const ::Window window : managedWindows) {
            XWindowAttributes attrs{};
            if (!GetWindowAttributesSafe(display, window, attrs)) {
                continue;
            }
            if (attrs.map_state != IsViewable ||
                attrs.c_class != InputOutput ||
                attrs.width <= 0 ||
                attrs.height <= 0) {
                continue;
            }

            AvailableWindow available;
            available.windowId = static_cast<std::uint64_t>(window);
            available.appId = ReadWindowAppId(display, window);
            available.windowTitle = ReadWindowTitle(display, window);
            available.appName = available.appId;
            available.width = attrs.width;
            available.height = attrs.height;
            available.onScreen = true;
            available.active = (window == activeWindow);
            if (!available.appId.empty()) {
                windows.push_back(std::move(available));
            }
        }
    } else {
        CollectWindows(display, root, activeWindow, windows);
    }

    std::sort(windows.begin(), windows.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs.appName != rhs.appName) {
            return lhs.appName < rhs.appName;
        }
        if (lhs.windowTitle != rhs.windowTitle) {
            return lhs.windowTitle < rhs.windowTitle;
        }
        return lhs.windowId < rhs.windowId;
    });
    return windows;
}

namespace {

// Texture From Pixmap

GLXFBConfig FindTextureFromPixmapFbConfig(Display* display, VisualID visualId, int depth) {
    if (!display || visualId == 0) {
        return nullptr;
    }

    const int screen = DefaultScreen(display);
    int count = 0;
    GLXFBConfig* configs = glXGetFBConfigs(display, screen, &count);
    if (!configs || count <= 0) {
        return nullptr;
    }

    GLXFBConfig match = nullptr;
    for (int index = 0; index < count; ++index) {
        int drawableType = 0;
        int bindTargets = 0;
        int bindRgb = 0;
        int bindRgba = 0;
        int candidateVisualId = 0;
        glXGetFBConfigAttrib(display, configs[index], GLX_DRAWABLE_TYPE, &drawableType);
        glXGetFBConfigAttrib(display, configs[index], GLX_BIND_TO_TEXTURE_TARGETS_EXT, &bindTargets);
        glXGetFBConfigAttrib(display, configs[index], GLX_BIND_TO_TEXTURE_RGB_EXT, &bindRgb);
        glXGetFBConfigAttrib(display, configs[index], GLX_BIND_TO_TEXTURE_RGBA_EXT, &bindRgba);
        glXGetFBConfigAttrib(display, configs[index], GLX_VISUAL_ID, &candidateVisualId);
        if ((drawableType & GLX_PIXMAP_BIT) == 0 ||
            (bindTargets & GLX_TEXTURE_2D_BIT_EXT) == 0 ||
            candidateVisualId == 0 ||
            static_cast<VisualID>(candidateVisualId) != visualId) {
            continue;
        }

        const bool wantsAlpha = depth >= 32;
        if (wantsAlpha ? (bindRgba == 0) : (bindRgb == 0 && bindRgba == 0)) {
            continue;
        }

        match = configs[index];
        break;
    }

    if (configs) {
        XFree(configs);
    }
    return match;
}

void DestroyTextureFromPixmapState(X11TextureFromPixmapState& state) {
    if (state.display && AreSharedGlxContextsReady()) {
        GlxContextRestoreState restore;
        if (MakeSharedGlxContextCurrent(GlxSharedContextRole::Render, restore)) {
            if (state.textureBound && state.glxPixmap && g_glXReleaseTexImageEXT) {
                g_glXReleaseTexImageEXT(state.display, state.glxPixmap, GLX_FRONT_LEFT_EXT);
                state.textureBound = false;
            }
            if (state.texture) {
                glDeleteTextures(1, &state.texture);
            }
            if (state.glxPixmap) {
                glXDestroyPixmap(state.display, state.glxPixmap);
            }
            glFlush();
            RestoreGlxContext(restore);
        }
    }

    state = {};
}

bool UpdateTextureFromPixmapBinding(X11TextureFromPixmapState& ioState,
                                    Pixmap pixmap,
                                    VisualID visualId,
                                    int depth,
                                    int width,
                                    int height,
                                    WindowCaptureTextureSnapshot& outTexture) {
    outTexture = {};
    ResolveTextureFromPixmapFunctions();
    if (!g_glXBindTexImageEXT || !g_glXReleaseTexImageEXT || !AreSharedGlxContextsReady()) {
        return false;
    }

    GlxSharedContextHandles handles = GetSharedGlxContextHandles();
    Display* glDisplay = reinterpret_cast<Display*>(handles.nativeDisplay);
    if (!glDisplay || pixmap == None || visualId == 0 || width <= 0 || height <= 0) {
        return false;
    }
    if (!HasGlxExtension(glDisplay, "GLX_EXT_texture_from_pixmap")) {
        return false;
    }

    const bool needsRecreate =
        ioState.display != glDisplay ||
        ioState.glxPixmap == 0 ||
        ioState.width != width ||
        ioState.height != height;

    if (!needsRecreate && ioState.textureBound && !ShouldRebindTextureFromPixmapEachPoll()) {
        outTexture.textureId = ioState.texture;
        outTexture.width = ioState.width;
        outTexture.height = ioState.height;
        outTexture.yInverted = ioState.yInverted;
        return outTexture.textureId != 0;
    }

    GlxContextRestoreState restore;
    if (!MakeSharedGlxContextCurrent(GlxSharedContextRole::Render, restore)) {
        return false;
    }

    if (needsRecreate) {
        if (ioState.textureBound && ioState.glxPixmap) {
            g_glXReleaseTexImageEXT(ioState.display, ioState.glxPixmap, GLX_FRONT_LEFT_EXT);
            ioState.textureBound = false;
        }
        if (ioState.glxPixmap) {
            glXDestroyPixmap(ioState.display, ioState.glxPixmap);
            ioState.glxPixmap = 0;
        }
        if (ioState.texture == 0) {
            glGenTextures(1, &ioState.texture);
        }

        ioState.fbConfig = FindTextureFromPixmapFbConfig(glDisplay, visualId, depth);
        if (!ioState.fbConfig) {
            RestoreGlxContext(restore);
            return false;
        }

        const int textureFormat = (depth >= 32) ? GLX_TEXTURE_FORMAT_RGBA_EXT : GLX_TEXTURE_FORMAT_RGB_EXT;
        const int attribs[] = {
            GLX_TEXTURE_TARGET_EXT, GLX_TEXTURE_2D_EXT,
            GLX_TEXTURE_FORMAT_EXT, textureFormat,
            None
        };
        ioState.glxPixmap = glXCreatePixmap(glDisplay, ioState.fbConfig, pixmap, attribs);
        if (!ioState.glxPixmap) {
            RestoreGlxContext(restore);
            return false;
        }

        glBindTexture(GL_TEXTURE_2D, ioState.texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        ioState.display = glDisplay;
        ioState.width = width;
        ioState.height = height;
        ioState.yInverted = true;
    } else if (ioState.textureBound && ioState.glxPixmap) {
        g_glXReleaseTexImageEXT(ioState.display, ioState.glxPixmap, GLX_FRONT_LEFT_EXT);
        ioState.textureBound = false;
    }

    g_glXBindTexImageEXT(glDisplay, ioState.glxPixmap, GLX_FRONT_LEFT_EXT, nullptr);
    ioState.textureBound = true;
    glFlush();

    outTexture.textureId = ioState.texture;
    outTexture.width = width;
    outTexture.height = height;
    outTexture.yInverted = ioState.yInverted;
    RestoreGlxContext(restore);
    return outTexture.textureId != 0;
}

// Image capture helpers

int CountBits(unsigned long value) {
    int count = 0;
    while (value != 0) {
        value &= (value - 1);
        ++count;
    }
    return count;
}

std::uint8_t ScaleMaskedComponent(unsigned long pixel, unsigned long mask) {
    if (mask == 0) {
        return 0;
    }

    int shift = 0;
    while (((mask >> shift) & 1UL) == 0) {
        ++shift;
    }
    const unsigned long normalizedMask = mask >> shift;
    const unsigned long value = (pixel & mask) >> shift;
    const int bits = CountBits(normalizedMask);
    if (bits <= 0) {
        return 0;
    }
    if (bits >= 8) {
        return static_cast<std::uint8_t>(value & 0xff);
    }
    const unsigned long maxValue = (1UL << bits) - 1UL;
    if (maxValue == 0) {
        return 0;
    }
    return static_cast<std::uint8_t>((value * 255UL) / maxValue);
}

bool ReadDrawableImage(Display* display,
                       Drawable drawable,
                       int width,
                       int height,
                       LatestFrameSnapshot& outFrame) {
    if (!display || drawable == None || width <= 0 || height <= 0) {
        return false;
    }

    XImage* image = nullptr;
    if (!CallX11WithErrorTrap(display, [&]() {
            image = XGetImage(display,
                              drawable,
                              0,
                              0,
                              static_cast<unsigned int>(width),
                              static_cast<unsigned int>(height),
                              AllPlanes,
                              ZPixmap);
        })) {
        image = nullptr;
    }
    if (!image) {
        return false;
    }

    outFrame = {};
    outFrame.width = width;
    outFrame.height = height;
    outFrame.bytesPerRow = width * 4;
    outFrame.contentWidth = width;
    outFrame.contentHeight = height;
    outFrame.pixels.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const unsigned long pixel = XGetPixel(image, x, y);
            const std::size_t offset = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4;
            outFrame.pixels[offset + 0] = ScaleMaskedComponent(pixel, image->blue_mask);
            outFrame.pixels[offset + 1] = ScaleMaskedComponent(pixel, image->green_mask);
            outFrame.pixels[offset + 2] = ScaleMaskedComponent(pixel, image->red_mask);
            outFrame.pixels[offset + 3] = 255;
        }
    }

    XDestroyImage(image);
    return true;
}

bool EnsureSharedImage(Display* display,
                       Visual* visual,
                       unsigned int depth,
                       int width,
                       int height,
                       X11SharedImage& ioBuffer) {
    if (!display || !visual || width <= 0 || height <= 0) {
        return false;
    }

    if (ioBuffer.image &&
        ioBuffer.width == width &&
        ioBuffer.height == height) {
        return true;
    }

    if (ioBuffer.attached && ioBuffer.image) {
        XShmDetach(display, &ioBuffer.shmInfo);
        ioBuffer.attached = false;
    }
    if (ioBuffer.image) {
        ioBuffer.image->data = nullptr;
        XDestroyImage(ioBuffer.image);
        ioBuffer.image = nullptr;
    }
    if (ioBuffer.shmInfo.shmaddr && ioBuffer.shmInfo.shmaddr != reinterpret_cast<char*>(-1)) {
        shmdt(ioBuffer.shmInfo.shmaddr);
    }
    if (ioBuffer.shmInfo.shmid >= 0) {
        shmctl(ioBuffer.shmInfo.shmid, IPC_RMID, nullptr);
    }
    ioBuffer.shmInfo = {};
    ioBuffer.shmInfo.shmid = -1;
    ioBuffer.width = 0;
    ioBuffer.height = 0;

    XImage* image = XShmCreateImage(display, visual, depth, ZPixmap, nullptr, &ioBuffer.shmInfo, width, height);
    if (!image) {
        return false;
    }

    const std::size_t bufferBytes =
        static_cast<std::size_t>(image->bytes_per_line) * static_cast<std::size_t>(image->height);
    ioBuffer.shmInfo.shmid = shmget(IPC_PRIVATE, bufferBytes, IPC_CREAT | 0600);
    if (ioBuffer.shmInfo.shmid < 0) {
        XDestroyImage(image);
        return false;
    }

    ioBuffer.shmInfo.shmaddr = static_cast<char*>(shmat(ioBuffer.shmInfo.shmid, nullptr, 0));
    if (!ioBuffer.shmInfo.shmaddr || ioBuffer.shmInfo.shmaddr == reinterpret_cast<char*>(-1)) {
        shmctl(ioBuffer.shmInfo.shmid, IPC_RMID, nullptr);
        ioBuffer.shmInfo.shmid = -1;
        XDestroyImage(image);
        return false;
    }

    ioBuffer.shmInfo.readOnly = False;
    image->data = ioBuffer.shmInfo.shmaddr;
    Bool attached = False;
    if (!CallX11WithErrorTrap(display, [&]() {
            attached = XShmAttach(display, &ioBuffer.shmInfo);
        }) ||
        !attached) {
        shmdt(ioBuffer.shmInfo.shmaddr);
        shmctl(ioBuffer.shmInfo.shmid, IPC_RMID, nullptr);
        ioBuffer.shmInfo.shmaddr = nullptr;
        ioBuffer.shmInfo.shmid = -1;
        XDestroyImage(image);
        return false;
    }

    XSync(display, False);
    ioBuffer.image = image;
    ioBuffer.attached = true;
    ioBuffer.width = width;
    ioBuffer.height = height;
    return true;
}

void DestroySharedImage(Display* display, X11SharedImage& ioBuffer) {
    if (display && ioBuffer.attached && ioBuffer.image) {
        XShmDetach(display, &ioBuffer.shmInfo);
        XSync(display, False);
    }
    ioBuffer.attached = false;
    if (ioBuffer.image) {
        ioBuffer.image->data = nullptr;
        XDestroyImage(ioBuffer.image);
        ioBuffer.image = nullptr;
    }
    if (ioBuffer.shmInfo.shmaddr && ioBuffer.shmInfo.shmaddr != reinterpret_cast<char*>(-1)) {
        shmdt(ioBuffer.shmInfo.shmaddr);
    }
    if (ioBuffer.shmInfo.shmid >= 0) {
        shmctl(ioBuffer.shmInfo.shmid, IPC_RMID, nullptr);
    }
    ioBuffer.shmInfo = {};
    ioBuffer.shmInfo.shmid = -1;
    ioBuffer.width = 0;
    ioBuffer.height = 0;
}

bool ConvertXImageToFrame(const XImage& image,
                          int width,
                          int height,
                          LatestFrameSnapshot& outFrame) {
    if (width <= 0 || height <= 0) {
        return false;
    }

    outFrame = {};
    outFrame.width = width;
    outFrame.height = height;
    outFrame.bytesPerRow = width * 4;
    outFrame.contentWidth = width;
    outFrame.contentHeight = height;
    outFrame.pixels.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const unsigned long pixel = XGetPixel(const_cast<XImage*>(&image), x, y);
            const std::size_t offset =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4;
            outFrame.pixels[offset + 0] = ScaleMaskedComponent(pixel, image.blue_mask);
            outFrame.pixels[offset + 1] = ScaleMaskedComponent(pixel, image.green_mask);
            outFrame.pixels[offset + 2] = ScaleMaskedComponent(pixel, image.red_mask);
            outFrame.pixels[offset + 3] = 255;
        }
    }
    return true;
}

bool FrameLooksAllBlack(const LatestFrameSnapshot& frame) {
    if (frame.width <= 0 || frame.height <= 0 || frame.bytesPerRow < frame.width * 4 || frame.pixels.empty()) {
        return true;
    }

    const int sampleCols = std::clamp(frame.width, 1, 64);
    const int sampleRows = std::clamp(frame.height, 1, 64);
    for (int row = 0; row < sampleRows; ++row) {
        const int y = (row * frame.height) / sampleRows;
        const std::size_t rowOffset = static_cast<std::size_t>(std::min(y, frame.height - 1)) *
                                      static_cast<std::size_t>(frame.bytesPerRow);
        for (int col = 0; col < sampleCols; ++col) {
            const int x = (col * frame.width) / sampleCols;
            const std::size_t offset = rowOffset + static_cast<std::size_t>(std::min(x, frame.width - 1)) * 4;
            if (offset + 2 >= frame.pixels.size()) {
                continue;
            }
            if (frame.pixels[offset + 0] != 0 ||
                frame.pixels[offset + 1] != 0 ||
                frame.pixels[offset + 2] != 0) {
                return false;
            }
        }
    }

    return true;
}

bool ReadDrawableImageWithShm(Display* display,
                              Drawable drawable,
                              Visual* visual,
                              unsigned int depth,
                              int width,
                              int height,
                              X11SharedImage& ioBuffer,
                              LatestFrameSnapshot& outFrame) {
    if (!EnsureSharedImage(display, visual, depth, width, height, ioBuffer)) {
        return false;
    }
    Bool gotImage = False;
    if (!CallX11WithErrorTrap(display, [&]() {
            gotImage = XShmGetImage(display, drawable, ioBuffer.image, 0, 0, AllPlanes);
        }) ||
        !gotImage) {
        return false;
    }
    return ConvertXImageToFrame(*ioBuffer.image, width, height, outFrame);
}

// Composite / Damage

void DestroyDamageWatch(Display* display, X11DamageWatch& ioWatch) {
    if (!display) {
        ioWatch = {};
        return;
    }
    if (ioWatch.pixmap != None) {
        CallX11WithErrorTrap(display, [&]() {
            XFreePixmap(display, ioWatch.pixmap);
        });
    }
    if (ioWatch.damage != None) {
        CallX11WithErrorTrap(display, [&]() {
            XDamageDestroy(display, ioWatch.damage);
        });
    }
    if (ioWatch.redirectedWindow != None) {
        CallX11WithErrorTrap(display, [&]() {
            XCompositeUnredirectWindow(display, ioWatch.redirectedWindow, CompositeRedirectAutomatic);
        });
    }
    ioWatch = {};
}

bool EnsureDamageWatch(Display* display,
                       ::Window window,
                       int width,
                       int height,
                       X11DamageWatch& ioWatch) {
    if (!display || window == None || width <= 0 || height <= 0) {
        return false;
    }

    if (ioWatch.redirectedWindow == window &&
        ioWatch.damage != None &&
        ioWatch.pixmap != None &&
        ioWatch.pixmapWidth == width &&
        ioWatch.pixmapHeight == height) {
        return true;
    }

    const bool sameWindow = ioWatch.redirectedWindow == window;
    if (sameWindow && ioWatch.pixmap != None) {
        CallX11WithErrorTrap(display, [&]() {
            XFreePixmap(display, ioWatch.pixmap);
        });
        ioWatch.pixmap = None;
        ioWatch.pixmapWidth = 0;
        ioWatch.pixmapHeight = 0;
    }
    if (!sameWindow) {
        DestroyDamageWatch(display, ioWatch);
    }
    int errorBase = 0;
    if (ioWatch.damage == None && !XDamageQueryExtension(display, &ioWatch.eventBase, &errorBase)) {
        return false;
    }

    if (!sameWindow) {
        if (!CallX11WithErrorTrap(display, [&]() {
                XCompositeRedirectWindow(display, window, CompositeRedirectAutomatic);
                ioWatch.damage = XDamageCreate(display, window, XDamageReportNonEmpty);
            })) {
            ioWatch.damage = None;
        }
        if (ioWatch.damage == None) {
            CallX11WithErrorTrap(display, [&]() {
                XCompositeUnredirectWindow(display, window, CompositeRedirectAutomatic);
            });
            return false;
        }
        ioWatch.redirectedWindow = window;
        XSync(display, False);
    }

    Pixmap pixmap = None;
    if (!CallX11WithErrorTrap(display, [&]() {
            pixmap = XCompositeNameWindowPixmap(display, window);
        })) {
        pixmap = None;
    }
    if (pixmap == None) {
        if (!sameWindow) {
            DestroyDamageWatch(display, ioWatch);
        }
        return false;
    }

    ioWatch.pixmap = pixmap;
    ioWatch.pixmapWidth = width;
    ioWatch.pixmapHeight = height;
    return true;
}

bool ConsumeDamageNotifications(Display* display,
                                const X11DamageWatch& watch) {
    if (!display || watch.damage == None) {
        return true;
    }

    bool sawDamage = false;
    while (XPending(display) > 0) {
        XEvent event{};
        XNextEvent(display, &event);
        if (event.type != watch.eventBase + XDamageNotify) {
            continue;
        }

        auto* damageEvent = reinterpret_cast<XDamageNotifyEvent*>(&event);
        if (damageEvent->damage != watch.damage) {
            continue;
        }

        XDamageSubtract(display, watch.damage, None, None);
        sawDamage = true;
    }
    return sawDamage;
}

bool CaptureX11WindowFrame(Display* display,
                           ::Window window,
                           bool useComposite,
                           bool useShmFallback,
                           X11SharedImage& ioSharedImage,
                           X11DamageWatch& ioDamageWatch,
                           LatestFrameSnapshot& outFrame,
                           bool& invalidGeometry) {
    invalidGeometry = false;
    if (!display || window == None) {
        invalidGeometry = true;
        return false;
    }

    XWindowAttributes attrs{};
    if (!GetWindowAttributesSafe(display, window, attrs) ||
        attrs.width <= 0 ||
        attrs.height <= 0) {
        invalidGeometry = true;
        return false;
    }

    if (!useComposite) {
        if (useShmFallback &&
            ReadDrawableImageWithShm(display,
                                     window,
                                     attrs.visual,
                                     static_cast<unsigned int>(attrs.depth),
                                     attrs.width,
                                     attrs.height,
                                     ioSharedImage,
                                     outFrame)) {
            return true;
        }
        return ReadDrawableImage(display, window, attrs.width, attrs.height, outFrame);
    }

    if (!EnsureDamageWatch(display, window, attrs.width, attrs.height, ioDamageWatch)) {
        return false;
    }

    ConsumeDamageNotifications(display, ioDamageWatch);
    if (ioDamageWatch.pixmap == None) {
        return false;
    }

    bool success = false;
    if (useShmFallback) {
        success = ReadDrawableImageWithShm(display,
                                           ioDamageWatch.pixmap,
                                           attrs.visual,
                                           static_cast<unsigned int>(attrs.depth),
                                           attrs.width,
                                           attrs.height,
                                           ioSharedImage,
                                           outFrame);
    }
    if (!success) {
        success = ReadDrawableImage(display, ioDamageWatch.pixmap, attrs.width, attrs.height, outFrame);
    }
    if (!success) {
        return false;
    }

    if (!FrameLooksAllBlack(outFrame)) {
        return true;
    }

    invalidGeometry = true;
    return false;
}

} // namespace

// X11 capture thread

void RunX11CaptureThread(const std::string& key, X11SourceRecord* record) {
    const std::string safeKey = SanitizeDebugValue(key);
    DebugWindowCaptureLog("x11 thread starting key=%s fps=%d appId='%s' title='%s'",
                          safeKey.c_str(),
                          record ? record->request.fps : -1,
                          record ? record->request.appId.c_str() : "",
                          record ? record->request.windowTitle.c_str() : "");
    Display* display = XOpenDisplay(nullptr);
    if (!display) {
        DebugWindowCaptureLog("x11 thread failed to open display key=%s", safeKey.c_str());
        std::lock_guard<std::mutex> lock(g_mutex);
        auto it = g_records.find(key);
        if (it != g_records.end()) {
            it->second->textureSnapshot = {};
            it->second->status.backend = WindowCaptureBackend::X11;
            it->second->status.access = WindowCaptureAccessState::Granted;
            it->second->status.state = WindowCaptureState::Error;
            it->second->status.message = "Failed to open the X11 display for window capture.";
        }
        return;
    }

    int compositeEventBase = 0;
    int compositeErrorBase = 0;
    int compositeMajor = 0;
    int compositeMinor = 0;
    const bool compositeSupported =
        XCompositeQueryExtension(display, &compositeEventBase, &compositeErrorBase) != 0 &&
        XCompositeQueryVersion(display, &compositeMajor, &compositeMinor) != 0;
    const bool shmAvailable = XShmQueryExtension(display) != 0;
    DebugWindowCaptureLog("x11 thread display ready key=%s composite=%d shm=%d",
                          safeKey.c_str(),
                          compositeSupported ? 1 : 0,
                          shmAvailable ? 1 : 0);
    const bool preferComposite = PreferX11CompositeCapture();
    bool useComposite = compositeSupported && preferComposite;
    std::uint32_t consecutiveFailures = 0;
    std::uint32_t staleFrames = 0;
    ::Window currentWindow = None;
    X11SharedImage sharedImage;
    sharedImage.shmInfo.shmid = -1;
    X11DamageWatch damageWatch;
    X11TextureFromPixmapState textureFromPixmap;
    bool reportedNoMatch = false;

    while (!record->stopRequested.load(std::memory_order_acquire)) {
        const WindowCaptureRequest request = record->request;
        std::vector<AvailableWindow> windows = EnumerateX11Windows(display);
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_availableWindows = windows;
        }

        const int matchIndex = FindBestMatchingWindowIndex(windows,
                                                           request.appId,
                                                           request.windowTitle,
                                                           request.titleMatchMode,
                                                           request.fallbackMode,
                                                           0,
                                                           request.preferredWidth,
                                                           request.preferredHeight);
        if (matchIndex < 0) {
            if (!reportedNoMatch) {
                DebugWindowCaptureLog("x11 thread no match key=%s appId='%s' title='%s' mode=%d fallback=%d",
                                      safeKey.c_str(),
                                      request.appId.c_str(),
                                      request.windowTitle.c_str(),
                                      static_cast<int>(request.titleMatchMode),
                                      static_cast<int>(request.fallbackMode));
                std::lock_guard<std::mutex> lock(g_mutex);
                auto it = g_records.find(key);
                if (it != g_records.end()) {
                    it->second->textureSnapshot = {};
                    it->second->status.backend = WindowCaptureBackend::X11;
                    it->second->status.access = WindowCaptureAccessState::Granted;
                    it->second->status.state = WindowCaptureState::NotFound;
                    it->second->status.message = "Selected window is not currently available.";
                }
            }
            reportedNoMatch = true;
            currentWindow = None;
            SleepInterruptible(record->stopRequested, std::chrono::seconds(2));
            continue;
        }

        reportedNoMatch = false;
        const AvailableWindow& matched = windows[static_cast<std::size_t>(matchIndex)];
        if (currentWindow != static_cast<::Window>(matched.windowId)) {
            DebugWindowCaptureLog("x11 thread selected key=%s window=0x%llx size=%dx%d composite=%d",
                                  safeKey.c_str(),
                                  static_cast<unsigned long long>(matched.windowId),
                                  matched.width,
                                  matched.height,
                                  useComposite ? 1 : 0);
            DestroyDamageWatch(display, damageWatch);
            DestroySharedImage(display, sharedImage);
            DestroyTextureFromPixmapState(textureFromPixmap);
            currentWindow = static_cast<::Window>(matched.windowId);
            useComposite = compositeSupported && preferComposite;
            consecutiveFailures = 0;
            staleFrames = 0;
        }

        bool deliveredGpuTexture = false;
        if (useComposite) {
            XWindowAttributes attrs{};
            if (GetWindowAttributesSafe(display, currentWindow, attrs) &&
                attrs.width > 0 &&
                attrs.height > 0 &&
                EnsureDamageWatch(display, currentWindow, attrs.width, attrs.height, damageWatch)) {
                WindowCaptureTextureSnapshot textureSnapshot;
                if (UpdateTextureFromPixmapBinding(textureFromPixmap,
                                                  damageWatch.pixmap,
                                                  XVisualIDFromVisual(attrs.visual),
                                                  attrs.depth,
                                                  attrs.width,
                                                  attrs.height,
                                                  textureSnapshot)) {
                    std::lock_guard<std::mutex> lock(g_mutex);
                    auto it = g_records.find(key);
                    if (it != g_records.end()) {
                        textureSnapshot.frameNumber = it->second->textureSnapshot.frameNumber + 1;
                        it->second->textureSnapshot = textureSnapshot;
                        it->second->status.backend = WindowCaptureBackend::X11;
                        it->second->status.access = WindowCaptureAccessState::Granted;
                        it->second->status.state = WindowCaptureState::Streaming;
                        it->second->status.message = "Streaming via XComposite texture binding";
                        it->second->status.frameNumber = textureSnapshot.frameNumber;
                        it->second->status.width = textureSnapshot.width;
                        it->second->status.height = textureSnapshot.height;
                        deliveredGpuTexture = true;
                    }
                }
            }
        }

        if (deliveredGpuTexture) {
            const int fps = std::clamp(request.fps, 1, 240);
            const int sleepMs = ShouldRebindTextureFromPixmapEachPoll()
                ? std::max(1, 1000 / fps)
                : 100;
            SleepInterruptible(record->stopRequested, std::chrono::milliseconds(sleepMs));
            continue;
        }

        LatestFrameSnapshot frame;
        frame = record->frame;
        bool invalidGeometry = false;
        if (!CaptureX11WindowFrame(display,
                                   currentWindow,
                                   useComposite,
                                   shmAvailable,
                                   sharedImage,
                                   damageWatch,
                                   frame,
                                   invalidGeometry)) {
            DebugWindowCaptureLog("x11 thread capture failed key=%s invalidGeometry=%d useComposite=%d failures=%u stale=%u",
                                  safeKey.c_str(),
                                  invalidGeometry ? 1 : 0,
                                  useComposite ? 1 : 0,
                                  consecutiveFailures + 1,
                                  staleFrames + 1);
            ++consecutiveFailures;
            ++staleFrames;
            if (useComposite && ShouldDowngradeX11CompositeCapture(invalidGeometry, consecutiveFailures, staleFrames)) {
                useComposite = false;
                DestroyDamageWatch(display, damageWatch);
            }

            std::lock_guard<std::mutex> lock(g_mutex);
            auto it = g_records.find(key);
            if (it != g_records.end()) {
                it->second->textureSnapshot = {};
                it->second->status.backend = WindowCaptureBackend::X11;
                it->second->status.access = WindowCaptureAccessState::Granted;
                it->second->status.state = WindowCaptureState::Starting;
                it->second->status.message = useComposite
                    ? "Retrying composite capture..."
                    : "Retrying fallback image capture...";
            }
            SleepInterruptible(record->stopRequested, std::chrono::milliseconds(100));
            continue;
        }

        consecutiveFailures = 0;
        staleFrames = 0;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            auto it = g_records.find(key);
            if (it != g_records.end()) {
                it->second->textureSnapshot = {};
                frame.frameNumber = it->second->frame.frameNumber + 1;
                it->second->frame = frame;
                it->second->status.backend = WindowCaptureBackend::X11;
                it->second->status.access = WindowCaptureAccessState::Granted;
                it->second->status.state = WindowCaptureState::Streaming;
                it->second->status.message = useComposite
                    ? "Streaming via XComposite"
                    : "Streaming via X11 image fallback";
                it->second->status.frameNumber = frame.frameNumber;
                it->second->status.width = frame.contentWidth;
                it->second->status.height = frame.contentHeight;
            }
        }

        const int fps = std::clamp(request.fps, 1, 240);
        SleepInterruptible(record->stopRequested, std::chrono::milliseconds(std::max(1, 1000 / fps)));
    }

    DestroyDamageWatch(display, damageWatch);
    DestroySharedImage(display, sharedImage);
    DestroyTextureFromPixmapState(textureFromPixmap);
    XCloseDisplay(display);
}

} // namespace platform::x11

#endif
