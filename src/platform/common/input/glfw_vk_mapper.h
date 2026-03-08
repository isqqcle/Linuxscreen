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

bool IsMouseVk(VkCode vk);
bool IsKeyboardVk(VkCode vk);
bool IsNonTextVk(VkCode vk);
VkCode NormalizeModifierVkFromConfig(VkCode vk, int nativeScanCode = 0);
bool MatchesRebindSourceVk(VkCode incomingVk, VkCode fromKey);
bool TryMapVkToCodepoint(VkCode vk, bool shiftDown, std::uint32_t& outCodepoint);

} // namespace platform::input
