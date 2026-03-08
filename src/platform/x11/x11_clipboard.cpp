#include "x11_clipboard.h"

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>

#include <cstdio>
#include <cstring>
#include <array>
#include <string>
#include <unistd.h>

namespace platform::x11 {

namespace {

Display* g_clipDisplay = nullptr;
Window   g_clipWindow  = 0;

std::string g_clipBuffer;

constexpr int kClipboardTimeoutMs = 200;
constexpr int kClipboardPollIntervalUs = 5000;

void ClearClipboardEventQueue() {
    while (XPending(g_clipDisplay) > 0) {
        XEvent event;
        XNextEvent(g_clipDisplay, &event);
    }
}

bool WaitForClipboardSelectionNotify(Atom property, XSelectionEvent& outSelection) {
    int elapsedUs = 0;
    while (elapsedUs < kClipboardTimeoutMs * 1000) {
        if (XPending(g_clipDisplay) > 0) {
            XEvent event;
            XNextEvent(g_clipDisplay, &event);
            if (event.type != SelectionNotify) {
                continue;
            }
            if (event.xselection.requestor != g_clipWindow) {
                continue;
            }
            if (event.xselection.property != property && event.xselection.property != None) {
                continue;
            }
            outSelection = event.xselection;
            return true;
        }
        usleep(kClipboardPollIntervalUs);
        elapsedUs += kClipboardPollIntervalUs;
    }
    return false;
}

std::string Latin1ToUtf8(const unsigned char* data, unsigned long size) {
    std::string out;
    out.reserve(size * 2);
    for (unsigned long i = 0; i < size; ++i) {
        const unsigned char byte = data[i];
        if (byte < 0x80) {
            out.push_back(static_cast<char>(byte));
        } else {
            out.push_back(static_cast<char>(0xC0u | (byte >> 6)));
            out.push_back(static_cast<char>(0x80u | (byte & 0x3Fu)));
        }
    }
    return out;
}

bool ConvertTextPropertyListToUtf8(XTextProperty& textProp, std::string& outUtf8) {
    char** textList = nullptr;
    int textCount = 0;

#if defined(X_HAVE_UTF8_STRING)
    int utf8Status = Xutf8TextPropertyToTextList(g_clipDisplay, &textProp, &textList, &textCount);
    if (utf8Status == Success && textList && textCount > 0) {
        outUtf8.clear();
        for (int i = 0; i < textCount; ++i) {
            if (!textList[i]) {
                continue;
            }
            if (!outUtf8.empty()) {
                outUtf8.push_back('\n');
            }
            outUtf8 += textList[i];
        }
        XFreeStringList(textList);
        return true;
    }
    if (textList) {
        XFreeStringList(textList);
    }
    textList = nullptr;
    textCount = 0;
#endif

    int mbStatus = XmbTextPropertyToTextList(g_clipDisplay, &textProp, &textList, &textCount);
    if (mbStatus == Success && textList && textCount > 0) {
        outUtf8.clear();
        for (int i = 0; i < textCount; ++i) {
            if (!textList[i]) {
                continue;
            }
            if (!outUtf8.empty()) {
                outUtf8.push_back('\n');
            }
            outUtf8 += textList[i];
        }
        XFreeStringList(textList);
        return true;
    }
    if (textList) {
        XFreeStringList(textList);
    }
    return false;
}

bool ConvertClipboardPropertyToUtf8(Atom actualType,
                                    int actualFormat,
                                    unsigned char* propData,
                                    unsigned long nitems,
                                    Atom utf8Atom,
                                    Atom textPlainUtf8Atom,
                                    Atom textAtom,
                                    Atom compoundTextAtom,
                                    std::string& outUtf8) {
    if (!propData) {
        return false;
    }
    if (actualFormat != 8) {
        return false;
    }

    if (nitems == 0) {
        outUtf8.clear();
        return true;
    }

    if (actualType == utf8Atom || actualType == textPlainUtf8Atom) {
        outUtf8.assign(reinterpret_cast<const char*>(propData), nitems);
        return true;
    }

    if (actualType == textAtom || actualType == compoundTextAtom) {
        XTextProperty textProp;
        textProp.value = propData;
        textProp.encoding = actualType;
        textProp.format = actualFormat;
        textProp.nitems = nitems;
        if (ConvertTextPropertyListToUtf8(textProp, outUtf8)) {
            return true;
        }
        return false;
    }

    if (actualType == XA_STRING) {
        outUtf8 = Latin1ToUtf8(propData, nitems);
        return true;
    }

    outUtf8.assign(reinterpret_cast<const char*>(propData), nitems);
    return true;
}

bool TryReadClipboardTarget(Atom clipAtom,
                            Atom targetAtom,
                            Atom targetProp,
                            Atom utf8Atom,
                            Atom textPlainUtf8Atom,
                            Atom textAtom,
                            Atom compoundTextAtom,
                            std::string& outUtf8) {
    ClearClipboardEventQueue();
    XConvertSelection(g_clipDisplay, clipAtom, targetAtom, targetProp, g_clipWindow, CurrentTime);
    XFlush(g_clipDisplay);

    XSelectionEvent selection;
    if (!WaitForClipboardSelectionNotify(targetProp, selection)) {
        return false;
    }
    if (selection.property == None) {
        return false;
    }

    Atom actualType = None;
    int actualFormat = 0;
    unsigned long nitems = 0;
    unsigned long bytesAfter = 0;
    unsigned char* propData = nullptr;

    int result = XGetWindowProperty(g_clipDisplay,
                                    g_clipWindow,
                                    targetProp,
                                    0,
                                    1 << 20,
                                    True,
                                    AnyPropertyType,
                                    &actualType,
                                    &actualFormat,
                                    &nitems,
                                    &bytesAfter,
                                    &propData);
    if (result != Success) {
        if (propData) {
            XFree(propData);
        }
        return false;
    }

    const bool converted = ConvertClipboardPropertyToUtf8(actualType,
                                                          actualFormat,
                                                          propData,
                                                          nitems,
                                                          utf8Atom,
                                                          textPlainUtf8Atom,
                                                          textAtom,
                                                          compoundTextAtom,
                                                          outUtf8);
    if (propData) {
        XFree(propData);
    }
    return converted;
}

bool EnsureClipboardConnection() {
    if (g_clipDisplay) return true;

    g_clipDisplay = XOpenDisplay(nullptr);
    if (!g_clipDisplay) {
        fprintf(stderr, "[Linuxscreen][clipboard] XOpenDisplay failed\n");
        return false;
    }
    
    g_clipWindow = XCreateSimpleWindow(
        g_clipDisplay, DefaultRootWindow(g_clipDisplay),
        0, 0, 1, 1, 0, 0, 0);

    return true;
}

} // namespace

const char* X11GetClipboardText(ImGuiContext* /*ctx*/) {
    if (!EnsureClipboardConnection()) return nullptr;

    Atom clipAtom    = XInternAtom(g_clipDisplay, "CLIPBOARD", False);
    Atom utf8Atom    = XInternAtom(g_clipDisplay, "UTF8_STRING", False);
    Atom textPlainUtf8Atom = XInternAtom(g_clipDisplay, "text/plain;charset=utf-8", False);
    Atom textPlainAtom = XInternAtom(g_clipDisplay, "text/plain", False);
    Atom textAtom = XInternAtom(g_clipDisplay, "TEXT", False);
    Atom compoundTextAtom = XInternAtom(g_clipDisplay, "COMPOUND_TEXT", False);
    Atom targetProp  = XInternAtom(g_clipDisplay, "LINUXSCREEN_CLIP", False);

    const std::array<Atom, 6> preferredTargets = {
        utf8Atom,
        textPlainUtf8Atom,
        textPlainAtom,
        textAtom,
        compoundTextAtom,
        XA_STRING,
    };

    for (Atom target : preferredTargets) {
        if (target == None) {
            continue;
        }
        if (TryReadClipboardTarget(clipAtom,
                                   target,
                                   targetProp,
                                   utf8Atom,
                                   textPlainUtf8Atom,
                                   textAtom,
                                   compoundTextAtom,
                                   g_clipBuffer)) {
            return g_clipBuffer.c_str();
        }
    }

    return nullptr;
}

void ShutdownX11Clipboard() {
    if (g_clipDisplay) {
        if (g_clipWindow) {
            XDestroyWindow(g_clipDisplay, g_clipWindow);
            g_clipWindow = 0;
        }
        XCloseDisplay(g_clipDisplay);
        g_clipDisplay = nullptr;
    }
    g_clipBuffer.clear();
}

} // namespace platform::x11
