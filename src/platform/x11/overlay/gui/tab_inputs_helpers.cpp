namespace {

std::string NormalizeResolvedKeyLabel(const char* keyName) {
    if (!keyName || keyName[0] == '\0') {
        return {};
    }

    std::string label(keyName);
    bool hasVisibleChar = false;
    for (unsigned char ch : label) {
        if (std::iscntrl(ch) != 0) {
            return {};
        }
        if (std::isspace(ch) == 0) {
            hasVisibleChar = true;
        }
    }
    if (!hasVisibleChar) {
        return {};
    }

    if (label.size() == 1) {
        const unsigned char ch = static_cast<unsigned char>(label[0]);
        if (std::islower(ch) != 0) {
            label[0] = static_cast<char>(std::toupper(ch));
        }
    }
    return label;
}

uint32_t ResolveKeyboardVkFromScanCode(int scanCode) {
    if (scanCode <= 0) {
        return 0;
    }

    using GlfwGetKeyScancodeFnLocal = int (*)(int key);
    static std::once_flag keyScancodeOnce;
    static GlfwGetKeyScancodeFnLocal getKeyScancode = nullptr;
    std::call_once(keyScancodeOnce, []() {
        getKeyScancode = reinterpret_cast<GlfwGetKeyScancodeFnLocal>(dlsym(RTLD_DEFAULT, "glfwGetKeyScancode"));
    });
    if (!getKeyScancode) {
        return 0;
    }

    for (uint32_t vk = 1; vk <= platform::input::VK_APPS; ++vk) {
        if (!platform::input::IsKeyboardVk(vk)) {
            continue;
        }

        const int glfwKey = platform::input::VkToGlfwKey(vk);
        if (glfwKey < 0) {
            continue;
        }

        if (getKeyScancode(glfwKey) == scanCode) {
            return vk;
        }
    }

    return 0;
}

#ifndef __APPLE__
std::string TryResolveX11PrintableScanCodeLabel(int scanCode) {
    if (scanCode <= 0) {
        return {};
    }

    Display* dpy = glXGetCurrentDisplay();
    if (dpy == nullptr) {
        const RuntimeHandles handles = GetRuntimeHandles();
        dpy = reinterpret_cast<Display*>(handles.nativeDisplay);
    }
    if (dpy == nullptr) {
        return {};
    }

    const KeySym keysym = XkbKeycodeToKeysym(dpy, static_cast<KeyCode>(scanCode), 0, 0);
    if (keysym == NoSymbol) {
        return {};
    }

    if (keysym >= 0x20 && keysym <= 0x7Eu) {
        const char text[2] = { static_cast<char>(keysym), '\0' };
        return NormalizeResolvedKeyLabel(text);
    }

    return {};
}
#else
std::string TryResolveX11PrintableScanCodeLabel(int /*scanCode*/) {
    return {};
}
#endif

const char* GetSpecialBindingScanCodeLabel(int scanCode) {
    switch (scanCode) {
    case 9:
        return "ESC";
    case 22:
        return "BACKSPACE";
    case 23:
        return "TAB";
    case 36:
        return "ENTER";
    case 37:
        return "LCTRL";
    case 50:
        return "LSHIFT";
    case 62:
        return "RSHIFT";
    case 64:
        return "LALT";
    case 65:
        return "SPACE";
    case 66:
        return "CAPS LOCK";
    case 67:
        return "F1";
    case 68:
        return "F2";
    case 69:
        return "F3";
    case 70:
        return "F4";
    case 71:
        return "F5";
    case 72:
        return "F6";
    case 73:
        return "F7";
    case 74:
        return "F8";
    case 75:
        return "F9";
    case 76:
        return "F10";
    case 95:
        return "F11";
    case 96:
        return "F12";
    case 105:
        return "RCTRL";
    case 108:
        return "RALT";
    case 110:
        return "HOME";
    case 111:
        return "UP";
    case 112:
        return "PAGE UP";
    case 113:
        return "LEFT";
    case 114:
        return "RIGHT";
    case 115:
        return "END";
    case 116:
        return "DOWN";
    case 117:
        return "PAGE DOWN";
    case 118:
        return "INSERT";
    case 119:
        return "DELETE";
    case 133:
        return "LWIN";
    case 134:
        return "RWIN";
    default:
        break;
    }
    return nullptr;
}

bool IsLikelyModifierBinding(const platform::input::BindingKey& key) {
    if (!platform::input::IsKeyboardBindingKey(key)) {
        return false;
    }

    switch (key.code) {
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

std::string FormatKeyboardScanCode(int scanCode) {
    if (scanCode <= 0) {
        return std::string("<unset>");
    }

    const uint32_t resolvedVk = ResolveKeyboardVkFromScanCode(scanCode);
    if (resolvedVk != 0 &&
        (platform::input::IsNonTextVk(resolvedVk) || platform::input::IsModifierGlfwKey(static_cast<int>(resolvedVk)))) {
        return FormatSingleVk(resolvedVk);
    }

    using GlfwGetKeyNameFnLocal = const char* (*)(int key, int scancode);
    static std::once_flag keyNameOnce;
    static GlfwGetKeyNameFnLocal getKeyName = nullptr;
    std::call_once(keyNameOnce, []() {
        getKeyName = reinterpret_cast<GlfwGetKeyNameFnLocal>(dlsym(RTLD_DEFAULT, "glfwGetKeyName"));
    });
    if (getKeyName) {
        if (resolvedVk != 0) {
            const int glfwKey = platform::input::VkToGlfwKey(resolvedVk);
            if (glfwKey >= 0) {
                const std::string resolved = NormalizeResolvedKeyLabel(getKeyName(glfwKey, scanCode));
                if (!resolved.empty() && resolved != "?") {
                    return resolved;
                }
            }
        }

        const std::string resolved = NormalizeResolvedKeyLabel(getKeyName(-1, scanCode));
        if (!resolved.empty() && resolved != "?") {
            return resolved;
        }
    }

    if (const std::string x11Resolved = TryResolveX11PrintableScanCodeLabel(scanCode); !x11Resolved.empty()) {
        return x11Resolved;
    }

    if (resolvedVk != 0) {
        return FormatSingleVk(resolvedVk);
    }

    if (const char* special = GetSpecialBindingScanCodeLabel(scanCode)) {
        return std::string(special);
    }

    return "SCAN " + std::to_string(scanCode);
}

} // namespace

std::string FormatSingleBinding(const platform::input::BindingKey& key) {
    if (!platform::input::IsValidBindingKey(key)) {
        return std::string("<unset>");
    }
    if (platform::input::IsMouseBindingKey(key)) {
        return "MOUSE" + std::to_string(key.code + 1);
    }
    return FormatKeyboardScanCode(key.code);
}

std::string FormatBindingChord(const std::vector<platform::input::BindingKey>& keys) {
    if (keys.empty()) {
        return "<none>";
    }

    std::ostringstream oss;
    for (std::size_t i = 0; i < keys.size(); ++i) {
        if (i > 0) {
            oss << "+";
        }
        oss << FormatSingleBinding(keys[i]);
    }
    return oss.str();
}

std::string FormatHotkey(const std::vector<platform::input::BindingKey>& keys) {
    return FormatBindingChord(keys);
}

std::string FormatSingleVk(uint32_t vk) {
    if (vk == 0) {
        return std::string("<unset>");
    }
    if (vk >= 0xE000u && vk < 0xF000u) {
        return "SCAN " + std::to_string(vk - 0xE000u);
    }

    if (vk >= platform::input::VK_A && vk <= platform::input::VK_Z) {
        return std::string(1, static_cast<char>(vk));
    }
    if (vk >= platform::input::VK_0 && vk <= platform::input::VK_9) {
        return std::string(1, static_cast<char>(vk));
    }
    if (vk >= platform::input::VK_F1 && vk <= platform::input::VK_F24) {
        return "F" + std::to_string((vk - platform::input::VK_F1) + 1);
    }
    if (vk >= platform::input::VK_NUMPAD0 && vk <= platform::input::VK_NUMPAD9) {
        return "NUM " + std::to_string(vk - platform::input::VK_NUMPAD0);
    }

    switch (vk) {
    case platform::input::VK_LBUTTON:
        return "MOUSE1";
    case platform::input::VK_RBUTTON:
        return "MOUSE2";
    case platform::input::VK_MBUTTON:
        return "MOUSE3";
    case platform::input::VK_XBUTTON1:
        return "MOUSE4";
    case platform::input::VK_XBUTTON2:
        return "MOUSE5";
    case platform::input::VK_SHIFT:
        return "SHIFT";
    case platform::input::VK_LSHIFT:
        return "LSHIFT";
    case platform::input::VK_RSHIFT:
        return "RSHIFT";
    case platform::input::VK_CONTROL:
        return "CTRL";
    case platform::input::VK_LCONTROL:
        return "LCTRL";
    case platform::input::VK_RCONTROL:
        return "RCTRL";
    case platform::input::VK_MENU:
        return "ALT";
    case platform::input::VK_LMENU:
        return "LALT";
    case platform::input::VK_RMENU:
        return "RALT";
    case platform::input::VK_LWIN:
        return "LWIN";
    case platform::input::VK_RWIN:
        return "RWIN";
    case platform::input::VK_APPS:
        return "APPS";
    case platform::input::VK_BACK:
        return "BACKSPACE";
    case platform::input::VK_TAB:
        return "TAB";
    case platform::input::VK_RETURN:
        return "ENTER";
    case platform::input::VK_CAPITAL:
        return "CAPS LOCK";
    case platform::input::VK_ESCAPE:
        return "ESC";
    case platform::input::VK_SPACE:
        return "SPACE";
    case platform::input::VK_PRIOR:
        return "PAGE UP";
    case platform::input::VK_NEXT:
        return "PAGE DOWN";
    case platform::input::VK_END:
        return "END";
    case platform::input::VK_HOME:
        return "HOME";
    case platform::input::VK_LEFT:
        return "LEFT";
    case platform::input::VK_UP:
        return "UP";
    case platform::input::VK_RIGHT:
        return "RIGHT";
    case platform::input::VK_DOWN:
        return "DOWN";
    case platform::input::VK_SNAPSHOT:
        return "PRINT SCREEN";
    case platform::input::VK_INSERT:
        return "INSERT";
    case platform::input::VK_DELETE:
        return "DELETE";
    case platform::input::VK_MULTIPLY:
        return "NUM *";
    case platform::input::VK_ADD:
        return "NUM +";
    case platform::input::VK_SEPARATOR:
        return "NUM SEP";
    case platform::input::VK_SUBTRACT:
        return "NUM -";
    case platform::input::VK_DECIMAL:
        return "NUM .";
    case platform::input::VK_DIVIDE:
        return "NUM /";
    case platform::input::VK_NUMLOCK:
        return "NUM LOCK";
    case platform::input::VK_SCROLL:
        return "SCROLL LOCK";
    case platform::input::VK_PAUSE:
        return "PAUSE";
    case platform::input::VK_OEM_1:
        return ";";
    case platform::input::VK_OEM_PLUS:
        return "=";
    case platform::input::VK_OEM_COMMA:
        return ",";
    case platform::input::VK_OEM_MINUS:
        return "-";
    case platform::input::VK_OEM_PERIOD:
        return ".";
    case platform::input::VK_OEM_2:
        return "/";
    case platform::input::VK_OEM_3:
        return "`";
    case platform::input::VK_OEM_4:
        return "[";
    case platform::input::VK_OEM_5:
        return "\\";
    case platform::input::VK_OEM_6:
        return "]";
    case platform::input::VK_OEM_7:
        return "'";
    case platform::input::VK_OEM_102:
        return "NON-US \\";
    default:
        break;
    }

    std::ostringstream oss;
    oss << "0x" << std::hex << std::uppercase << vk << std::dec;
    return oss.str();
}

// Format a hotkey (vector of VK codes) as a human-readable string
std::string FormatHotkey(const std::vector<uint32_t>& keys) {
    if (keys.empty()) { return "<none>"; }

    std::ostringstream oss;
    for (size_t i = 0; i < keys.size(); ++i) {
        if (i > 0) { oss << "+"; }
        oss << FormatSingleVk(keys[i]);
    }
    return oss.str();
}

bool IsModifierForCapture(uint32_t vk) {
    return platform::input::IsModifierGlfwKey(static_cast<int>(vk));
}

bool IsWindowsKeyVk(uint32_t vk) {
    return vk == platform::input::VK_LWIN || vk == platform::input::VK_RWIN;
}

void InsertCaptureKeyOrdered(std::vector<uint32_t>& keys, uint32_t vk) {
    const bool isModifier = IsModifierForCapture(vk);
    if (!isModifier) {
        keys.push_back(vk);
        return;
    }

    auto insertPos = keys.begin();
    while (insertPos != keys.end() && IsModifierForCapture(*insertPos)) {
        ++insertPos;
    }
    keys.insert(insertPos, vk);
}

void InsertCaptureBindingOrdered(std::vector<platform::input::BindingKey>& keys,
                                 const platform::input::BindingKey& key,
                                 bool isModifier) {
    if (!isModifier) {
        keys.push_back(key);
        return;
    }

    auto insertPos = keys.begin();
    while (insertPos != keys.end() && IsLikelyModifierBinding(*insertPos)) {
        ++insertPos;
    }
    keys.insert(insertPos, key);
}

bool CaptureTargetUsesSharedModal(platform::config::CaptureTarget target) {
    return target == platform::config::CaptureTarget::Hotkey ||
           target == platform::config::CaptureTarget::GuiHotkey ||
           target == platform::config::CaptureTarget::RebindToggleHotkey ||
           target == platform::config::CaptureTarget::AltSecondary ||
           target == platform::config::CaptureTarget::Exclusion ||
           target == platform::config::CaptureTarget::SensitivityHotkey ||
           target == platform::config::CaptureTarget::SensitivityExclusion ||
           target == platform::config::CaptureTarget::RebindFrom ||
           target == platform::config::CaptureTarget::RebindTo ||
           target == platform::config::CaptureTarget::RebindTypes ||
           target == platform::config::CaptureTarget::RebindDraftInput;
}

bool CanClearCaptureTarget(platform::config::CaptureTarget target) {
    return target != platform::config::CaptureTarget::Exclusion &&
           target != platform::config::CaptureTarget::SensitivityExclusion;
}

bool CaptureTargetUsesSingleKeyDisplay(platform::config::CaptureTarget target) {
    return target == platform::config::CaptureTarget::Exclusion ||
           target == platform::config::CaptureTarget::SensitivityExclusion ||
           target == platform::config::CaptureTarget::RebindFrom ||
           target == platform::config::CaptureTarget::RebindTo ||
           target == platform::config::CaptureTarget::RebindTypes ||
           target == platform::config::CaptureTarget::RebindDraftInput;
}

bool IsRebindCaptureTarget(platform::config::CaptureTarget target) {
    return target == platform::config::CaptureTarget::RebindFrom ||
           target == platform::config::CaptureTarget::RebindTo ||
           target == platform::config::CaptureTarget::RebindTypes ||
           target == platform::config::CaptureTarget::RebindDraftInput;
}

uint32_t ResolveSingleCaptureVk(const std::vector<uint32_t>& keys) {
    if (keys.empty()) {
        return 0;
    }

    for (auto it = keys.rbegin(); it != keys.rend(); ++it) {
        if (!IsModifierForCapture(*it)) {
            return *it;
        }
    }

    return keys.back();
}

platform::input::BindingKey ResolveSingleCaptureBinding(const std::vector<platform::input::BindingKey>& keys) {
    if (keys.empty()) {
        return {};
    }

    for (auto it = keys.rbegin(); it != keys.rend(); ++it) {
        if (!IsLikelyModifierBinding(*it)) {
            return *it;
        }
    }

    return keys.back();
}

int GetDerivedX11ScanCodeForVk(uint32_t vk);

bool IsValidUnicodeScalar(std::uint32_t codepoint) {
    if (codepoint == 0 || codepoint > 0x10FFFFu) {
        return false;
    }
    return !(codepoint >= 0xD800u && codepoint <= 0xDFFFu);
}

std::string CodepointToUtf8(std::uint32_t codepoint) {
    if (!IsValidUnicodeScalar(codepoint)) {
        return std::string();
    }

    std::string out;
    if (codepoint <= 0x7Fu) {
        out.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7FFu) {
        out.push_back(static_cast<char>(0xC0u | ((codepoint >> 6) & 0x1Fu)));
        out.push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
    } else if (codepoint <= 0xFFFFu) {
        out.push_back(static_cast<char>(0xE0u | ((codepoint >> 12) & 0x0Fu)));
        out.push_back(static_cast<char>(0x80u | ((codepoint >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
    } else {
        out.push_back(static_cast<char>(0xF0u | ((codepoint >> 18) & 0x07u)));
        out.push_back(static_cast<char>(0x80u | ((codepoint >> 12) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | ((codepoint >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
    }

    return out;
}

std::string FormatCodepointUPlus(std::uint32_t codepoint) {
    char buffer[32] = {};
    if (codepoint <= 0xFFFFu) {
        std::snprintf(buffer, sizeof(buffer), "U+%04X", static_cast<unsigned>(codepoint));
    } else {
        std::snprintf(buffer, sizeof(buffer), "U+%06X", static_cast<unsigned>(codepoint));
    }
    return std::string(buffer);
}

std::string CodepointToDisplay(std::uint32_t codepoint) {
    if (!IsValidUnicodeScalar(codepoint)) {
        return std::string("[None]");
    }
    if (codepoint < 0x20u || codepoint == 0x7Fu) {
        return FormatCodepointUPlus(codepoint);
    }
    const std::string utf8 = CodepointToUtf8(codepoint);
    if (utf8.empty()) {
        return FormatCodepointUPlus(codepoint);
    }
    return utf8;
}

std::string TrimAsciiWhitespace(const std::string& input) {
    std::size_t begin = 0;
    while (begin < input.size() && std::isspace(static_cast<unsigned char>(input[begin])) != 0) {
        ++begin;
    }

    std::size_t end = input.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(input[end - 1])) != 0) {
        --end;
    }

    return input.substr(begin, end - begin);
}

bool StartsWithCaseInsensitive(const std::string& value, const char* prefix) {
    if (!prefix) {
        return false;
    }

    std::size_t prefixLength = 0;
    while (prefix[prefixLength] != '\0') {
        ++prefixLength;
    }

    if (value.size() < prefixLength) {
        return false;
    }

    for (std::size_t i = 0; i < prefixLength; ++i) {
        unsigned char lhs = static_cast<unsigned char>(value[i]);
        unsigned char rhs = static_cast<unsigned char>(prefix[i]);
        if (std::toupper(lhs) != std::toupper(rhs)) {
            return false;
        }
    }

    return true;
}

bool TryDecodeFirstUtf8Codepoint(const std::string& input, std::uint32_t& outCodepoint) {
    if (input.empty()) {
        return false;
    }

    const unsigned char* bytes = reinterpret_cast<const unsigned char*>(input.data());
    const std::size_t size = input.size();
    std::uint32_t cp = 0;
    std::size_t length = 0;

    const unsigned char b0 = bytes[0];
    if (b0 < 0x80) {
        cp = b0;
        length = 1;
    } else if ((b0 & 0xE0) == 0xC0 && size >= 2) {
        cp = static_cast<std::uint32_t>(b0 & 0x1F);
        length = 2;
    } else if ((b0 & 0xF0) == 0xE0 && size >= 3) {
        cp = static_cast<std::uint32_t>(b0 & 0x0F);
        length = 3;
    } else if ((b0 & 0xF8) == 0xF0 && size >= 4) {
        cp = static_cast<std::uint32_t>(b0 & 0x07);
        length = 4;
    } else {
        return false;
    }

    for (std::size_t i = 1; i < length; ++i) {
        const unsigned char continuation = bytes[i];
        if ((continuation & 0xC0) != 0x80) {
            return false;
        }
        cp = (cp << 6) | static_cast<std::uint32_t>(continuation & 0x3F);
    }

    if ((length == 2 && cp < 0x80) || (length == 3 && cp < 0x800) || (length == 4 && cp < 0x10000)) {
        return false;
    }

    if (!IsValidUnicodeScalar(cp)) {
        return false;
    }

    outCodepoint = cp;
    return true;
}

bool TryParseUnicodeInputString(const std::string& input, std::uint32_t& outCodepoint) {
    const std::string trimmed = TrimAsciiWhitespace(input);
    if (trimmed.empty()) {
        return false;
    }

    auto parseHexCodepoint = [&](std::string hex) -> bool {
        if (hex.size() >= 2 && hex.front() == '{' && hex.back() == '}') {
            hex = hex.substr(1, hex.size() - 2);
        }
        if (hex.empty()) {
            return false;
        }

        try {
            std::size_t index = 0;
            const unsigned long value = std::stoul(hex, &index, 16);
            if (index == 0 || index != hex.size()) {
                return false;
            }
            const std::uint32_t cp = static_cast<std::uint32_t>(value);
            if (!IsValidUnicodeScalar(cp)) {
                return false;
            }
            outCodepoint = cp;
            return true;
        } catch (...) {
            return false;
        }
    };

    if (StartsWithCaseInsensitive(trimmed, "U+")) {
        return parseHexCodepoint(trimmed.substr(2));
    }
    if (StartsWithCaseInsensitive(trimmed, "\\u") || StartsWithCaseInsensitive(trimmed, "\\U")) {
        return parseHexCodepoint(trimmed.substr(2));
    }
    if (StartsWithCaseInsensitive(trimmed, "0X")) {
        return parseHexCodepoint(trimmed.substr(2));
    }

    if (TryDecodeFirstUtf8Codepoint(trimmed, outCodepoint)) {
        return true;
    }

    return parseHexCodepoint(trimmed);
}

platform::input::BindingKey BindingFromLegacyUiVk(uint32_t vk) {
    const int mouseButton = platform::input::VkToGlfwMouseButton(vk);
    if (mouseButton >= 0) {
        return platform::input::MakeMouseButtonBindingKey(mouseButton);
    }

    const int scanCode = GetDerivedX11ScanCodeForVk(vk);
    if (scanCode > 0) {
        return platform::input::MakeKeyboardBindingKey(scanCode);
    }
    return {};
}

uint32_t LegacyUiVkFromBinding(const platform::input::BindingKey& binding) {
    if (platform::input::IsMouseBindingKey(binding)) {
        return platform::input::GlfwMouseButtonToVk(binding.code);
    }
    if (!platform::input::IsKeyboardBindingKey(binding)) {
        return 0;
    }
    return ResolveKeyboardVkFromScanCode(binding.code);
}

bool IsPrimaryMouseBinding(const platform::input::BindingKey& binding) {
    return platform::input::IsMouseBindingKey(binding) && (binding.code == 0 || binding.code == 1);
}

bool IsIdentityRebindForKey(const platform::config::KeyRebind& rebind, const platform::input::BindingKey& originalBinding) {
    if (rebind.fromInput != originalBinding) {
        return false;
    }
    if (rebind.toInput != originalBinding) {
        return false;
    }
    if (platform::input::IsValidBindingKey(rebind.customOutputKey) && rebind.customOutputKey != rebind.toInput) {
        return false;
    }
    if (rebind.customOutputUnicode != 0) {
        return false;
    }
    if (rebind.customOutputShiftUnicode != 0) {
        return false;
    }
    return true;
}

bool IsNoOpRebindForKey(const platform::config::KeyRebind& rebind, const platform::input::BindingKey& originalBinding) {
    const bool repeatBlacklistedMouseSource = IsPrimaryMouseBinding(originalBinding);
    if (rebind.consumeSourceInput) {
        return false;
    }
    if (!repeatBlacklistedMouseSource && rebind.keyRepeatDisabled) {
        return false;
    }
    if (!repeatBlacklistedMouseSource && (rebind.keyRepeatStartDelay > 0 || rebind.keyRepeatDelay > 0)) {
        return false;
    }
    return IsIdentityRebindForKey(rebind, originalBinding);
}

bool IsNoOpRebindForKey(const platform::config::KeyRebind& rebind, uint32_t originalVk) {
    return IsNoOpRebindForKey(rebind, BindingFromLegacyUiVk(originalVk));
}

std::string RebindDisplayName(const platform::config::KeyRebind& rebind) {
    const std::string trimmedName = TrimAsciiWhitespace(rebind.name);
    if (!trimmedName.empty()) {
        return trimmedName;
    }
    return FormatSingleBinding(rebind.fromInput);
}

int FindAnyRebindIndexForKey(const platform::config::LinuxscreenConfig& config, uint32_t fromVk) {
    const platform::input::BindingKey fromBinding = BindingFromLegacyUiVk(fromVk);
    for (int i = 0; i < static_cast<int>(config.keyRebinds.rebinds.size()); ++i) {
        if (config.keyRebinds.rebinds[static_cast<std::size_t>(i)].fromInput == fromBinding) {
            return i;
        }
    }
    return -1;
}

int FindBestRebindIndexForKey(const platform::config::LinuxscreenConfig& config, uint32_t fromVk) {
    const platform::input::BindingKey fromBinding = BindingFromLegacyUiVk(fromVk);
    int first = -1;
    int enabledAny = -1;
    int enabledConfigured = -1;
    int configuredAny = -1;

    for (int i = 0; i < static_cast<int>(config.keyRebinds.rebinds.size()); ++i) {
        const auto& rebind = config.keyRebinds.rebinds[static_cast<std::size_t>(i)];
        if (rebind.fromInput != fromBinding) {
            continue;
        }
        if (first == -1) {
            first = i;
        }

        const bool configured = (platform::input::IsValidBindingKey(rebind.fromInput) &&
                                 (rebind.consumeSourceInput || platform::input::IsValidBindingKey(rebind.toInput)));
        if (configured && configuredAny == -1) {
            configuredAny = i;
        }
        if (rebind.enabled && enabledAny == -1) {
            enabledAny = i;
        }
        if (rebind.enabled && configured) {
            enabledConfigured = i;
            break;
        }
    }

    if (enabledConfigured != -1) {
        return enabledConfigured;
    }
    if (configuredAny != -1) {
        return configuredAny;
    }
    if (enabledAny != -1) {
        return enabledAny;
    }
    return first;
}

int EnsureRebindForKey(platform::config::LinuxscreenConfig& config, uint32_t fromVk) {
    const int existing = FindBestRebindIndexForKey(config, fromVk);
    if (existing >= 0) {
        return existing;
    }

    platform::config::KeyRebind rebind;
    rebind.fromInput = BindingFromLegacyUiVk(fromVk);
    rebind.toInput = rebind.fromInput;
    rebind.enabled = true;
    config.keyRebinds.rebinds.push_back(rebind);
    return static_cast<int>(config.keyRebinds.rebinds.size()) - 1;
}

void EraseRebindAdjustingLayoutState(platform::config::LinuxscreenConfig& config, int eraseIndex) {
    if (eraseIndex < 0 || eraseIndex >= static_cast<int>(config.keyRebinds.rebinds.size())) {
        return;
    }

    config.keyRebinds.rebinds.erase(config.keyRebinds.rebinds.begin() + eraseIndex);

    auto adjustIndex = [&](int& index) {
        if (index == -1) {
            return;
        }
        if (index == eraseIndex) {
            index = -1;
        } else if (index > eraseIndex) {
            --index;
        }
    };

    adjustIndex(g_rebindLayoutState.contextPreferredIndex);
    adjustIndex(g_rebindLayoutState.bindIndex);
    adjustIndex(g_rebindLayoutState.unicodeEditIndex);
    adjustIndex(g_rebindLayoutState.customSourceCaptureIndex);
    if (g_rebindLayoutState.bindIndex < 0) {
        g_rebindLayoutState.bindTarget = RebindLayoutBindTarget::Unset;
    }
    if (g_rebindLayoutState.customSourceCaptureIndex < 0) {
        g_rebindLayoutState.customSourceCaptureSequence = 0;
    }
}

std::string FormatScanDisplay(int scanCode, uint32_t fallbackVk) {
    if (scanCode <= 0) {
        return FormatSingleVk(fallbackVk);
    }

    std::ostringstream oss;
    oss << FormatSingleVk(fallbackVk) << " (" << scanCode << ")";
    return oss.str();
}

using GlfwGetKeyScancodeFn = int (*)(int key);
using GlfwGetKeyNameFn = const char* (*)(int key, int scancode);

GlfwGetKeyScancodeFn GetGlfwGetKeyScancodeFn() {
    static std::once_flag once;
    static GlfwGetKeyScancodeFn fn = nullptr;
    std::call_once(once, []() {
        fn = reinterpret_cast<GlfwGetKeyScancodeFn>(dlsym(RTLD_DEFAULT, "glfwGetKeyScancode"));
    });
    return fn;
}

GlfwGetKeyNameFn GetGlfwGetKeyNameFn() {
    static std::once_flag once;
    static GlfwGetKeyNameFn fn = nullptr;
    std::call_once(once, []() {
        fn = reinterpret_cast<GlfwGetKeyNameFn>(dlsym(RTLD_DEFAULT, "glfwGetKeyName"));
    });
    return fn;
}

int GetDerivedX11ScanCodeForVk(uint32_t vk) {
    if (!platform::input::IsKeyboardVk(vk)) {
        return 0;
    }

    if (GlfwGetKeyScancodeFn getKeyScancode = GetGlfwGetKeyScancodeFn()) {
        const int glfwKey = platform::input::VkToGlfwKey(vk);
        if (glfwKey >= 0) {
            const int scanCode = getKeyScancode(glfwKey);
            if (scanCode > 0) {
                return scanCode;
            }
        }
    }

    return 0;
}

void AddKnownScanOption(std::map<int, uint32_t>& scanToVk, uint32_t vk) {
    if (!platform::input::IsKeyboardVk(vk)) {
        return;
    }

    const int scanCode = GetDerivedX11ScanCodeForVk(vk);
    if (scanCode <= 0) {
        return;
    }

    auto it = scanToVk.find(scanCode);
    if (it == scanToVk.end()) {
        scanToVk.emplace(scanCode, vk);
        return;
    }

    if (it->second == 0 && vk != 0) {
        it->second = vk;
    }
}

std::map<int, uint32_t> BuildKnownScanOptions(uint32_t preferredVk) {
    std::map<int, uint32_t> scanToVk;

    auto addVk = [&](uint32_t vk) { AddKnownScanOption(scanToVk, vk); };
    addVk(preferredVk);

    for (uint32_t vk = 1; vk <= platform::input::VK_APPS; ++vk) {
        if (!platform::input::IsKeyboardVk(vk)) {
            continue;
        }
        addVk(vk);
    }

    return scanToVk;
}

bool TryResolvePreviewCodepoint(uint32_t nativeKey, int scanCode, bool shiftDown, std::uint32_t& outCodepoint) {
    outCodepoint = 0;
    if (nativeKey == 0 || platform::input::IsNonTextVk(nativeKey)) {
        return false;
    }

    if (GlfwGetKeyNameFn getKeyName = GetGlfwGetKeyNameFn()) {
        if (TryDecodeFirstUtf8Codepoint(getKeyName(static_cast<int>(nativeKey), scanCode), outCodepoint)) {
            if (shiftDown &&
                outCodepoint >= static_cast<std::uint32_t>('a') &&
                outCodepoint <= static_cast<std::uint32_t>('z')) {
                outCodepoint = static_cast<std::uint32_t>('A' + (outCodepoint - static_cast<std::uint32_t>('a')));
            }
            return outCodepoint != 0;
        }
    }

    return false;
}

bool HasExplicitTextOverride(const platform::config::KeyRebind& rebind) {
    return rebind.useCustomOutput &&
           (platform::input::IsValidBindingKey(rebind.customOutputKey) || rebind.customOutputUnicode != 0 ||
            rebind.customOutputShiftUnicode != 0);
}

bool HasCustomUnicodeTextOutput(const platform::config::KeyRebind& rebind) {
    return rebind.useCustomOutput && (rebind.customOutputUnicode != 0 || rebind.customOutputShiftUnicode != 0);
}

platform::input::BindingKey ResolvePreviewTriggerBinding(const platform::config::KeyRebind& rebind, uint32_t originalVk) {
    if (platform::input::IsValidBindingKey(rebind.toInput)) {
        return rebind.toInput;
    }
    return BindingFromLegacyUiVk(originalVk);
}

platform::input::BindingKey ResolvePreviewTextBinding(const platform::config::KeyRebind& rebind, uint32_t originalVk) {
    if (rebind.useCustomOutput && platform::input::IsValidBindingKey(rebind.customOutputKey)) {
        return rebind.customOutputKey;
    }
    return ResolvePreviewTriggerBinding(rebind, originalVk);
}

std::string CodepointToPreviewDisplay(std::uint32_t codepoint) {
    if (!IsValidUnicodeScalar(codepoint)) {
        return std::string();
    }
    if (codepoint <= 0x20u || codepoint == 0x7Fu) {
        return FormatCodepointUPlus(codepoint);
    }
    return CodepointToDisplay(codepoint);
}

std::string FormatCannotTypePreview(const platform::input::BindingKey& textBinding, uint32_t originalVk) {
    if (platform::input::IsValidBindingKey(textBinding)) {
        return "Cannot type (" + FormatSingleBinding(textBinding) + ")";
    }
    return "Cannot type (" + FormatSingleVk(originalVk) + ")";
}

std::string ResolveRebindTextPreview(const platform::config::KeyRebind& rebind, uint32_t originalVk, bool shiftDown) {
    const platform::input::BindingKey textBinding = ResolvePreviewTextBinding(rebind, originalVk);
    const uint32_t textVk = LegacyUiVkFromBinding(textBinding);
    const int textScanCode = platform::input::IsKeyboardBindingKey(textBinding) ? textBinding.code : 0;

    if (shiftDown &&
        IsValidUnicodeScalar(rebind.customOutputShiftUnicode) &&
        !platform::input::IsNonTextVk(textVk)) {
        return CodepointToPreviewDisplay(rebind.customOutputShiftUnicode);
    }

    if (IsValidUnicodeScalar(rebind.customOutputUnicode) && !platform::input::IsNonTextVk(textVk)) {
        return CodepointToPreviewDisplay(rebind.customOutputUnicode);
    }

    std::uint32_t translated = 0;
    if (!platform::input::IsNonTextVk(textVk) &&
        TryResolvePreviewCodepoint(textVk, textScanCode, shiftDown, translated) &&
        translated != 0) {
        return CodepointToPreviewDisplay(translated);
    }

    if (platform::input::IsNonTextVk(textVk)) {
        return FormatCannotTypePreview(textBinding, originalVk);
    }
    if (platform::input::IsValidBindingKey(textBinding)) {
        return FormatSingleBinding(textBinding);
    }
    return FormatCannotTypePreview(textBinding, originalVk);
}

std::string TypesValueForRebind(const platform::config::KeyRebind* rebind, uint32_t originalVk) {
    if (!rebind) {
        return FormatSingleVk(originalVk);
    }
    if (HasCustomUnicodeTextOutput(*rebind)) {
        const std::string baseDisplay = ResolveRebindTextPreview(*rebind, originalVk, false);
        const std::string shiftDisplay = ResolveRebindTextPreview(*rebind, originalVk, true);
        if (baseDisplay == shiftDisplay) {
            return baseDisplay;
        }
        return baseDisplay + " / " + shiftDisplay;
    }

    const platform::input::BindingKey textBinding = ResolvePreviewTextBinding(*rebind, originalVk);
    const uint32_t textVk = LegacyUiVkFromBinding(textBinding);
    if (platform::input::IsNonTextVk(textVk)) {
        return FormatCannotTypePreview(textBinding, originalVk);
    }
    if (platform::input::IsValidBindingKey(textBinding)) {
        return FormatSingleBinding(textBinding);
    }
    return FormatSingleVk(originalVk);
}

std::string TriggersValueForRebind(const platform::config::KeyRebind* rebind, uint32_t originalVk) {
    const platform::input::BindingKey triggerBinding =
        rebind ? ResolvePreviewTriggerBinding(*rebind, originalVk) : BindingFromLegacyUiVk(originalVk);
    if (platform::input::IsValidBindingKey(triggerBinding)) {
        return FormatSingleBinding(triggerBinding);
    }
    return FormatSingleVk(originalVk);
}

struct RebindButtonPalette {
    ImVec4 normal;
    ImVec4 hovered;
    ImVec4 active;
};

enum class RebindInputState {
    EnabledRemap = 0,
    DisabledPassThrough,
    BlockInput,
};

RebindInputState GetRebindInputState(const platform::config::KeyRebind& rebind) {
    if (rebind.enabled && rebind.consumeSourceInput) {
        return RebindInputState::BlockInput;
    }
    if (rebind.enabled) {
        return RebindInputState::EnabledRemap;
    }
    return RebindInputState::DisabledPassThrough;
}

void SetRebindInputState(platform::config::KeyRebind& rebind, RebindInputState state) {
    switch (state) {
    case RebindInputState::EnabledRemap:
        rebind.enabled = true;
        rebind.consumeSourceInput = false;
        break;
    case RebindInputState::DisabledPassThrough:
        rebind.enabled = false;
        rebind.consumeSourceInput = false;
        break;
    case RebindInputState::BlockInput:
        rebind.enabled = true;
        rebind.consumeSourceInput = true;
        break;
    }
}

const char* RebindInputStateLabel(RebindInputState state) {
    switch (state) {
    case RebindInputState::EnabledRemap:
        return "Rebind Enabled";
    case RebindInputState::DisabledPassThrough:
        return "Rebind Disabled";
    case RebindInputState::BlockInput:
        return "No Output";
    }
    return "Unknown";
}

RebindInputState NextRebindInputState(RebindInputState state) {
    switch (state) {
    case RebindInputState::EnabledRemap:
        return RebindInputState::BlockInput;
    case RebindInputState::BlockInput:
        return RebindInputState::DisabledPassThrough;
    case RebindInputState::DisabledPassThrough:
        return RebindInputState::EnabledRemap;
    }
    return RebindInputState::EnabledRemap;
}

RebindButtonPalette GetRebindButtonPalette(bool activeRebind, RebindInputState state) {
    if (!activeRebind) {
        return { ImVec4(0.20f, 0.21f, 0.25f, 1.0f), ImVec4(0.28f, 0.29f, 0.35f, 1.0f), ImVec4(0.16f, 0.17f, 0.21f, 1.0f) };
    }
    if (state == RebindInputState::EnabledRemap) {
        return { ImVec4(0.12f, 0.36f, 0.24f, 1.0f), ImVec4(0.18f, 0.48f, 0.32f, 1.0f), ImVec4(0.10f, 0.28f, 0.18f, 1.0f) };
    }
    if (state == RebindInputState::BlockInput) {
        return { ImVec4(0.44f, 0.14f, 0.14f, 1.0f), ImVec4(0.56f, 0.18f, 0.18f, 1.0f), ImVec4(0.34f, 0.11f, 0.11f, 1.0f) };
    }
    return { ImVec4(0.23f, 0.23f, 0.27f, 1.0f), ImVec4(0.30f, 0.30f, 0.35f, 1.0f), ImVec4(0.19f, 0.19f, 0.23f, 1.0f) };
}

std::string ShortenForButton(const std::string& value, std::size_t maxChars) {
    if (maxChars == 0) {
        return std::string();
    }

    std::string out = TrimAsciiWhitespace(value);
    const std::size_t paren = out.find(" (");
    if (paren != std::string::npos) {
        out = out.substr(0, paren);
    }

    if (out.rfind("scan ", 0) == 0) {
        std::size_t pos = 5;
        while (pos < out.size() && std::isdigit(static_cast<unsigned char>(out[pos]))) {
            ++pos;
        }
        if (pos > 5) {
            out = "s" + out.substr(5, pos - 5);
        }
    }

    if (out.size() <= maxChars) {
        return out;
    }
    if (maxChars <= 2) {
        return out.substr(0, maxChars);
    }
    return out.substr(0, maxChars - 1) + "~";
}

std::size_t EstimateButtonTextChars(float buttonWidth) {
    // Approximate visible monospace-ish characters at current font sizing.
    // Keep a small buffer for button padding/border.
    const float usable = std::max(0.0f, buttonWidth - 6.0f);
    const std::size_t chars = static_cast<std::size_t>(usable / 7.3f);
    return std::max<std::size_t>(2, chars);
}

std::string BuildRepeatDelaySliderFormat(int delayMs, const char* zeroLabel) {
    if (delayMs <= 0) {
        return zeroLabel ? std::string(zeroLabel) : std::string("Default");
    }

    char rateBuffer[32] = {};
    if (1000 % delayMs == 0) {
        std::snprintf(rateBuffer, sizeof(rateBuffer), "%d", 1000 / delayMs);
    } else {
        const double repeatsPerSecond = 1000.0 / static_cast<double>(delayMs);
        std::snprintf(rateBuffer, sizeof(rateBuffer), "%.1f", repeatsPerSecond);
    }

    std::string format = "%d ms (";
    format += rateBuffer;
    format += " repeats/s)";
    return format;
}

constexpr int kHotkeySlotRepeatWarningThresholdMs = 50;

int GetNativeRepeatDelayMsForWarning() {
    static std::mutex cacheMutex;
    static int cachedRepeatDelayMs = 0;

    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        if (cachedRepeatDelayMs > 0) {
            return cachedRepeatDelayMs;
        }
    }

    int repeatDelayMs = 33;
    bool shouldCache = true;
#ifndef __APPLE__
    Display* dpy = glXGetCurrentDisplay();
    if (dpy != nullptr) {
        unsigned int delay = 0;
        unsigned int interval = 0;
        if (XkbGetAutoRepeatRate(dpy, XkbUseCoreKbd, &delay, &interval) && interval > 0) {
            repeatDelayMs = static_cast<int>(interval);
        }
    }
    shouldCache = (dpy != nullptr);
#endif

    repeatDelayMs = std::max(1, repeatDelayMs);

    if (shouldCache) {
        std::lock_guard<std::mutex> lock(cacheMutex);
        cachedRepeatDelayMs = repeatDelayMs;
    }

    return repeatDelayMs;
}

int ResolveEffectiveRepeatDelayMs(int configuredRepeatDelayMs) {
    if (configuredRepeatDelayMs > 0) {
        return configuredRepeatDelayMs;
    }
    return GetNativeRepeatDelayMsForWarning();
}

int ResolveEffectivePerInputRepeatDelayMs(const platform::config::LinuxscreenConfig& config, std::uint32_t sourceVk, int perInputRepeatDelayMs) {
    if (perInputRepeatDelayMs > 0) {
        return perInputRepeatDelayMs;
    }

    const bool allowGlobalRepeat = !platform::input::IsMouseVk(sourceVk) || config.keyRepeatAffectsMouseButtons;
    if (allowGlobalRepeat && config.keyRepeatDelay > 0) {
        return config.keyRepeatDelay;
    }

    return GetNativeRepeatDelayMsForWarning();
}

bool ShouldWarnAboutHotkeySlotRepeatRate(int effectiveRepeatDelayMs) {
    return effectiveRepeatDelayMs > 0 && effectiveRepeatDelayMs < kHotkeySlotRepeatWarningThresholdMs;
}

std::string BuildCompactRebindLine(const platform::config::KeyRebind* rebind, uint32_t originalVk, float buttonWidth) {
    if (!rebind || !platform::input::IsValidBindingKey(rebind->fromInput)) {
        return std::string();
    }

    const RebindInputState state = GetRebindInputState(*rebind);
    if (!platform::input::IsValidBindingKey(rebind->toInput)) {
        return std::string();
    }

    const bool configured = !IsNoOpRebindForKey(*rebind, originalVk);
    if (!configured && state == RebindInputState::EnabledRemap) {
        return std::string();
    }
    if (!configured && state == RebindInputState::DisabledPassThrough) {
        return std::string();
    }

    const std::size_t tokenLimit = EstimateButtonTextChars(buttonWidth);
    std::string types = ShortenForButton(TypesValueForRebind(rebind, originalVk), tokenLimit);
    std::string triggers = ShortenForButton(TriggersValueForRebind(rebind, originalVk), tokenLimit);
    return "C:" + types + "\nG:" + triggers;
}
