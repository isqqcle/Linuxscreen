#include "glfw_vk_mapper.h"

namespace platform::input {

namespace {

constexpr int GLFW_KEY_SPACE = 32;
constexpr int GLFW_KEY_APOSTROPHE = 39;
constexpr int GLFW_KEY_COMMA = 44;
constexpr int GLFW_KEY_MINUS = 45;
constexpr int GLFW_KEY_PERIOD = 46;
constexpr int GLFW_KEY_SLASH = 47;
constexpr int GLFW_KEY_0 = 48;
constexpr int GLFW_KEY_9 = 57;
constexpr int GLFW_KEY_SEMICOLON = 59;
constexpr int GLFW_KEY_EQUAL = 61;
constexpr int GLFW_KEY_A = 65;
constexpr int GLFW_KEY_Z = 90;
constexpr int GLFW_KEY_LEFT_BRACKET = 91;
constexpr int GLFW_KEY_BACKSLASH = 92;
constexpr int GLFW_KEY_RIGHT_BRACKET = 93;
constexpr int GLFW_KEY_GRAVE_ACCENT = 96;

constexpr int GLFW_KEY_ESCAPE = 256;
constexpr int GLFW_KEY_ENTER = 257;
constexpr int GLFW_KEY_TAB = 258;
constexpr int GLFW_KEY_BACKSPACE = 259;
constexpr int GLFW_KEY_INSERT = 260;
constexpr int GLFW_KEY_DELETE = 261;
constexpr int GLFW_KEY_RIGHT = 262;
constexpr int GLFW_KEY_LEFT = 263;
constexpr int GLFW_KEY_DOWN = 264;
constexpr int GLFW_KEY_UP = 265;
constexpr int GLFW_KEY_PAGE_UP = 266;
constexpr int GLFW_KEY_PAGE_DOWN = 267;
constexpr int GLFW_KEY_HOME = 268;
constexpr int GLFW_KEY_END = 269;

constexpr int GLFW_KEY_CAPS_LOCK = 280;
constexpr int GLFW_KEY_SCROLL_LOCK = 281;
constexpr int GLFW_KEY_NUM_LOCK = 282;
constexpr int GLFW_KEY_PRINT_SCREEN = 283;
constexpr int GLFW_KEY_PAUSE = 284;
constexpr int GLFW_KEY_F1 = 290;
constexpr int GLFW_KEY_F24 = 313;

constexpr int GLFW_KEY_KP_0 = 320;
constexpr int GLFW_KEY_KP_1 = 321;
constexpr int GLFW_KEY_KP_2 = 322;
constexpr int GLFW_KEY_KP_3 = 323;
constexpr int GLFW_KEY_KP_4 = 324;
constexpr int GLFW_KEY_KP_5 = 325;
constexpr int GLFW_KEY_KP_6 = 326;
constexpr int GLFW_KEY_KP_7 = 327;
constexpr int GLFW_KEY_KP_8 = 328;
constexpr int GLFW_KEY_KP_9 = 329;
constexpr int GLFW_KEY_KP_DECIMAL = 330;
constexpr int GLFW_KEY_KP_DIVIDE = 331;
constexpr int GLFW_KEY_KP_MULTIPLY = 332;
constexpr int GLFW_KEY_KP_SUBTRACT = 333;
constexpr int GLFW_KEY_KP_ADD = 334;
constexpr int GLFW_KEY_KP_ENTER = 335;
constexpr int GLFW_KEY_KP_EQUAL = 336;

constexpr int GLFW_KEY_LEFT_SHIFT = 340;
constexpr int GLFW_KEY_LEFT_CONTROL = 341;
constexpr int GLFW_KEY_LEFT_ALT = 342;
constexpr int GLFW_KEY_LEFT_SUPER = 343;
constexpr int GLFW_KEY_RIGHT_SHIFT = 344;
constexpr int GLFW_KEY_RIGHT_CONTROL = 345;
constexpr int GLFW_KEY_RIGHT_ALT = 346;
constexpr int GLFW_KEY_RIGHT_SUPER = 347;
constexpr int GLFW_KEY_MENU = 348;

constexpr int GLFW_MOUSE_BUTTON_LEFT = 0;
constexpr int GLFW_MOUSE_BUTTON_RIGHT = 1;
constexpr int GLFW_MOUSE_BUTTON_MIDDLE = 2;
constexpr int GLFW_MOUSE_BUTTON_4 = 3;
constexpr int GLFW_MOUSE_BUTTON_5 = 4;

} // namespace

VkCode GlfwKeyToVk(int key, int scancode, int mods) {
    (void)scancode;
    (void)mods;

    if (key >= GLFW_KEY_A && key <= GLFW_KEY_Z) { return static_cast<VkCode>(key); }
    if (key >= GLFW_KEY_0 && key <= GLFW_KEY_9) { return static_cast<VkCode>(key); }

    if (key >= GLFW_KEY_F1 && key <= GLFW_KEY_F24) {
        const int offset = key - GLFW_KEY_F1;
        return VK_F1 + static_cast<VkCode>(offset);
    }

    switch (key) {
    case GLFW_KEY_SPACE:
        return VK_SPACE;
    case GLFW_KEY_APOSTROPHE:
        return VK_OEM_7;
    case GLFW_KEY_COMMA:
        return VK_OEM_COMMA;
    case GLFW_KEY_MINUS:
        return VK_OEM_MINUS;
    case GLFW_KEY_PERIOD:
        return VK_OEM_PERIOD;
    case GLFW_KEY_SLASH:
        return VK_OEM_2;
    case GLFW_KEY_SEMICOLON:
        return VK_OEM_1;
    case GLFW_KEY_EQUAL:
        return VK_OEM_PLUS;
    case GLFW_KEY_LEFT_BRACKET:
        return VK_OEM_4;
    case GLFW_KEY_BACKSLASH:
        return VK_OEM_5;
    case GLFW_KEY_RIGHT_BRACKET:
        return VK_OEM_6;
    case GLFW_KEY_GRAVE_ACCENT:
        return VK_OEM_3;
    case GLFW_KEY_ESCAPE:
        return VK_ESCAPE;
    case GLFW_KEY_ENTER:
        return VK_RETURN;
    case GLFW_KEY_TAB:
        return VK_TAB;
    case GLFW_KEY_BACKSPACE:
        return VK_BACK;
    case GLFW_KEY_INSERT:
        return VK_INSERT;
    case GLFW_KEY_DELETE:
        return VK_DELETE;
    case GLFW_KEY_RIGHT:
        return VK_RIGHT;
    case GLFW_KEY_LEFT:
        return VK_LEFT;
    case GLFW_KEY_DOWN:
        return VK_DOWN;
    case GLFW_KEY_UP:
        return VK_UP;
    case GLFW_KEY_PAGE_UP:
        return VK_PRIOR;
    case GLFW_KEY_PAGE_DOWN:
        return VK_NEXT;
    case GLFW_KEY_HOME:
        return VK_HOME;
    case GLFW_KEY_END:
        return VK_END;
    case GLFW_KEY_CAPS_LOCK:
        return VK_CAPITAL;
    case GLFW_KEY_SCROLL_LOCK:
        return VK_SCROLL;
    case GLFW_KEY_NUM_LOCK:
        return VK_NUMLOCK;
    case GLFW_KEY_PRINT_SCREEN:
        return VK_SNAPSHOT;
    case GLFW_KEY_PAUSE:
        return VK_PAUSE;
    case GLFW_KEY_KP_0:
        return VK_NUMPAD0;
    case GLFW_KEY_KP_1:
        return VK_NUMPAD1;
    case GLFW_KEY_KP_2:
        return VK_NUMPAD2;
    case GLFW_KEY_KP_3:
        return VK_NUMPAD3;
    case GLFW_KEY_KP_4:
        return VK_NUMPAD4;
    case GLFW_KEY_KP_5:
        return VK_NUMPAD5;
    case GLFW_KEY_KP_6:
        return VK_NUMPAD6;
    case GLFW_KEY_KP_7:
        return VK_NUMPAD7;
    case GLFW_KEY_KP_8:
        return VK_NUMPAD8;
    case GLFW_KEY_KP_9:
        return VK_NUMPAD9;
    case GLFW_KEY_KP_DECIMAL:
        return VK_DECIMAL;
    case GLFW_KEY_KP_DIVIDE:
        return VK_DIVIDE;
    case GLFW_KEY_KP_MULTIPLY:
        return VK_MULTIPLY;
    case GLFW_KEY_KP_SUBTRACT:
        return VK_SUBTRACT;
    case GLFW_KEY_KP_ADD:
        return VK_ADD;
    case GLFW_KEY_KP_ENTER:
        return VK_RETURN;
    case GLFW_KEY_KP_EQUAL:
        return VK_OEM_PLUS;
    case GLFW_KEY_LEFT_SHIFT:
        return VK_LSHIFT;
    case GLFW_KEY_LEFT_CONTROL:
        return VK_LCONTROL;
    case GLFW_KEY_LEFT_ALT:
        return VK_LMENU;
    case GLFW_KEY_LEFT_SUPER:
        return VK_LWIN;
    case GLFW_KEY_RIGHT_SHIFT:
        return VK_RSHIFT;
    case GLFW_KEY_RIGHT_CONTROL:
        return VK_RCONTROL;
    case GLFW_KEY_RIGHT_ALT:
        return VK_RMENU;
    case GLFW_KEY_RIGHT_SUPER:
        return VK_RWIN;
    case GLFW_KEY_MENU:
        return VK_APPS;
    default:
        break;
    }

    return VK_NONE;
}

VkCode GlfwMouseButtonToVk(int button) {
    switch (button) {
    case GLFW_MOUSE_BUTTON_LEFT:
        return VK_LBUTTON;
    case GLFW_MOUSE_BUTTON_RIGHT:
        return VK_RBUTTON;
    case GLFW_MOUSE_BUTTON_MIDDLE:
        return VK_MBUTTON;
    case GLFW_MOUSE_BUTTON_4:
        return VK_XBUTTON1;
    case GLFW_MOUSE_BUTTON_5:
        return VK_XBUTTON2;
    default:
        break;
    }
    return VK_NONE;
}

int VkToGlfwKey(uint32_t vk) {
    if (vk >= VK_A && vk <= VK_Z) { return static_cast<int>(vk); }
    if (vk >= VK_0 && vk <= VK_9) { return static_cast<int>(vk); }

    if (vk >= VK_F1 && vk <= VK_F24) {
        const uint32_t offset = vk - VK_F1;
        return GLFW_KEY_F1 + static_cast<int>(offset);
    }

    switch (vk) {
    case VK_SPACE:
        return GLFW_KEY_SPACE;
    case VK_OEM_7:
        return GLFW_KEY_APOSTROPHE;
    case VK_OEM_COMMA:
        return GLFW_KEY_COMMA;
    case VK_OEM_MINUS:
        return GLFW_KEY_MINUS;
    case VK_OEM_PERIOD:
        return GLFW_KEY_PERIOD;
    case VK_OEM_2:
        return GLFW_KEY_SLASH;
    case VK_OEM_1:
        return GLFW_KEY_SEMICOLON;
    case VK_OEM_PLUS:
        return GLFW_KEY_EQUAL;
    case VK_OEM_4:
        return GLFW_KEY_LEFT_BRACKET;
    case VK_OEM_5:
        return GLFW_KEY_BACKSLASH;
    case VK_OEM_6:
        return GLFW_KEY_RIGHT_BRACKET;
    case VK_OEM_3:
        return GLFW_KEY_GRAVE_ACCENT;
    case VK_ESCAPE:
        return GLFW_KEY_ESCAPE;
    case VK_RETURN:
        return GLFW_KEY_ENTER;
    case VK_TAB:
        return GLFW_KEY_TAB;
    case VK_BACK:
        return GLFW_KEY_BACKSPACE;
    case VK_INSERT:
        return GLFW_KEY_INSERT;
    case VK_DELETE:
        return GLFW_KEY_DELETE;
    case VK_RIGHT:
        return GLFW_KEY_RIGHT;
    case VK_LEFT:
        return GLFW_KEY_LEFT;
    case VK_DOWN:
        return GLFW_KEY_DOWN;
    case VK_UP:
        return GLFW_KEY_UP;
    case VK_PRIOR:
        return GLFW_KEY_PAGE_UP;
    case VK_NEXT:
        return GLFW_KEY_PAGE_DOWN;
    case VK_HOME:
        return GLFW_KEY_HOME;
    case VK_END:
        return GLFW_KEY_END;
    case VK_CAPITAL:
        return GLFW_KEY_CAPS_LOCK;
    case VK_SCROLL:
        return GLFW_KEY_SCROLL_LOCK;
    case VK_NUMLOCK:
        return GLFW_KEY_NUM_LOCK;
    case VK_SNAPSHOT:
        return GLFW_KEY_PRINT_SCREEN;
    case VK_PAUSE:
        return GLFW_KEY_PAUSE;
    case VK_NUMPAD0:
        return GLFW_KEY_KP_0;
    case VK_NUMPAD1:
        return GLFW_KEY_KP_1;
    case VK_NUMPAD2:
        return GLFW_KEY_KP_2;
    case VK_NUMPAD3:
        return GLFW_KEY_KP_3;
    case VK_NUMPAD4:
        return GLFW_KEY_KP_4;
    case VK_NUMPAD5:
        return GLFW_KEY_KP_5;
    case VK_NUMPAD6:
        return GLFW_KEY_KP_6;
    case VK_NUMPAD7:
        return GLFW_KEY_KP_7;
    case VK_NUMPAD8:
        return GLFW_KEY_KP_8;
    case VK_NUMPAD9:
        return GLFW_KEY_KP_9;
    case VK_DECIMAL:
        return GLFW_KEY_KP_DECIMAL;
    case VK_DIVIDE:
        return GLFW_KEY_KP_DIVIDE;
    case VK_MULTIPLY:
        return GLFW_KEY_KP_MULTIPLY;
    case VK_SUBTRACT:
        return GLFW_KEY_KP_SUBTRACT;
    case VK_ADD:
        return GLFW_KEY_KP_ADD;
    case VK_LSHIFT:
    case VK_SHIFT:
        return GLFW_KEY_LEFT_SHIFT;
    case VK_RSHIFT:
        return GLFW_KEY_RIGHT_SHIFT;
    case VK_LCONTROL:
    case VK_CONTROL:
        return GLFW_KEY_LEFT_CONTROL;
    case VK_RCONTROL:
        return GLFW_KEY_RIGHT_CONTROL;
    case VK_LMENU:
    case VK_MENU:
        return GLFW_KEY_LEFT_ALT;
    case VK_RMENU:
        return GLFW_KEY_RIGHT_ALT;
    case VK_LWIN:
        return GLFW_KEY_LEFT_SUPER;
    case VK_RWIN:
        return GLFW_KEY_RIGHT_SUPER;
    case VK_APPS:
        return GLFW_KEY_MENU;
    default:
        break;
    }

    return -1;
}

int VkToGlfwMouseButton(uint32_t vk) {
    switch (vk) {
    case VK_LBUTTON:
        return GLFW_MOUSE_BUTTON_LEFT;
    case VK_RBUTTON:
        return GLFW_MOUSE_BUTTON_RIGHT;
    case VK_MBUTTON:
        return GLFW_MOUSE_BUTTON_MIDDLE;
    case VK_XBUTTON1:
        return GLFW_MOUSE_BUTTON_4;
    case VK_XBUTTON2:
        return GLFW_MOUSE_BUTTON_5;
    default:
        break;
    }
    return -1;
}

InputAction GlfwActionToInputAction(int action) {
    switch (action) {
    case static_cast<int>(GlfwAction::Press):
        return InputAction::Press;
    case static_cast<int>(GlfwAction::Release):
        return InputAction::Release;
    case static_cast<int>(GlfwAction::Repeat):
        return InputAction::Repeat;
    default:
        break;
    }
    return InputAction::Unknown;
}

bool IsMouseVk(VkCode vk) {
    switch (vk) {
    case VK_LBUTTON:
    case VK_RBUTTON:
    case VK_MBUTTON:
    case VK_XBUTTON1:
    case VK_XBUTTON2:
        return true;
    default:
        break;
    }
    return false;
}

bool IsKeyboardVk(VkCode vk) {
    if (IsMouseVk(vk) || vk == VK_NONE) {
        return false;
    }
    return VkToGlfwKey(vk) >= 0;
}

bool IsNonTextVk(VkCode vk) {
    if (IsMouseVk(vk)) {
        return true;
    }

    if (vk >= VK_F1 && vk <= VK_F24) {
        return true;
    }

    switch (vk) {
    case VK_SHIFT:
    case VK_LSHIFT:
    case VK_RSHIFT:
    case VK_CONTROL:
    case VK_LCONTROL:
    case VK_RCONTROL:
    case VK_MENU:
    case VK_LMENU:
    case VK_RMENU:
    case VK_LWIN:
    case VK_RWIN:
    case VK_APPS:
    case VK_ESCAPE:
    case VK_CAPITAL:
    case VK_NUMLOCK:
    case VK_SCROLL:
    case VK_PAUSE:
    case VK_SNAPSHOT:
    case VK_INSERT:
    case VK_DELETE:
    case VK_HOME:
    case VK_END:
    case VK_PRIOR:
    case VK_NEXT:
    case VK_LEFT:
    case VK_RIGHT:
    case VK_UP:
    case VK_DOWN:
        return true;
    default:
        break;
    }

    return false;
}

VkCode NormalizeModifierVkFromConfig(VkCode vk, int nativeScanCode) {
    const int scanLow = nativeScanCode & 0xFF;
    const bool hasExtended = (nativeScanCode & 0xFF00) != 0 || nativeScanCode == 105 || nativeScanCode == 108 || nativeScanCode == 285 ||
                             nativeScanCode == 312;

    switch (vk) {
    case VK_SHIFT:
        // GLFW/X11 commonly reports right shift as 62 (or raw low-byte 0x36 in Windows-style encoding).
        if (nativeScanCode == 62 || scanLow == 0x36) {
            return VK_RSHIFT;
        }
        return VK_LSHIFT;
    case VK_CONTROL:
        return hasExtended ? VK_RCONTROL : VK_LCONTROL;
    case VK_MENU:
        return hasExtended ? VK_RMENU : VK_LMENU;
    default:
        break;
    }

    return vk;
}

bool MatchesRebindSourceVk(VkCode incomingVk, VkCode fromKey) {
    if (incomingVk == VK_NONE || fromKey == VK_NONE) {
        return false;
    }
    if (incomingVk == fromKey) {
        return true;
    }

    if (fromKey == VK_CONTROL && (incomingVk == VK_LCONTROL || incomingVk == VK_RCONTROL)) {
        return true;
    }
    if (fromKey == VK_SHIFT && (incomingVk == VK_LSHIFT || incomingVk == VK_RSHIFT)) {
        return true;
    }
    if (fromKey == VK_MENU && (incomingVk == VK_LMENU || incomingVk == VK_RMENU)) {
        return true;
    }

    if (incomingVk == VK_CONTROL && (fromKey == VK_LCONTROL || fromKey == VK_RCONTROL)) {
        return true;
    }
    if (incomingVk == VK_SHIFT && (fromKey == VK_LSHIFT || fromKey == VK_RSHIFT)) {
        return true;
    }
    if (incomingVk == VK_MENU && (fromKey == VK_LMENU || fromKey == VK_RMENU)) {
        return true;
    }

    return false;
}

bool TryMapVkToCodepoint(VkCode vk, bool shiftDown, std::uint32_t& outCodepoint) {
    outCodepoint = 0;

    if (vk >= VK_A && vk <= VK_Z) {
        const std::uint32_t base = shiftDown ? static_cast<std::uint32_t>('A') : static_cast<std::uint32_t>('a');
        outCodepoint = base + static_cast<std::uint32_t>(vk - VK_A);
        return true;
    }

    if (vk >= VK_0 && vk <= VK_9) {
        static constexpr char kShiftedDigits[] = { ')', '!', '@', '#', '$', '%', '^', '&', '*', '(' };
        const std::uint32_t idx = static_cast<std::uint32_t>(vk - VK_0);
        outCodepoint = shiftDown ? static_cast<std::uint32_t>(kShiftedDigits[idx]) : static_cast<std::uint32_t>('0' + idx);
        return true;
    }

    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) {
        outCodepoint = static_cast<std::uint32_t>('0' + (vk - VK_NUMPAD0));
        return true;
    }

    switch (vk) {
    case VK_SPACE:
        outCodepoint = static_cast<std::uint32_t>(' ');
        return true;
    case VK_RETURN:
        outCodepoint = static_cast<std::uint32_t>('\r');
        return true;
    case VK_TAB:
        outCodepoint = static_cast<std::uint32_t>('\t');
        return true;
    case VK_BACK:
        outCodepoint = static_cast<std::uint32_t>('\b');
        return true;
    case VK_OEM_MINUS:
        outCodepoint = static_cast<std::uint32_t>(shiftDown ? '_' : '-');
        return true;
    case VK_OEM_PLUS:
        outCodepoint = static_cast<std::uint32_t>(shiftDown ? '+' : '=');
        return true;
    case VK_OEM_1:
        outCodepoint = static_cast<std::uint32_t>(shiftDown ? ':' : ';');
        return true;
    case VK_OEM_2:
        outCodepoint = static_cast<std::uint32_t>(shiftDown ? '?' : '/');
        return true;
    case VK_OEM_3:
        outCodepoint = static_cast<std::uint32_t>(shiftDown ? '~' : '`');
        return true;
    case VK_OEM_4:
        outCodepoint = static_cast<std::uint32_t>(shiftDown ? '{' : '[');
        return true;
    case VK_OEM_5:
        outCodepoint = static_cast<std::uint32_t>(shiftDown ? '|' : '\\');
        return true;
    case VK_OEM_6:
        outCodepoint = static_cast<std::uint32_t>(shiftDown ? '}' : ']');
        return true;
    case VK_OEM_7:
        outCodepoint = static_cast<std::uint32_t>(shiftDown ? '"' : '\'');
        return true;
    case VK_OEM_COMMA:
        outCodepoint = static_cast<std::uint32_t>(shiftDown ? '<' : ',');
        return true;
    case VK_OEM_PERIOD:
        outCodepoint = static_cast<std::uint32_t>(shiftDown ? '>' : '.');
        return true;
    case VK_DECIMAL:
        outCodepoint = static_cast<std::uint32_t>('.');
        return true;
    case VK_DIVIDE:
        outCodepoint = static_cast<std::uint32_t>('/');
        return true;
    case VK_MULTIPLY:
        outCodepoint = static_cast<std::uint32_t>('*');
        return true;
    case VK_SUBTRACT:
        outCodepoint = static_cast<std::uint32_t>('-');
        return true;
    case VK_ADD:
        outCodepoint = static_cast<std::uint32_t>('+');
        return true;
    default:
        break;
    }

    return false;
}

} // namespace platform::input
