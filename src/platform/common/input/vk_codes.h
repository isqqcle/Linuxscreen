#pragma once

#include <cstdint>

namespace platform::input {

using VkCode = std::uint32_t;

constexpr VkCode VK_NONE = 0;

// Mouse buttons are encoded outside the native GLFW key range so keyboard and mouse
// can still share a lightweight transport type without a translation table.
constexpr VkCode kMouseVkBase = 0x10000u;
constexpr VkCode VK_LBUTTON = kMouseVkBase + 0;
constexpr VkCode VK_RBUTTON = kMouseVkBase + 1;
constexpr VkCode VK_MBUTTON = kMouseVkBase + 2;
constexpr VkCode VK_XBUTTON1 = kMouseVkBase + 3;
constexpr VkCode VK_XBUTTON2 = kMouseVkBase + 4;

// Generic modifiers are synthetic aliases used only for UI/config compatibility.
constexpr VkCode VK_SHIFT = 0x20000u;
constexpr VkCode VK_CONTROL = 0x20001u;
constexpr VkCode VK_MENU = 0x20002u;
constexpr VkCode VK_SEPARATOR = 0x20003u;

// GLFW key codes
constexpr VkCode VK_SPACE = 32;
constexpr VkCode VK_OEM_7 = 39;
constexpr VkCode VK_OEM_COMMA = 44;
constexpr VkCode VK_OEM_MINUS = 45;
constexpr VkCode VK_OEM_PERIOD = 46;
constexpr VkCode VK_OEM_2 = 47;
constexpr VkCode VK_0 = 48;
constexpr VkCode VK_9 = 57;
constexpr VkCode VK_OEM_1 = 59;
constexpr VkCode VK_OEM_PLUS = 61;
constexpr VkCode VK_A = 65;
constexpr VkCode VK_Z = 90;
constexpr VkCode VK_OEM_4 = 91;
constexpr VkCode VK_OEM_5 = 92;
constexpr VkCode VK_OEM_6 = 93;
constexpr VkCode VK_OEM_3 = 96;
constexpr VkCode VK_OEM_102 = 161;

constexpr VkCode VK_ESCAPE = 256;
constexpr VkCode VK_RETURN = 257;
constexpr VkCode VK_TAB = 258;
constexpr VkCode VK_BACK = 259;
constexpr VkCode VK_INSERT = 260;
constexpr VkCode VK_DELETE = 261;
constexpr VkCode VK_RIGHT = 262;
constexpr VkCode VK_LEFT = 263;
constexpr VkCode VK_DOWN = 264;
constexpr VkCode VK_UP = 265;
constexpr VkCode VK_PRIOR = 266;
constexpr VkCode VK_NEXT = 267;
constexpr VkCode VK_HOME = 268;
constexpr VkCode VK_END = 269;
constexpr VkCode VK_CAPITAL = 280;
constexpr VkCode VK_SCROLL = 281;
constexpr VkCode VK_NUMLOCK = 282;
constexpr VkCode VK_SNAPSHOT = 283;
constexpr VkCode VK_PAUSE = 284;
constexpr VkCode VK_F1 = 290;
constexpr VkCode VK_F24 = 313;
constexpr VkCode VK_NUMPAD0 = 320;
constexpr VkCode VK_NUMPAD1 = 321;
constexpr VkCode VK_NUMPAD2 = 322;
constexpr VkCode VK_NUMPAD3 = 323;
constexpr VkCode VK_NUMPAD4 = 324;
constexpr VkCode VK_NUMPAD5 = 325;
constexpr VkCode VK_NUMPAD6 = 326;
constexpr VkCode VK_NUMPAD7 = 327;
constexpr VkCode VK_NUMPAD8 = 328;
constexpr VkCode VK_NUMPAD9 = 329;
constexpr VkCode VK_DECIMAL = 330;
constexpr VkCode VK_DIVIDE = 331;
constexpr VkCode VK_MULTIPLY = 332;
constexpr VkCode VK_SUBTRACT = 333;
constexpr VkCode VK_ADD = 334;
constexpr VkCode VK_LSHIFT = 340;
constexpr VkCode VK_LCONTROL = 341;
constexpr VkCode VK_LMENU = 342;
constexpr VkCode VK_LWIN = 343;
constexpr VkCode VK_RSHIFT = 344;
constexpr VkCode VK_RCONTROL = 345;
constexpr VkCode VK_RMENU = 346;
constexpr VkCode VK_RWIN = 347;
constexpr VkCode VK_APPS = 348;

inline bool IsMouseVk(VkCode vk) { return vk >= kMouseVkBase && vk < (kMouseVkBase + 32u); }

inline bool IsControlVariant(VkCode vk) { return vk == VK_CONTROL || vk == VK_LCONTROL || vk == VK_RCONTROL; }

inline bool IsShiftVariant(VkCode vk) { return vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT; }

inline bool IsAltVariant(VkCode vk) { return vk == VK_MENU || vk == VK_LMENU || vk == VK_RMENU; }

inline bool IsModifierVk(VkCode vk) { return IsControlVariant(vk) || IsShiftVariant(vk) || IsAltVariant(vk); }

inline VkCode ToGenericModifier(VkCode vk) {
    if (IsControlVariant(vk)) { return VK_CONTROL; }
    if (IsShiftVariant(vk)) { return VK_SHIFT; }
    if (IsAltVariant(vk)) { return VK_MENU; }
    return vk;
}

} // namespace platform::input
