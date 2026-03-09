#include "x11_runtime.h"

#ifndef __APPLE__
#include <X11/Xlib.h>
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <mutex>
#include <string>
#include <vector>

namespace platform::x11 {

namespace {
std::atomic<RuntimeState> g_runtimeState{ RuntimeState::Uninitialized };
BootstrapConfig g_bootstrapConfig{};
std::mutex g_runtimeHandlesMutex;
RuntimeHandles g_runtimeHandles{};
std::atomic<std::uint64_t> g_swapObservationCount{ 0 };
std::atomic<int> g_glfwWindowWidth{ 0 };
std::atomic<int> g_glfwWindowHeight{ 0 };
std::atomic<int> g_glfwFramebufferWidth{ 0 };
std::atomic<int> g_glfwFramebufferHeight{ 0 };
#ifndef __APPLE__
std::mutex g_x11OpsMutex;
std::mutex g_x11ErrorTrapMutex;
std::atomic<int> g_lastX11ErrorCode{ 0 };
#endif

std::mutex g_errorMutex;
std::string g_lastErrorMessage;

std::mutex g_guiStateMutex;
std::vector<platform::input::VkCode> g_guiHotkey = { platform::input::VK_CONTROL, static_cast<platform::input::VkCode>('I') };
std::vector<platform::input::VkCode> g_rebindToggleHotkey;
std::atomic<bool> g_guiVisible{ false };
std::atomic<std::uint64_t> g_guiToggleCount{ 0 };
std::atomic<bool> g_rebindToggleIndicatorEnabled{ false };
std::atomic<std::int64_t> g_rebindToggleIndicatorUntilMs{ 0 };

constexpr std::size_t kImGuiInputQueueCapacity = 2048;
std::mutex g_imguiInputQueueMutex;
std::array<platform::input::InputEvent, kImGuiInputQueueCapacity> g_imguiInputQueue{};
std::size_t g_imguiInputQueueRead = 0;
std::size_t g_imguiInputQueueWrite = 0;
std::size_t g_imguiInputQueueCountLocal = 0;
std::atomic<std::uint64_t> g_imguiInputQueueCount{ 0 };
std::atomic<std::uint64_t> g_imguiInputDroppedCount{ 0 };

void ClearImGuiInputQueueUnsafe() {
    g_imguiInputQueueRead = 0;
    g_imguiInputQueueWrite = 0;
    g_imguiInputQueueCountLocal = 0;
    g_imguiInputQueueCount.store(0, std::memory_order_release);
}

void SetLastErrorMessage(const std::string& message) {
    std::lock_guard<std::mutex> lock(g_errorMutex);
    g_lastErrorMessage = message;
}

void ClearLastErrorMessage() {
    std::lock_guard<std::mutex> lock(g_errorMutex);
    g_lastErrorMessage.clear();
}

#ifndef __APPLE__
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
#endif
}

bool Initialize(const BootstrapConfig& config) {
#ifndef __APPLE__
    if (std::getenv("DISPLAY") == nullptr) {
        g_runtimeState.store(RuntimeState::Failed, std::memory_order_release);
        SetLastErrorMessage("X11 initialize failed: DISPLAY is not set");
        return false;
    }
#endif

    g_bootstrapConfig = config;

    {
        std::lock_guard<std::mutex> lock(g_runtimeHandlesMutex);
        g_runtimeHandles = RuntimeHandles{};
    }

    g_swapObservationCount.store(0, std::memory_order_release);
    g_glfwWindowWidth.store(0, std::memory_order_release);
    g_glfwWindowHeight.store(0, std::memory_order_release);
    g_glfwFramebufferWidth.store(0, std::memory_order_release);
    g_glfwFramebufferHeight.store(0, std::memory_order_release);
    g_guiVisible.store(false, std::memory_order_release);
    g_guiToggleCount.store(0, std::memory_order_release);
    g_rebindToggleIndicatorEnabled.store(false, std::memory_order_release);
    g_rebindToggleIndicatorUntilMs.store(0, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(g_guiStateMutex);
        g_rebindToggleHotkey.clear();
    }
    {
        std::lock_guard<std::mutex> lock(g_imguiInputQueueMutex);
        ClearImGuiInputQueueUnsafe();
    }
    g_imguiInputDroppedCount.store(0, std::memory_order_release);
    ClearLastErrorMessage();
    g_runtimeState.store(RuntimeState::Initialized, std::memory_order_release);
    return true;
}

bool InstallHooks() {
    RuntimeState state = g_runtimeState.load(std::memory_order_acquire);
    if (state != RuntimeState::Initialized && state != RuntimeState::HooksInstalled) {
        g_runtimeState.store(RuntimeState::Failed, std::memory_order_release);
        SetLastErrorMessage("X11 InstallHooks failed: runtime not initialized");
        return false;
    }

    if (!g_bootstrapConfig.enableSwapHook) {
        g_runtimeState.store(RuntimeState::Failed, std::memory_order_release);
        SetLastErrorMessage("X11 InstallHooks failed: swap hook disabled by config");
        return false;
    }

    ClearLastErrorMessage();
    g_runtimeState.store(RuntimeState::HooksInstalled, std::memory_order_release);
    return true;
}

void Shutdown() {
    g_runtimeState.store(RuntimeState::ShuttingDown, std::memory_order_release);

    {
        std::lock_guard<std::mutex> lock(g_runtimeHandlesMutex);
        g_runtimeHandles = RuntimeHandles{};
    }

    g_swapObservationCount.store(0, std::memory_order_release);
    g_glfwWindowWidth.store(0, std::memory_order_release);
    g_glfwWindowHeight.store(0, std::memory_order_release);
    g_glfwFramebufferWidth.store(0, std::memory_order_release);
    g_glfwFramebufferHeight.store(0, std::memory_order_release);
    g_guiVisible.store(false, std::memory_order_release);
    g_guiToggleCount.store(0, std::memory_order_release);
    g_rebindToggleIndicatorEnabled.store(false, std::memory_order_release);
    g_rebindToggleIndicatorUntilMs.store(0, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(g_guiStateMutex);
        g_rebindToggleHotkey.clear();
    }
    {
        std::lock_guard<std::mutex> lock(g_imguiInputQueueMutex);
        ClearImGuiInputQueueUnsafe();
    }
    g_imguiInputDroppedCount.store(0, std::memory_order_release);
    ClearLastErrorMessage();
    g_runtimeState.store(RuntimeState::Uninitialized, std::memory_order_release);
}

RuntimeHandles GetRuntimeHandles() {
    std::lock_guard<std::mutex> lock(g_runtimeHandlesMutex);
    return g_runtimeHandles;
}

RuntimeState GetRuntimeState() { return g_runtimeState.load(std::memory_order_acquire); }

std::string GetLastErrorMessage() {
    std::lock_guard<std::mutex> lock(g_errorMutex);
    return g_lastErrorMessage;
}

void RecordSwapHandles(void* nativeDisplay, unsigned long drawable, void* glContext) {
    std::lock_guard<std::mutex> lock(g_runtimeHandlesMutex);
    g_runtimeHandles.nativeDisplay = nativeDisplay;
    g_runtimeHandles.nativeWindow = static_cast<std::uint64_t>(drawable);
    g_runtimeHandles.glContext = glContext;
    g_swapObservationCount.fetch_add(1, std::memory_order_acq_rel);
}

bool IsInitialized() {
    RuntimeState state = g_runtimeState.load(std::memory_order_acquire);
    return state == RuntimeState::Initialized || state == RuntimeState::HooksInstalled;
}

std::uint64_t GetSwapObservationCount() { return g_swapObservationCount.load(std::memory_order_acquire); }

bool ResizeGameWindow(int width, int height) {
#ifdef __APPLE__
    (void)width; (void)height;
    return false;
#else
    if (width <= 0 || height <= 0) {
        return false;
    }

    Display* display = nullptr;
    Window window = 0;
    {
        std::lock_guard<std::mutex> lock(g_runtimeHandlesMutex);
        display = reinterpret_cast<Display*>(g_runtimeHandles.nativeDisplay);
        window = static_cast<Window>(g_runtimeHandles.nativeWindow);
    }

    if (!display || window == 0) {
        return false;
    }

    std::lock_guard<std::mutex> lock(g_x11OpsMutex);
    return CallX11WithErrorTrap(display, [&]() {
        XResizeWindow(display, window, static_cast<unsigned int>(width), static_cast<unsigned int>(height));
        XFlush(display);
    });
#endif
}

bool GetGameWindowSize(int& outWidth, int& outHeight) {
    outWidth = 0;
    outHeight = 0;
#ifdef __APPLE__
    return false;
#else
    Display* display = nullptr;
    Window window = 0;
    {
        std::lock_guard<std::mutex> lock(g_runtimeHandlesMutex);
        display = reinterpret_cast<Display*>(g_runtimeHandles.nativeDisplay);
        window = static_cast<Window>(g_runtimeHandles.nativeWindow);
    }

    if (!display || window == 0) {
        return false;
    }

    int status = 0;
    std::lock_guard<std::mutex> lock(g_x11OpsMutex);
    XWindowAttributes attrs{};
    const bool success = CallX11WithErrorTrap(display, [&]() {
        status = XGetWindowAttributes(display, window, &attrs);
    });
    if (!success || status == 0) {
        return false;
    }

    outWidth = attrs.width;
    outHeight = attrs.height;
    return outWidth > 0 && outHeight > 0;
#endif
}

void RecordGlfwWindowMetrics(int windowWidth, int windowHeight, int framebufferWidth, int framebufferHeight) {
    if (windowWidth > 0 && windowHeight > 0) {
        g_glfwWindowWidth.store(windowWidth, std::memory_order_release);
        g_glfwWindowHeight.store(windowHeight, std::memory_order_release);
    }
    if (framebufferWidth > 0 && framebufferHeight > 0) {
        g_glfwFramebufferWidth.store(framebufferWidth, std::memory_order_release);
        g_glfwFramebufferHeight.store(framebufferHeight, std::memory_order_release);
    }
}

bool GetGlfwWindowMetrics(int& outWindowWidth, int& outWindowHeight, int& outFramebufferWidth, int& outFramebufferHeight) {
    outWindowWidth = g_glfwWindowWidth.load(std::memory_order_acquire);
    outWindowHeight = g_glfwWindowHeight.load(std::memory_order_acquire);
    outFramebufferWidth = g_glfwFramebufferWidth.load(std::memory_order_acquire);
    outFramebufferHeight = g_glfwFramebufferHeight.load(std::memory_order_acquire);

    const bool hasWindow = outWindowWidth > 0 && outWindowHeight > 0;
    const bool hasFramebuffer = outFramebufferWidth > 0 && outFramebufferHeight > 0;
    return hasWindow || hasFramebuffer;
}

void SetGuiHotkey(const std::vector<platform::input::VkCode>& keys) {
    std::lock_guard<std::mutex> lock(g_guiStateMutex);
    g_guiHotkey = keys;
}

std::vector<platform::input::VkCode> GetGuiHotkey() {
    std::lock_guard<std::mutex> lock(g_guiStateMutex);
    return g_guiHotkey;
}

void SetRebindToggleHotkey(const std::vector<platform::input::VkCode>& keys) {
    std::lock_guard<std::mutex> lock(g_guiStateMutex);
    g_rebindToggleHotkey = keys;
}

std::vector<platform::input::VkCode> GetRebindToggleHotkey() {
    std::lock_guard<std::mutex> lock(g_guiStateMutex);
    return g_rebindToggleHotkey;
}

bool ToggleGuiVisible() {
    const bool newValue = !g_guiVisible.load(std::memory_order_acquire);
    g_guiVisible.store(newValue, std::memory_order_release);
    g_guiToggleCount.fetch_add(1, std::memory_order_acq_rel);
    return newValue;
}

void SetGuiVisible(bool visible) { g_guiVisible.store(visible, std::memory_order_release); }

bool IsGuiVisible() { return g_guiVisible.load(std::memory_order_acquire); }

std::uint64_t GetGuiToggleCount() { return g_guiToggleCount.load(std::memory_order_acquire); }

void ShowRebindToggleIndicator(bool rebindsEnabled, std::uint64_t durationMs) {
    const std::int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    g_rebindToggleIndicatorEnabled.store(rebindsEnabled, std::memory_order_release);
    g_rebindToggleIndicatorUntilMs.store(nowMs + static_cast<std::int64_t>(durationMs), std::memory_order_release);
}

bool GetRebindToggleIndicator(bool& outRebindsEnabled) {
    const std::int64_t untilMs = g_rebindToggleIndicatorUntilMs.load(std::memory_order_acquire);
    if (untilMs <= 0) {
        return false;
    }

    const std::int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    if (nowMs > untilMs) {
        return false;
    }

    outRebindsEnabled = g_rebindToggleIndicatorEnabled.load(std::memory_order_acquire);
    return true;
}

bool EnqueueImGuiInputEvent(const platform::input::InputEvent& event) {
    bool dropped = false;
    {
        std::lock_guard<std::mutex> lock(g_imguiInputQueueMutex);

        if (g_imguiInputQueueCountLocal == kImGuiInputQueueCapacity) {
            g_imguiInputQueueRead = (g_imguiInputQueueRead + 1) % kImGuiInputQueueCapacity;
            --g_imguiInputQueueCountLocal;
            dropped = true;
        }

        g_imguiInputQueue[g_imguiInputQueueWrite] = event;
        g_imguiInputQueueWrite = (g_imguiInputQueueWrite + 1) % kImGuiInputQueueCapacity;
        ++g_imguiInputQueueCountLocal;
        g_imguiInputQueueCount.store(static_cast<std::uint64_t>(g_imguiInputQueueCountLocal), std::memory_order_release);
    }

    if (dropped) { g_imguiInputDroppedCount.fetch_add(1, std::memory_order_acq_rel); }
    return !dropped;
}

std::size_t DrainImGuiInputEvents(std::vector<platform::input::InputEvent>& outEvents, std::size_t maxEvents) {
    if (maxEvents == 0) { return 0; }

    std::size_t drained = 0;
    {
        std::lock_guard<std::mutex> lock(g_imguiInputQueueMutex);
        drained = (std::min)(maxEvents, g_imguiInputQueueCountLocal);
        outEvents.reserve(outEvents.size() + drained);

        for (std::size_t i = 0; i < drained; ++i) {
            outEvents.push_back(g_imguiInputQueue[g_imguiInputQueueRead]);
            g_imguiInputQueueRead = (g_imguiInputQueueRead + 1) % kImGuiInputQueueCapacity;
        }

        g_imguiInputQueueCountLocal -= drained;
        g_imguiInputQueueCount.store(static_cast<std::uint64_t>(g_imguiInputQueueCountLocal), std::memory_order_release);
    }

    return drained;
}

void ClearImGuiInputEvents() {
    std::lock_guard<std::mutex> lock(g_imguiInputQueueMutex);
    ClearImGuiInputQueueUnsafe();
}

std::uint64_t GetImGuiInputQueuedCount() { return g_imguiInputQueueCount.load(std::memory_order_acquire); }

std::uint64_t GetImGuiInputDroppedCount() { return g_imguiInputDroppedCount.load(std::memory_order_acquire); }

} // namespace platform::x11
