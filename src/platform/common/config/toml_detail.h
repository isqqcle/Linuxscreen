#pragma once

#include "../config_toml.h"

#include <string>

#include <toml.hpp>

namespace platform::config::detail {

template <typename T>
T GetOr(const toml::table& tbl, const std::string& key, T defaultValue) {
    if (auto node = tbl.get(key)) {
        if (auto val = node->value<T>()) { return *val; }
    }
    return defaultValue;
}

std::string GetStringOr(const toml::table& tbl, const std::string& key, const std::string& defaultValue);
const toml::array* GetArray(const toml::table& tbl, const std::string& key);
const toml::table* GetTable(const toml::table& tbl, const std::string& key);

const char* MirrorBorderTypeToString(MirrorBorderType type);
MirrorBorderType StringToMirrorBorderType(const std::string& value);
const char* MirrorBorderShapeToString(MirrorBorderShape shape);
MirrorBorderShape StringToMirrorBorderShape(const std::string& value);

std::string NormalizeAspectFitMode(const std::string& value);
std::string NormalizeBackgroundSelectedMode(const std::string& value);

const char* GradientAnimationTypeToString(GradientAnimationType type);
GradientAnimationType StringToGradientAnimationType(const std::string& value);

bool IsValidUnicodeScalar(std::uint32_t cp);
std::string TrimAsciiWhitespace(const std::string& input);
bool StartsWithCaseInsensitive(const std::string& value, const char* prefix);
bool TryDecodeFirstUtf8Codepoint(const std::string& input, std::uint32_t& outCodepoint);
bool TryParseUnicodeCodepointString(const std::string& input, std::uint32_t& outCodepoint);

} // namespace platform::config::detail
