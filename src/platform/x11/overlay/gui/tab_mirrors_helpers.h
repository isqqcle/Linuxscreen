#pragma once

#include "../overlay_internal.h"

namespace platform::x11 {

bool DrawRelativeToCombo(const char* label, std::string& relativeTo);
bool EndsWithSuffix(const std::string& value, const char* suffix);
bool IsPieRelativeTo(const std::string& relativeTo);
bool ShouldUseViewportRelativeTo(const std::string& relativeTo);
std::string GetRelativeToAnchorBase(const std::string& relativeTo);
bool IsLeftAlignedAnchor(const std::string& anchorBase);
bool IsRightAlignedAnchor(const std::string& anchorBase);
std::string NormalizeAspectFitMode(const std::string& value);
bool DrawAspectFitModeCombo(const char* label, std::string& mode);

} // namespace platform::x11
