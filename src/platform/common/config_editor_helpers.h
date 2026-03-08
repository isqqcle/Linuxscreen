#pragma once

#include "linuxscreen_config.h"

#include <functional>
#include <string>
#include <vector>

namespace platform::config {

// Ensure mode.layers and legacy mirror/group lists stay synchronized.
void SyncModeLayerLegacyLists(ModeConfig& mode);

bool IsLayerInMode(const ModeConfig& mode, ModeLayerType layerType, const std::string& layerId);
void AddLayerToMode(ModeConfig& mode, ModeLayerType layerType, const std::string& layerId);
void RemoveLayerFromMode(ModeConfig& mode, ModeLayerType layerType, const std::string& layerId);

bool IsMirrorInMode(const ModeConfig& mode, const std::string& mirrorId);

bool IsGroupInMode(const ModeConfig& mode, const std::string& groupId);

void AddMirrorToMode(ModeConfig& mode, const std::string& mirrorId);
void RemoveMirrorFromMode(ModeConfig& mode, const std::string& mirrorId);

void AddGroupToMode(ModeConfig& mode, const std::string& groupId);
void RemoveGroupFromMode(ModeConfig& mode, const std::string& groupId);

std::vector<std::string> GetModesContainingMirror(const LinuxscreenConfig& config, const std::string& mirrorId);

ModeConfig CreateNewMode(const std::string& name);

HotkeyConfig CreateNewHotkey(const std::string& targetMode);

// Rename helpers that preserve config references.
void RenameMirror(LinuxscreenConfig& config, const std::string& oldName, const std::string& newName);
void RenameGroup(LinuxscreenConfig& config, const std::string& oldName, const std::string& newName);
void RenameMirrorInModeLayers(ModeConfig& mode, const std::string& oldName, const std::string& newName);
void RenameGroupInModeLayers(ModeConfig& mode, const std::string& oldName, const std::string& newName);

void RemoveMirrorReferences(LinuxscreenConfig& config, const std::string& mirrorId);
void RenameModeInHotkeys(LinuxscreenConfig& config, const std::string& oldName, const std::string& newName);
void RemoveModeFromHotkeys(LinuxscreenConfig& config, const std::string& modeName);

} // namespace platform::config
