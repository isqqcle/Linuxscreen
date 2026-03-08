#pragma once

#include <string>

namespace platform::config {

// Resolves (x, y, relativeTo) to absolute screen-space pixels given viewport size.
// captureW/H are the mirror's capture dimensions (used for right/bottom edge calculations).
void GetRelativeCoords(const std::string& relativeTo, int configX, int configY,
                       int captureW, int captureH,
                       int viewportW, int viewportH,
                       int& outX, int& outY);

} // namespace platform::config
