#include "hotkey_capture_state.h"

#include <sstream>

namespace platform::config {

// Hotkey capture state (shared between overlay and interposer)
std::atomic<bool> g_hotkeyCapturing{ false };
std::mutex g_capturedKeysMutex;
std::vector<CapturedBindingKey> g_capturedBindingKeys;
std::atomic<bool> g_hotkeyCaptureDone{ false };
std::atomic<CaptureTarget> g_captureTarget{ CaptureTarget::None };
std::atomic<int> g_captureTargetIndex{ -1 };
std::atomic<int> g_captureTargetSubIndex{ -1 };
std::atomic<CaptureCompletion> g_captureCompletion{ CaptureCompletion::None };

std::atomic<std::uint64_t> g_bindingInputSequence{ 0 };
std::atomic<input::BindingKeyKind> g_bindingInputKind{ input::BindingKeyKind::None };
std::atomic<int> g_bindingInputCode{ 0 };
std::atomic<input::VkCode> g_bindingInputVk{ input::VK_NONE };
std::atomic<int> g_bindingInputNativeKey{ 0 };
std::atomic<int> g_bindingInputMods{ 0 };
std::atomic<bool> g_bindingInputIsModifier{ false };
std::atomic<input::InputAction> g_bindingInputAction{ input::InputAction::Unknown };

void StartCapture(CaptureTarget target, int targetIndex, int targetSubIndex) {
    std::lock_guard<std::mutex> lock(g_capturedKeysMutex);
    g_capturedBindingKeys.clear();
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

int GetCaptureTargetSubIndex() { return g_captureTargetSubIndex.load(std::memory_order_acquire); }

std::vector<CapturedBindingKey> GetCapturedBindingKeys() {
    std::lock_guard<std::mutex> lock(g_capturedKeysMutex);
    return g_capturedBindingKeys;
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

void CompleteCaptureConfirmedDetailed(const std::vector<CapturedBindingKey>& capturedKeys) {
    {
        std::lock_guard<std::mutex> lock(g_capturedKeysMutex);
        g_capturedBindingKeys = capturedKeys;
    }
    g_captureCompletion.store(CaptureCompletion::Confirmed, std::memory_order_release);
    g_hotkeyCaptureDone.store(true, std::memory_order_release);
    g_hotkeyCapturing.store(false, std::memory_order_release);
}

void CompleteCaptureCleared() {
    {
        std::lock_guard<std::mutex> lock(g_capturedKeysMutex);
        g_capturedBindingKeys.clear();
    }
    g_captureCompletion.store(CaptureCompletion::Cleared, std::memory_order_release);
    g_hotkeyCaptureDone.store(true, std::memory_order_release);
    g_hotkeyCapturing.store(false, std::memory_order_release);
}

void CompleteCaptureCanceled() {
    {
        std::lock_guard<std::mutex> lock(g_capturedKeysMutex);
        g_capturedBindingKeys.clear();
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
    g_capturedBindingKeys.clear();
}

bool IsHotkeyCapturing() {
    return g_hotkeyCapturing.load(std::memory_order_acquire);
}

std::string FormatCapturedKeys() {
    std::lock_guard<std::mutex> lock(g_capturedKeysMutex);
    if (g_capturedBindingKeys.empty()) { return ""; }
    
    std::ostringstream oss;
    for (size_t i = 0; i < g_capturedBindingKeys.size(); ++i) {
        if (i > 0) { oss << "+"; }
        const auto& key = g_capturedBindingKeys[i].binding;
        oss << static_cast<int>(key.kind) << ":" << key.code;
    }
    return oss.str();
}

void RegisterBindingInputEvent(input::BindingKey binding,
                               input::VkCode displayVk,
                               int nativeKey,
                               int nativeMods,
                               bool isModifier,
                               input::InputAction action) {
    g_bindingInputKind.store(binding.kind, std::memory_order_relaxed);
    g_bindingInputCode.store(binding.code, std::memory_order_relaxed);
    g_bindingInputVk.store(displayVk, std::memory_order_relaxed);
    g_bindingInputNativeKey.store(nativeKey, std::memory_order_relaxed);
    g_bindingInputMods.store(nativeMods, std::memory_order_relaxed);
    g_bindingInputIsModifier.store(isModifier, std::memory_order_relaxed);
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
    outEvent.binding.kind = g_bindingInputKind.load(std::memory_order_relaxed);
    outEvent.binding.code = g_bindingInputCode.load(std::memory_order_relaxed);
    outEvent.vk = g_bindingInputVk.load(std::memory_order_relaxed);
    outEvent.nativeKey = g_bindingInputNativeKey.load(std::memory_order_relaxed);
    outEvent.nativeScanCode = input::IsKeyboardBindingKey(outEvent.binding) ? outEvent.binding.code : 0;
    outEvent.nativeMods = g_bindingInputMods.load(std::memory_order_relaxed);
    outEvent.isMouseButton = input::IsMouseBindingKey(outEvent.binding);
    outEvent.isModifier = g_bindingInputIsModifier.load(std::memory_order_relaxed);
    outEvent.action = g_bindingInputAction.load(std::memory_order_relaxed);
    lastSeenSequence = currentSequence;
    return input::IsValidBindingKey(outEvent.binding);
}

} // namespace platform::config
