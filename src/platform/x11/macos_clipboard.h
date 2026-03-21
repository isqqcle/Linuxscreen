#pragma once

#include <string>

namespace platform::x11 {

bool GetMacOSClipboardText(std::string& outText);
bool SetMacOSClipboardText(const char* text);

} // namespace platform::x11
