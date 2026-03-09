#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "glx_mirror_pipeline.h"
#include "mirror/glx_shared_contexts.h"
#include "overlay/imgui_input_bridge.h"
#include "overlay/imgui_overlay.h"
#include "x11_runtime.h"

#include "../common/anchor_coords.h"
#include "../common/config_io.h"
#include "../common/game_state_monitor.h"
#include "../common/input/glfw_vk_mapper.h"
#include "../common/input/hotkey_capture_state.h"
#include "../common/input/hotkey_dispatcher.h"
#include "../common/input/hotkey_matcher.h"
#include "../common/input/input_event.h"
#include "../common/input/key_state_tracker.h"
#include "../common/input/vk_codes.h"

#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <limits.h>
using Display = void;
using GLXContext = void*;
using GLXDrawable = unsigned long;
using Bool = int;
using __GLXextFuncPtr = void (*)();
#else
#include <GL/glx.h>
#include <X11/XKBlib.h>
#endif
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <chrono>
#include <dlfcn.h>
#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <vector>
#include <unistd.h>

struct GLFWwindow;
struct GLFWimage {
    int width;
    int height;
    unsigned char* pixels;
};

using GlfwKeyCallback = void (*)(GLFWwindow*, int, int, int, int);
using GlfwCharCallback = void (*)(GLFWwindow*, unsigned int);
using GlfwCharModsCallback = void (*)(GLFWwindow*, unsigned int, int);
using GlfwMouseButtonCallback = void (*)(GLFWwindow*, int, int, int);
using GlfwCursorPosCallback = void (*)(GLFWwindow*, double, double);
using GlfwScrollCallback = void (*)(GLFWwindow*, double, double);
using GlfwWindowFocusCallback = void (*)(GLFWwindow*, int);
using GlfwWindowSizeCallback = void (*)(GLFWwindow*, int, int);
using GlfwFramebufferSizeCallback = void (*)(GLFWwindow*, int, int);
using GlfwGetCursorPosFn = void (*)(GLFWwindow*, double*, double*);
using GlfwSetCursorPosFn = void (*)(GLFWwindow*, double, double);

extern "C" GlfwKeyCallback glfwSetKeyCallback(GLFWwindow* window, GlfwKeyCallback callback);
extern "C" GlfwCharCallback glfwSetCharCallback(GLFWwindow* window, GlfwCharCallback callback);
extern "C" GlfwCharModsCallback glfwSetCharModsCallback(GLFWwindow* window, GlfwCharModsCallback callback);
extern "C" int glfwGetKey(GLFWwindow* window, int key);
extern "C" GlfwMouseButtonCallback glfwSetMouseButtonCallback(GLFWwindow* window, GlfwMouseButtonCallback callback);
extern "C" GlfwCursorPosCallback glfwSetCursorPosCallback(GLFWwindow* window, GlfwCursorPosCallback callback);
extern "C" GlfwScrollCallback glfwSetScrollCallback(GLFWwindow* window, GlfwScrollCallback callback);
extern "C" GlfwWindowFocusCallback glfwSetWindowFocusCallback(GLFWwindow* window, GlfwWindowFocusCallback callback);
extern "C" GlfwWindowSizeCallback glfwSetWindowSizeCallback(GLFWwindow* window, GlfwWindowSizeCallback callback);
extern "C" GlfwFramebufferSizeCallback glfwSetFramebufferSizeCallback(GLFWwindow* window, GlfwFramebufferSizeCallback callback);
extern "C" void glfwSetInputMode(GLFWwindow* window, int mode, int value);
extern "C" void glfwGetCursorPos(GLFWwindow* window, double* xpos, double* ypos);
extern "C" void glfwSetCursorPos(GLFWwindow* window, double xpos, double ypos);
extern "C" void glfwSetWindowIcon(GLFWwindow* window, int count, const GLFWimage* images);
#ifdef __APPLE__
extern "C" void glfwMakeContextCurrent(GLFWwindow* window);
extern "C" void glfwDestroyWindow(GLFWwindow* window);
#endif

#ifndef __APPLE__
extern "C" void glXSwapBuffers(Display* dpy, GLXDrawable drawable);
extern "C" Bool glXSwapBuffersMscOML(Display* dpy, GLXDrawable drawable, int64_t target_msc, int64_t divisor, int64_t remainder);
extern "C" void glfwSwapBuffers(GLFWwindow* window);
extern "C" __GLXextFuncPtr glXGetProcAddress(const GLubyte* procName);
extern "C" __GLXextFuncPtr glXGetProcAddressARB(const GLubyte* procName);
#endif
extern "C" void glViewport(GLint x, GLint y, GLsizei width, GLsizei height);
extern "C" void glScissor(GLint x, GLint y, GLsizei width, GLsizei height);
extern "C" void glBindFramebuffer(GLenum target, GLuint framebuffer);

namespace {

#ifndef __APPLE__
using GlXSwapBuffersFn = void (*)(Display*, GLXDrawable);
using GlXSwapBuffersMscOMLFn = Bool (*)(Display*, GLXDrawable, int64_t, int64_t, int64_t);
#endif
using GlfwSwapBuffersFn = void (*)(GLFWwindow*);
using GlfwSetKeyCallbackFn = GlfwKeyCallback (*)(GLFWwindow*, GlfwKeyCallback);
using GlfwSetCharCallbackFn = GlfwCharCallback (*)(GLFWwindow*, GlfwCharCallback);
using GlfwSetCharModsCallbackFn = GlfwCharModsCallback (*)(GLFWwindow*, GlfwCharModsCallback);
using GlfwSetMouseButtonCallbackFn = GlfwMouseButtonCallback (*)(GLFWwindow*, GlfwMouseButtonCallback);
using GlfwSetCursorPosCallbackFn = GlfwCursorPosCallback (*)(GLFWwindow*, GlfwCursorPosCallback);
using GlfwSetScrollCallbackFn = GlfwScrollCallback (*)(GLFWwindow*, GlfwScrollCallback);
using GlfwSetWindowFocusCallbackFn = GlfwWindowFocusCallback (*)(GLFWwindow*, GlfwWindowFocusCallback);
using GlfwSetWindowSizeCallbackFn = GlfwWindowSizeCallback (*)(GLFWwindow*, GlfwWindowSizeCallback);
using GlfwSetFramebufferSizeCallbackFn = GlfwFramebufferSizeCallback (*)(GLFWwindow*, GlfwFramebufferSizeCallback);
using GlfwSetWindowSizeFn = void (*)(GLFWwindow*, int, int);
using GlfwSetWindowIconFn = void (*)(GLFWwindow*, int, const GLFWimage*);
using GlfwGetWindowSizeFn = void (*)(GLFWwindow*, int*, int*);
using GlfwGetFramebufferSizeFn = void (*)(GLFWwindow*, int*, int*);
#ifdef __APPLE__
using GlfwMakeContextCurrentFn = void (*)(GLFWwindow*);
using GlfwDestroyWindowFn = void (*)(GLFWwindow*);
#endif
using GlfwGetKeyFn = int (*)(GLFWwindow*, int);
using GlfwGetKeyScancodeFn = int (*)(int);
using GlfwGetCursorPosProc = void (*)(GLFWwindow*, double*, double*);
using GlfwSetCursorPosProc = void (*)(GLFWwindow*, double, double);
using GlfwSetInputModeFn = void (*)(GLFWwindow*, int, int);
using GlViewportFn = void (*)(GLint, GLint, GLsizei, GLsizei);
using GlScissorFn = void (*)(GLint, GLint, GLsizei, GLsizei);
using GlBindFramebufferFn = void (*)(GLenum, GLuint);
#ifndef __APPLE__
using GlXGetProcAddressFn = __GLXextFuncPtr (*)(const GLubyte*);
#endif
using DlSymFn = void* (*)(void*, const char*);

enum class SwapHookSource : std::uint8_t {
    Unknown = 0,
    GlXSwapBuffers = 1,
    GlXSwapBuffersMscOML = 2,
    GlfwSwapBuffers = 3,
#ifdef __APPLE__
    CGLFlushDrawable = 4,
#endif
};

#ifdef __APPLE__
using CGLFlushDrawableFn = CGLError (*)(CGLContextObj);
#endif

#ifndef __APPLE__
std::atomic<GlXSwapBuffersFn> g_realGlXSwapBuffers{ nullptr };

std::atomic<GlXSwapBuffersMscOMLFn> g_realGlXSwapBuffersMscOML{ nullptr };
#endif

std::atomic<GlfwSwapBuffersFn> g_realGlfwSwapBuffers{ nullptr };

std::atomic<GlfwSetKeyCallbackFn> g_realGlfwSetKeyCallback{ nullptr };
std::atomic<GlfwSetCharCallbackFn> g_realGlfwSetCharCallback{ nullptr };
std::atomic<GlfwSetCharModsCallbackFn> g_realGlfwSetCharModsCallback{ nullptr };
std::atomic<GlfwSetMouseButtonCallbackFn> g_realGlfwSetMouseButtonCallback{ nullptr };
std::atomic<GlfwSetCursorPosCallbackFn> g_realGlfwSetCursorPosCallback{ nullptr };
std::atomic<GlfwSetScrollCallbackFn> g_realGlfwSetScrollCallback{ nullptr };
std::atomic<GlfwSetWindowFocusCallbackFn> g_realGlfwSetWindowFocusCallback{ nullptr };
std::atomic<GlfwSetWindowSizeCallbackFn> g_realGlfwSetWindowSizeCallback{ nullptr };
std::atomic<GlfwSetFramebufferSizeCallbackFn> g_realGlfwSetFramebufferSizeCallback{ nullptr };
std::atomic<GlfwSetWindowSizeFn> g_realGlfwSetWindowSize{ nullptr };
std::atomic<GlfwSetWindowIconFn> g_realGlfwSetWindowIcon{ nullptr };
std::atomic<GlfwGetWindowSizeFn> g_realGlfwGetWindowSize{ nullptr };
std::atomic<GlfwGetFramebufferSizeFn> g_realGlfwGetFramebufferSize{ nullptr };
#ifdef __APPLE__
std::atomic<GlfwMakeContextCurrentFn> g_realGlfwMakeContextCurrent{ nullptr };
std::atomic<GlfwDestroyWindowFn> g_realGlfwDestroyWindow{ nullptr };
#endif
std::atomic<GlfwGetKeyFn> g_realGlfwGetKey{ nullptr };
std::atomic<GlfwGetKeyScancodeFn> g_realGlfwGetKeyScancode{ nullptr };
std::atomic<GlfwGetCursorPosProc> g_realGlfwGetCursorPos{ nullptr };
std::atomic<GlfwSetCursorPosProc> g_realGlfwSetCursorPos{ nullptr };
std::atomic<GlfwSetInputModeFn> g_realGlfwSetInputMode{ nullptr };
std::atomic<GlViewportFn> g_realGlViewport{ nullptr };
std::atomic<GlScissorFn> g_realGlScissor{ nullptr };
std::atomic<GlBindFramebufferFn> g_realGlBindFramebuffer{ nullptr };

#ifndef __APPLE__
std::atomic<GlXGetProcAddressFn> g_realGlXGetProcAddress{ nullptr };

std::atomic<GlXGetProcAddressFn> g_realGlXGetProcAddressARB{ nullptr };
#endif

std::atomic<void*> g_libGlHandle{ nullptr };
std::once_flag g_libGlOpenOnce;

std::atomic<void*> g_libGlfwHandle{ nullptr };
std::once_flag g_libGlfwOpenOnce;
std::atomic<void*> g_glfwResolverHandle{ nullptr };

std::atomic<void*> g_libDlHandle{ nullptr };
std::once_flag g_libDlOpenOnce;

std::atomic<DlSymFn> g_realDlSym{ nullptr };

std::atomic<bool> g_loggedBootstrap{ false };
std::atomic<bool> g_loggedFirstSwap{ false };
std::atomic<bool> g_loggedResolveFailure{ false };
std::atomic<bool> g_loggedInstallHookFailure{ false };
std::atomic<bool> g_loggedLaunchContextFailure{ false };
std::atomic<bool> g_loggedDebugEnabled{ false };
std::atomic<bool> g_loggedProcessIdentity{ false };
std::atomic<bool> g_loggedNoMscFallback{ false };
std::atomic<bool> g_loggedNoGlfwSwap{ false };
std::atomic<bool> g_loggedProcHookSwap{ false };
std::atomic<bool> g_loggedProcHookMsc{ false };
std::atomic<bool> g_loggedDlSymHookSwap{ false };
std::atomic<bool> g_loggedDlSymHookMsc{ false };
std::atomic<bool> g_loggedDlSymHookGlfw{ false };
std::atomic<bool> g_loggedDlSymHookGetProc{ false };
std::atomic<bool> g_loggedDlSymHookGetProcArb{ false };
std::atomic<bool> g_loggedDlSymResolveFailure{ false };
std::atomic<bool> g_loggedFirstGlfwKeyCallback{ false };
std::atomic<bool> g_loggedFirstGlfwCharCallback{ false };
std::atomic<bool> g_loggedFirstGlfwCharModsCallback{ false };
std::atomic<bool> g_loggedFirstGlfwMouseCallback{ false };
std::atomic<bool> g_loggedFirstGlfwCursorPosCallback{ false };
std::atomic<bool> g_loggedFirstGlfwFocusCallback{ false };
std::atomic<bool> g_loggedGlfwCallbackResolveFailure{ false };
std::atomic<bool> g_loggedGlfwSetKeyHook{ false };
std::atomic<bool> g_loggedGlfwSetCharHook{ false };
std::atomic<bool> g_loggedGlfwSetCharModsHook{ false };
std::atomic<bool> g_loggedGlfwSetMouseHook{ false };
std::atomic<bool> g_loggedGlfwSetCursorPosHook{ false };
std::atomic<bool> g_loggedGlfwSetScrollHook{ false };
std::atomic<bool> g_loggedGlfwSetFocusHook{ false };
std::atomic<bool> g_loggedGlfwSetWindowSizeHook{ false };
std::atomic<bool> g_loggedGlfwSetFramebufferSizeHook{ false };
std::atomic<bool> g_loggedGlfwSetWindowIconBypass{ false };
std::atomic<bool> g_loggedGlfwInputHookDisabled{ false };
std::atomic<bool> g_loggedImGuiInputBridgeNoImGui{ false };
std::atomic<bool> g_loggedImGuiInputBridgeNoContext{ false };
std::atomic<bool> g_loggedFirstImGuiOverlayFrame{ false };
std::atomic<bool> g_loggedImGuiOverlayMissingWindow{ false };
std::atomic<bool> g_loggedImGuiOverlayMissingGlContext{ false };
std::atomic<bool> g_loggedImGuiOverlayInitFailed{ false };
std::atomic<bool> g_loggedMissingGlfwKeyUserCallback{ false };
std::atomic<bool> g_loggedMissingGlfwCharUserCallback{ false };
std::atomic<bool> g_loggedMissingGlfwCharModsUserCallback{ false };
std::atomic<bool> g_loggedMissingGlfwMouseUserCallback{ false };
std::atomic<bool> g_loggedMissingGlfwCursorPosUserCallback{ false };
std::atomic<bool> g_loggedMissingGlfwScrollUserCallback{ false };
std::atomic<bool> g_loggedMissingGlfwFocusUserCallback{ false };
std::atomic<bool> g_loggedMissingGlfwWindowSizeUserCallback{ false };
std::atomic<bool> g_loggedMissingGlfwFramebufferSizeUserCallback{ false };
std::atomic<bool> g_loggedImGuiInputQueueDrop{ false };
std::atomic<bool> g_loggedProactiveCharCallbackInstall{ false };
std::atomic<bool> g_loggedUnsupportedKeyRebindMapping{ false };
std::atomic<bool> g_loggedUnsupportedMouseRebindMapping{ false };
std::atomic<bool> g_loggedMissingRebindMouseDispatchCallback{ false };
std::atomic<bool> g_loggedMissingRebindKeyDispatchCallback{ false };
std::atomic<bool> g_loggedMissingGlfwCharModsSymbol{ false };
std::atomic<bool> g_charCallbackInstalled{ false };

std::atomic<std::uintptr_t> g_lastDisplay{ 0 };
std::atomic<unsigned long> g_lastDrawable{ 0 };
std::atomic<std::uintptr_t> g_lastContext{ 0 };
std::atomic<SwapHookSource> g_firstSwapSource{ SwapHookSource::Unknown };

std::mutex g_glfwCallbackMutex;
#ifdef __APPLE__
std::mutex g_glfwContextWindowMutex;
std::map<void*, GLFWwindow*> g_glfwContextWindowMap;
#endif

struct GlfwCallbackState {
    GlfwKeyCallback key = nullptr;
    GlfwCharCallback character = nullptr;
    GlfwCharModsCallback characterMods = nullptr;
    GlfwMouseButtonCallback mouseButton = nullptr;
    GlfwCursorPosCallback cursorPos = nullptr;
    GlfwScrollCallback scroll = nullptr;
    GlfwWindowFocusCallback focus = nullptr;
    GlfwWindowSizeCallback windowSize = nullptr;
    GlfwFramebufferSizeCallback framebufferSize = nullptr;
};

std::map<GLFWwindow*, GlfwCallbackState> g_glfwCallbackMap;

struct SyntheticRebindKeyState {
    std::map<int, int> sourceToTarget;
    std::map<int, int> targetPressCount;
    std::map<int, bool> physicalKeyDown;
    std::map<int, std::set<int>> targetSuppressedKeys;
    std::map<int, int> suppressedKeyRefCount;
};

std::mutex g_syntheticRebindKeyMutex;
std::map<GLFWwindow*, SyntheticRebindKeyState> g_syntheticRebindKeyStates;

struct ManagedRepeatKey {
    GLFWwindow* window = nullptr;
    int sourceCode = -1;
    bool sourceIsMouseButton = false;
};

bool operator<(const ManagedRepeatKey& lhs, const ManagedRepeatKey& rhs) {
    if (lhs.window != rhs.window) {
        return lhs.window < rhs.window;
    }
    if (lhs.sourceCode != rhs.sourceCode) {
        return lhs.sourceCode < rhs.sourceCode;
    }
    return lhs.sourceIsMouseButton < rhs.sourceIsMouseButton;
}

enum class ManagedRepeatCharMode : std::uint8_t {
    NoCharacter = 0,
    InjectSynthetic = 1,
    HandledByKeyCallback = 2,
    ConsumeNativeOnly = 3,
};

struct ManagedRepeatSettings {
    bool enabled = false;
    bool disableRepeat = false;
    int effectiveStartDelayMs = 0;
    int effectiveRepeatDelayMs = 0;
};

struct ManagedRepeatState {
    ManagedRepeatKey key;
    platform::input::VkCode sourceVk = platform::input::VK_NONE;
    int nativeScanCode = 0;
    int nativeMods = 0;
    std::uint64_t keyboardPressOrder = 0;
    int effectiveRepeatDelayMs = 0;
    std::chrono::steady_clock::time_point nextRepeatTime{};
};

enum class ManagedDimensionSpace : std::uint8_t {
    WindowLogical,
    FramebufferPhysical,
};

struct PlacementTransform {
    bool valid = false;
    int logicalWidth = 0;
    int logicalHeight = 0;
    int physicalWidth = 0;
    int physicalHeight = 0;
    int windowWidth = 0;
    int windowHeight = 0;
    int framebufferWidth = 0;
    int framebufferHeight = 0;
    int windowTopLeftX = 0;
    int windowTopLeftY = 0;
    int framebufferBottomLeftX = 0;
    int framebufferBottomLeftY = 0;
    int mainTranslateX = 0;
    int mainTranslateY = 0;
};

struct TrackedCursorState {
    std::atomic<bool> rawValid{ false };
    std::atomic<double> rawX{ 0.0 };
    std::atomic<double> rawY{ 0.0 };
    std::atomic<bool> capturedValid{ false };
    std::atomic<double> capturedX{ 0.0 };
    std::atomic<double> capturedY{ 0.0 };
    std::atomic<bool> captureEnterPending{ false };
};

struct PendingSyntheticCursorPosCallback {
    std::atomic<bool> valid{ false };
    std::atomic<GLFWwindow*> window{ nullptr };
    std::atomic<double> rawX{ 0.0 };
    std::atomic<double> rawY{ 0.0 };
};

std::mutex g_managedRepeatMutex;
std::map<ManagedRepeatKey, ManagedRepeatState> g_managedRepeatStates;
std::set<ManagedRepeatKey> g_managedRepeatInvalidatedKeys;
std::atomic<std::uint64_t> g_managedKeyboardPressSequence{ 0 };
std::mutex g_nativeRepeatDefaultsMutex;
bool g_nativeRepeatDefaultsInitialized = false;
int g_nativeRepeatStartDelayMs = 400;
int g_nativeRepeatDelayMs = 33;

std::mutex g_inputStateMutex;
platform::input::KeyStateTracker g_keyStateTracker;
std::atomic<int64_t> g_lastGuiToggleTimeMs{ 0 };
std::atomic<int64_t> g_lastRebindToggleTimeMs{ 0 };
std::atomic<int> g_lastObservedRebindsEnabledState{ -1 };
std::atomic<int> g_lastResizeRequestWidth{ 0 };
std::atomic<int> g_lastResizeRequestHeight{ 0 };
std::atomic<int> g_lastSwapViewportWidth{ 0 };
std::atomic<int> g_lastSwapViewportHeight{ 0 };
std::atomic<int> g_lastSwapViewportX{ 0 };
std::atomic<int> g_lastSwapViewportY{ 0 };
std::atomic<GLFWwindow*> g_lastSwapWindow{ nullptr };
std::atomic<bool> g_gameWantsCursorDisabled{ false };
std::atomic<bool> g_cursorCaptureActive{ false };
std::atomic<bool> g_restoreCursorDisabledOnGuiClose{ false };
std::atomic<int> g_captureEntrySuppressRemaining{ 0 };
std::atomic<double> g_captureEntrySuppressCenterX{ 0.0 };
std::atomic<double> g_captureEntrySuppressCenterY{ 0.0 };
TrackedCursorState g_trackedCursorState;
PendingSyntheticCursorPosCallback g_pendingSyntheticCursorPosCallback;

// HotkeyDispatcher for mode-switch hotkeys — lazily initialized via function
// to avoid static init order issues on macOS (mutex must be constructed before
// the __attribute__((constructor)) runs).
platform::input::HotkeyDispatcher& g_hotkeyDispatcher() {
    static platform::input::HotkeyDispatcher instance;
    return instance;
}
std::atomic<bool> g_hotkeyDispatcherInitialized{ false };

struct TempSensitivityOverrideState {
    bool active = false;
    float sensitivityX = 1.0f;
    float sensitivityY = 1.0f;
    int activeSensHotkeyIndex = -1;
    bool triggerOnHold = false;
};

std::mutex g_tempSensitivityMutex;
TempSensitivityOverrideState g_tempSensitivityOverride;
std::vector<TempSensitivityOverrideState> g_tempSensitivityOverrideStack;
std::mutex g_sensitivityDebounceMutex;
std::vector<std::chrono::steady_clock::time_point> g_sensitivityHotkeyLastTriggerTimes;

std::mutex g_cursorSensitivityStateMutex;
bool g_cursorSensitivityBaselineValid = false;
double g_cursorSensitivityLastRawX = 0.0;
double g_cursorSensitivityLastRawY = 0.0;
double g_cursorSensitivityLastOutputX = 0.0;
double g_cursorSensitivityLastOutputY = 0.0;
double g_cursorSensitivityAccumX = 0.0;
double g_cursorSensitivityAccumY = 0.0;

struct PendingCharRemap {
    std::uint64_t sequence = 0;
    std::uint32_t outputCodepoint = 0;
    bool consume = false;
};

std::mutex g_pendingCharRemapMutex;
std::deque<PendingCharRemap> g_pendingCharRemaps;
std::atomic<std::uint64_t> g_pendingCharRemapSequence{ 0 };

static std::atomic<uint64_t> g_lastHotkeyConfigVersion{0};

static std::atomic<int64_t> g_nextGameStateResetCheckMs{0};
std::mutex g_gameStateResetMutex;
std::string g_previousGameStateForReset;
bool g_hasPreviousGameStateForReset = false;

thread_local bool g_inSwapHook = false;
thread_local bool g_inDlsymHook = false;
thread_local bool g_bypassViewportPlacement = false;
thread_local bool g_dispatchingManagedSyntheticRepeat = false;
#ifdef __APPLE__
thread_local bool g_bypassDlsymInterpose = false;
#endif

struct ViewportPlacementBypassGuard {
    explicit ViewportPlacementBypassGuard(bool enabled) : previous(g_bypassViewportPlacement) {
        if (enabled) {
            g_bypassViewportPlacement = true;
        }
    }

    ~ViewportPlacementBypassGuard() {
        g_bypassViewportPlacement = previous;
    }

    bool previous;
};

void ClearTempSensitivityOverrideInternal();
void UpdateSensitivityStateForModeSwitchInternal(const std::string& targetMode,
                                                 const platform::config::LinuxscreenConfig& config);
void ResetCursorSensitivityState();

} // namespace

namespace platform::x11 {

void SetViewportPlacementBypass(bool bypass) {
    g_bypassViewportPlacement = bypass;
}

void ClearTempSensitivityOverride() {
    ClearTempSensitivityOverrideInternal();
}

bool ReleaseHeldSensitivityOverrideForInputReset() {
    bool overrideChanged = false;
    {
        std::lock_guard<std::mutex> lock(g_tempSensitivityMutex);
        if (!g_tempSensitivityOverride.active || !g_tempSensitivityOverride.triggerOnHold) {
            return false;
        }

        const TempSensitivityOverrideState restoredState =
            g_tempSensitivityOverrideStack.empty() ? TempSensitivityOverrideState{} : g_tempSensitivityOverrideStack.back();
        if (!g_tempSensitivityOverrideStack.empty()) {
            g_tempSensitivityOverrideStack.pop_back();
        }

        overrideChanged = g_tempSensitivityOverride.active != restoredState.active ||
                          std::fabs(g_tempSensitivityOverride.sensitivityX - restoredState.sensitivityX) > 0.000001f ||
                          std::fabs(g_tempSensitivityOverride.sensitivityY - restoredState.sensitivityY) > 0.000001f ||
                          g_tempSensitivityOverride.activeSensHotkeyIndex != restoredState.activeSensHotkeyIndex ||
                          g_tempSensitivityOverride.triggerOnHold != restoredState.triggerOnHold;
        g_tempSensitivityOverride = restoredState;
    }

    if (overrideChanged) {
        ResetCursorSensitivityState();
    }
    return overrideChanged;
}

void UpdateSensitivityStateForModeSwitch(const std::string& targetMode, const config::LinuxscreenConfig& config) {
    UpdateSensitivityStateForModeSwitchInternal(targetMode, config);
}

} // namespace platform::x11

namespace {

pid_t GetCachedPid() {
    static pid_t pid = getpid();
    return pid;
}

std::string GetProcessExePath() {
    char pathBuffer[4096];
#ifdef __APPLE__
    uint32_t bufSize = sizeof(pathBuffer);
    if (_NSGetExecutablePath(pathBuffer, &bufSize) != 0) { return std::string("<unknown>"); }
    char realPath[PATH_MAX];
    if (realpath(pathBuffer, realPath)) { return std::string(realPath); }
    return std::string(pathBuffer);
#else
    ssize_t n = readlink("/proc/self/exe", pathBuffer, sizeof(pathBuffer) - 1);
    if (n <= 0) { return std::string("<unknown>"); }
    pathBuffer[n] = '\0';
    return std::string(pathBuffer);
#endif
}

std::string GetProcessBaseName() {
    const std::string exePath = GetProcessExePath();
    const std::size_t slashPos = exePath.find_last_of('/');
    if (slashPos == std::string::npos) {
        return exePath;
    }
    return exePath.substr(slashPos + 1);
}

const char* SwapHookSourceToString(SwapHookSource source) {
    switch (source) {
    case SwapHookSource::GlXSwapBuffers:
        return "glXSwapBuffers";
    case SwapHookSource::GlXSwapBuffersMscOML:
        return "glXSwapBuffersMscOML";
    case SwapHookSource::GlfwSwapBuffers:
        return "glfwSwapBuffers";
#ifdef __APPLE__
    case SwapHookSource::CGLFlushDrawable:
        return "CGLFlushDrawable";
#endif
    case SwapHookSource::Unknown:
        break;
    }
    return "unknown";
}

bool IsTruthyEnvValue(const char* value) {
    if (!value || value[0] == '\0') { return false; }
    return (value[0] == '1' && value[1] == '\0') || (value[0] == 'y' || value[0] == 'Y') || (value[0] == 't' || value[0] == 'T');
}

bool IsDebugEnabled() {
    static std::once_flag debugOnce;
    static bool debugEnabled = false;
    std::call_once(debugOnce, []() { debugEnabled = IsTruthyEnvValue(std::getenv("LINUXSCREEN_X11_DEBUG")); });
    return debugEnabled;
}

bool IsGlfwInputHookEnabled() { return true; }

bool IsImGuiInputBridgeEnabled() { return false; }

bool IsResizeEnabled() { return true; }

bool IsPhysicalWindowResizeFallbackEnabled() { return false; }

GlfwSetWindowSizeFn GetRealGlfwSetWindowSize();
GlfwSetWindowIconFn GetRealGlfwSetWindowIcon();
GlfwGetWindowSizeFn GetRealGlfwGetWindowSize();
GlfwGetFramebufferSizeFn GetRealGlfwGetFramebufferSize();
#ifdef __APPLE__
GlfwMakeContextCurrentFn GetRealGlfwMakeContextCurrent();
GlfwDestroyWindowFn GetRealGlfwDestroyWindow();
#endif
GlfwGetKeyFn GetRealGlfwGetKey();
GlfwSetWindowSizeCallbackFn GetRealGlfwSetWindowSizeCallback();
GlfwSetFramebufferSizeCallbackFn GetRealGlfwSetFramebufferSizeCallback();
GlfwSetCharModsCallbackFn GetRealGlfwSetCharModsCallback();
GlfwGetKeyScancodeFn GetRealGlfwGetKeyScancode();
GlfwGetCursorPosProc GetRealGlfwGetCursorPos();
GlfwSetCursorPosProc GetRealGlfwSetCursorPos();
GlfwSetInputModeFn GetRealGlfwSetInputMode();
void ResetCursorSensitivityState();
void RefreshTrackedGlfwWindowMetrics(GLFWwindow* window);
#ifdef __APPLE__
void TrackGlfwWindowForCurrentContext(GLFWwindow* window, void* glContext);
GLFWwindow* FindTrackedGlfwWindowForContext(void* glContext);
void ForgetTrackedGlfwWindow(GLFWwindow* window);
bool ShouldBypassGlfwSetWindowIconOnMac();
#endif
bool GetCurrentPlacementContainerMetrics(int& outWindowWidth,
                                         int& outWindowHeight,
                                         int& outFramebufferWidth,
                                         int& outFramebufferHeight);
bool ResolveActiveModeTargetDimensionsForSpace(ManagedDimensionSpace space,
                                               int windowWidth,
                                               int windowHeight,
                                               int framebufferWidth,
                                               int framebufferHeight,
                                               int& outWidth,
                                               int& outHeight);
bool ResolveResizeDispatchDimensionsForActiveMode(int incomingWidth,
                                                  int incomingHeight,
                                                  ManagedDimensionSpace space,
                                                  int& outWidth,
                                                  int& outHeight);

bool GetSizeFromLatestGlfwWindow(int& outWidth, int& outHeight) {
    outWidth = 0;
    outHeight = 0;

    GLFWwindow* window = g_lastSwapWindow.load(std::memory_order_acquire);
    GlfwGetWindowSizeFn getWindowSize = GetRealGlfwGetWindowSize();
    if (!window || !getWindowSize) {
        return false;
    }

    getWindowSize(window, &outWidth, &outHeight);
    return outWidth > 0 && outHeight > 0;
}

bool GetFramebufferSizeFromLatestGlfwWindow(int& outWidth, int& outHeight) {
    outWidth = 0;
    outHeight = 0;

    GLFWwindow* window = g_lastSwapWindow.load(std::memory_order_acquire);
    GlfwGetFramebufferSizeFn getFramebufferSize = GetRealGlfwGetFramebufferSize();
    if (!window || !getFramebufferSize) {
        return false;
    }

    getFramebufferSize(window, &outWidth, &outHeight);
    return outWidth > 0 && outHeight > 0;
}

void RefreshTrackedGlfwWindowMetrics(GLFWwindow* window) {
    if (!window) {
        return;
    }

    g_lastSwapWindow.store(window, std::memory_order_release);

    int windowWidth = 0;
    int windowHeight = 0;
    int framebufferWidth = 0;
    int framebufferHeight = 0;
    GlfwGetWindowSizeFn getWindowSize = GetRealGlfwGetWindowSize();
    GlfwGetFramebufferSizeFn getFramebufferSize = GetRealGlfwGetFramebufferSize();
    if (getWindowSize) {
        getWindowSize(window, &windowWidth, &windowHeight);
    }
    if (getFramebufferSize) {
        getFramebufferSize(window, &framebufferWidth, &framebufferHeight);
    }
    platform::x11::RecordGlfwWindowMetrics(windowWidth, windowHeight, framebufferWidth, framebufferHeight);
}

#ifdef __APPLE__
void TrackGlfwWindowForCurrentContext(GLFWwindow* window, void* glContext) {
    if (!window || !glContext) {
        return;
    }

    std::lock_guard<std::mutex> lock(g_glfwContextWindowMutex);
    g_glfwContextWindowMap[glContext] = window;
}

GLFWwindow* FindTrackedGlfwWindowForContext(void* glContext) {
    if (!glContext) {
        return nullptr;
    }

    std::lock_guard<std::mutex> lock(g_glfwContextWindowMutex);
    auto it = g_glfwContextWindowMap.find(glContext);
    if (it == g_glfwContextWindowMap.end()) {
        return nullptr;
    }
    return it->second;
}

void ForgetTrackedGlfwWindow(GLFWwindow* window) {
    if (!window) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(g_glfwContextWindowMutex);
        for (auto it = g_glfwContextWindowMap.begin(); it != g_glfwContextWindowMap.end();) {
            if (it->second == window) {
                it = g_glfwContextWindowMap.erase(it);
            } else {
                ++it;
            }
        }
    }

    GLFWwindow* expectedWindow = window;
    (void)g_lastSwapWindow.compare_exchange_strong(expectedWindow, nullptr, std::memory_order_acq_rel);
}

bool IsJavaProcess() {
    static std::once_flag once;
    static bool isJava = false;
    std::call_once(once, []() {
        const std::string baseName = GetProcessBaseName();
        isJava = (baseName == "java" || baseName == "javaw");
    });
    return isJava;
}

bool ShouldBypassGlfwSetWindowIconOnMac() {
    const char* env = std::getenv("LINUXSCREEN_MACOS_BYPASS_GLFW_ICON");
    if (env && env[0] != '\0') {
        return IsTruthyEnvValue(env);
    }
    return IsJavaProcess();
}
#endif

bool GetCurrentPhysicalContainerSize(int& outWidth, int& outHeight) {
    outWidth = 0;
    outHeight = 0;

    if (GetFramebufferSizeFromLatestGlfwWindow(outWidth, outHeight)) {
        return true;
    }

    if (GetSizeFromLatestGlfwWindow(outWidth, outHeight)) {
        return true;
    }

    int windowWidth = 0;
    int windowHeight = 0;
    int framebufferWidth = 0;
    int framebufferHeight = 0;
    if (platform::x11::GetGlfwWindowMetrics(windowWidth, windowHeight, framebufferWidth, framebufferHeight)) {
        if (framebufferWidth > 0 && framebufferHeight > 0) {
            outWidth = framebufferWidth;
            outHeight = framebufferHeight;
            return true;
        }
        if (windowWidth > 0 && windowHeight > 0) {
            outWidth = windowWidth;
            outHeight = windowHeight;
            return true;
        }
    }

    return platform::x11::GetGameWindowSize(outWidth, outHeight);
}

bool GetCurrentContainerSizeForModeTarget(int& outWidth, int& outHeight) {
    if (GetCurrentPhysicalContainerSize(outWidth, outHeight)) {
        return true;
    }

    outWidth = 0;
    outHeight = 0;
    GLint viewport[4] = { 0, 0, 0, 0 };
    glGetIntegerv(GL_VIEWPORT, viewport);
    outWidth = viewport[2];
    outHeight = viewport[3];
    return outWidth > 0 && outHeight > 0;
}

bool ResizeLatestGlfwWindow(int width, int height) {
    GLFWwindow* window = g_lastSwapWindow.load(std::memory_order_acquire);
    GlfwSetWindowSizeFn setWindowSize = GetRealGlfwSetWindowSize();
    if (!window || !setWindowSize) {
        return false;
    }

    setWindowSize(window, width, height);
    return true;
}

bool DispatchResizeEventToGame(int width, int height) {
    GLFWwindow* window = g_lastSwapWindow.load(std::memory_order_acquire);
    if (!window || width <= 0 || height <= 0) {
        return false;
    }

    GlfwWindowSizeCallback windowSizeCallback = nullptr;
    GlfwFramebufferSizeCallback framebufferSizeCallback = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_glfwCallbackMutex);
        auto it = g_glfwCallbackMap.find(window);
        if (it != g_glfwCallbackMap.end()) {
            windowSizeCallback = it->second.windowSize;
            framebufferSizeCallback = it->second.framebufferSize;
        }
    }

    if (!windowSizeCallback && !framebufferSizeCallback) {
        if (IsDebugEnabled()) {
            bool expectedWindow = false;
            bool expectedFramebuffer = false;
            (void)g_loggedMissingGlfwWindowSizeUserCallback.compare_exchange_strong(expectedWindow, true, std::memory_order_acq_rel);
            (void)g_loggedMissingGlfwFramebufferSizeUserCallback.compare_exchange_strong(expectedFramebuffer, true,
                                                                                        std::memory_order_acq_rel);
        }
        return false;
    }

    if (windowSizeCallback) {
        int dispatchWidth = width;
        int dispatchHeight = height;
        (void)ResolveResizeDispatchDimensionsForActiveMode(width,
                                                           height,
                                                           ManagedDimensionSpace::WindowLogical,
                                                           dispatchWidth,
                                                           dispatchHeight);
        windowSizeCallback(window, dispatchWidth, dispatchHeight);
    }
    if (framebufferSizeCallback) {
        int dispatchWidth = width;
        int dispatchHeight = height;
        (void)ResolveResizeDispatchDimensionsForActiveMode(width,
                                                           height,
                                                           ManagedDimensionSpace::FramebufferPhysical,
                                                           dispatchWidth,
                                                           dispatchHeight);
        framebufferSizeCallback(window, dispatchWidth, dispatchHeight);
    }
    return true;
}

bool ResolveResizeDispatchDimensionsForActiveMode(int incomingWidth,
                                                  int incomingHeight,
                                                  ManagedDimensionSpace space,
                                                  int& outWidth,
                                                  int& outHeight) {
    outWidth = incomingWidth;
    outHeight = incomingHeight;

    int windowWidth = 0;
    int windowHeight = 0;
    int framebufferWidth = 0;
    int framebufferHeight = 0;
    if (!GetCurrentPlacementContainerMetrics(windowWidth, windowHeight, framebufferWidth, framebufferHeight)) {
        return false;
    }

    int targetWidth = 0;
    int targetHeight = 0;
    if (!ResolveActiveModeTargetDimensionsForSpace(space,
                                                   windowWidth,
                                                   windowHeight,
                                                   framebufferWidth,
                                                   framebufferHeight,
                                                   targetWidth,
                                                   targetHeight)) {
        return false;
    }
    if (targetWidth <= 0 || targetHeight <= 0) {
        return false;
    }

    outWidth = targetWidth;
    outHeight = targetHeight;
    return outWidth != incomingWidth || outHeight != incomingHeight;
}

bool ResolveActiveModeViewportPlacement(int viewportWidth,
                                        int viewportHeight,
                                        int containerWidth,
                                        int containerHeight,
                                        GLint& outX,
                                        GLint& outY) {
    if (viewportWidth <= 0 || viewportHeight <= 0 || containerWidth <= 0 || containerHeight <= 0) {
        return false;
    }

    auto& modeState = platform::x11::GetMirrorModeState();
    auto config = modeState.GetConfigSnapshot();
    if (!config) {
        return false;
    }

    const std::string activeModeName = modeState.GetActiveModeName();
    if (activeModeName.empty()) {
        return false;
    }

    const platform::config::ModeConfig* activeMode = nullptr;
    for (const auto& mode : config->modes) {
        if (mode.name == activeModeName) {
            activeMode = &mode;
            break;
        }
    }
    if (!activeMode) {
        return false;
    }

    int targetTopLeftX = 0;
    int targetTopLeftY = 0;
    platform::config::GetRelativeCoords(activeMode->positionPreset,
                                        activeMode->x,
                                        activeMode->y,
                                        viewportWidth,
                                        viewportHeight,
                                        containerWidth,
                                        containerHeight,
                                        targetTopLeftX,
                                        targetTopLeftY);

    outX = static_cast<GLint>(targetTopLeftX);
    outY = static_cast<GLint>(containerHeight - targetTopLeftY - viewportHeight);
    return true;
}

bool GetCurrentPlacementContainerMetrics(int& outWindowWidth,
                                         int& outWindowHeight,
                                         int& outFramebufferWidth,
                                         int& outFramebufferHeight) {
    outWindowWidth = 0;
    outWindowHeight = 0;
    outFramebufferWidth = 0;
    outFramebufferHeight = 0;

    if (platform::x11::GetGlfwWindowMetrics(outWindowWidth, outWindowHeight, outFramebufferWidth, outFramebufferHeight)) {
        if (outWindowWidth > 0 && outWindowHeight > 0 && outFramebufferWidth > 0 && outFramebufferHeight > 0) {
            return true;
        }
    }

    if (outWindowWidth <= 0 || outWindowHeight <= 0) {
        (void)GetSizeFromLatestGlfwWindow(outWindowWidth, outWindowHeight);
    }
    if (outFramebufferWidth <= 0 || outFramebufferHeight <= 0) {
        (void)GetFramebufferSizeFromLatestGlfwWindow(outFramebufferWidth, outFramebufferHeight);
    }

    if (outWindowWidth <= 0 || outWindowHeight <= 0) {
        outWindowWidth = outFramebufferWidth;
        outWindowHeight = outFramebufferHeight;
    }
    if (outFramebufferWidth <= 0 || outFramebufferHeight <= 0) {
        outFramebufferWidth = outWindowWidth;
        outFramebufferHeight = outWindowHeight;
    }

    return outWindowWidth > 0 && outWindowHeight > 0 && outFramebufferWidth > 0 && outFramebufferHeight > 0;
}

bool ResolveActiveModeTargetDimensionsForContainer(int containerWidth, int containerHeight, int& outWidth, int& outHeight) {
    outWidth = 0;
    outHeight = 0;

    if (containerWidth <= 0 || containerHeight <= 0) {
        return false;
    }

    auto& modeState = platform::x11::GetMirrorModeState();
    if (modeState.GetActiveModeName().empty()) {
        return false;
    }

    return modeState.GetActiveModeTargetDimensions(containerWidth, containerHeight, outWidth, outHeight) &&
           outWidth > 0 && outHeight > 0;
}

double ResolveFramebufferScaleAxis(int windowAxis, int framebufferAxis) {
    if (windowAxis <= 0 || framebufferAxis <= 0) {
        return 1.0;
    }

    const double scale = static_cast<double>(framebufferAxis) / static_cast<double>(windowAxis);
    return scale > 0.0 ? scale : 1.0;
}

int ConvertFramebufferDimensionToWindowDimension(int framebufferDimension, double framebufferScale) {
    if (framebufferDimension <= 0) {
        return 0;
    }
    if (!(framebufferScale > 0.0)) {
        framebufferScale = 1.0;
    }

    return std::max(1, static_cast<int>(std::lround(static_cast<double>(framebufferDimension) / framebufferScale)));
}

bool ResolveActiveModeTargetDimensionsForSpace(ManagedDimensionSpace space,
                                               int windowWidth,
                                               int windowHeight,
                                               int framebufferWidth,
                                               int framebufferHeight,
                                               int& outWidth,
                                               int& outHeight) {
    outWidth = 0;
    outHeight = 0;

    if (space == ManagedDimensionSpace::FramebufferPhysical) {
        if (ResolveActiveModeTargetDimensionsForContainer(framebufferWidth, framebufferHeight, outWidth, outHeight)) {
            return true;
        }
        return ResolveActiveModeTargetDimensionsForContainer(windowWidth, windowHeight, outWidth, outHeight);
    }

    int framebufferTargetWidth = 0;
    int framebufferTargetHeight = 0;
    if (ResolveActiveModeTargetDimensionsForContainer(framebufferWidth, framebufferHeight, framebufferTargetWidth, framebufferTargetHeight)) {
        const double framebufferScaleX = ResolveFramebufferScaleAxis(windowWidth, framebufferWidth);
        const double framebufferScaleY = ResolveFramebufferScaleAxis(windowHeight, framebufferHeight);
        outWidth = ConvertFramebufferDimensionToWindowDimension(framebufferTargetWidth, framebufferScaleX);
        outHeight = ConvertFramebufferDimensionToWindowDimension(framebufferTargetHeight, framebufferScaleY);
        return outWidth > 0 && outHeight > 0;
    }

    return ResolveActiveModeTargetDimensionsForContainer(windowWidth, windowHeight, outWidth, outHeight);
}

bool ResolvePlacementTransform(PlacementTransform& outTransform) {
    outTransform = PlacementTransform{};

    int windowWidth = 0;
    int windowHeight = 0;
    int framebufferWidth = 0;
    int framebufferHeight = 0;
    if (!GetCurrentPlacementContainerMetrics(windowWidth, windowHeight, framebufferWidth, framebufferHeight)) {
        return false;
    }

    int logicalWidth = 0;
    int logicalHeight = 0;
    if (!ResolveActiveModeTargetDimensionsForSpace(ManagedDimensionSpace::WindowLogical,
                                                   windowWidth,
                                                   windowHeight,
                                                   framebufferWidth,
                                                   framebufferHeight,
                                                   logicalWidth,
                                                   logicalHeight)) {
        return false;
    }

    int physicalWidth = 0;
    int physicalHeight = 0;
    if (!ResolveActiveModeTargetDimensionsForSpace(ManagedDimensionSpace::FramebufferPhysical,
                                                   windowWidth,
                                                   windowHeight,
                                                   framebufferWidth,
                                                   framebufferHeight,
                                                   physicalWidth,
                                                   physicalHeight)) {
        return false;
    }
    if (logicalWidth <= 0 || logicalHeight <= 0 || physicalWidth <= 0 || physicalHeight <= 0) {
        return false;
    }

    GLint windowBottomLeftX = 0;
    GLint windowBottomLeftY = 0;
    if (!ResolveActiveModeViewportPlacement(logicalWidth,
                                            logicalHeight,
                                            windowWidth,
                                            windowHeight,
                                            windowBottomLeftX,
                                            windowBottomLeftY)) {
        return false;
    }

    GLint framebufferBottomLeftX = 0;
    GLint framebufferBottomLeftY = 0;
    if (!ResolveActiveModeViewportPlacement(physicalWidth,
                                            physicalHeight,
                                            framebufferWidth,
                                            framebufferHeight,
                                            framebufferBottomLeftX,
                                            framebufferBottomLeftY)) {
        return false;
    }

    outTransform.valid = true;
    outTransform.logicalWidth = logicalWidth;
    outTransform.logicalHeight = logicalHeight;
    outTransform.physicalWidth = physicalWidth;
    outTransform.physicalHeight = physicalHeight;
    outTransform.windowWidth = windowWidth;
    outTransform.windowHeight = windowHeight;
    outTransform.framebufferWidth = framebufferWidth;
    outTransform.framebufferHeight = framebufferHeight;
    outTransform.windowTopLeftX = static_cast<int>(windowBottomLeftX);
    outTransform.windowTopLeftY = windowHeight - (static_cast<int>(windowBottomLeftY) + logicalHeight);
    outTransform.framebufferBottomLeftX = static_cast<int>(framebufferBottomLeftX);
    outTransform.framebufferBottomLeftY = static_cast<int>(framebufferBottomLeftY);
    if (!platform::x11::IsOverscanActive()) {
        outTransform.mainTranslateX = outTransform.framebufferBottomLeftX;
        outTransform.mainTranslateY = outTransform.framebufferBottomLeftY;
    }
    return true;
}

bool WindowToGame(double windowX, double windowY, double& outGameX, double& outGameY) {
    outGameX = windowX;
    outGameY = windowY;

    PlacementTransform transform;
    if (!ResolvePlacementTransform(transform)) {
        return false;
    }

    outGameX = windowX - static_cast<double>(transform.windowTopLeftX);
    outGameY = windowY - static_cast<double>(transform.windowTopLeftY);

    return transform.windowTopLeftX != 0 || transform.windowTopLeftY != 0 ||
           transform.logicalWidth != transform.windowWidth ||
           transform.logicalHeight != transform.windowHeight;
}

bool GameToWindow(double gameX, double gameY, double& outWindowX, double& outWindowY) {
    outWindowX = gameX;
    outWindowY = gameY;

    PlacementTransform transform;
    if (!ResolvePlacementTransform(transform)) {
        return false;
    }

    outWindowX = gameX + static_cast<double>(transform.windowTopLeftX);
    outWindowY = gameY + static_cast<double>(transform.windowTopLeftY);

    return transform.windowTopLeftX != 0 || transform.windowTopLeftY != 0 ||
           transform.logicalWidth != transform.windowWidth ||
           transform.logicalHeight != transform.windowHeight;
}

bool ResolveCurrentWindowCursorCenter(double& outX, double& outY) {
    PlacementTransform transform;
    if (!ResolvePlacementTransform(transform) ||
        transform.logicalWidth <= 0 || transform.logicalHeight <= 0) {
        return false;
    }

    outX = static_cast<double>(transform.windowTopLeftX) + static_cast<double>(transform.logicalWidth) * 0.5;
    outY = static_cast<double>(transform.windowTopLeftY) + static_cast<double>(transform.logicalHeight) * 0.5;
    return true;
}

bool TranslateMainViewport(GLint x, GLint y, GLsizei width, GLsizei height, GLint& outX, GLint& outY) {
    outX = x;
    outY = y;

    if (width <= 0 || height <= 0) {
        return false;
    }

    PlacementTransform transform;
    if (!ResolvePlacementTransform(transform)) {
        return false;
    }

    outX = x + static_cast<GLint>(transform.mainTranslateX);
    outY = y + static_cast<GLint>(transform.mainTranslateY);

    return transform.mainTranslateX != 0 || transform.mainTranslateY != 0;
}

bool IsCanonicalMainContentRect(GLint x, GLint y, GLsizei width, GLsizei height) {
    if (x != 0 || y != 0 || width <= 0 || height <= 0) {
        return false;
    }

    PlacementTransform transform;
    if (!ResolvePlacementTransform(transform)) {
        return false;
    }

    return width == static_cast<GLsizei>(transform.physicalWidth) &&
           height == static_cast<GLsizei>(transform.physicalHeight);
}

bool TranslateMainScissor(GLint x, GLint y, GLsizei width, GLsizei height, GLint& outX, GLint& outY) {
    return TranslateMainViewport(x, y, width, height, outX, outY);
}

void StoreTrackedRawCursorPosition(double rawX, double rawY) {
    g_trackedCursorState.rawX.store(rawX, std::memory_order_release);
    g_trackedCursorState.rawY.store(rawY, std::memory_order_release);
    g_trackedCursorState.rawValid.store(true, std::memory_order_release);
}

bool LoadTrackedRawCursorPosition(double& outX, double& outY) {
    if (!g_trackedCursorState.rawValid.load(std::memory_order_acquire)) {
        return false;
    }

    outX = g_trackedCursorState.rawX.load(std::memory_order_relaxed);
    outY = g_trackedCursorState.rawY.load(std::memory_order_relaxed);
    return true;
}

void StoreTrackedCapturedCursorPosition(double capturedX, double capturedY) {
    g_trackedCursorState.capturedX.store(capturedX, std::memory_order_release);
    g_trackedCursorState.capturedY.store(capturedY, std::memory_order_release);
    g_trackedCursorState.capturedValid.store(true, std::memory_order_release);
}

bool LoadTrackedCapturedCursorPosition(double& outX, double& outY) {
    if (!g_trackedCursorState.capturedValid.load(std::memory_order_acquire)) {
        return false;
    }

    outX = g_trackedCursorState.capturedX.load(std::memory_order_relaxed);
    outY = g_trackedCursorState.capturedY.load(std::memory_order_relaxed);
    return true;
}

bool ResolveCurrentLogicalCursorCenter(double& outX, double& outY) {
    PlacementTransform transform;
    if (!ResolvePlacementTransform(transform) || transform.logicalWidth <= 0 || transform.logicalHeight <= 0) {
        return false;
    }

    outX = static_cast<double>(transform.logicalWidth) * 0.5;
    outY = static_cast<double>(transform.logicalHeight) * 0.5;
    return true;
}

bool IsCursorDisabledForGameInput() {
    return g_cursorCaptureActive.load(std::memory_order_acquire) && !platform::x11::IsGuiVisible();
}

void ArmCaptureEntryCenterSuppression(double centerX, double centerY) {
    g_captureEntrySuppressCenterX.store(centerX, std::memory_order_release);
    g_captureEntrySuppressCenterY.store(centerY, std::memory_order_release);
    g_captureEntrySuppressRemaining.store(2, std::memory_order_release);
}

void ClearCaptureEntryCenterSuppression() {
    g_captureEntrySuppressRemaining.store(0, std::memory_order_release);
}

bool ShouldSuppressCaptureEntryCursorEvent(double xpos, double ypos) {
    const int remaining = g_captureEntrySuppressRemaining.load(std::memory_order_acquire);
    if (remaining <= 0) {
        return false;
    }

    const double centerX = g_captureEntrySuppressCenterX.load(std::memory_order_relaxed);
    const double centerY = g_captureEntrySuppressCenterY.load(std::memory_order_relaxed);
    const double deltaX = std::fabs(xpos - centerX);
    const double deltaY = std::fabs(ypos - centerY);

    if (deltaX <= 32.0 && deltaY <= 32.0) {
        ClearCaptureEntryCenterSuppression();
        return false;
    }

    g_captureEntrySuppressRemaining.fetch_sub(1, std::memory_order_acq_rel);
    return true;
}

void ClearTrackedCursorCaptureState() {
    g_trackedCursorState.captureEnterPending.store(false, std::memory_order_release);
    ClearCaptureEntryCenterSuppression();
}

void ResolveTrackedCapturedCursorCenter(double& outX, double& outY) {
    if (ResolveCurrentWindowCursorCenter(outX, outY)) {
        return;
    }

    if (LoadTrackedRawCursorPosition(outX, outY)) {
        return;
    }

    const int fallbackWidth = g_lastSwapViewportWidth.load(std::memory_order_relaxed);
    const int fallbackHeight = g_lastSwapViewportHeight.load(std::memory_order_relaxed);
    if (fallbackWidth > 0 && fallbackHeight > 0) {
        outX = static_cast<double>(fallbackWidth) * 0.5;
        outY = static_cast<double>(fallbackHeight) * 0.5;
        return;
    }

    outX = 0.0;
    outY = 0.0;
}

void PrepareTrackedCursorForCapture(GLFWwindow* window) {
    double rawX = 0.0;
    double rawY = 0.0;
    bool haveRaw = LoadTrackedRawCursorPosition(rawX, rawY);
    if (!haveRaw && window) {
        GlfwGetCursorPosProc realGetCursorPos = GetRealGlfwGetCursorPos();
        if (realGetCursorPos) {
            realGetCursorPos(window, &rawX, &rawY);
            StoreTrackedRawCursorPosition(rawX, rawY);
            haveRaw = true;
        }
    }

    double capturedX = 0.0;
    double capturedY = 0.0;
    if (!LoadTrackedCapturedCursorPosition(capturedX, capturedY)) {
        ResolveTrackedCapturedCursorCenter(capturedX, capturedY);
        StoreTrackedCapturedCursorPosition(capturedX, capturedY);
    }
    g_trackedCursorState.captureEnterPending.store(true, std::memory_order_release);
    ResetCursorSensitivityState();
}

bool IsAnyImGuiInputConsumerEnabled() { return IsImGuiInputBridgeEnabled() || platform::x11::IsImGuiRenderEnabled(); }

void LogAlways(const char* format, ...);
void LogDebug(const char* format, ...);
void LogDebugOnce(std::atomic<bool>& onceFlag, const char* message);
int64_t NowMs();

bool IsGlxSharedContextEnabled() { return true; }

void ApplyGuiHotkeyFromConfig(const platform::config::LinuxscreenConfig& config) {
    std::vector<platform::input::VkCode> guiHotkey;
    guiHotkey.reserve(config.guiHotkey.size());
    for (const auto key : config.guiHotkey) {
        guiHotkey.push_back(static_cast<platform::input::VkCode>(key));
    }
    platform::x11::SetGuiHotkey(guiHotkey);
}

void ApplyRebindToggleHotkeyFromConfig(const platform::config::LinuxscreenConfig& config) {
    std::vector<platform::input::VkCode> toggleHotkey;
    toggleHotkey.reserve(config.rebindToggleHotkey.size());
    for (const auto key : config.rebindToggleHotkey) {
        toggleHotkey.push_back(static_cast<platform::input::VkCode>(key));
    }
    platform::x11::SetRebindToggleHotkey(toggleHotkey);
}

void MaybeInitSharedGlxContexts(void* dpy, std::uint64_t drawable, void* context, const char* sourceLabel) {
    if (!IsGlxSharedContextEnabled()) { return; }
#ifdef __APPLE__
    if (!context) { return; }
#else
    if (!dpy || !drawable || !context) { return; }
#endif

    static std::atomic<int64_t> nextRetryMs{ 0 };
    static std::atomic<int64_t> lastFailureLogMs{ 0 };
    static std::mutex infoMutex;
    static std::string lastInfoLogged;

    const int64_t now = NowMs();
    const int64_t retryAt = nextRetryMs.load(std::memory_order_acquire);
    if (retryAt != 0 && now < retryAt) { return; }

    const bool ready = platform::x11::EnsureSharedGlxContexts(reinterpret_cast<void*>(dpy), static_cast<std::uint64_t>(drawable),
                                                             reinterpret_cast<void*>(context));
    if (ready) {
        nextRetryMs.store(0, std::memory_order_release);
        const std::string info = platform::x11::GetSharedGlxContextLastInitInfo();
        if (!info.empty()) {
            std::lock_guard<std::mutex> lock(infoMutex);
            if (info != lastInfoLogged) {
                lastInfoLogged = info;
                LogAlways("%s (via %s)", info.c_str(), sourceLabel);
            }
        } else if (IsDebugEnabled()) {
            LogDebug("GLX shared contexts ready via %s", sourceLabel);
        }
        return;
    }

    const int64_t nextRetry = now + 2000;
    nextRetryMs.store(nextRetry, std::memory_order_release);

    const int64_t lastLog = lastFailureLogMs.load(std::memory_order_acquire);
    if (lastLog == 0 || now - lastLog >= 2000) {
        lastFailureLogMs.store(now, std::memory_order_release);
        const std::string error = platform::x11::GetSharedGlxContextLastError();
        LogAlways("WARNING: GLX shared context init failed via %s (%s)", sourceLabel,
                  error.empty() ? "unknown error" : error.c_str());
    }
}

void LogFormatted(const char* prefix, const char* format, va_list args) {
    std::fprintf(stderr, "%s[pid=%ld] ", prefix, static_cast<long>(GetCachedPid()));
    std::vfprintf(stderr, format, args);
    std::fputc('\n', stderr);
}

void LogAlways(const char* format, ...) {
    va_list args;
    va_start(args, format);
    LogFormatted("[Linuxscreen][x11] ", format, args);
    va_end(args);
}

void LogDebug(const char* format, ...) {
    if (!IsDebugEnabled()) { return; }
    va_list args;
    va_start(args, format);
    LogFormatted("[Linuxscreen][x11][debug] ", format, args);
    va_end(args);
}

void LogOnce(std::atomic<bool>& onceFlag, const char* message) {
    bool expected = false;
    if (onceFlag.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) { LogAlways("%s", message); }
}

void LogDebugOnce(std::atomic<bool>& onceFlag, const char* message) {
    if (!IsDebugEnabled()) { return; }
    bool expected = false;
    if (onceFlag.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) { LogDebug("%s", message); }
}

void ResetCursorSensitivityStateLocked() {
    g_cursorSensitivityBaselineValid = false;
    g_cursorSensitivityLastRawX = 0.0;
    g_cursorSensitivityLastRawY = 0.0;
    g_cursorSensitivityLastOutputX = 0.0;
    g_cursorSensitivityLastOutputY = 0.0;
    g_cursorSensitivityAccumX = 0.0;
    g_cursorSensitivityAccumY = 0.0;
}

void ResetCursorSensitivityState() {
    std::lock_guard<std::mutex> lock(g_cursorSensitivityStateMutex);
    ResetCursorSensitivityStateLocked();
}

void ClearTempSensitivityOverrideInternal() {
    {
        std::lock_guard<std::mutex> lock(g_tempSensitivityMutex);
        g_tempSensitivityOverride.active = false;
        g_tempSensitivityOverride.sensitivityX = 1.0f;
        g_tempSensitivityOverride.sensitivityY = 1.0f;
        g_tempSensitivityOverride.activeSensHotkeyIndex = -1;
        g_tempSensitivityOverride.triggerOnHold = false;
        g_tempSensitivityOverrideStack.clear();
    }
    ResetCursorSensitivityState();
}

bool SensitivitiesMatch(float lhsX, float lhsY, float rhsX, float rhsY) {
    return std::fabs(lhsX - rhsX) <= 0.000001f && std::fabs(lhsY - rhsY) <= 0.000001f;
}

float SelectModeSensitivityX(const platform::config::ModeConfig& mode) {
    if (!mode.sensitivityOverrideEnabled) {
        return 1.0f;
    }
    if (mode.separateXYSensitivity) {
        return mode.modeSensitivityX;
    }
    return mode.modeSensitivity;
}

float SelectModeSensitivityY(const platform::config::ModeConfig& mode) {
    if (!mode.sensitivityOverrideEnabled) {
        return 1.0f;
    }
    if (mode.separateXYSensitivity) {
        return mode.modeSensitivityY;
    }
    return mode.modeSensitivity;
}

bool TryGetModeSensitivityOverride(const platform::config::LinuxscreenConfig& configSnapshot,
                                   const std::string& modeName,
                                   float& outSensitivityX,
                                   float& outSensitivityY) {
    for (const auto& mode : configSnapshot.modes) {
        if (mode.name != modeName) {
            continue;
        }
        if (!mode.sensitivityOverrideEnabled) {
            return false;
        }

        outSensitivityX = SelectModeSensitivityX(mode);
        outSensitivityY = SelectModeSensitivityY(mode);
        return true;
    }

    return false;
}

void ResolveActiveSensitivity(const platform::config::LinuxscreenConfig& configSnapshot,
                              float& outSensitivityX,
                              float& outSensitivityY) {
    outSensitivityX = 1.0f;
    outSensitivityY = 1.0f;

    {
        std::lock_guard<std::mutex> lock(g_tempSensitivityMutex);
        if (g_tempSensitivityOverride.active) {
            outSensitivityX = g_tempSensitivityOverride.sensitivityX;
            outSensitivityY = g_tempSensitivityOverride.sensitivityY;
            return;
        }
    }

    const std::string activeModeName = platform::x11::GetMirrorModeState().GetActiveModeName();
    for (const auto& mode : configSnapshot.modes) {
        if (mode.name != activeModeName) {
            continue;
        }
        if (mode.sensitivityOverrideEnabled) {
            outSensitivityX = SelectModeSensitivityX(mode);
            outSensitivityY = SelectModeSensitivityY(mode);
            return;
        }
        break;
    }

    outSensitivityX = configSnapshot.mouseSensitivity;
    outSensitivityY = configSnapshot.mouseSensitivity;
}

void UpdateSensitivityStateForModeSwitchInternal(const std::string& targetMode,
                                                 const platform::config::LinuxscreenConfig& configSnapshot) {
    float currentSensitivityX = 1.0f;
    float currentSensitivityY = 1.0f;
    ResolveActiveSensitivity(configSnapshot, currentSensitivityX, currentSensitivityY);

    float tempSensitivityX = 1.0f;
    float tempSensitivityY = 1.0f;
    bool tempOverrideActive = false;
    {
        std::lock_guard<std::mutex> lock(g_tempSensitivityMutex);
        tempOverrideActive = g_tempSensitivityOverride.active;
        if (tempOverrideActive) {
            tempSensitivityX = g_tempSensitivityOverride.sensitivityX;
            tempSensitivityY = g_tempSensitivityOverride.sensitivityY;
        }
    }

    float targetModeSensitivityX = 1.0f;
    float targetModeSensitivityY = 1.0f;
    const bool targetModeHasSensitivityOverride =
        TryGetModeSensitivityOverride(configSnapshot, targetMode, targetModeSensitivityX, targetModeSensitivityY);

    if (tempOverrideActive &&
        targetModeHasSensitivityOverride &&
        !SensitivitiesMatch(targetModeSensitivityX,
                            targetModeSensitivityY,
                            currentSensitivityX,
                            currentSensitivityY)) {
        ClearTempSensitivityOverrideInternal();
        return;
    }

    float targetSensitivityX = configSnapshot.mouseSensitivity;
    float targetSensitivityY = configSnapshot.mouseSensitivity;
    if (tempOverrideActive) {
        targetSensitivityX = tempSensitivityX;
        targetSensitivityY = tempSensitivityY;
    } else if (targetModeHasSensitivityOverride) {
        targetSensitivityX = targetModeSensitivityX;
        targetSensitivityY = targetModeSensitivityY;
    }

    if (!SensitivitiesMatch(currentSensitivityX, currentSensitivityY, targetSensitivityX, targetSensitivityY)) {
        ResetCursorSensitivityState();
    }
}

bool CheckSensitivityHotkeyDebounce(size_t hotkeyIndex, int debounceMs) {
    if (debounceMs <= 0) {
        return true;
    }

    const auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(g_sensitivityDebounceMutex);
    if (g_sensitivityHotkeyLastTriggerTimes.size() <= hotkeyIndex) {
        g_sensitivityHotkeyLastTriggerTimes.resize(hotkeyIndex + 1);
    }

    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - g_sensitivityHotkeyLastTriggerTimes[hotkeyIndex]).count();
    if (elapsed < debounceMs) {
        return false;
    }

    g_sensitivityHotkeyLastTriggerTimes[hotkeyIndex] = now;
    return true;
}

bool EvaluateSensitivityHotkeys(const platform::config::LinuxscreenConfig& configSnapshot,
                                const platform::input::KeyStateTracker& tracker,
                                const platform::input::InputEvent& event,
                                const std::string& gameState,
                                platform::input::VkCode rebindTargetVk) {
    if (event.vk == platform::input::VK_NONE) {
        return false;
    }

    for (size_t sensIndex = 0; sensIndex < configSnapshot.sensitivityHotkeys.size(); ++sensIndex) {
        const auto& sensitivityHotkey = configSnapshot.sensitivityHotkeys[sensIndex];
        if (sensitivityHotkey.keys.empty()) {
            continue;
        }

        auto matchesHotkeyWithFallback = [&](bool triggerOnRelease, bool& matchedViaRebind) {
            matchedViaRebind = false;
            bool matched = platform::input::MatchesHotkey(tracker,
                                                          sensitivityHotkey.keys,
                                                          event,
                                                          sensitivityHotkey.conditions.exclusions,
                                                          triggerOnRelease);
            if (matched || rebindTargetVk == platform::input::VK_NONE) {
                return matched;
            }

            platform::input::InputEvent fallbackEvent = event;
            fallbackEvent.vk = rebindTargetVk;
            matched = platform::input::MatchesHotkey(tracker,
                                                     sensitivityHotkey.keys,
                                                     fallbackEvent,
                                                     sensitivityHotkey.conditions.exclusions,
                                                     triggerOnRelease);
            matchedViaRebind = matched;
            return matched;
        };

        if (sensitivityHotkey.triggerOnHold) {
            if (event.action == platform::input::InputAction::Press) {
                const bool stateAllowed = sensitivityHotkey.conditions.gameState.empty() ||
                                          gameState.empty() ||
                                          platform::config::HasMatchingGameStateCondition(sensitivityHotkey.conditions.gameState, gameState);
                if (!stateAllowed) {
                    continue;
                }

                bool matchedViaRebind = false;
                if (!matchesHotkeyWithFallback(false, matchedViaRebind)) {
                    continue;
                }

                if (!CheckSensitivityHotkeyDebounce(sensIndex, sensitivityHotkey.debounce)) {
                    continue;
                }

                float nextSensitivityX = sensitivityHotkey.sensitivity;
                float nextSensitivityY = sensitivityHotkey.sensitivity;
                if (sensitivityHotkey.separateXY) {
                    nextSensitivityX = sensitivityHotkey.sensitivityX;
                    nextSensitivityY = sensitivityHotkey.sensitivityY;
                }

                bool overrideChanged = false;
                {
                    std::lock_guard<std::mutex> lock(g_tempSensitivityMutex);
                    const bool sensitivityChanged = !g_tempSensitivityOverride.active ||
                                                   std::fabs(g_tempSensitivityOverride.sensitivityX - nextSensitivityX) > 0.000001f ||
                                                   std::fabs(g_tempSensitivityOverride.sensitivityY - nextSensitivityY) > 0.000001f;
                    const bool hotkeyIndexChanged = !g_tempSensitivityOverride.active ||
                                                   g_tempSensitivityOverride.activeSensHotkeyIndex != static_cast<int>(sensIndex) ||
                                                   !g_tempSensitivityOverride.triggerOnHold;
                    overrideChanged = sensitivityChanged || hotkeyIndexChanged;

                    g_tempSensitivityOverrideStack.push_back(g_tempSensitivityOverride);
                    g_tempSensitivityOverride.active = true;
                    g_tempSensitivityOverride.sensitivityX = nextSensitivityX;
                    g_tempSensitivityOverride.sensitivityY = nextSensitivityY;
                    g_tempSensitivityOverride.activeSensHotkeyIndex = static_cast<int>(sensIndex);
                    g_tempSensitivityOverride.triggerOnHold = true;
                }

                if (overrideChanged) {
                    ResetCursorSensitivityState();
                }
                return matchedViaRebind;
            }

            if (event.action != platform::input::InputAction::Release) {
                continue;
            }

            bool matchedViaRebind = false;
            if (!matchesHotkeyWithFallback(true, matchedViaRebind)) {
                continue;
            }

            bool overrideChanged = false;
            {
                std::lock_guard<std::mutex> lock(g_tempSensitivityMutex);
                if (!g_tempSensitivityOverride.active ||
                    !g_tempSensitivityOverride.triggerOnHold ||
                    g_tempSensitivityOverride.activeSensHotkeyIndex != static_cast<int>(sensIndex)) {
                    continue;
                }

                const TempSensitivityOverrideState restoredState =
                    g_tempSensitivityOverrideStack.empty() ? TempSensitivityOverrideState{} : g_tempSensitivityOverrideStack.back();
                if (!g_tempSensitivityOverrideStack.empty()) {
                    g_tempSensitivityOverrideStack.pop_back();
                }

                overrideChanged = g_tempSensitivityOverride.active != restoredState.active ||
                                  std::fabs(g_tempSensitivityOverride.sensitivityX - restoredState.sensitivityX) > 0.000001f ||
                                  std::fabs(g_tempSensitivityOverride.sensitivityY - restoredState.sensitivityY) > 0.000001f ||
                                  g_tempSensitivityOverride.activeSensHotkeyIndex != restoredState.activeSensHotkeyIndex ||
                                  g_tempSensitivityOverride.triggerOnHold != restoredState.triggerOnHold;
                g_tempSensitivityOverride = restoredState;
            }

            if (overrideChanged) {
                ResetCursorSensitivityState();
            }
            return matchedViaRebind;
        }

        const bool stateAllowed = sensitivityHotkey.conditions.gameState.empty() ||
                                  gameState.empty() ||
                                  platform::config::HasMatchingGameStateCondition(sensitivityHotkey.conditions.gameState, gameState);
        if (!stateAllowed || event.action != platform::input::InputAction::Press) {
            continue;
        }

        bool matchedViaRebind = false;
        const bool matched = matchesHotkeyWithFallback(false, matchedViaRebind);
        if (!matched) {
            continue;
        }

        if (!CheckSensitivityHotkeyDebounce(sensIndex, sensitivityHotkey.debounce)) {
            continue;
        }

        bool clearOverride = false;
        bool overrideChanged = false;
        {
            std::lock_guard<std::mutex> lock(g_tempSensitivityMutex);
            if (sensitivityHotkey.toggle &&
                g_tempSensitivityOverride.active &&
                g_tempSensitivityOverride.activeSensHotkeyIndex == static_cast<int>(sensIndex) &&
                !g_tempSensitivityOverride.triggerOnHold) {
                clearOverride = true;
            } else {
                float nextSensitivityX = sensitivityHotkey.sensitivity;
                float nextSensitivityY = sensitivityHotkey.sensitivity;
                if (sensitivityHotkey.separateXY) {
                    nextSensitivityX = sensitivityHotkey.sensitivityX;
                    nextSensitivityY = sensitivityHotkey.sensitivityY;
                }
                const int nextHotkeyIndex = sensitivityHotkey.toggle ? static_cast<int>(sensIndex) : -1;

                const bool sensitivityChanged = std::fabs(g_tempSensitivityOverride.sensitivityX - nextSensitivityX) > 0.000001f ||
                                               std::fabs(g_tempSensitivityOverride.sensitivityY - nextSensitivityY) > 0.000001f;
                const bool hotkeyIndexChanged = g_tempSensitivityOverride.activeSensHotkeyIndex != nextHotkeyIndex;
                overrideChanged = !g_tempSensitivityOverride.active || sensitivityChanged || hotkeyIndexChanged;

                g_tempSensitivityOverrideStack.clear();
                g_tempSensitivityOverride.active = true;
                g_tempSensitivityOverride.sensitivityX = nextSensitivityX;
                g_tempSensitivityOverride.sensitivityY = nextSensitivityY;
                g_tempSensitivityOverride.activeSensHotkeyIndex = nextHotkeyIndex;
                g_tempSensitivityOverride.triggerOnHold = false;
            }
        }

        if (clearOverride) {
            ClearTempSensitivityOverrideInternal();
        } else if (overrideChanged) {
            ResetCursorSensitivityState();
        }
        return matchedViaRebind;
    }

    return false;
}

bool AltSecondaryModeConfigMatches(const platform::config::AltSecondaryModeConfig& lhs,
                                   const platform::config::AltSecondaryModeConfig& rhs) {
    return lhs.keys == rhs.keys && lhs.mode == rhs.mode;
}

bool HotkeyConfigMatches(const platform::config::HotkeyConfig& lhs,
                         const platform::config::HotkeyConfig& rhs) {
    if (lhs.keys != rhs.keys ||
        lhs.mainMode != rhs.mainMode ||
        lhs.secondaryMode != rhs.secondaryMode ||
        lhs.returnMode != rhs.returnMode ||
        lhs.debounce != rhs.debounce ||
        lhs.triggerOnRelease != rhs.triggerOnRelease ||
        lhs.triggerOnHold != rhs.triggerOnHold ||
        lhs.blockKeyFromGame != rhs.blockKeyFromGame ||
        lhs.returnToDefaultOnRepeat != rhs.returnToDefaultOnRepeat ||
        lhs.allowExitToFullscreenRegardlessOfGameState != rhs.allowExitToFullscreenRegardlessOfGameState ||
        lhs.conditions.gameState != rhs.conditions.gameState ||
        lhs.conditions.exclusions != rhs.conditions.exclusions ||
        lhs.altSecondaryModes.size() != rhs.altSecondaryModes.size()) {
        return false;
    }

    for (std::size_t i = 0; i < lhs.altSecondaryModes.size(); ++i) {
        if (!AltSecondaryModeConfigMatches(lhs.altSecondaryModes[i], rhs.altSecondaryModes[i])) {
            return false;
        }
    }

    return true;
}

void LogProcessIdentityOnce() {
    bool expected = false;
    if (!g_loggedProcessIdentity.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) { return; }

    std::string exe = GetProcessExePath();
    LogAlways("preload process bootstrap exe=%s", exe.c_str());
}
