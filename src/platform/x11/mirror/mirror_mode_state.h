#pragma once

#include "../../common/linuxscreen_config.h"

#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace platform::x11 {

struct ResolvedMirrorRender {
    config::MirrorConfig config;
    // config.output.x/y/scale/relativeTo reflect final values after any group override.
    // config.captureWidth/captureHeight reflect final values after any group per-item sizing.
};

class MirrorModeState {
public:
    void ApplyModeSwitch(const std::string& modeName,
                         const config::LinuxscreenConfig& config,
                         int preferredScreenWidth = 0,
                         int preferredScreenHeight = 0);

    std::string GetActiveModeName() const;

    std::vector<ResolvedMirrorRender> GetActiveMirrorRenderList() const;

    std::shared_ptr<const config::LinuxscreenConfig> GetConfigSnapshot() const;

    static void CalculateModeDimensions(const config::ModeConfig& mode,
                                        int viewportW,
                                        int viewportH,
                                        int& outWidth,
                                        int& outHeight);

    // Get target dimensions for the active mode
    // Returns true if dimensions are valid and different from current
    bool GetActiveModeTargetDimensions(int viewportW, int viewportH, int& outWidth, int& outHeight) const;

    void Reset();

private:
    void ApplyModeSwitchLocked(const std::string& modeName,
                               const config::LinuxscreenConfig& config,
                               int preferredScreenWidth,
                               int preferredScreenHeight);

    mutable std::mutex mutex_;
    std::string activeModeName_;
    std::vector<ResolvedMirrorRender> activeMirrors_;
    std::shared_ptr<const config::LinuxscreenConfig> configSnapshot_;
};

} // namespace platform::x11
