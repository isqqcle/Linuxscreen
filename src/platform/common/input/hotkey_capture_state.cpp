#include "hotkey_capture_state.h"

#include <sstream>

namespace platform::config {

// Hotkey capture state (shared between overlay and interposer)
std::atomic<bool> g_hotkeyCapturing{ false };
std::mutex g_capturedKeysMutex;
std::vector<uint32_t> g_capturedKeys;
std::atomic<bool> g_hotkeyCaptureDone{ false };
std::atomic<CaptureTarget> g_captureTarget{ CaptureTarget::None };
std::atomic<int> g_captureTargetIndex{ -1 };
std::atomic<int> g_captureTargetSubIndex{ -1 };
std::atomic<CaptureCompletion> g_captureCompletion{ CaptureCompletion::None };

std::atomic<std::uint64_t> g_bindingInputSequence{ 0 };
std::atomic<input::VkCode> g_bindingInputVk{ input::VK_NONE };
std::atomic<int> g_bindingInputScanCode{ 0 };
std::atomic<int> g_bindingInputMods{ 0 };
std::atomic<bool> g_bindingInputIsMouseButton{ false };
std::atomic<input::InputAction> g_bindingInputAction{ input::InputAction::Unknown };

void StartCapture(CaptureTarget target, int targetIndex, int targetSubIndex) {
    std::lock_guard<std::mutex> lock(g_capturedKeysMutex);
    g_capturedKeys.clear();
    g_hotkeyCaptureDone.store(false, std::memory_order_release);
    g_captureTarget.store(target, std::memory_order_release);
    g_captureTargetIndex.store(targetIndex, std::memory_order_release);
    g_captureTargetSubIndex.store(targetSubIndex, std::memory_order_release);
    g_captureCompletion.store(CaptureCompletion::None, std::memory_order_release);
    g_hotkeyCapturing.store(true, std::memory_order_release);
}

void StartHotkeyCapture(int hotkeyIndex) {
    StartCapture(CaptureTarget::Hotkey, hotkeyIndex, -1);
}

void StartGuiHotkeyCapture() {
    StartCapture(CaptureTarget::GuiHotkey, -1, -1);
}

void StartRebindToggleHotkeyCapture() {
    StartCapture(CaptureTarget::RebindToggleHotkey, -1, -1);
}

void StartAltSecondaryCapture(int hotkeyIndex, int altIndex) {
    StartCapture(CaptureTarget::AltSecondary, hotkeyIndex, altIndex);
}

void StartExclusionCapture(int hotkeyIndex, int exclusionIndex) {
    StartCapture(CaptureTarget::Exclusion, hotkeyIndex, exclusionIndex);
}

void StartSensitivityHotkeyCapture(int hotkeyIndex) {
    StartCapture(CaptureTarget::SensitivityHotkey, hotkeyIndex, -1);
}

void StartSensitivityExclusionCapture(int hotkeyIndex, int exclusionIndex) {
    StartCapture(CaptureTarget::SensitivityExclusion, hotkeyIndex, exclusionIndex);
}

void StartRebindFromCapture(int rebindIndex) {
    StartCapture(CaptureTarget::RebindFrom, rebindIndex, -1);
}

void StartRebindToCapture(int rebindIndex) {
    StartCapture(CaptureTarget::RebindTo, rebindIndex, -1);
}

void StartRebindTypesCapture(int rebindIndex) {
    StartCapture(CaptureTarget::RebindTypes, rebindIndex, -1);
}

void StartRebindDraftInputCapture() {
    StartCapture(CaptureTarget::RebindDraftInput, -1, -1);
}

CaptureTarget GetCaptureTarget() {
    return g_captureTarget.load(std::memory_order_acquire);
}

int GetCaptureTargetIndex() {
    return g_captureTargetIndex.load(std::memory_order_acquire);
}

int GetCaptureTargetSubIndex() {
    return g_captureTargetSubIndex.load(std::memory_order_acquire);
}

std::vector<uint32_t> GetCapturedKeys() {
    std::lock_guard<std::mutex> lock(g_capturedKeysMutex);
    return g_capturedKeys;
}

bool IsHotkeyCaptureDone(int& outHotkeyIndex) {
    if (!g_hotkeyCaptureDone.load(std::memory_order_acquire)) { return false; }
    if (g_captureTarget.load(std::memory_order_acquire) != CaptureTarget::Hotkey) {
        return false;
    }
    outHotkeyIndex = g_captureTargetIndex.load(std::memory_order_acquire);
    return true;
}

bool IsCaptureDone(CaptureResult& outResult) {
    if (!g_hotkeyCaptureDone.load(std::memory_order_acquire)) {
        return false;
    }
    outResult.target = g_captureTarget.load(std::memory_order_acquire);
    outResult.targetIndex = g_captureTargetIndex.load(std::memory_order_acquire);
    outResult.targetSubIndex = g_captureTargetSubIndex.load(std::memory_order_acquire);
    outResult.completion = g_captureCompletion.load(std::memory_order_acquire);
    return true;
}

void CompleteCaptureConfirmed(const std::vector<uint32_t>& capturedKeys) {
    {
        std::lock_guard<std::mutex> lock(g_capturedKeysMutex);
        g_capturedKeys = capturedKeys;
    }
    g_captureCompletion.store(CaptureCompletion::Confirmed, std::memory_order_release);
    g_hotkeyCaptureDone.store(true, std::memory_order_release);
    g_hotkeyCapturing.store(false, std::memory_order_release);
}

void CompleteCaptureCleared() {
    {
        std::lock_guard<std::mutex> lock(g_capturedKeysMutex);
        g_capturedKeys.clear();
    }
    g_captureCompletion.store(CaptureCompletion::Cleared, std::memory_order_release);
    g_hotkeyCaptureDone.store(true, std::memory_order_release);
    g_hotkeyCapturing.store(false, std::memory_order_release);
}

void CompleteCaptureCanceled() {
    {
        std::lock_guard<std::mutex> lock(g_capturedKeysMutex);
        g_capturedKeys.clear();
    }
    g_captureCompletion.store(CaptureCompletion::Canceled, std::memory_order_release);
    g_hotkeyCaptureDone.store(true, std::memory_order_release);
    g_hotkeyCapturing.store(false, std::memory_order_release);
}

void ResetHotkeyCapture() {
    g_hotkeyCaptureDone.store(false, std::memory_order_release);
    g_captureTarget.store(CaptureTarget::None, std::memory_order_release);
    g_captureTargetIndex.store(-1, std::memory_order_release);
    g_captureTargetSubIndex.store(-1, std::memory_order_release);
    g_captureCompletion.store(CaptureCompletion::None, std::memory_order_release);
    g_hotkeyCapturing.store(false, std::memory_order_release);
    std::lock_guard<std::mutex> lock(g_capturedKeysMutex);
    g_capturedKeys.clear();
}

bool IsHotkeyCapturing() {
    return g_hotkeyCapturing.load(std::memory_order_acquire);
}

std::string FormatCapturedKeys() {
    std::lock_guard<std::mutex> lock(g_capturedKeysMutex);
    if (g_capturedKeys.empty()) { return ""; }
    
    std::ostringstream oss;
    for (size_t i = 0; i < g_capturedKeys.size(); ++i) {
        if (i > 0) { oss << "+"; }
        oss << "0x" << std::hex << g_capturedKeys[i] << std::dec;
    }
    return oss.str();
}

void RegisterBindingInputEvent(input::VkCode vk,
                               int nativeScanCode,
                               int nativeMods,
                               bool isMouseButton,
                               input::InputAction action) {
    g_bindingInputVk.store(vk, std::memory_order_relaxed);
    g_bindingInputScanCode.store(nativeScanCode, std::memory_order_relaxed);
    g_bindingInputMods.store(nativeMods, std::memory_order_relaxed);
    g_bindingInputIsMouseButton.store(isMouseButton, std::memory_order_relaxed);
    g_bindingInputAction.store(action, std::memory_order_relaxed);
    g_bindingInputSequence.fetch_add(1, std::memory_order_release);
}

std::uint64_t GetLatestBindingInputSequence() {
    return g_bindingInputSequence.load(std::memory_order_acquire);
}

bool ConsumeBindingInputEventSince(std::uint64_t& lastSeenSequence, BindingInputEvent& outEvent) {
    const std::uint64_t currentSequence = g_bindingInputSequence.load(std::memory_order_acquire);
    if (currentSequence == 0 || currentSequence == lastSeenSequence) {
        return false;
    }

    outEvent.sequence = currentSequence;
    outEvent.vk = g_bindingInputVk.load(std::memory_order_relaxed);
    outEvent.nativeScanCode = g_bindingInputScanCode.load(std::memory_order_relaxed);
    outEvent.nativeMods = g_bindingInputMods.load(std::memory_order_relaxed);
    outEvent.isMouseButton = g_bindingInputIsMouseButton.load(std::memory_order_relaxed);
    outEvent.action = g_bindingInputAction.load(std::memory_order_relaxed);
    lastSeenSequence = currentSequence;
    return outEvent.vk != input::VK_NONE;
}

} // namespace platform::config
