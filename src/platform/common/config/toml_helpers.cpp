#include "config_toml.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <initializer_list>

namespace platform::config {

namespace {

template <typename T>
T GetOr(const toml::table& tbl, const std::string& key, T defaultValue) {
    if (auto node = tbl.get(key)) {
        if (auto val = node->value<T>()) { return *val; }
    }
    return defaultValue;
}

std::string GetStringOr(const toml::table& tbl, const std::string& key, const std::string& defaultValue) {
    if (auto node = tbl.get(key)) {
        if (auto val = node->value<std::string>()) { return *val; }
    }
    return defaultValue;
}

const toml::array* GetArray(const toml::table& tbl, const std::string& key) {
    if (auto node = tbl.get(key)) { return node->as_array(); }
    return nullptr;
}

const toml::table* GetTable(const toml::table& tbl, const std::string& key) {
    if (auto node = tbl.get(key)) { return node->as_table(); }
    return nullptr;
}

const char* BindingKeyKindToTomlString(input::BindingKeyKind kind) {
    switch (kind) {
    case input::BindingKeyKind::Keyboard:
        return "keyboard";
    case input::BindingKeyKind::MouseButton:
        return "mouseButton";
    case input::BindingKeyKind::None:
    default:
        return "none";
    }
}

input::BindingKeyKind BindingKeyKindFromTomlString(const std::string& value) {
    if (value == "keyboard") {
        return input::BindingKeyKind::Keyboard;
    }
    if (value == "mouseButton") {
        return input::BindingKeyKind::MouseButton;
    }
    return input::BindingKeyKind::None;
}

toml::table BindingKeyToToml(const input::BindingKey& key) {
    toml::table out;
    out.insert("kind", BindingKeyKindToTomlString(key.kind));
    out.insert("code", static_cast<int64_t>(key.code));
    return out;
}

input::BindingKey BindingKeyFromTomlNode(const toml::node& node) {
    const toml::table* tbl = node.as_table();
    if (!tbl) {
        return {};
    }

    input::BindingKey key;
    key.kind = BindingKeyKindFromTomlString(GetStringOr(*tbl, "kind", "none"));
    key.code = static_cast<int>(GetOr<int64_t>(*tbl, "code", 0));
    if (!input::IsValidBindingKey(key)) {
        return {};
    }
    return key;
}

toml::array BindingKeysToToml(const std::vector<input::BindingKey>& keys) {
    toml::array arr;
    for (const auto& key : keys) {
        if (!input::IsValidBindingKey(key)) {
            continue;
        }
        arr.push_back(BindingKeyToToml(key));
    }
    return arr;
}

std::vector<input::BindingKey> BindingKeysFromToml(const toml::table& tbl, const char* key) {
    std::vector<input::BindingKey> values;
    if (auto arr = GetArray(tbl, key)) {
        values.reserve(arr->size());
        for (const auto& elem : *arr) {
            input::BindingKey parsed = BindingKeyFromTomlNode(elem);
            if (input::IsValidBindingKey(parsed)) {
                values.push_back(parsed);
            }
        }
    }
    return values;
}

void EraseKey(toml::table& tbl, const char* key) {
    if (key) {
        tbl.erase(key);
    }
}

void EraseKeys(toml::table& tbl, std::initializer_list<const char*> keys) {
    for (const char* key : keys) {
        EraseKey(tbl, key);
    }
}

const char* MirrorBorderTypeToString(MirrorBorderType type) {
    switch (type) {
    case MirrorBorderType::Static:
        return "Static";
    case MirrorBorderType::Dynamic:
    default:
        return "Dynamic";
    }
}

MirrorBorderType StringToMirrorBorderType(const std::string& value) {
    if (value == "Static") { return MirrorBorderType::Static; }
    return MirrorBorderType::Dynamic;
}

const char* MirrorBorderShapeToString(MirrorBorderShape shape) {
    switch (shape) {
    case MirrorBorderShape::Circle:
        return "Circle";
    case MirrorBorderShape::Rectangle:
    default:
        return "Rectangle";
    }
}

MirrorBorderShape StringToMirrorBorderShape(const std::string& value) {
    if (value == "Circle") { return MirrorBorderShape::Circle; }
    return MirrorBorderShape::Rectangle;
}

std::string NormalizeAspectFitMode(const std::string& value) {
    if (value == "fitWidth") {
        return "fitWidth";
    }
    if (value == "fitHeight") {
        return "fitHeight";
    }
    return "contain";
}

std::string NormalizeBackgroundSelectedMode(const std::string& value) {
    if (value == "image") {
        return "image";
    }
    if (value == "gradient") {
        return "gradient";
    }
    return "color";
}

const char* GradientAnimationTypeToString(GradientAnimationType type) {
    switch (type) {
    case GradientAnimationType::Rotate:
        return "Rotate";
    case GradientAnimationType::Slide:
        return "Slide";
    case GradientAnimationType::Wave:
        return "Wave";
    case GradientAnimationType::Spiral:
        return "Spiral";
    case GradientAnimationType::Fade:
        return "Fade";
    case GradientAnimationType::None:
    default:
        return "None";
    }
}

GradientAnimationType StringToGradientAnimationType(const std::string& value) {
    if (value == "Rotate") {
        return GradientAnimationType::Rotate;
    }
    if (value == "Slide") {
        return GradientAnimationType::Slide;
    }
    if (value == "Wave") {
        return GradientAnimationType::Wave;
    }
    if (value == "Spiral") {
        return GradientAnimationType::Spiral;
    }
    if (value == "Fade") {
        return GradientAnimationType::Fade;
    }
    return GradientAnimationType::None;
}

bool IsValidUnicodeScalar(std::uint32_t cp) {
    if (cp == 0 || cp > 0x10FFFFu) {
        return false;
    }
    return !(cp >= 0xD800u && cp <= 0xDFFFu);
}

std::string TrimAsciiWhitespace(const std::string& input) {
    std::size_t begin = 0;
    while (begin < input.size() && std::isspace(static_cast<unsigned char>(input[begin]))) {
        ++begin;
    }

    std::size_t end = input.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(input[end - 1]))) {
        --end;
    }

    return input.substr(begin, end - begin);
}

bool StartsWithCaseInsensitive(const std::string& value, const char* prefix) {
    if (!prefix) {
        return false;
    }

    std::size_t prefixLen = 0;
    while (prefix[prefixLen] != '\0') {
        ++prefixLen;
    }
    if (value.size() < prefixLen) {
        return false;
    }

    for (std::size_t i = 0; i < prefixLen; ++i) {
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

    // Reject overlong encodings
    if ((length == 2 && cp < 0x80) || (length == 3 && cp < 0x800) || (length == 4 && cp < 0x10000)) {
        return false;
    }

    if (!IsValidUnicodeScalar(cp)) {
        return false;
    }

    outCodepoint = cp;
    return true;
}

bool TryParseUnicodeCodepointString(const std::string& input, std::uint32_t& outCodepoint) {
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
    if (StartsWithCaseInsensitive(trimmed, "\\U") || StartsWithCaseInsensitive(trimmed, "\\u")) {
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

} // namespace
