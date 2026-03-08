#include "anchor_coords.h"

namespace platform::config {

void GetRelativeCoords(const std::string& relativeTo, int configX, int configY,
                       int captureW, int captureH,
                       int viewportW, int viewportH,
                       int& outX, int& outY) {
    std::string anchor = relativeTo;
    if (anchor.length() > 8 && anchor.substr(anchor.length() - 8) == "Viewport") {
        anchor = anchor.substr(0, anchor.length() - 8);
    } else if (anchor.length() > 6 && anchor.substr(anchor.length() - 6) == "Screen") {
        anchor = anchor.substr(0, anchor.length() - 6);
    }
    if (anchor == "custom") {
        outX = configX;
        outY = configY;
        return;
    }

    char firstChar = anchor.empty() ? '\0' : anchor[0];

    if (firstChar == 't') {
        outY = configY;
        if (anchor == "topLeft") {
            outX = configX;
        } else if (anchor == "topRight") {
            outX = viewportW - captureW - configX;
        } else {
            outX = (viewportW - captureW) / 2 + configX;
        }
    } else if (firstChar == 'b') {
        outY = viewportH - captureH - configY;
        if (anchor == "bottomLeft") {
            outX = configX;
        } else if (anchor == "bottomRight") {
            outX = viewportW - captureW - configX;
        } else {
            outX = (viewportW - captureW) / 2 + configX;
        }
    } else if (firstChar == 'c') {
        outX = (viewportW - captureW) / 2 + configX;
        outY = (viewportH - captureH) / 2 + configY;
    } else if (firstChar == 'm') {
        outY = (viewportH - captureH) / 2 + configY;
        if (anchor == "middleLeft") {
            outX = configX;
        } else {
            outX = viewportW - captureW - configX;
        }
    } else if (firstChar == 'p') {
        // Pie chart coordinates used by some configs
        const int PIE_Y_TOP = 220;
        const int PIE_X_LEFT = 92;
        const int PIE_X_RIGHT = 36;
        int base_x = (anchor == "pieLeft") ? viewportW - PIE_X_LEFT : viewportW - PIE_X_RIGHT;
        outX = base_x + configX;
        outY = viewportH - PIE_Y_TOP + configY;
    } else {
        outY = viewportH - captureH - configY;
        outX = configX;
    }
}

} // namespace platform::config
