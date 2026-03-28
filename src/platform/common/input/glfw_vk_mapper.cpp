#include "glfw_vk_mapper.h"

namespace platform::input {

namespace {

constexpr int GLFW_MOUSE_BUTTON_LEFT = 0;
constexpr int GLFW_MOUSE_BUTTON_RIGHT = 1;
constexpr int GLFW_MOUSE_BUTTON_MIDDLE = 2;
constexpr int GLFW_MOUSE_BUTTON_4 = 3;
constexpr int GLFW_MOUSE_BUTTON_5 = 4;

} // namespace

VkCode GlfwKeyToVk(int key, int scancode, int mods) {
    (void)scancode;
    (void)mods;
    return key > 0 ? static_cast<VkCode>(key) : VK_NONE;
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
    if (vk == VK_NONE || IsMouseVk(vk) || vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU || vk == VK_SEPARATOR) {
        return -1;
    }
    return static_cast<int>(vk);
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

bool IsKeyboardVk(VkCode vk) {
    if (vk == VK_NONE || IsMouseVk(vk) || vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU || vk == VK_SEPARATOR) {
        return false;
    }
    return VkToGlfwKey(vk) >= static_cast<int>(VK_SPACE);
}

bool IsNonTextVk(VkCode vk) {
    if (IsMouseVk(vk)) {
        return true;
    }

    if (vk >= VK_F1 && vk <= VK_F24) {
        return true;
    }

    switch (vk) {
    case VK_BACK:
    case VK_TAB:
    case VK_RETURN:
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

bool IsModifierScanCode(int nativeScanCode) {
    switch (nativeScanCode) {
    case 37:
    case 50:
    case 62:
    case 64:
    case 105:
    case 108:
    case 133:
    case 134:
        return true;
    default:
        break;
    }
    return false;
}

bool IsModifierGlfwKey(int key) {
    switch (key) {
    case static_cast<int>(VK_LSHIFT):
    case static_cast<int>(VK_RSHIFT):
    case static_cast<int>(VK_LCONTROL):
    case static_cast<int>(VK_RCONTROL):
    case static_cast<int>(VK_LMENU):
    case static_cast<int>(VK_RMENU):
    case static_cast<int>(VK_LWIN):
    case static_cast<int>(VK_RWIN):
        return true;
    default:
        break;
    }
    return false;
}

bool IsShiftGlfwKey(int key) {
    return key == static_cast<int>(VK_LSHIFT) || key == static_cast<int>(VK_RSHIFT);
}

} // namespace platform::input
