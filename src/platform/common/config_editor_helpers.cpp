#include "config_editor_helpers.h"

#include <algorithm>

namespace platform::config {

namespace {

void EnsureModeLayersFromLegacyLists(ModeConfig& mode) {
    if (!mode.layers.empty()) {
        return;
    }

    mode.layers.reserve(mode.mirrorIds.size() + mode.groupIds.size());
    for (const auto& mirrorId : mode.mirrorIds) {
        if (mirrorId.empty()) {
            continue;
        }
        mode.layers.push_back(ModeLayerConfig{ModeLayerType::Mirror, mirrorId, true});
    }
    for (const auto& groupId : mode.groupIds) {
        if (groupId.empty()) {
            continue;
        }
        mode.layers.push_back(ModeLayerConfig{ModeLayerType::Group, groupId, true});
    }
}

void RebuildLegacyListsFromLayers(ModeConfig& mode) {
    mode.mirrorIds.clear();
    mode.groupIds.clear();
    mode.mirrorIds.reserve(mode.layers.size());
    mode.groupIds.reserve(mode.layers.size());

    for (const auto& layer : mode.layers) {
        if (layer.id.empty()) {
            continue;
        }
        if (layer.type == ModeLayerType::Mirror) {
            mode.mirrorIds.push_back(layer.id);
        } else {
            mode.groupIds.push_back(layer.id);
        }
    }
}

} // namespace

void SyncModeLayerLegacyLists(ModeConfig& mode) {
    EnsureModeLayersFromLegacyLists(mode);
    RebuildLegacyListsFromLayers(mode);
}

bool IsLayerInMode(const ModeConfig& mode, ModeLayerType layerType, const std::string& layerId) {
    if (layerId.empty()) {
        return false;
    }

    if (!mode.layers.empty()) {
        for (const auto& layer : mode.layers) {
            if (layer.type == layerType && layer.id == layerId) {
                return true;
            }
        }
        return false;
    }

    const std::vector<std::string>& ids = (layerType == ModeLayerType::Mirror) ? mode.mirrorIds : mode.groupIds;
    for (const auto& id : ids) {
        if (id == layerId) {
            return true;
        }
    }
    return false;
}

void AddLayerToMode(ModeConfig& mode, ModeLayerType layerType, const std::string& layerId) {
    if (layerId.empty()) {
        return;
    }

    EnsureModeLayersFromLegacyLists(mode);
    if (!IsLayerInMode(mode, layerType, layerId)) {
        mode.layers.push_back(ModeLayerConfig{layerType, layerId, true});
    }
    RebuildLegacyListsFromLayers(mode);
}

void RemoveLayerFromMode(ModeConfig& mode, ModeLayerType layerType, const std::string& layerId) {
    if (layerId.empty()) {
        return;
    }

    EnsureModeLayersFromLegacyLists(mode);
    mode.layers.erase(
        std::remove_if(mode.layers.begin(),
                       mode.layers.end(),
                       [&](const ModeLayerConfig& layer) {
                           return layer.type == layerType && layer.id == layerId;
                       }),
        mode.layers.end());
    RebuildLegacyListsFromLayers(mode);
}

bool IsMirrorInMode(const ModeConfig& mode, const std::string& mirrorId) {
    return IsLayerInMode(mode, ModeLayerType::Mirror, mirrorId);
}

bool IsGroupInMode(const ModeConfig& mode, const std::string& groupId) {
    return IsLayerInMode(mode, ModeLayerType::Group, groupId);
}

void AddMirrorToMode(ModeConfig& mode, const std::string& mirrorId) {
    AddLayerToMode(mode, ModeLayerType::Mirror, mirrorId);
}

void RemoveMirrorFromMode(ModeConfig& mode, const std::string& mirrorId) {
    RemoveLayerFromMode(mode, ModeLayerType::Mirror, mirrorId);
}

void AddGroupToMode(ModeConfig& mode, const std::string& groupId) {
    AddLayerToMode(mode, ModeLayerType::Group, groupId);
}

void RemoveGroupFromMode(ModeConfig& mode, const std::string& groupId) {
    RemoveLayerFromMode(mode, ModeLayerType::Group, groupId);
}

std::vector<std::string> GetModesContainingMirror(const LinuxscreenConfig& config, const std::string& mirrorId) {
    std::vector<std::string> modes;
    for (const auto& mode : config.modes) {
        if (IsMirrorInMode(mode, mirrorId)) {
            modes.push_back(mode.name);
        }
    }
    return modes;
}

ModeConfig CreateNewMode(const std::string& name) {
    ModeConfig mode;
    mode.name = name;
    return mode;
}

HotkeyConfig CreateNewHotkey(const std::string& targetMode) {
    HotkeyConfig hk;
    hk.keys = { 0x11 };  // Default to Ctrl key (VK_CONTROL)
    hk.mainMode = targetMode;
    hk.secondaryMode = "";
    hk.returnMode = "";
    hk.debounce = 100;
    hk.triggerOnRelease = false;
    hk.triggerOnHold = false;
    hk.blockKeyFromGame = false;
    return hk;
}

void RenameMirrorInModeLayers(ModeConfig& mode, const std::string& oldName, const std::string& newName) {
    if (oldName.empty() || oldName == newName) {
        return;
    }

    EnsureModeLayersFromLegacyLists(mode);
    for (auto& layer : mode.layers) {
        if (layer.type == ModeLayerType::Mirror && layer.id == oldName) {
            layer.id = newName;
        }
    }
    RebuildLegacyListsFromLayers(mode);
}

void RenameGroupInModeLayers(ModeConfig& mode, const std::string& oldName, const std::string& newName) {
    if (oldName.empty() || oldName == newName) {
        return;
    }

    EnsureModeLayersFromLegacyLists(mode);
    for (auto& layer : mode.layers) {
        if (layer.type == ModeLayerType::Group && layer.id == oldName) {
            layer.id = newName;
        }
    }
    RebuildLegacyListsFromLayers(mode);
}

void RenameMirror(LinuxscreenConfig& config, const std::string& oldName, const std::string& newName) {
    for (auto& mirror : config.mirrors) {
        if (mirror.name == oldName) {
            mirror.name = newName;
            break;
        }
    }
    
    for (auto& mode : config.modes) {
        RenameMirrorInModeLayers(mode, oldName, newName);
    }
    
    for (auto& group : config.mirrorGroups) {
        for (auto& item : group.mirrors) {
            if (item.mirrorId == oldName) {
                item.mirrorId = newName;
            }
        }
    }
}

void RenameGroup(LinuxscreenConfig& config, const std::string& oldName, const std::string& newName) {
    if (oldName.empty() || oldName == newName) {
        return;
    }

    for (auto& group : config.mirrorGroups) {
        if (group.name == oldName) {
            group.name = newName;
            break;
        }
    }

    for (auto& mode : config.modes) {
        RenameGroupInModeLayers(mode, oldName, newName);
    }
}

void RemoveMirrorReferences(LinuxscreenConfig& config, const std::string& mirrorId) {
    if (mirrorId.empty()) {
        return;
    }

    for (auto& mode : config.modes) {
        RemoveMirrorFromMode(mode, mirrorId);
    }

    for (auto& group : config.mirrorGroups) {
        group.mirrors.erase(std::remove_if(group.mirrors.begin(),
                                           group.mirrors.end(),
                                           [&](const MirrorGroupItem& item) {
                                               return item.mirrorId == mirrorId;
                                           }),
                            group.mirrors.end());
    }
}

void RenameModeInHotkeys(LinuxscreenConfig& config, const std::string& oldName, const std::string& newName) {
    if (oldName.empty() || oldName == newName) {
        return;
    }

    for (auto& hotkey : config.hotkeys) {
        if (hotkey.mainMode == oldName) {
            hotkey.mainMode = newName;
        }
        if (hotkey.secondaryMode == oldName) {
            hotkey.secondaryMode = newName;
        }
        if (hotkey.returnMode == oldName) {
            hotkey.returnMode = newName;
        }
        for (auto& alt : hotkey.altSecondaryModes) {
            if (alt.mode == oldName) {
                alt.mode = newName;
            }
        }
    }
}

void RemoveModeFromHotkeys(LinuxscreenConfig& config, const std::string& modeName) {
    if (modeName.empty()) {
        return;
    }

    for (auto& hotkey : config.hotkeys) {
        if (hotkey.mainMode == modeName) {
            hotkey.mainMode.clear();
            hotkey.secondaryMode.clear();
            hotkey.altSecondaryModes.clear();
            continue;
        }

        if (hotkey.secondaryMode == modeName) {
            hotkey.secondaryMode.clear();
        }
        if (hotkey.returnMode == modeName) {
            hotkey.returnMode.clear();
        }

        hotkey.altSecondaryModes.erase(std::remove_if(hotkey.altSecondaryModes.begin(),
                                                      hotkey.altSecondaryModes.end(),
                                                      [&](const AltSecondaryModeConfig& alt) {
                                                          return alt.mode == modeName;
                                                      }),
                                       hotkey.altSecondaryModes.end());
    }
}

} // namespace platform::config
