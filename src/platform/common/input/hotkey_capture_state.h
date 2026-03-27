#pragma once

#include "input_event.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace platform::config {

enum class CaptureTarget {
    None = 0,
    Hotkey,
    GuiHotkey,
    RebindToggleHotkey,
    AltSecondary,
    Exclusion,
    SensitivityHotkey,
    SensitivityExclusion,
    RebindFrom,
    RebindTo,
    RebindTypes,
    RebindDraftInput,
};

enum class CaptureCompletion {
    None = 0,
    Confirmed,
    Cleared,
    Canceled,
};

struct CaptureResult {
    CaptureTarget target = CaptureTarget::None;
    int targetIndex = -1;
    int targetSubIndex = -1;
    CaptureCompletion completion = CaptureCompletion::None;
};

struct CapturedBindingKey {
    input::BindingKey binding;
    input::VkCode vk = input::VK_NONE;
    int nativeKey = 0;
    int nativeScanCode = 0;
    bool isMouseButton = false;
    bool isModifier = false;
};

struct BindingInputEvent {
    std::uint64_t sequence = 0;
    input::BindingKey binding;
    input::VkCode vk = input::VK_NONE;
    int nativeKey = 0;
    int nativeScanCode = 0;
    int nativeMods = 0;
    bool isMouseButton = false;
    bool isModifier = false;
    input::InputAction action = input::InputAction::Unknown;
};

extern std::atomic<bool> g_hotkeyCapturing;
extern std::mutex g_capturedKeysMutex;
extern std::vector<CapturedBindingKey> g_capturedBindingKeys;
extern std::atomic<bool> g_hotkeyCaptureDone;
extern std::atomic<CaptureTarget> g_captureTarget;
extern std::atomic<int> g_captureTargetIndex;
extern std::atomic<int> g_captureTargetSubIndex;

void StartHotkeyCapture(int hotkeyIndex);
void StartGuiHotkeyCapture();
void StartRebindToggleHotkeyCapture();
void StartAltSecondaryCapture(int hotkeyIndex, int altIndex);
void StartExclusionCapture(int hotkeyIndex, int exclusionIndex);
void StartSensitivityHotkeyCapture(int hotkeyIndex);
void StartSensitivityExclusionCapture(int hotkeyIndex, int exclusionIndex);
void StartRebindFromCapture(int rebindIndex);
void StartRebindToCapture(int rebindIndex);
void StartRebindTypesCapture(int rebindIndex);
void StartRebindDraftInputCapture();

CaptureTarget GetCaptureTarget();
int GetCaptureTargetIndex();
int GetCaptureTargetSubIndex();

std::vector<CapturedBindingKey> GetCapturedBindingKeys();

bool IsHotkeyCaptureDone(int& outHotkeyIndex);
bool IsCaptureDone(CaptureResult& outResult);

void CompleteCaptureConfirmedDetailed(const std::vector<CapturedBindingKey>& capturedKeys);
void CompleteCaptureCleared();
void CompleteCaptureCanceled();

void ResetHotkeyCapture();

bool IsHotkeyCapturing();

std::string FormatCapturedKeys();

// Register the latest key/button input event for binding UIs.
// This is independent from hotkey capture and allows popup-based bind workflows.
void RegisterBindingInputEvent(input::BindingKey binding,
                               input::VkCode displayVk,
                               int nativeKey,
                               int nativeMods,
                               bool isModifier,
                               input::InputAction action);

// Read/consume the latest binding event sequence. Caller stores last seen sequence.
std::uint64_t GetLatestBindingInputSequence();
bool ConsumeBindingInputEventSince(std::uint64_t& lastSeenSequence, BindingInputEvent& outEvent);

} // namespace platform::config
