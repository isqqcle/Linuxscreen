#include "imgui_input_bridge.h"

#include "../x11_runtime.h"

#include "../../common/input/glfw_vk_mapper.h"
#include "../../common/input/input_event.h"
#include "../../common/input/vk_codes.h"

#include <cstddef>
#include <vector>

#include "imgui.h"

namespace platform::x11 {

namespace {

void ApplyGlfwModifierState(ImGuiIO& io, int nativeMods) {
    const int shiftMask = static_cast<int>(input::GlfwMod::Shift);
    const int controlMask = static_cast<int>(input::GlfwMod::Control);
    const int altMask = static_cast<int>(input::GlfwMod::Alt);
    const int superMask = static_cast<int>(input::GlfwMod::Super);

    io.AddKeyEvent(ImGuiMod_Ctrl, (nativeMods & controlMask) != 0);
    io.AddKeyEvent(ImGuiMod_Shift, (nativeMods & shiftMask) != 0);
    io.AddKeyEvent(ImGuiMod_Alt, (nativeMods & altMask) != 0);
    io.AddKeyEvent(ImGuiMod_Super, (nativeMods & superMask) != 0);
}

ImGuiKey VkToImGuiKey(input::VkCode vk) {
    if (vk >= input::VK_A && vk <= input::VK_Z) { return static_cast<ImGuiKey>(ImGuiKey_A + (vk - input::VK_A)); }
    if (vk >= input::VK_0 && vk <= input::VK_9) { return static_cast<ImGuiKey>(ImGuiKey_0 + (vk - input::VK_0)); }
    if (vk >= input::VK_F1 && vk <= input::VK_F24) { return static_cast<ImGuiKey>(ImGuiKey_F1 + (vk - input::VK_F1)); }

    switch (vk) {
    case input::VK_TAB:
        return ImGuiKey_Tab;
    case input::VK_LEFT:
        return ImGuiKey_LeftArrow;
    case input::VK_RIGHT:
        return ImGuiKey_RightArrow;
    case input::VK_UP:
        return ImGuiKey_UpArrow;
    case input::VK_DOWN:
        return ImGuiKey_DownArrow;
    case input::VK_PRIOR:
        return ImGuiKey_PageUp;
    case input::VK_NEXT:
        return ImGuiKey_PageDown;
    case input::VK_HOME:
        return ImGuiKey_Home;
    case input::VK_END:
        return ImGuiKey_End;
    case input::VK_INSERT:
        return ImGuiKey_Insert;
    case input::VK_DELETE:
        return ImGuiKey_Delete;
    case input::VK_BACK:
        return ImGuiKey_Backspace;
    case input::VK_SPACE:
        return ImGuiKey_Space;
    case input::VK_RETURN:
        return ImGuiKey_Enter;
    case input::VK_ESCAPE:
        return ImGuiKey_Escape;
    case input::VK_OEM_7:
        return ImGuiKey_Apostrophe;
    case input::VK_OEM_COMMA:
        return ImGuiKey_Comma;
    case input::VK_OEM_MINUS:
        return ImGuiKey_Minus;
    case input::VK_OEM_PERIOD:
        return ImGuiKey_Period;
    case input::VK_OEM_2:
        return ImGuiKey_Slash;
    case input::VK_OEM_1:
        return ImGuiKey_Semicolon;
    case input::VK_OEM_PLUS:
        return ImGuiKey_Equal;
    case input::VK_OEM_4:
        return ImGuiKey_LeftBracket;
    case input::VK_OEM_5:
        return ImGuiKey_Backslash;
    case input::VK_OEM_6:
        return ImGuiKey_RightBracket;
    case input::VK_OEM_3:
        return ImGuiKey_GraveAccent;
    case input::VK_CAPITAL:
        return ImGuiKey_CapsLock;
    case input::VK_SCROLL:
        return ImGuiKey_ScrollLock;
    case input::VK_NUMLOCK:
        return ImGuiKey_NumLock;
    case input::VK_SNAPSHOT:
        return ImGuiKey_PrintScreen;
    case input::VK_PAUSE:
        return ImGuiKey_Pause;
    case input::VK_NUMPAD0:
        return ImGuiKey_Keypad0;
    case input::VK_NUMPAD1:
        return ImGuiKey_Keypad1;
    case input::VK_NUMPAD2:
        return ImGuiKey_Keypad2;
    case input::VK_NUMPAD3:
        return ImGuiKey_Keypad3;
    case input::VK_NUMPAD4:
        return ImGuiKey_Keypad4;
    case input::VK_NUMPAD5:
        return ImGuiKey_Keypad5;
    case input::VK_NUMPAD6:
        return ImGuiKey_Keypad6;
    case input::VK_NUMPAD7:
        return ImGuiKey_Keypad7;
    case input::VK_NUMPAD8:
        return ImGuiKey_Keypad8;
    case input::VK_NUMPAD9:
        return ImGuiKey_Keypad9;
    case input::VK_DECIMAL:
        return ImGuiKey_KeypadDecimal;
    case input::VK_DIVIDE:
        return ImGuiKey_KeypadDivide;
    case input::VK_MULTIPLY:
        return ImGuiKey_KeypadMultiply;
    case input::VK_SUBTRACT:
        return ImGuiKey_KeypadSubtract;
    case input::VK_ADD:
        return ImGuiKey_KeypadAdd;
    case input::VK_LSHIFT:
        return ImGuiKey_LeftShift;
    case input::VK_RSHIFT:
        return ImGuiKey_RightShift;
    case input::VK_LCONTROL:
        return ImGuiKey_LeftCtrl;
    case input::VK_RCONTROL:
        return ImGuiKey_RightCtrl;
    case input::VK_LMENU:
        return ImGuiKey_LeftAlt;
    case input::VK_RMENU:
        return ImGuiKey_RightAlt;
    case input::VK_LWIN:
        return ImGuiKey_LeftSuper;
    case input::VK_RWIN:
        return ImGuiKey_RightSuper;
    default:
        break;
    }

    return ImGuiKey_None;
}

int VkMouseButtonToImGuiButton(input::VkCode vk) {
    switch (vk) {
    case input::VK_LBUTTON:
        return 0;
    case input::VK_RBUTTON:
        return 1;
    case input::VK_MBUTTON:
        return 2;
    case input::VK_XBUTTON1:
        return 3;
    case input::VK_XBUTTON2:
        return 4;
    default:
        break;
    }

    return -1;
}

bool InputActionToDown(input::InputAction action, bool& down) {
    switch (action) {
    case input::InputAction::Press:
    case input::InputAction::Repeat:
        down = true;
        return true;
    case input::InputAction::Release:
        down = false;
        return true;
    default:
        break;
    }

    return false;
}

bool ApplyInputEventToImGui(ImGuiIO& io, const input::InputEvent& event) {
    switch (event.type) {
    case input::InputEventType::Key: {
        ApplyGlfwModifierState(io, event.nativeMods);

        const ImGuiKey key = VkToImGuiKey(event.vk);
        if (key == ImGuiKey_None) { return false; }

        bool down = false;
        if (!InputActionToDown(event.action, down)) { return false; }

        io.AddKeyEvent(key, down);
        io.SetKeyEventNativeData(key, event.nativeKey, event.nativeScanCode);
        return true;
    }
    case input::InputEventType::Character:
        if (event.charCodepoint == 0) { return false; }
        io.AddInputCharacter(event.charCodepoint);
        return true;
    case input::InputEventType::MouseButton: {
        ApplyGlfwModifierState(io, event.nativeMods);

        const int button = VkMouseButtonToImGuiButton(event.vk);
        if (button < 0) { return false; }

        bool down = false;
        if (!InputActionToDown(event.action, down)) { return false; }

        io.AddMouseButtonEvent(button, down);
        return true;
    }
    case input::InputEventType::Scroll:
        if (event.scrollX == 0.0 && event.scrollY == 0.0) { return false; }
        io.AddMouseWheelEvent(static_cast<float>(event.scrollX), static_cast<float>(event.scrollY));
        return true;
    case input::InputEventType::CursorPosition:
        io.AddMousePosEvent(static_cast<float>(event.x), static_cast<float>(event.y));
        return true;
    case input::InputEventType::Focus:
        io.AddFocusEvent(event.focused);
        return true;
    case input::InputEventType::Unknown:
        break;
    }

    return false;
}

} // namespace

bool HasImGuiInputBridgeSupport() { return true; }

ImGuiInputDrainResult DrainImGuiInputEventsToCurrentContext(std::size_t maxEvents) {
    ImGuiInputDrainResult result;
    if (maxEvents == 0) { return result; }

    std::vector<input::InputEvent> events;
    events.reserve(maxEvents);
    result.drained = DrainImGuiInputEvents(events, maxEvents);

    result.hasImGuiSupport = true;
    if (!ImGui::GetCurrentContext()) { return result; }

    result.hadCurrentContext = true;
    ImGuiIO& io = ImGui::GetIO();
    for (const input::InputEvent& event : events) {
        if (ApplyInputEventToImGui(io, event)) { ++result.applied; }
    }

    return result;
}

} // namespace platform::x11
