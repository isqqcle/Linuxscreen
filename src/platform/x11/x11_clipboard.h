#pragma once

struct ImGuiContext;

namespace platform::x11 {

const char* X11GetClipboardText(ImGuiContext* ctx);
void X11SetClipboardText(ImGuiContext* ctx, const char* text);
void X11PumpClipboardEvents();

void ShutdownX11Clipboard();

} // namespace platform::x11
