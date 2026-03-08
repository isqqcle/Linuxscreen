#pragma once

struct ImGuiContext;

namespace platform::x11 {

const char* X11GetClipboardText(ImGuiContext* ctx);

void ShutdownX11Clipboard();

} // namespace platform::x11
