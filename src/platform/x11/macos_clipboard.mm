#include "macos_clipboard.h"

#import <AppKit/AppKit.h>

namespace platform::x11 {

bool GetMacOSClipboardText(std::string& outText) {
    @autoreleasepool {
        NSPasteboard* pasteboard = [NSPasteboard generalPasteboard];
        if (pasteboard == nil) {
            return false;
        }

        NSString* text = [pasteboard stringForType:NSPasteboardTypeString];
        if (text == nil) {
            return false;
        }

        const char* utf8 = [text UTF8String];
        outText.assign(utf8 != nullptr ? utf8 : "");
        return true;
    }
}

bool SetMacOSClipboardText(const char* text) {
    @autoreleasepool {
        NSPasteboard* pasteboard = [NSPasteboard generalPasteboard];
        if (pasteboard == nil) {
            return false;
        }

        const char* value = text != nullptr ? text : "";
        NSString* nsText = [NSString stringWithUTF8String:value];
        if (nsText == nil) {
            nsText = @"";
        }

        [pasteboard clearContents];
        return [pasteboard setString:nsText forType:NSPasteboardTypeString];
    }
}

} // namespace platform::x11
