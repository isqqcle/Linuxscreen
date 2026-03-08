#include "../overlay_internal.h"
#include "imgui_overlay_helpers.h"
#include "tab_eyezoom.h"
#include "tab_mirrors.h"
#include "tab_mirrors_helpers.h"
#include "tab_mirrors_state.h"

namespace platform::x11 {

MirrorEditorState g_mirrorEditorState;

struct RelativeToOption {
    const char* value;
    const char* label;
};

std::string TrimEditorName(const char* value) {
    if (!value) {
        return {};
    }

    std::string text = value;
    std::size_t begin = 0;
    while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
        ++begin;
    }

    std::size_t end = text.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
        --end;
    }

    return text.substr(begin, end - begin);
}

void CopyEditorNameToBuffer(char* buffer, std::size_t bufferSize, const std::string& value) {
    if (!buffer || bufferSize == 0) {
        return;
    }

    std::strncpy(buffer, value.c_str(), bufferSize - 1);
    buffer[bufferSize - 1] = '\0';
}

bool HasDuplicateMirrorName(const platform::config::LinuxscreenConfig& config,
                            const std::string& name,
                            int ignoreIndex) {
    for (std::size_t i = 0; i < config.mirrors.size(); ++i) {
        if (static_cast<int>(i) == ignoreIndex) {
            continue;
        }
        if (config.mirrors[i].name == name) {
            return true;
        }
    }
    return false;
}

bool HasDuplicateGroupName(const platform::config::LinuxscreenConfig& config,
                           const std::string& name,
                           int ignoreIndex) {
    for (std::size_t i = 0; i < config.mirrorGroups.size(); ++i) {
        if (static_cast<int>(i) == ignoreIndex) {
            continue;
        }
        if (config.mirrorGroups[i].name == name) {
            return true;
        }
    }
    return false;
}

static const RelativeToOption kMirrorRelativeToOptions[] = {
    { "bottomCenterScreen", "Bottom Center (Screen)" },
    { "bottomLeftScreen", "Bottom Left (Screen)" },
    { "bottomLeftViewport", "Bottom Left (Viewport)" },
    { "bottomRightScreen", "Bottom Right (Screen)" },
    { "bottomRightViewport", "Bottom Right (Viewport)" },
    { "centerScreen", "Center (Screen)" },
    { "centerViewport", "Center (Viewport)" },
    { "middleLeftScreen", "Middle Left (Screen)" },
    { "middleLeftViewport", "Middle Left (Viewport)" },
    { "middleRightScreen", "Middle Right (Screen)" },
    { "middleRightViewport", "Middle Right (Viewport)" },
    { "pieLeft", "Pie-Chart Left" },
    { "pieRight", "Pie-Chart Right" },
    { "topCenterScreen", "Top Center (Screen)" },
    { "topLeftScreen", "Top Left (Screen)" },
    { "topLeftViewport", "Top Left (Viewport)" },
    { "topRightScreen", "Top Right (Screen)" },
    { "topRightViewport", "Top Right (Viewport)" },
};

int FindMirrorRelativeToOptionIndex(const std::string& value) {
    for (int idx = 0; idx < IM_ARRAYSIZE(kMirrorRelativeToOptions); ++idx) {
        if (value == kMirrorRelativeToOptions[idx].value) {
            return idx;
        }
    }
    return -1;
}

bool DrawRelativeToCombo(const char* label, std::string& relativeTo) {
    bool changed = false;
    const int currentIndex = FindMirrorRelativeToOptionIndex(relativeTo);
    const char* preview = (currentIndex >= 0) ? kMirrorRelativeToOptions[currentIndex].label : "Unknown";
    if (ImGui::BeginCombo(label, preview)) {
        for (const auto& option : kMirrorRelativeToOptions) {
            const bool selected = (relativeTo == option.value);
            if (ImGui::Selectable(option.label, selected)) {
                relativeTo = option.value;
                changed = true;
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

bool EndsWithSuffix(const std::string& value, const char* suffix) {
    if (!suffix) {
        return false;
    }

    const std::size_t suffixLen = std::strlen(suffix);
    if (value.size() < suffixLen) {
        return false;
    }

    return value.compare(value.size() - suffixLen, suffixLen, suffix) == 0;
}

bool IsPieRelativeTo(const std::string& relativeTo) {
    std::string anchor = relativeTo;
    if (EndsWithSuffix(anchor, "Viewport")) {
        anchor = anchor.substr(0, anchor.size() - 8);
    } else if (EndsWithSuffix(anchor, "Screen")) {
        anchor = anchor.substr(0, anchor.size() - 6);
    }
    return anchor == "pieLeft" || anchor == "pieRight";
}

bool ShouldUseViewportRelativeTo(const std::string& relativeTo) {
    return EndsWithSuffix(relativeTo, "Viewport") || IsPieRelativeTo(relativeTo);
}

std::string GetRelativeToAnchorBase(const std::string& relativeTo) {
    std::string anchor = relativeTo;
    if (EndsWithSuffix(anchor, "Viewport")) {
        anchor = anchor.substr(0, anchor.size() - 8);
    } else if (EndsWithSuffix(anchor, "Screen")) {
        anchor = anchor.substr(0, anchor.size() - 6);
    }
    return anchor;
}

bool IsLeftAlignedAnchor(const std::string& anchorBase) {
    return anchorBase == "topLeft" || anchorBase == "middleLeft" || anchorBase == "bottomLeft";
}

bool IsRightAlignedAnchor(const std::string& anchorBase) {
    return anchorBase == "topRight" || anchorBase == "middleRight" || anchorBase == "bottomRight";
}

struct AspectFitModeOption {
    const char* value;
    const char* label;
};

static const AspectFitModeOption kAspectFitModeOptions[] = {
    { "contain", "Contain" },
    { "fitWidth", "Fit Width" },
    { "fitHeight", "Fit Height" },
};

std::string NormalizeAspectFitMode(const std::string& value) {
    for (const auto& option : kAspectFitModeOptions) {
        if (value == option.value) {
            return option.value;
        }
    }
    return "contain";
}

bool DrawAspectFitModeCombo(const char* label, std::string& mode) {
    mode = NormalizeAspectFitMode(mode);
    bool changed = false;
    const char* preview = "Contain";
    for (const auto& option : kAspectFitModeOptions) {
        if (mode == option.value) {
            preview = option.label;
            break;
        }
    }

    if (ImGui::BeginCombo(label, preview)) {
        for (const auto& option : kAspectFitModeOptions) {
            const bool selected = (mode == option.value);
            if (ImGui::Selectable(option.label, selected)) {
                mode = option.value;
                changed = true;
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

#include "tab_mirrors_editor.cpp"

} // namespace platform::x11
