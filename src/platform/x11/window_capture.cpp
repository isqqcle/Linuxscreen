#include "window_capture.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace platform::x11 {

namespace {

std::string LowercaseCopy(const std::string& value) {
    std::string lowered = value;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return lowered;
}

bool ContainsTitleMatch(const std::string& lhs, const std::string& rhs) {
    if (lhs.empty() || rhs.empty()) {
        return false;
    }

    return LowercaseCopy(lhs).find(LowercaseCopy(rhs)) != std::string::npos;
}

bool StartsWithTitleMatch(const std::string& value, const std::string& pattern) {
    if (value.empty() || pattern.empty()) {
        return false;
    }

    const std::string valueLower = LowercaseCopy(value);
    const std::string patternLower = LowercaseCopy(pattern);
    return valueLower.rfind(patternLower, 0) == 0;
}

bool EndsWithTitleMatch(const std::string& value, const std::string& pattern) {
    if (value.empty() || pattern.empty()) {
        return false;
    }

    const std::string valueLower = LowercaseCopy(value);
    const std::string patternLower = LowercaseCopy(pattern);
    if (patternLower.size() > valueLower.size()) {
        return false;
    }

    return valueLower.compare(valueLower.size() - patternLower.size(), patternLower.size(), patternLower) == 0;
}

bool RequiresTitlePattern(platform::config::MirrorSourceTitleMatchMode mode) {
    return mode != platform::config::MirrorSourceTitleMatchMode::Disabled;
}

bool HasConfiguredRequestIdentity(const std::string& appId,
                                  const std::string& windowTitle,
                                  platform::config::MirrorSourceTitleMatchMode titleMatchMode,
                                  platform::config::MirrorSourceFallbackMode fallbackMode,
                                  const std::string& selectionToken) {
    if (!selectionToken.empty()) {
        return true;
    }

    if (appId.empty()) {
        return false;
    }

    if (!windowTitle.empty()) {
        return true;
    }

    return !RequiresTitlePattern(titleMatchMode) ||
           fallbackMode == platform::config::MirrorSourceFallbackMode::SameApp;
}

int TitleMatchSpecificity(platform::config::MirrorSourceTitleMatchMode mode) {
    switch (mode) {
    case platform::config::MirrorSourceTitleMatchMode::Exact:
        return 4;
    case platform::config::MirrorSourceTitleMatchMode::StartsWith:
    case platform::config::MirrorSourceTitleMatchMode::EndsWith:
        return 3;
    case platform::config::MirrorSourceTitleMatchMode::Contains:
        return 2;
    case platform::config::MirrorSourceTitleMatchMode::Disabled:
    default:
        return 1;
    }
}

int EvaluateTitleMatchScore(const AvailableWindow& window,
                            const std::string& windowTitle,
                            platform::config::MirrorSourceTitleMatchMode titleMatchMode) {
    switch (titleMatchMode) {
    case platform::config::MirrorSourceTitleMatchMode::Exact:
        return (!windowTitle.empty() && window.windowTitle == windowTitle) ? 4000000 : -1;
    case platform::config::MirrorSourceTitleMatchMode::StartsWith:
        return StartsWithTitleMatch(window.windowTitle, windowTitle) ? 3700000 : -1;
    case platform::config::MirrorSourceTitleMatchMode::EndsWith:
        return EndsWithTitleMatch(window.windowTitle, windowTitle) ? 3600000 : -1;
    case platform::config::MirrorSourceTitleMatchMode::Contains:
        return ContainsTitleMatch(window.windowTitle, windowTitle) ? 3400000 : -1;
    case platform::config::MirrorSourceTitleMatchMode::Disabled:
    default:
        return -1;
    }
}

int ScoreWindowAffinity(const AvailableWindow& window,
                        int preferredWidth,
                        int preferredHeight) {
    int score = 0;
    if (window.onScreen) {
        score += 1000000;
    }
    if (window.active) {
        score += 500000;
    }

    if (preferredWidth > 0 && preferredHeight > 0 && window.width > 0 && window.height > 0) {
        const int sizeDelta = std::abs(window.width - preferredWidth) + std::abs(window.height - preferredHeight);
        score += std::max(0, 4000000 - sizeDelta * 2000);
    }

    score += std::max(0, window.width) * std::max(0, window.height);
    return score;
}

int ScoreWindowMatch(const AvailableWindow& window,
                     const std::string& appId,
                     const std::string& windowTitle,
                     platform::config::MirrorSourceTitleMatchMode titleMatchMode,
                     platform::config::MirrorSourceFallbackMode fallbackMode,
                     int preferredWidth,
                     int preferredHeight) {
    if (window.appId != appId) {
        return -1;
    }

    if (titleMatchMode == platform::config::MirrorSourceTitleMatchMode::Disabled) {
        return 3000000 + ScoreWindowAffinity(window, preferredWidth, preferredHeight);
    }

    const int titleScore = EvaluateTitleMatchScore(window, windowTitle, titleMatchMode);
    if (titleScore >= 0) {
        return titleScore + ScoreWindowAffinity(window, preferredWidth, preferredHeight);
    }

    if (fallbackMode != platform::config::MirrorSourceFallbackMode::SameApp) {
        return -1;
    }

    return 2000000 + ScoreWindowAffinity(window, preferredWidth, preferredHeight);
}

} // namespace

bool IsWindowCaptureSource(const platform::config::MirrorSourceConfig& source) {
    return source.type == platform::config::MirrorSourceType::Window;
}

bool HasConfiguredWindowCaptureSource(const platform::config::MirrorSourceConfig& source) {
    return IsWindowCaptureSource(source) &&
           HasConfiguredRequestIdentity(source.appId,
                                        source.windowTitle,
                                        source.titleMatchMode,
                                        source.fallbackMode,
                                        source.selectionToken);
}

bool IsConfiguredWindowCaptureRequest(const WindowCaptureRequest& request) {
    return HasConfiguredRequestIdentity(request.appId,
                                        request.windowTitle,
                                        request.titleMatchMode,
                                        request.fallbackMode,
                                        request.selectionToken);
}

std::string NormalizeMirrorCaptureAnchor(const std::string& relativeTo) {
    if (relativeTo.empty() || relativeTo == "custom") {
        return "topLeftScreen";
    }
    return relativeTo;
}

std::string MakeWindowCaptureKey(const WindowCaptureRequest& request) {
    if (!request.selectionToken.empty()) {
        return std::string("token\n") + request.selectionToken;
    }

    return request.appId + "\n" +
           request.windowTitle + "\n" +
           std::to_string(static_cast<int>(request.titleMatchMode)) + "\n" +
           std::to_string(static_cast<int>(request.fallbackMode));
}

std::string MakeWindowCaptureKey(const platform::config::MirrorSourceConfig& source) {
    if (!source.selectionToken.empty()) {
        return std::string("token\n") + source.selectionToken;
    }

    return source.appId + "\n" +
           source.windowTitle + "\n" +
           std::to_string(static_cast<int>(source.titleMatchMode)) + "\n" +
           std::to_string(static_cast<int>(source.fallbackMode));
}

std::vector<WindowCaptureRequest> NormalizeWindowCaptureRequests(
    const std::vector<WindowCaptureRequest>& requests) {
    std::vector<WindowCaptureRequest> normalized;
    for (const auto& request : requests) {
        if (!IsConfiguredWindowCaptureRequest(request)) {
            continue;
        }

        const std::string key = MakeWindowCaptureKey(request);
        auto it = std::find_if(normalized.begin(), normalized.end(), [&](const auto& candidate) {
            return MakeWindowCaptureKey(candidate) == key;
        });

        const int clampedFps = std::clamp(request.fps, 1, 240);
        if (it == normalized.end()) {
            WindowCaptureRequest deduped = request;
            deduped.fps = clampedFps;
            normalized.push_back(std::move(deduped));
        } else {
            it->fps = std::max(it->fps, clampedFps);
            if (TitleMatchSpecificity(request.titleMatchMode) < TitleMatchSpecificity(it->titleMatchMode)) {
                it->titleMatchMode = request.titleMatchMode;
            }
            if (request.fallbackMode == platform::config::MirrorSourceFallbackMode::SameApp) {
                it->fallbackMode = request.fallbackMode;
            }
            if (it->selectionToken.empty() && !request.selectionToken.empty()) {
                it->selectionToken = request.selectionToken;
            }
            if (it->preferredWidth <= 0 && request.preferredWidth > 0) {
                it->preferredWidth = request.preferredWidth;
            }
            if (it->preferredHeight <= 0 && request.preferredHeight > 0) {
                it->preferredHeight = request.preferredHeight;
            }
        }
    }
    return normalized;
}

int FindBestMatchingWindowIndex(const std::vector<AvailableWindow>& windows,
                                const std::string& appId,
                                const std::string& windowTitle,
                                platform::config::MirrorSourceTitleMatchMode titleMatchMode,
                                platform::config::MirrorSourceFallbackMode fallbackMode,
                                std::uint64_t preferredWindowId,
                                int preferredWidth,
                                int preferredHeight) {
    int bestIndex = -1;
    int bestScore = -1;
    for (std::size_t index = 0; index < windows.size(); ++index) {
        int score = ScoreWindowMatch(windows[index],
                                     appId,
                                     windowTitle,
                                     titleMatchMode,
                                     fallbackMode,
                                     preferredWidth,
                                     preferredHeight);
        if (score >= 0 && preferredWindowId != 0 && windows[index].windowId == preferredWindowId) {
            score += 6000000;
        }
        if (score > bestScore) {
            bestScore = score;
            bestIndex = static_cast<int>(index);
        }
    }
    return bestIndex;
}

WindowCaptureBackend DetectLinuxWindowCaptureBackendForEnvironment(const char* sessionType,
                                                                   const char* display,
                                                                   const char* waylandDisplay) {
    const std::string session = sessionType ? LowercaseCopy(sessionType) : std::string();
    const bool hasDisplay = display && *display;
    const bool hasWaylandDisplay = waylandDisplay && *waylandDisplay;

    if (session == "wayland") {
        return WindowCaptureBackend::Wayland;
    }
    if (session == "x11") {
        return WindowCaptureBackend::X11;
    }
    if (hasWaylandDisplay) {
        return WindowCaptureBackend::Wayland;
    }
    if (hasDisplay) {
        return WindowCaptureBackend::X11;
    }
    return WindowCaptureBackend::Unknown;
}

bool ShouldDowngradeX11CompositeCapture(bool invalidGeometry,
                                        std::uint32_t consecutiveFailures,
                                        std::uint32_t consecutiveStaleFrames) {
    return invalidGeometry || consecutiveFailures >= 3 || consecutiveStaleFrames >= 6;
}

} // namespace platform::x11
