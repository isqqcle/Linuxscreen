#pragma once

#include "input_event.h"

namespace platform::input {

enum class GlfwAction : int {
    Release = 0,
    Press = 1,
    Repeat = 2,
};

enum class GlfwMod : int {
    Shift = 0x0001,
    Control = 0x0002,
    Alt = 0x0004,
    Super = 0x0008,
};

VkCode GlfwKeyToVk(int key, int scancode, int mods);
VkCode GlfwMouseButtonToVk(int button);
int VkToGlfwKey(uint32_t vk);
int VkToGlfwMouseButton(uint32_t vk);
InputAction GlfwActionToInputAction(int action);

bool IsKeyboardVk(VkCode vk);
bool IsNonTextVk(VkCode vk);
bool IsModifierScanCode(int nativeScanCode);
bool IsModifierGlfwKey(int key);
bool IsShiftGlfwKey(int key);

} // namespace platform::input
