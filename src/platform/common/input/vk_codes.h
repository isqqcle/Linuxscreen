#pragma once

#include <cstdint>

namespace platform::input {

using VkCode = std::uint32_t;

constexpr VkCode VK_NONE = 0x00;
constexpr VkCode VK_LBUTTON = 0x01;
constexpr VkCode VK_RBUTTON = 0x02;
constexpr VkCode VK_MBUTTON = 0x04;
constexpr VkCode VK_XBUTTON1 = 0x05;
constexpr VkCode VK_XBUTTON2 = 0x06;
constexpr VkCode VK_BACK = 0x08;
constexpr VkCode VK_TAB = 0x09;
constexpr VkCode VK_RETURN = 0x0D;
constexpr VkCode VK_SHIFT = 0x10;
constexpr VkCode VK_CONTROL = 0x11;
constexpr VkCode VK_MENU = 0x12;
constexpr VkCode VK_PAUSE = 0x13;
constexpr VkCode VK_CAPITAL = 0x14;
constexpr VkCode VK_ESCAPE = 0x1B;
constexpr VkCode VK_SPACE = 0x20;
constexpr VkCode VK_PRIOR = 0x21;
constexpr VkCode VK_NEXT = 0x22;
constexpr VkCode VK_END = 0x23;
constexpr VkCode VK_HOME = 0x24;
constexpr VkCode VK_LEFT = 0x25;
constexpr VkCode VK_UP = 0x26;
constexpr VkCode VK_RIGHT = 0x27;
constexpr VkCode VK_DOWN = 0x28;
constexpr VkCode VK_SNAPSHOT = 0x2C;
constexpr VkCode VK_INSERT = 0x2D;
constexpr VkCode VK_DELETE = 0x2E;

constexpr VkCode VK_0 = 0x30;
constexpr VkCode VK_9 = 0x39;
constexpr VkCode VK_A = 0x41;
constexpr VkCode VK_Z = 0x5A;

constexpr VkCode VK_LWIN = 0x5B;
constexpr VkCode VK_RWIN = 0x5C;
constexpr VkCode VK_APPS = 0x5D;

constexpr VkCode VK_NUMPAD0 = 0x60;
constexpr VkCode VK_NUMPAD1 = 0x61;
constexpr VkCode VK_NUMPAD2 = 0x62;
constexpr VkCode VK_NUMPAD3 = 0x63;
constexpr VkCode VK_NUMPAD4 = 0x64;
constexpr VkCode VK_NUMPAD5 = 0x65;
constexpr VkCode VK_NUMPAD6 = 0x66;
constexpr VkCode VK_NUMPAD7 = 0x67;
constexpr VkCode VK_NUMPAD8 = 0x68;
constexpr VkCode VK_NUMPAD9 = 0x69;
constexpr VkCode VK_MULTIPLY = 0x6A;
constexpr VkCode VK_ADD = 0x6B;
constexpr VkCode VK_SEPARATOR = 0x6C;
constexpr VkCode VK_SUBTRACT = 0x6D;
constexpr VkCode VK_DECIMAL = 0x6E;
constexpr VkCode VK_DIVIDE = 0x6F;

constexpr VkCode VK_F1 = 0x70;
constexpr VkCode VK_F24 = 0x87;

constexpr VkCode VK_NUMLOCK = 0x90;
constexpr VkCode VK_SCROLL = 0x91;

constexpr VkCode VK_LSHIFT = 0xA0;
constexpr VkCode VK_RSHIFT = 0xA1;
constexpr VkCode VK_LCONTROL = 0xA2;
constexpr VkCode VK_RCONTROL = 0xA3;
constexpr VkCode VK_LMENU = 0xA4;
constexpr VkCode VK_RMENU = 0xA5;

constexpr VkCode VK_OEM_1 = 0xBA;
constexpr VkCode VK_OEM_PLUS = 0xBB;
constexpr VkCode VK_OEM_COMMA = 0xBC;
constexpr VkCode VK_OEM_MINUS = 0xBD;
constexpr VkCode VK_OEM_PERIOD = 0xBE;
constexpr VkCode VK_OEM_2 = 0xBF;
constexpr VkCode VK_OEM_3 = 0xC0;
constexpr VkCode VK_OEM_4 = 0xDB;
constexpr VkCode VK_OEM_5 = 0xDC;
constexpr VkCode VK_OEM_6 = 0xDD;
constexpr VkCode VK_OEM_7 = 0xDE;

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
