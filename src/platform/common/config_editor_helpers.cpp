#include "config_editor_helpers.h"

#include <algorithm>
#include <unordered_map>

namespace platform::config {

namespace {

std::string BuildUniqueCopyName(const std::string& sourceName,
                                const std::string& fallbackStem,
                                const std::function<bool(const std::string&)>& exists) {
    const std::string stem = sourceName.empty() ? fallbackStem : sourceName;
    if (!exists(stem)) {
        return stem;
    }

    const std::string baseCopyName = stem + " (Copy)";
    if (!exists(baseCopyName)) {
        return baseCopyName;
    }

    int suffix = 2;
    for (;;) {
        const std::string candidate = stem + " (Copy " + std::to_string(suffix) + ")";
        if (!exists(candidate)) {
            return candidate;
        }
        ++suffix;
    }
}

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
    return GetModesContainingMirrorDirect(config, mirrorId);
}

std::vector<std::string> GetModesContainingMirrorDirect(const LinuxscreenConfig& config, const std::string& mirrorId) {
    std::vector<std::string> modes;
    for (const auto& mode : config.modes) {
        if (IsMirrorInMode(mode, mirrorId)) {
            modes.push_back(mode.name);
        }
    }
    return modes;
}

std::vector<std::string> GetModesContainingGroup(const LinuxscreenConfig& config, const std::string& groupId) {
    std::vector<std::string> modes;
    for (const auto& mode : config.modes) {
        if (IsGroupInMode(mode, groupId)) {
            modes.push_back(mode.name);
        }
    }
    return modes;
}

std::vector<std::string> GetGroupsContainingMirror(const LinuxscreenConfig& config, const std::string& mirrorId) {
    std::vector<std::string> groups;
    for (const auto& group : config.mirrorGroups) {
        const bool containsMirror = std::any_of(group.mirrors.begin(),
                                                group.mirrors.end(),
                                                [&](const MirrorGroupItem& item) {
                                                    return item.mirrorId == mirrorId;
                                                });
        if (containsMirror) {
            groups.push_back(group.name);
        }
    }
    return groups;
}

ModeConfig CreateNewMode(const std::string& name) {
    ModeConfig mode;
    mode.name = name;
    return mode;
}

HotkeyConfig CreateNewHotkey(const std::string& targetMode) {
    HotkeyConfig hk;
    hk.keys = { platform::input::MakeKeyboardBindingKey(37) };
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

std::string MakeUniqueMirrorCopyName(const LinuxscreenConfig& config, const std::string& sourceName) {
    return BuildUniqueCopyName(sourceName, "Mirror", [&](const std::string& name) {
        for (const auto& mirror : config.mirrors) {
            if (mirror.name == name) {
                return true;
            }
        }
        return false;
    });
}

std::string MakeUniqueGroupCopyName(const LinuxscreenConfig& config, const std::string& sourceName) {
    return BuildUniqueCopyName(sourceName, "Group", [&](const std::string& name) {
        for (const auto& group : config.mirrorGroups) {
            if (group.name == name) {
                return true;
            }
        }
        return false;
    });
}

bool TryImportMirrorPreset(LinuxscreenConfig& target,
                           const LinuxscreenConfig& presetSource,
                           const std::string& mirrorName,
                           int& outImportedMirrorIndex) {
    outImportedMirrorIndex = -1;

    const auto presetIt = std::find_if(presetSource.mirrors.begin(),
                                       presetSource.mirrors.end(),
                                       [&](const MirrorConfig& mirror) {
                                           return mirror.name == mirrorName;
                                       });
    if (presetIt == presetSource.mirrors.end()) {
        return false;
    }

    MirrorConfig importedMirror = *presetIt;
    importedMirror.name = MakeUniqueMirrorCopyName(target, importedMirror.name);
    target.mirrors.push_back(std::move(importedMirror));
    outImportedMirrorIndex = static_cast<int>(target.mirrors.size()) - 1;
    return true;
}

bool TryImportGroupPreset(LinuxscreenConfig& target,
                          const LinuxscreenConfig& presetSource,
                          const std::string& groupName,
                          int& outImportedGroupIndex,
                          std::vector<int>& outImportedMirrorIndices) {
    outImportedGroupIndex = -1;
    outImportedMirrorIndices.clear();

    const auto presetGroupIt = std::find_if(presetSource.mirrorGroups.begin(),
                                            presetSource.mirrorGroups.end(),
                                            [&](const MirrorGroupConfig& group) {
                                                return group.name == groupName;
                                            });
    if (presetGroupIt == presetSource.mirrorGroups.end()) {
        return false;
    }

    MirrorGroupConfig importedGroup = *presetGroupIt;
    std::vector<MirrorConfig> importedMirrors;
    std::unordered_map<std::string, std::string> presetMirrorNameMap;

    auto mirrorNameExists = [&](const std::string& name) {
        for (const auto& mirror : target.mirrors) {
            if (mirror.name == name) {
                return true;
            }
        }
        for (const auto& mirror : importedMirrors) {
            if (mirror.name == name) {
                return true;
            }
        }
        return false;
    };

    for (const auto& item : importedGroup.mirrors) {
        if (item.mirrorId.empty() || presetMirrorNameMap.find(item.mirrorId) != presetMirrorNameMap.end()) {
            continue;
        }

        const auto presetMirrorIt = std::find_if(presetSource.mirrors.begin(),
                                                 presetSource.mirrors.end(),
                                                 [&](const MirrorConfig& mirror) {
                                                     return mirror.name == item.mirrorId;
                                                 });
        if (presetMirrorIt == presetSource.mirrors.end()) {
            return false;
        }

        MirrorConfig importedMirror = *presetMirrorIt;
        importedMirror.name = BuildUniqueCopyName(importedMirror.name, "Mirror", mirrorNameExists);
        presetMirrorNameMap[item.mirrorId] = importedMirror.name;
        importedMirrors.push_back(std::move(importedMirror));
    }

    for (auto& item : importedGroup.mirrors) {
        const auto mappingIt = presetMirrorNameMap.find(item.mirrorId);
        if (mappingIt == presetMirrorNameMap.end()) {
            return false;
        }
        item.mirrorId = mappingIt->second;
    }

    importedGroup.name = MakeUniqueGroupCopyName(target, importedGroup.name);

    const int firstImportedMirrorIndex = static_cast<int>(target.mirrors.size());
    target.mirrors.insert(target.mirrors.end(), importedMirrors.begin(), importedMirrors.end());
    outImportedMirrorIndices.reserve(importedMirrors.size());
    for (int i = 0; i < static_cast<int>(importedMirrors.size()); ++i) {
        outImportedMirrorIndices.push_back(firstImportedMirrorIndex + i);
    }

    target.mirrorGroups.push_back(std::move(importedGroup));
    outImportedGroupIndex = static_cast<int>(target.mirrorGroups.size()) - 1;
    return true;
}

} // namespace platform::config
