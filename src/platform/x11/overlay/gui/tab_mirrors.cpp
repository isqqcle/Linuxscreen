#include "../overlay_internal.h"
#include "../../calc_overlay_runtime.h"
#include "imgui_overlay_helpers.h"
#include "tab_eyezoom.h"
#include "tab_mirrors.h"
#include "tab_mirrors_helpers.h"
#include "tab_mirrors_state.h"

#include <cmath>
#include <thread>

namespace platform::x11 {

MirrorEditorState g_mirrorEditorState;

namespace {

constexpr float kDirectEditDragThreshold = 3.0f;
constexpr float kDirectEditHandleRadius = 7.0f;
constexpr float kDirectEditOutputScaleMin = 0.01f;
constexpr float kDirectEditOutputScaleMax = 100.0f;
constexpr float kGroupItemPercentMin = 0.1f;
constexpr float kGroupItemPercentMax = 10.0f;

enum MirrorVisualEdgeMask {
    kMirrorVisualEdgeNone = 0,
    kMirrorVisualEdgeLeft = 1 << 0,
    kMirrorVisualEdgeRight = 1 << 1,
    kMirrorVisualEdgeTop = 1 << 2,
    kMirrorVisualEdgeBottom = 1 << 3,
};

struct MirrorDirectEditViewportContext {
    bool hasDisplay = false;
    float displayWidth = 0.0f;
    float displayHeight = 0.0f;
    bool hasModeViewport = false;
    float modeViewportX = 0.0f;
    float modeViewportY = 0.0f;
    float modeViewportWidth = 0.0f;
    float modeViewportHeight = 0.0f;
    const platform::config::ModeConfig* activeMode = nullptr;
};

struct DirectEditResolvedItem {
    ResolvedMirrorRender resolved;
    ImRect rect;
};

struct DirectEditGroupTarget {
    std::string groupId;
    ImRect rect;
};

enum class DirectEditHitTargetKind {
    Item = 0,
    Group = 1,
};

struct DirectEditHitTarget {
    DirectEditHitTargetKind kind = DirectEditHitTargetKind::Item;
    MirrorEditorState::DirectEditSelection selection;
    ImRect rect;
    int itemIndex = -1;
    std::string key;
};

void SyncMirrorDirectEditSelectionToSidebar(const platform::config::LinuxscreenConfig& config);
std::string BuildDirectEditSelectionKey(const MirrorEditorState::DirectEditSelection& selection) {
    std::string key = selection.mirrorId;
    key += "|";
    key += selection.groupId;
    key += "|";
    key += std::to_string(selection.groupItemIndex);
    key += "|";
    key += std::to_string(static_cast<int>(selection.kind));
    return key;
}

std::string BuildDirectEditCycleKey(const MirrorEditorState::DirectEditSelection& selection) {
    if (selection.kind == MirrorDirectEditSelectionKind::Group) {
        return std::string("group|") + selection.groupId;
    }
    return BuildDirectEditSelectionKey(selection);
}

bool ResolveDirectEditGroupRect(const std::vector<DirectEditResolvedItem>& items,
                                const std::string& groupId,
                                ImRect& outRect) {
    bool found = false;
    for (const auto& item : items) {
        if (item.resolved.sourceGroupId != groupId) {
            continue;
        }
        if (!found) {
            outRect = item.rect;
            found = true;
            continue;
        }
        outRect.Add(item.rect.Min);
        outRect.Add(item.rect.Max);
    }
    return found;
}

std::string ResolveDirectEditGroupMirrorId(const std::vector<DirectEditResolvedItem>& items,
                                           const MirrorEditorState::DirectEditSelection& currentSelection,
                                           const std::string& groupId) {
    if (!groupId.empty() &&
        currentSelection.groupId == groupId &&
        !currentSelection.mirrorId.empty()) {
        for (const auto& item : items) {
            if (item.resolved.sourceGroupId == groupId &&
                item.resolved.sourceMirrorId == currentSelection.mirrorId) {
                return currentSelection.mirrorId;
            }
        }
    }

    for (const auto& item : items) {
        if (item.resolved.sourceGroupId == groupId && !item.resolved.sourceMirrorId.empty()) {
            return item.resolved.sourceMirrorId;
        }
    }

    return {};
}

MirrorEditorState::DirectEditSelection MakeDirectEditSelectionFromItem(const DirectEditResolvedItem& item) {
    MirrorEditorState::DirectEditSelection selection;
    selection.kind = (item.resolved.sourceKind == ResolvedMirrorSourceKind::GroupItem)
        ? MirrorDirectEditSelectionKind::GroupItem
        : MirrorDirectEditSelectionKind::Mirror;
    selection.mirrorId = item.resolved.sourceMirrorId;
    selection.groupId = item.resolved.sourceGroupId;
    selection.groupItemIndex = item.resolved.sourceGroupItemIndex;
    return selection;
}

bool IsDirectEditSelectionVisible(const MirrorEditorState::DirectEditSelection& selection,
                                  const std::vector<DirectEditResolvedItem>& items,
                                  const std::vector<DirectEditGroupTarget>& groupTargets) {
    if (selection.kind == MirrorDirectEditSelectionKind::Group) {
        for (const auto& groupTarget : groupTargets) {
            if (groupTarget.groupId == selection.groupId) {
                return true;
            }
        }
        return false;
    }

    for (const auto& item : items) {
        if (selection.kind == MirrorDirectEditSelectionKind::Mirror &&
            item.resolved.sourceKind == ResolvedMirrorSourceKind::Mirror &&
            item.resolved.sourceMirrorId == selection.mirrorId) {
            return true;
        }
        if (selection.kind == MirrorDirectEditSelectionKind::GroupItem &&
            item.resolved.sourceKind == ResolvedMirrorSourceKind::GroupItem &&
            item.resolved.sourceMirrorId == selection.mirrorId &&
            item.resolved.sourceGroupId == selection.groupId &&
            item.resolved.sourceGroupItemIndex == selection.groupItemIndex) {
            return true;
        }
    }
    return false;
}

int FindMirrorGroupIndexByName(const platform::config::LinuxscreenConfig& config, const std::string& groupId) {
    for (std::size_t i = 0; i < config.mirrorGroups.size(); ++i) {
        if (config.mirrorGroups[i].name == groupId) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

std::vector<std::string> GetRelevantModesForDirectEditSelection(const platform::config::LinuxscreenConfig& config,
                                                                const MirrorEditorState::DirectEditSelection& selection) {
    if (selection.kind == MirrorDirectEditSelectionKind::Mirror && !selection.mirrorId.empty()) {
        return platform::config::GetModesContainingMirrorDirect(config, selection.mirrorId);
    }
    if ((selection.kind == MirrorDirectEditSelectionKind::GroupItem ||
         selection.kind == MirrorDirectEditSelectionKind::Group) &&
        !selection.groupId.empty()) {
        return platform::config::GetModesContainingGroup(config, selection.groupId);
    }
    return {};
}

void RefreshDirectEditSelectionForCurrentMode(const platform::config::LinuxscreenConfig& config,
                                              const std::vector<DirectEditResolvedItem>& items,
                                              const std::vector<DirectEditGroupTarget>& groupTargets) {
    auto& selection = g_mirrorEditorState.directEditSelection;
    const auto selectionVisible = [&]() {
        return IsDirectEditSelectionVisible(selection, items, groupTargets);
    };

        if (selection.kind == MirrorDirectEditSelectionKind::Group && selectionVisible()) {
            selection.mirrorId = ResolveDirectEditGroupMirrorId(items, selection, selection.groupId);
        } else if (!selectionVisible()) {
            if (!items.empty()) {
                selection = MakeDirectEditSelectionFromItem(items.front());
                g_mirrorEditorState.directEditShowGroupInspector = false;
                SyncMirrorDirectEditSelectionToSidebar(config);
            } else {
                selection = MirrorEditorState::DirectEditSelection{};
                g_mirrorEditorState.directEditShowGroupInspector = false;
            }
            g_mirrorEditorState.directEditCycleIndex = 0;
            g_mirrorEditorState.directEditLastCycleStackKey.clear();
        }

        if (selection.kind == MirrorDirectEditSelectionKind::Group) {
            g_mirrorEditorState.directEditShowGroupInspector = true;
        } else if (selection.kind != MirrorDirectEditSelectionKind::GroupItem) {
            g_mirrorEditorState.directEditShowGroupInspector = false;
        }

    if (!IsDirectEditSelectionVisible(g_mirrorEditorState.directEditPendingClickSelection, items, groupTargets)) {
        g_mirrorEditorState.directEditPendingClickSelection = MirrorEditorState::DirectEditSelection{};
        g_mirrorEditorState.directEditHasPendingClickSelection = false;
    }

    const platform::config::MirrorConfig* selectedMirror = nullptr;
    if (!selection.mirrorId.empty()) {
        for (const auto& mirror : config.mirrors) {
            if (mirror.name == selection.mirrorId) {
                selectedMirror = &mirror;
                break;
            }
        }
    }
    if (!selectedMirror || selectedMirror->input.empty()) {
        g_mirrorEditorState.directEditSelectedCaptureZoneIndex = 0;
    } else {
        g_mirrorEditorState.directEditSelectedCaptureZoneIndex =
            std::clamp(g_mirrorEditorState.directEditSelectedCaptureZoneIndex,
                       0,
                       static_cast<int>(selectedMirror->input.size()) - 1);
    }
}

void CaptureDirectEditGroupStartState(MirrorEditorState::VisualDragState& dragState,
                                      const platform::config::MirrorGroupConfig& group,
                                      const std::vector<DirectEditResolvedItem>& items) {
    dragState.startGroupWidthsPercent.clear();
    dragState.startGroupHeightsPercent.clear();
    dragState.startGroupOffsetsX.clear();
    dragState.startGroupOffsetsY.clear();
    dragState.startGroupRects.clear();
    dragState.startGroupWidthsPercent.reserve(group.mirrors.size());
    dragState.startGroupHeightsPercent.reserve(group.mirrors.size());
    dragState.startGroupOffsetsX.reserve(group.mirrors.size());
    dragState.startGroupOffsetsY.reserve(group.mirrors.size());
    dragState.startGroupRects.resize(group.mirrors.size(), ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    for (const auto& item : group.mirrors) {
        dragState.startGroupWidthsPercent.push_back(item.widthPercent);
        dragState.startGroupHeightsPercent.push_back(item.heightPercent);
        dragState.startGroupOffsetsX.push_back(item.offsetX);
        dragState.startGroupOffsetsY.push_back(item.offsetY);
    }
    for (const auto& resolvedItem : items) {
        if (resolvedItem.resolved.sourceGroupId != group.name ||
            resolvedItem.resolved.sourceGroupItemIndex < 0 ||
            resolvedItem.resolved.sourceGroupItemIndex >= static_cast<int>(dragState.startGroupRects.size())) {
            continue;
        }
        dragState.startGroupRects[static_cast<std::size_t>(resolvedItem.resolved.sourceGroupItemIndex)] =
            ImVec4(resolvedItem.rect.Min.x, resolvedItem.rect.Min.y, resolvedItem.rect.Max.x, resolvedItem.rect.Max.y);
    }
}

bool ResolveMirrorSourceSizeForDirectEdit(const platform::config::MirrorConfig& mirror,
                                          const MirrorDirectEditViewportContext& ctx,
                                          float& outWidth,
                                          float& outHeight);
bool ResolveCropRectInSourceForDirectEdit(const platform::config::MirrorConfig& mirror,
                                          const platform::config::MirrorCaptureConfig& zone,
                                          const MirrorDirectEditViewportContext& ctx,
                                          float sourceWidth,
                                          float sourceHeight,
                                          ImRect& outRect);

MirrorDirectEditViewportContext BuildMirrorDirectEditViewportContext(const platform::config::LinuxscreenConfig& config,
                                                                     float displayWidth,
                                                                     float displayHeight) {
    MirrorDirectEditViewportContext ctx;
    ctx.hasDisplay = displayWidth > 0.0f && displayHeight > 0.0f;
    ctx.displayWidth = displayWidth;
    ctx.displayHeight = displayHeight;
    if (!ctx.hasDisplay) {
        return ctx;
    }

    const std::string activeModeName = GetMirrorModeState().GetActiveModeName();
    for (const auto& mode : config.modes) {
        if (mode.name == activeModeName) {
            ctx.activeMode = &mode;
            break;
        }
    }
    if (!ctx.activeMode) {
        return ctx;
    }

    int modeWidth = 0;
    int modeHeight = 0;
    MirrorModeState::CalculateModeDimensions(*ctx.activeMode,
                                             static_cast<int>(displayWidth),
                                             static_cast<int>(displayHeight),
                                             modeWidth,
                                             modeHeight);
    if (modeWidth <= 0 || modeHeight <= 0) {
        return ctx;
    }

    std::string anchorPreset = ctx.activeMode->positionPreset.empty() ? "topLeftScreen" : ctx.activeMode->positionPreset;
    if (anchorPreset == "custom") {
        anchorPreset = "topLeftScreen";
    }

    int viewportX = 0;
    int viewportY = 0;
    platform::config::GetRelativeCoords(anchorPreset,
                                        ctx.activeMode->x,
                                        ctx.activeMode->y,
                                        modeWidth,
                                        modeHeight,
                                        static_cast<int>(displayWidth),
                                        static_cast<int>(displayHeight),
                                        viewportX,
                                        viewportY);
    ctx.hasModeViewport = true;
    ctx.modeViewportX = static_cast<float>(viewportX);
    ctx.modeViewportY = static_cast<float>(viewportY);
    ctx.modeViewportWidth = static_cast<float>(modeWidth);
    ctx.modeViewportHeight = static_cast<float>(modeHeight);
    return ctx;
}

bool ResolveOutputContainerForDirectEdit(const platform::config::MirrorRenderConfig& output,
                                         const MirrorDirectEditViewportContext& ctx,
                                         float& outWidth,
                                         float& outHeight,
                                         float& outOffsetX,
                                         float& outOffsetY) {
    if (!ctx.hasDisplay) {
        outWidth = 0.0f;
        outHeight = 0.0f;
        outOffsetX = 0.0f;
        outOffsetY = 0.0f;
        return false;
    }

    outWidth = ctx.displayWidth;
    outHeight = ctx.displayHeight;
    outOffsetX = 0.0f;
    outOffsetY = 0.0f;
    if (ctx.hasModeViewport && ShouldUseViewportRelativeTo(output.relativeTo)) {
        outWidth = ctx.modeViewportWidth;
        outHeight = ctx.modeViewportHeight;
        outOffsetX = ctx.modeViewportX;
        outOffsetY = ctx.modeViewportY;
    }
    return outWidth > 0.0f && outHeight > 0.0f;
}

bool ResolveDirectEditOutputContainerSize(const platform::config::MirrorRenderConfig& output,
                                          const MirrorDirectEditViewportContext& ctx,
                                          float& outWidth,
                                          float& outHeight) {
    float outOffsetX = 0.0f;
    float outOffsetY = 0.0f;
    return ResolveOutputContainerForDirectEdit(output, ctx, outWidth, outHeight, outOffsetX, outOffsetY);
}

void UpdateDirectEditPixelsFromRelative(platform::config::MirrorRenderConfig& output,
                                        const MirrorDirectEditViewportContext& ctx);

void ResolveDirectEditOutputPixels(const platform::config::MirrorRenderConfig& output,
                                   const MirrorDirectEditViewportContext& ctx,
                                   int& outX,
                                   int& outY) {
    platform::config::MirrorRenderConfig resolved = output;
    if (resolved.useRelativePosition) {
        UpdateDirectEditPixelsFromRelative(resolved, ctx);
    }
    outX = resolved.x;
    outY = resolved.y;
}

void UpdateDirectEditRelativeFromPixels(platform::config::MirrorRenderConfig& output,
                                        const MirrorDirectEditViewportContext& ctx) {
    float containerWidth = 0.0f;
    float containerHeight = 0.0f;
    if (!ResolveDirectEditOutputContainerSize(output, ctx, containerWidth, containerHeight) ||
        !(containerWidth > 0.0f) ||
        !(containerHeight > 0.0f)) {
        return;
    }
    output.relativeX = static_cast<float>(output.x) / containerWidth;
    output.relativeY = static_cast<float>(output.y) / containerHeight;
}

void UpdateDirectEditPixelsFromRelative(platform::config::MirrorRenderConfig& output,
                                        const MirrorDirectEditViewportContext& ctx) {
    float containerWidth = 0.0f;
    float containerHeight = 0.0f;
    if (!ResolveDirectEditOutputContainerSize(output, ctx, containerWidth, containerHeight) ||
        !(containerWidth > 0.0f) ||
        !(containerHeight > 0.0f)) {
        return;
    }
    output.x = static_cast<int>(output.relativeX * containerWidth);
    output.y = static_cast<int>(output.relativeY * containerHeight);
}

void UpdateDirectEditMirrorRelativeSizeFromScale(platform::config::MirrorConfig& mirror,
                                                 const MirrorDirectEditViewportContext& ctx) {
    float containerWidth = 0.0f;
    float containerHeight = 0.0f;
    if (!ResolveDirectEditOutputContainerSize(mirror.output, ctx, containerWidth, containerHeight)) {
        return;
    }

    const float scaleX = mirror.output.separateScale ? mirror.output.scaleX : mirror.output.scale;
    const float scaleY = mirror.output.separateScale ? mirror.output.scaleY : mirror.output.scale;
    const int border = platform::config::GetMirrorDynamicBorderPadding(mirror.border);
    const float baseWidth = static_cast<float>(mirror.captureWidth + (2 * border));
    const float baseHeight = static_cast<float>(mirror.captureHeight + (2 * border));
    if (!(baseWidth > 0.0f) || !(baseHeight > 0.0f) || !(containerWidth > 0.0f) || !(containerHeight > 0.0f)) {
        return;
    }

    mirror.output.relativeWidth = std::clamp((baseWidth * scaleX) / containerWidth, kDirectEditOutputScaleMin, kDirectEditOutputScaleMax);
    mirror.output.relativeHeight = std::clamp((baseHeight * scaleY) / containerHeight, kDirectEditOutputScaleMin, kDirectEditOutputScaleMax);
}

void UpdateDirectEditMirrorScaleFromRelativeSize(platform::config::MirrorConfig& mirror,
                                                 const MirrorDirectEditViewportContext& ctx) {
    float containerWidth = 0.0f;
    float containerHeight = 0.0f;
    if (!ResolveDirectEditOutputContainerSize(mirror.output, ctx, containerWidth, containerHeight)) {
        return;
    }

    const int border = platform::config::GetMirrorDynamicBorderPadding(mirror.border);
    const float baseWidth = static_cast<float>(mirror.captureWidth + (2 * border));
    const float baseHeight = static_cast<float>(mirror.captureHeight + (2 * border));
    if (!(baseWidth > 0.0f) || !(baseHeight > 0.0f)) {
        return;
    }

    const float scaleX = std::clamp((containerWidth * mirror.output.relativeWidth) / baseWidth,
                                    kDirectEditOutputScaleMin,
                                    kDirectEditOutputScaleMax);
    const float scaleY = std::clamp((containerHeight * mirror.output.relativeHeight) / baseHeight,
                                    kDirectEditOutputScaleMin,
                                    kDirectEditOutputScaleMax);
    if (mirror.output.preserveAspectRatio) {
        const float uniformScale = std::clamp(ResolveUniformScaleByFitMode(scaleX,
                                                                           scaleY,
                                                                           NormalizeAspectFitMode(mirror.output.aspectFitMode)),
                                              kDirectEditOutputScaleMin,
                                              kDirectEditOutputScaleMax);
        mirror.output.separateScale = false;
        mirror.output.scale = uniformScale;
        mirror.output.scaleX = uniformScale;
        mirror.output.scaleY = uniformScale;
    } else {
        mirror.output.separateScale = true;
        mirror.output.scale = scaleX;
        mirror.output.scaleX = scaleX;
        mirror.output.scaleY = scaleY;
    }
}

bool GetDirectEditMirrorUniformScale(const platform::config::MirrorConfig& mirror, float& outScale) {
    const float scaleX = std::clamp(mirror.output.separateScale ? mirror.output.scaleX : mirror.output.scale,
                                    kDirectEditOutputScaleMin,
                                    kDirectEditOutputScaleMax);
    const float scaleY = std::clamp(mirror.output.separateScale ? mirror.output.scaleY : mirror.output.scale,
                                    kDirectEditOutputScaleMin,
                                    kDirectEditOutputScaleMax);
    outScale = std::clamp(ResolveUniformScaleByFitMode(scaleX,
                                                       scaleY,
                                                       NormalizeAspectFitMode(mirror.output.aspectFitMode)),
                          kDirectEditOutputScaleMin,
                          kDirectEditOutputScaleMax);
    return true;
}

void SetDirectEditMirrorUniformScale(platform::config::MirrorConfig& mirror, float scale) {
    const float clampedScale = std::clamp(scale, kDirectEditOutputScaleMin, kDirectEditOutputScaleMax);
    mirror.output.separateScale = false;
    mirror.output.scale = clampedScale;
    mirror.output.scaleX = clampedScale;
    mirror.output.scaleY = clampedScale;
}

void ResolveAnchorOffsetsFromRect(const std::string& relativeTo,
                                  float left,
                                  float top,
                                  float width,
                                  float height,
                                  float containerWidth,
                                  float containerHeight,
                                  float& outX,
                                  float& outY) {
    const std::string anchorBase = GetRelativeToAnchorBase(relativeTo);
    outX = left;
    outY = top;

    if (anchorBase == "topRight") {
        outX = containerWidth - left - width;
        return;
    }
    if (anchorBase == "topCenter") {
        outX = left - ((containerWidth - width) * 0.5f);
        return;
    }
    if (anchorBase == "bottomLeft") {
        outY = containerHeight - top - height;
        return;
    }
    if (anchorBase == "bottomRight") {
        outX = containerWidth - left - width;
        outY = containerHeight - top - height;
        return;
    }
    if (anchorBase == "bottomCenter") {
        outX = left - ((containerWidth - width) * 0.5f);
        outY = containerHeight - top - height;
        return;
    }
    if (anchorBase == "center") {
        outX = left - ((containerWidth - width) * 0.5f);
        outY = top - ((containerHeight - height) * 0.5f);
        return;
    }
    if (anchorBase == "middleLeft") {
        outY = top - ((containerHeight - height) * 0.5f);
        return;
    }
    if (anchorBase == "middleRight") {
        outX = containerWidth - left - width;
        outY = top - ((containerHeight - height) * 0.5f);
        return;
    }
    if (anchorBase == "pieLeft" || anchorBase == "pieRight") {
        const float pieBaseX = (anchorBase == "pieLeft") ? (containerWidth - 92.0f) : (containerWidth - 36.0f);
        const float pieBaseY = containerHeight - 220.0f;
        outX = left - pieBaseX;
        outY = top - pieBaseY;
    }
}

bool ResolveOutputRectForDirectEdit(const platform::config::MirrorConfig& mirror,
                                    const MirrorDirectEditViewportContext& ctx,
                                    ImRect& outRect) {
    float containerWidth = 0.0f;
    float containerHeight = 0.0f;
    float containerOffsetX = 0.0f;
    float containerOffsetY = 0.0f;
    if (!ResolveOutputContainerForDirectEdit(mirror.output,
                                             ctx,
                                             containerWidth,
                                             containerHeight,
                                             containerOffsetX,
                                             containerOffsetY)) {
        return false;
    }

    const int dynamicBorder = platform::config::GetMirrorDynamicBorderPadding(mirror.border);
    const float baseWidth = static_cast<float>(mirror.captureWidth + (2 * dynamicBorder));
    const float baseHeight = static_cast<float>(mirror.captureHeight + (2 * dynamicBorder));
    if (!(baseWidth > 0.0f) || !(baseHeight > 0.0f)) {
        return false;
    }

    float width = 0.0f;
    float height = 0.0f;
    const float scaleX = mirror.output.separateScale ? mirror.output.scaleX : mirror.output.scale;
    const float scaleY = mirror.output.separateScale ? mirror.output.scaleY : mirror.output.scale;
    if (mirror.output.preserveAspectRatio) {
        const float uniformScale = std::clamp(ResolveUniformScaleByFitMode(scaleX,
                                                                           scaleY,
                                                                           NormalizeAspectFitMode(mirror.output.aspectFitMode)),
                                              kDirectEditOutputScaleMin,
                                              kDirectEditOutputScaleMax);
        width = baseWidth * uniformScale;
        height = baseHeight * uniformScale;
    } else {
        width = baseWidth * std::clamp(scaleX, kDirectEditOutputScaleMin, kDirectEditOutputScaleMax);
        height = baseHeight * std::clamp(scaleY, kDirectEditOutputScaleMin, kDirectEditOutputScaleMax);
    }

    int topLeftX = 0;
    int topLeftY = 0;
    platform::config::GetRelativeCoords(mirror.output.relativeTo,
                                        mirror.output.x,
                                        mirror.output.y,
                                        static_cast<int>(std::round(width)),
                                        static_cast<int>(std::round(height)),
                                        static_cast<int>(std::round(containerWidth)),
                                        static_cast<int>(std::round(containerHeight)),
                                        topLeftX,
                                        topLeftY);
    outRect = ImRect(ImVec2(containerOffsetX + static_cast<float>(topLeftX),
                            containerOffsetY + static_cast<float>(topLeftY)),
                     ImVec2(containerOffsetX + static_cast<float>(topLeftX) + width,
                            containerOffsetY + static_cast<float>(topLeftY) + height));
    return true;
}

bool ResolveDirectEditGroupItemScale(const platform::config::LinuxscreenConfig& config,
                                     const MirrorEditorState::DirectEditSelection& selection,
                                     const MirrorDirectEditViewportContext& ctx,
                                     const MirrorEditorState::VisualDragState& dragState,
                                     float& outScaleX,
                                     float& outScaleY) {
    outScaleX = 1.0f;
    outScaleY = 1.0f;
    if (selection.groupId.empty() || selection.groupItemIndex < 0) {
        return false;
    }

    const platform::config::MirrorGroupConfig* group = nullptr;
    for (const auto& candidate : config.mirrorGroups) {
        if (candidate.name == selection.groupId) {
            group = &candidate;
            break;
        }
    }
    if (!group || selection.groupItemIndex >= static_cast<int>(group->mirrors.size())) {
        return false;
    }

    const auto& groupItem = group->mirrors[static_cast<std::size_t>(selection.groupItemIndex)];
    const platform::config::MirrorConfig* mirror = nullptr;
    for (const auto& candidate : config.mirrors) {
        if (candidate.name == groupItem.mirrorId) {
            mirror = &candidate;
            break;
        }
    }
    if (!mirror) {
        return false;
    }

    ImRect nativeRect;
    if (!ResolveOutputRectForDirectEdit(*mirror, ctx, nativeRect)) {
        return false;
    }

    const float localWidth = std::max(1.0f, nativeRect.GetWidth() * groupItem.widthPercent);
    const float localHeight = std::max(1.0f, nativeRect.GetHeight() * groupItem.heightPercent);
    const float draggedWidth = std::max(1.0f, dragState.startRect.z - dragState.startRect.x);
    const float draggedHeight = std::max(1.0f, dragState.startRect.w - dragState.startRect.y);
    outScaleX = std::max(0.0001f, draggedWidth / localWidth);
    outScaleY = std::max(0.0001f, draggedHeight / localHeight);
    return true;
}

bool ApplyOutputRectPositionToRenderConfig(platform::config::MirrorRenderConfig& output,
                                           const ImRect& rect,
                                           const MirrorDirectEditViewportContext& ctx) {
    float containerWidth = 0.0f;
    float containerHeight = 0.0f;
    float containerOffsetX = 0.0f;
    float containerOffsetY = 0.0f;
    if (!ResolveOutputContainerForDirectEdit(output,
                                             ctx,
                                             containerWidth,
                                             containerHeight,
                                             containerOffsetX,
                                             containerOffsetY)) {
        return false;
    }

    const float localLeft = rect.Min.x - containerOffsetX;
    const float localTop = rect.Min.y - containerOffsetY;
    const float width = std::max(1.0f, rect.GetWidth());
    const float height = std::max(1.0f, rect.GetHeight());

    float anchorX = 0.0f;
    float anchorY = 0.0f;
    ResolveAnchorOffsetsFromRect(output.relativeTo,
                                 localLeft,
                                 localTop,
                                 width,
                                 height,
                                 containerWidth,
                                 containerHeight,
                                 anchorX,
                                 anchorY);

    if (output.useRelativePosition) {
        output.relativeX = std::clamp(anchorX / containerWidth, -1.0f, 2.0f);
        output.relativeY = std::clamp(anchorY / containerHeight, -1.0f, 2.0f);
    }
    output.x = static_cast<int>(std::round(anchorX));
    output.y = static_cast<int>(std::round(anchorY));
    return true;
}

void ApplyOutputRectToMirror(platform::config::MirrorConfig& mirror,
                             const ImRect& rect,
                             const MirrorDirectEditViewportContext& ctx) {
    if (!ApplyOutputRectPositionToRenderConfig(mirror.output, rect, ctx)) {
        return;
    }

    const int dynamicBorder = platform::config::GetMirrorDynamicBorderPadding(mirror.border);
    const float baseWidth = static_cast<float>(mirror.captureWidth + (2 * dynamicBorder));
    const float baseHeight = static_cast<float>(mirror.captureHeight + (2 * dynamicBorder));
    if (!(baseWidth > 0.0f) || !(baseHeight > 0.0f)) {
        return;
    }
    const float width = std::max(1.0f, rect.GetWidth());
    const float height = std::max(1.0f, rect.GetHeight());

    const float scaleX = std::clamp(width / baseWidth, kDirectEditOutputScaleMin, kDirectEditOutputScaleMax);
    const float scaleY = std::clamp(height / baseHeight, kDirectEditOutputScaleMin, kDirectEditOutputScaleMax);
    mirror.output.useRelativeSize = false;
    mirror.output.preserveAspectRatio = false;
    mirror.output.separateScale = true;
    mirror.output.scale = scaleX;
    mirror.output.scaleX = scaleX;
    mirror.output.scaleY = scaleY;
}

platform::config::MirrorConfig* FindMirrorConfigByName(platform::config::LinuxscreenConfig& config,
                                                       const std::string& mirrorId) {
    for (auto& mirror : config.mirrors) {
        if (mirror.name == mirrorId) {
            return &mirror;
        }
    }
    return nullptr;
}

platform::config::MirrorGroupConfig* FindMirrorGroupByName(platform::config::LinuxscreenConfig& config,
                                                           const std::string& groupId) {
    for (auto& group : config.mirrorGroups) {
        if (group.name == groupId) {
            return &group;
        }
    }
    return nullptr;
}

int ResolveDirectEditCaptureZoneIndex(const platform::config::MirrorConfig& mirror) {
    if (mirror.input.empty()) {
        return -1;
    }
    const int clampedIndex = std::clamp(g_mirrorEditorState.directEditSelectedCaptureZoneIndex,
                                        0,
                                        static_cast<int>(mirror.input.size()) - 1);
    return clampedIndex;
}

int ResolveHoveredHandleIndex(const ImRect& rect, float radius, const ImVec2& mousePos) {
    const ImVec2 handles[] = {
        rect.Min,
        ImVec2(rect.GetCenter().x, rect.Min.y),
        ImVec2(rect.Max.x, rect.Min.y),
        ImVec2(rect.Min.x, rect.GetCenter().y),
        ImVec2(rect.Max.x, rect.GetCenter().y),
        ImVec2(rect.Min.x, rect.Max.y),
        ImVec2(rect.GetCenter().x, rect.Max.y),
        rect.Max,
    };
    for (int i = 0; i < static_cast<int>(IM_ARRAYSIZE(handles)); ++i) {
        const ImVec2 delta = mousePos - handles[i];
        if ((delta.x * delta.x) + (delta.y * delta.y) <= (radius * radius)) {
            return i;
        }
    }
    return -1;
}

int ResolveHandleEdgeMask(int handleIndex, MirrorVisualEditorMode mode) {
    switch (handleIndex) {
    case 0:
        return kMirrorVisualEdgeLeft | kMirrorVisualEdgeTop;
    case 1:
        return kMirrorVisualEdgeTop;
    case 2:
        return kMirrorVisualEdgeRight | kMirrorVisualEdgeTop;
    case 3:
        return kMirrorVisualEdgeLeft;
    case 4:
        return kMirrorVisualEdgeRight;
    case 5:
        return kMirrorVisualEdgeLeft | kMirrorVisualEdgeBottom;
    case 6:
        return kMirrorVisualEdgeBottom;
    case 7:
        return kMirrorVisualEdgeRight | kMirrorVisualEdgeBottom;
    default:
        return kMirrorVisualEdgeNone;
    }
}

bool ShouldShowHandleIndex(int handleIndex, MirrorVisualEditorMode mode) {
    (void)mode;
    return handleIndex >= 0 && handleIndex < 8;
}

int ResolveHoveredHandleEdgeMask(const ImRect& rect,
                                 float radius,
                                 const ImVec2& mousePos,
                                 MirrorVisualEditorMode mode) {
    const int handleIndex = ResolveHoveredHandleIndex(rect, radius, mousePos);
    return ResolveHandleEdgeMask(handleIndex, mode);
}

bool IsDirectEditShiftHeld() {
    return ImGui::IsKeyDown(ImGuiKey_LeftShift) || ImGui::IsKeyDown(ImGuiKey_RightShift);
}

bool IsDirectEditAltHeld() {
    return ImGui::IsKeyDown(ImGuiKey_LeftAlt) || ImGui::IsKeyDown(ImGuiKey_RightAlt);
}

MirrorVisualEditorMode ResolveEffectiveDirectEditMode() {
    return IsDirectEditAltHeld() ? MirrorVisualEditorMode::Crop : MirrorVisualEditorMode::Layout;
}

void DrawDashedLine(ImDrawList* drawList,
                    const ImVec2& start,
                    const ImVec2& end,
                    ImU32 color,
                    float thickness,
                    float dashLength = 8.0f,
                    float gapLength = 5.0f) {
    const ImVec2 delta = end - start;
    const float length = std::sqrt((delta.x * delta.x) + (delta.y * delta.y));
    if (!(length > 0.0f)) {
        return;
    }

    const ImVec2 direction(delta.x / length, delta.y / length);
    float offset = 0.0f;
    while (offset < length) {
        const float segmentEnd = std::min(length, offset + dashLength);
        const ImVec2 segStart(start.x + (direction.x * offset), start.y + (direction.y * offset));
        const ImVec2 segEnd(start.x + (direction.x * segmentEnd), start.y + (direction.y * segmentEnd));
        drawList->AddLine(segStart, segEnd, color, thickness);
        offset += dashLength + gapLength;
    }
}

void DrawDashedRect(ImDrawList* drawList,
                    const ImRect& rect,
                    ImU32 color,
                    float thickness) {
    DrawDashedLine(drawList, rect.Min, ImVec2(rect.Max.x, rect.Min.y), color, thickness);
    DrawDashedLine(drawList, ImVec2(rect.Max.x, rect.Min.y), rect.Max, color, thickness);
    DrawDashedLine(drawList, rect.Max, ImVec2(rect.Min.x, rect.Max.y), color, thickness);
    DrawDashedLine(drawList, ImVec2(rect.Min.x, rect.Max.y), rect.Min, color, thickness);
}

void DrawCornerBrackets(ImDrawList* drawList,
                        const ImRect& rect,
                        ImU32 color,
                        float thickness,
                        float length = 12.0f) {
    const ImVec2 tl = rect.Min;
    const ImVec2 tr(rect.Max.x, rect.Min.y);
    const ImVec2 bl(rect.Min.x, rect.Max.y);
    const ImVec2 br = rect.Max;
    drawList->AddLine(tl, ImVec2(tl.x + length, tl.y), color, thickness);
    drawList->AddLine(tl, ImVec2(tl.x, tl.y + length), color, thickness);
    drawList->AddLine(tr, ImVec2(tr.x - length, tr.y), color, thickness);
    drawList->AddLine(tr, ImVec2(tr.x, tr.y + length), color, thickness);
    drawList->AddLine(bl, ImVec2(bl.x + length, bl.y), color, thickness);
    drawList->AddLine(bl, ImVec2(bl.x, bl.y - length), color, thickness);
    drawList->AddLine(br, ImVec2(br.x - length, br.y), color, thickness);
    drawList->AddLine(br, ImVec2(br.x, br.y - length), color, thickness);
}

void DrawGroupLabelChip(ImDrawList* drawList,
                        const ImRect& rect,
                        const std::string& label,
                        ImU32 textColor,
                        ImU32 fillColor) {
    const ImVec2 textSize = ImGui::CalcTextSize(label.c_str());
    const ImVec2 chipMin(rect.Min.x + 6.0f, rect.Min.y + 6.0f);
    const ImVec2 chipMax(chipMin.x + textSize.x + 12.0f, chipMin.y + textSize.y + 8.0f);
    drawList->AddRectFilled(chipMin, chipMax, fillColor, 4.0f);
    drawList->AddText(chipMin + ImVec2(6.0f, 4.0f), textColor, label.c_str());
}

ImRect ApplyDirectEditResizeDelta(const ImRect& startRect,
                                  int edgeMask,
                                  const ImVec2& delta,
                                  bool preserveAspectRatio) {
    ImRect result = startRect;
    if (edgeMask == kMirrorVisualEdgeNone) {
        result.Translate(delta);
        return result;
    }

    if (!preserveAspectRatio) {
        if (edgeMask & kMirrorVisualEdgeLeft) {
            result.Min.x = startRect.Min.x + delta.x;
        }
        if (edgeMask & kMirrorVisualEdgeRight) {
            result.Max.x = startRect.Max.x + delta.x;
        }
        if (edgeMask & kMirrorVisualEdgeTop) {
            result.Min.y = startRect.Min.y + delta.y;
        }
        if (edgeMask & kMirrorVisualEdgeBottom) {
            result.Max.y = startRect.Max.y + delta.y;
        }

        if (result.Max.x <= result.Min.x + 1.0f) {
            if (edgeMask & kMirrorVisualEdgeLeft) {
                result.Min.x = result.Max.x - 1.0f;
            } else {
                result.Max.x = result.Min.x + 1.0f;
            }
        }
        if (result.Max.y <= result.Min.y + 1.0f) {
            if (edgeMask & kMirrorVisualEdgeTop) {
                result.Min.y = result.Max.y - 1.0f;
            } else {
                result.Max.y = result.Min.y + 1.0f;
            }
        }
        return result;
    }

    const float startWidth = std::max(1.0f, startRect.GetWidth());
    const float startHeight = std::max(1.0f, startRect.GetHeight());
    const float aspectRatio = startWidth / startHeight;

    float targetWidth = startWidth;
    float targetHeight = startHeight;
    const bool horizontalResize = (edgeMask & (kMirrorVisualEdgeLeft | kMirrorVisualEdgeRight)) != 0;
    const bool verticalResize = (edgeMask & (kMirrorVisualEdgeTop | kMirrorVisualEdgeBottom)) != 0;

    if (horizontalResize && verticalResize) {
        const float signedWidthDelta =
            (edgeMask & kMirrorVisualEdgeLeft) ? -delta.x : delta.x;
        const float signedHeightDelta =
            (edgeMask & kMirrorVisualEdgeTop) ? -delta.y : delta.y;
        const float widthScale = 1.0f + (signedWidthDelta / startWidth);
        const float heightScale = 1.0f + (signedHeightDelta / startHeight);
        const float uniformScale = std::max(0.01f, (widthScale + heightScale) * 0.5f);
        targetWidth = std::max(1.0f, startWidth * uniformScale);
        targetHeight = std::max(1.0f, startHeight * uniformScale);
    } else if (horizontalResize) {
        const float signedWidthDelta =
            (edgeMask & kMirrorVisualEdgeLeft) ? -delta.x : delta.x;
        targetWidth = std::max(1.0f, startWidth + signedWidthDelta);
        targetHeight = std::max(1.0f, targetWidth / aspectRatio);
    } else if (verticalResize) {
        const float signedHeightDelta =
            (edgeMask & kMirrorVisualEdgeTop) ? -delta.y : delta.y;
        targetHeight = std::max(1.0f, startHeight + signedHeightDelta);
        targetWidth = std::max(1.0f, targetHeight * aspectRatio);
    }

    if (horizontalResize && verticalResize) {
        if (edgeMask & kMirrorVisualEdgeLeft) {
            result.Min.x = startRect.Max.x - targetWidth;
            result.Max.x = startRect.Max.x;
        } else {
            result.Min.x = startRect.Min.x;
            result.Max.x = startRect.Min.x + targetWidth;
        }
        if (edgeMask & kMirrorVisualEdgeTop) {
            result.Min.y = startRect.Max.y - targetHeight;
            result.Max.y = startRect.Max.y;
        } else {
            result.Min.y = startRect.Min.y;
            result.Max.y = startRect.Min.y + targetHeight;
        }
        return result;
    }

    const ImVec2 center = startRect.GetCenter();
    if (horizontalResize) {
        if (edgeMask & kMirrorVisualEdgeLeft) {
            result.Min.x = startRect.Max.x - targetWidth;
            result.Max.x = startRect.Max.x;
        } else {
            result.Min.x = startRect.Min.x;
            result.Max.x = startRect.Min.x + targetWidth;
        }
        result.Min.y = center.y - (targetHeight * 0.5f);
        result.Max.y = center.y + (targetHeight * 0.5f);
        return result;
    }

    if (edgeMask & kMirrorVisualEdgeTop) {
        result.Min.y = startRect.Max.y - targetHeight;
        result.Max.y = startRect.Max.y;
    } else {
        result.Min.y = startRect.Min.y;
        result.Max.y = startRect.Min.y + targetHeight;
    }
    result.Min.x = center.x - (targetWidth * 0.5f);
    result.Max.x = center.x + (targetWidth * 0.5f);
    return result;
}

ImRect ClampDirectEditRectToSizeLimits(const ImRect& startRect,
                                       int edgeMask,
                                       const ImRect& rect,
                                       float minWidth,
                                       float minHeight,
                                       float maxWidth,
                                       float maxHeight,
                                       bool preserveAspectRatio) {
    if (edgeMask == kMirrorVisualEdgeNone) {
        return rect;
    }

    float targetWidth = std::clamp(rect.GetWidth(), minWidth, maxWidth);
    float targetHeight = std::clamp(rect.GetHeight(), minHeight, maxHeight);
    if (preserveAspectRatio) {
        const float widthScale = targetWidth / std::max(1.0f, rect.GetWidth());
        const float heightScale = targetHeight / std::max(1.0f, rect.GetHeight());
        const float uniformScale = std::min(widthScale, heightScale);
        targetWidth = std::clamp(rect.GetWidth() * uniformScale, minWidth, maxWidth);
        targetHeight = std::clamp(rect.GetHeight() * uniformScale, minHeight, maxHeight);
    }

    ImRect result = startRect;
    const bool horizontalResize = (edgeMask & (kMirrorVisualEdgeLeft | kMirrorVisualEdgeRight)) != 0;
    const bool verticalResize = (edgeMask & (kMirrorVisualEdgeTop | kMirrorVisualEdgeBottom)) != 0;
    if (horizontalResize && verticalResize) {
        if (edgeMask & kMirrorVisualEdgeLeft) {
            result.Min.x = startRect.Max.x - targetWidth;
            result.Max.x = startRect.Max.x;
        } else {
            result.Min.x = startRect.Min.x;
            result.Max.x = startRect.Min.x + targetWidth;
        }
        if (edgeMask & kMirrorVisualEdgeTop) {
            result.Min.y = startRect.Max.y - targetHeight;
            result.Max.y = startRect.Max.y;
        } else {
            result.Min.y = startRect.Min.y;
            result.Max.y = startRect.Min.y + targetHeight;
        }
        return result;
    }

    const ImVec2 center = startRect.GetCenter();
    if (horizontalResize) {
        if (edgeMask & kMirrorVisualEdgeLeft) {
            result.Min.x = startRect.Max.x - targetWidth;
            result.Max.x = startRect.Max.x;
        } else {
            result.Min.x = startRect.Min.x;
            result.Max.x = startRect.Min.x + targetWidth;
        }
        result.Min.y = center.y - (targetHeight * 0.5f);
        result.Max.y = center.y + (targetHeight * 0.5f);
        return result;
    }

    if (edgeMask & kMirrorVisualEdgeTop) {
        result.Min.y = startRect.Max.y - targetHeight;
        result.Max.y = startRect.Max.y;
    } else {
        result.Min.y = startRect.Min.y;
        result.Max.y = startRect.Min.y + targetHeight;
    }
    result.Min.x = center.x - (targetWidth * 0.5f);
    result.Max.x = center.x + (targetWidth * 0.5f);
    return result;
}

void DrawCaptureGuides(ImDrawList* drawList,
                       const platform::config::MirrorConfig& mirror,
                       const platform::config::MirrorCaptureConfig& zone,
                       const MirrorDirectEditViewportContext& viewportContext,
                       const ImRect& mirrorRect) {
    if (!drawList || mirror.source.type != platform::config::MirrorSourceType::GameFramebuffer) {
        return;
    }

    float sourceWidth = 0.0f;
    float sourceHeight = 0.0f;
    if (!ResolveMirrorSourceSizeForDirectEdit(mirror, viewportContext, sourceWidth, sourceHeight)) {
        return;
    }

    ImRect cropRect;
    if (!ResolveCropRectInSourceForDirectEdit(mirror, zone, viewportContext, sourceWidth, sourceHeight, cropRect)) {
        return;
    }

    const ImU32 guideColor = IM_COL32(255, 72, 72, 220);
    drawList->AddRect(cropRect.Min, cropRect.Max, guideColor, 0.0f, 0, 1.0f);
    drawList->AddLine(cropRect.Min, mirrorRect.Min, guideColor, 1.0f);
    drawList->AddLine(ImVec2(cropRect.Max.x, cropRect.Min.y), ImVec2(mirrorRect.Max.x, mirrorRect.Min.y), guideColor, 1.0f);
    drawList->AddLine(ImVec2(cropRect.Min.x, cropRect.Max.y), ImVec2(mirrorRect.Min.x, mirrorRect.Max.y), guideColor, 1.0f);
    drawList->AddLine(cropRect.Max, mirrorRect.Max, guideColor, 1.0f);
}

bool ResolveMirrorSourceSizeForDirectEdit(const platform::config::MirrorConfig& mirror,
                                          const MirrorDirectEditViewportContext& ctx,
                                          float& outWidth,
                                          float& outHeight) {
    outWidth = static_cast<float>(mirror.captureWidth);
    outHeight = static_cast<float>(mirror.captureHeight);
    if (mirror.source.type == platform::config::MirrorSourceType::GameFramebuffer) {
        if (ctx.hasDisplay) {
            outWidth = ctx.displayWidth;
            outHeight = ctx.displayHeight;
        }
        return outWidth > 0.0f && outHeight > 0.0f;
    }
    if (mirror.source.lastKnownWidth > 0 && mirror.source.lastKnownHeight > 0) {
        outWidth = static_cast<float>(mirror.source.lastKnownWidth);
        outHeight = static_cast<float>(mirror.source.lastKnownHeight);
    }
    return outWidth > 0.0f && outHeight > 0.0f;
}

ImRect ResolveMirrorSourceViewportRectForDirectEdit(const platform::config::MirrorConfig& mirror,
                                                    const MirrorDirectEditViewportContext& ctx,
                                                    float sourceWidth,
                                                    float sourceHeight) {
    ImRect viewport(ImVec2(0.0f, 0.0f), ImVec2(sourceWidth, sourceHeight));
    if (mirror.source.type != platform::config::MirrorSourceType::GameFramebuffer || !ctx.hasModeViewport) {
        return viewport;
    }
    viewport.Min.x = std::clamp(ctx.modeViewportX, 0.0f, sourceWidth);
    viewport.Min.y = std::clamp(ctx.modeViewportY, 0.0f, sourceHeight);
    viewport.Max.x = std::clamp(viewport.Min.x + ctx.modeViewportWidth, viewport.Min.x, sourceWidth);
    viewport.Max.y = std::clamp(viewport.Min.y + ctx.modeViewportHeight, viewport.Min.y, sourceHeight);
    return viewport;
}

bool ResolveCropRectInSourceForDirectEdit(const platform::config::MirrorConfig& mirror,
                                          const platform::config::MirrorCaptureConfig& zone,
                                          const MirrorDirectEditViewportContext& ctx,
                                          float sourceWidth,
                                          float sourceHeight,
                                          ImRect& outRect) {
    if (!(sourceWidth > 0.0f) || !(sourceHeight > 0.0f) || mirror.captureWidth <= 0 || mirror.captureHeight <= 0) {
        return false;
    }
    const ImRect viewportRect = ResolveMirrorSourceViewportRectForDirectEdit(mirror, ctx, sourceWidth, sourceHeight);
    float containerWidth = sourceWidth;
    float containerHeight = sourceHeight;
    float containerOffsetX = 0.0f;
    float containerOffsetY = 0.0f;
    if (ShouldUseViewportRelativeTo(zone.relativeTo)) {
        containerWidth = viewportRect.GetWidth();
        containerHeight = viewportRect.GetHeight();
        containerOffsetX = viewportRect.Min.x;
        containerOffsetY = viewportRect.Min.y;
    }

    int cropX = 0;
    int cropY = 0;
    platform::config::GetRelativeCoords(zone.relativeTo,
                                        zone.x,
                                        zone.y,
                                        mirror.captureWidth,
                                        mirror.captureHeight,
                                        static_cast<int>(std::round(containerWidth)),
                                        static_cast<int>(std::round(containerHeight)),
                                        cropX,
                                        cropY);
    outRect = ImRect(ImVec2(containerOffsetX + static_cast<float>(cropX),
                            containerOffsetY + static_cast<float>(cropY)),
                     ImVec2(containerOffsetX + static_cast<float>(cropX + mirror.captureWidth),
                            containerOffsetY + static_cast<float>(cropY + mirror.captureHeight)));
    return true;
}

void ApplyCropZonePositionToMirror(platform::config::MirrorCaptureConfig& zone,
                                   const std::string& relativeTo,
                                   const ImRect& rect,
                                   float containerWidth,
                                   float containerHeight) {
    const float width = std::max(1.0f, rect.GetWidth());
    const float height = std::max(1.0f, rect.GetHeight());
    float anchorX = 0.0f;
    float anchorY = 0.0f;
    ResolveAnchorOffsetsFromRect(relativeTo,
                                 rect.Min.x,
                                 rect.Min.y,
                                 width,
                                 height,
                                 containerWidth,
                                 containerHeight,
                                 anchorX,
                                 anchorY);
    zone.x = static_cast<int>(std::round(anchorX));
    zone.y = static_cast<int>(std::round(anchorY));
}

void NormalizeCropZoneAnchorForDirectEdit(platform::config::MirrorConfig& mirror,
                                          platform::config::MirrorCaptureConfig& zone,
                                          const MirrorDirectEditViewportContext& ctx,
                                          float sourceWidth,
                                          float sourceHeight) {
    ImRect currentRect;
    if (!ResolveCropRectInSourceForDirectEdit(mirror, zone, ctx, sourceWidth, sourceHeight, currentRect)) {
        return;
    }

    const bool useViewport = ShouldUseViewportRelativeTo(zone.relativeTo);
    zone.relativeTo = useViewport ? "topLeftViewport" : "topLeftScreen";

    const ImRect viewportRect = ResolveMirrorSourceViewportRectForDirectEdit(mirror, ctx, sourceWidth, sourceHeight);
    zone.x = static_cast<int>(std::round(currentRect.Min.x - (useViewport ? viewportRect.Min.x : 0.0f)));
    zone.y = static_cast<int>(std::round(currentRect.Min.y - (useViewport ? viewportRect.Min.y : 0.0f)));
}

void ApplyCropSizeToMirror(platform::config::MirrorConfig& mirror,
                           platform::config::MirrorCaptureConfig& zone,
                           const ImRect& localRect,
                           float containerWidth,
                           float containerHeight,
                           float width,
                           float height,
                           float outputScaleX,
                           float outputScaleY) {
    mirror.captureWidth = std::max(1, static_cast<int>(std::round(width)));
    mirror.captureHeight = std::max(1, static_cast<int>(std::round(height)));
    ApplyCropZonePositionToMirror(zone,
                                  zone.relativeTo,
                                  ImRect(localRect.Min,
                                         ImVec2(localRect.Min.x + static_cast<float>(mirror.captureWidth),
                                                localRect.Min.y + static_cast<float>(mirror.captureHeight))),
                                  containerWidth,
                                  containerHeight);

    // Crop editing should resize the visible mirror with the drag. Keep the
    // active output scales fixed and let the output rect grow/shrink from the
    // new capture size rather than preserving the old screen-space size.
    mirror.output.useRelativeSize = false;
    mirror.output.preserveAspectRatio = false;
    mirror.output.separateScale = true;
    mirror.output.scaleX = std::clamp(outputScaleX, kDirectEditOutputScaleMin, kDirectEditOutputScaleMax);
    mirror.output.scaleY = std::clamp(outputScaleY, kDirectEditOutputScaleMin, kDirectEditOutputScaleMax);
    mirror.output.scale = mirror.output.scaleX;
}

void ApplyCropRectToMirror(platform::config::MirrorConfig& mirror,
                           platform::config::MirrorCaptureConfig& zone,
                           const MirrorDirectEditViewportContext& ctx,
                          const ImRect& rect,
                          float sourceWidth,
                          float sourceHeight,
                          bool resizeCapture,
                          float outputScaleX,
                          float outputScaleY) {
    const ImRect viewportRect = ResolveMirrorSourceViewportRectForDirectEdit(mirror, ctx, sourceWidth, sourceHeight);
    float containerWidth = sourceWidth;
    float containerHeight = sourceHeight;
    float left = rect.Min.x;
    float top = rect.Min.y;
    if (ShouldUseViewportRelativeTo(zone.relativeTo)) {
        containerWidth = viewportRect.GetWidth();
        containerHeight = viewportRect.GetHeight();
        left -= viewportRect.Min.x;
        top -= viewportRect.Min.y;
    }

    const ImRect localRect(ImVec2(left, top), ImVec2(left + rect.GetWidth(), top + rect.GetHeight()));
    if (!resizeCapture) {
        ApplyCropZonePositionToMirror(zone, zone.relativeTo, localRect, containerWidth, containerHeight);
        return;
    }

    ApplyCropSizeToMirror(mirror,
                          zone,
                          localRect,
                          containerWidth,
                          containerHeight,
                          localRect.GetWidth(),
                          localRect.GetHeight(),
                          outputScaleX,
                          outputScaleY);
}

void SyncMirrorDirectEditSelectionToSidebar(const platform::config::LinuxscreenConfig& config) {
    if (g_mirrorEditorState.directEditSelection.mirrorId.empty()) {
        return;
    }
    for (std::size_t i = 0; i < config.mirrors.size(); ++i) {
        if (config.mirrors[i].name == g_mirrorEditorState.directEditSelection.mirrorId) {
            g_mirrorEditorState.mirrorListSelectionIndex = static_cast<int>(i);
            break;
        }
    }
}

} // namespace

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

bool ResolveSharedGroupScaleReference(const platform::config::LinuxscreenConfig& config,
                                      const platform::config::MirrorGroupConfig& group,
                                      const MirrorDirectEditViewportContext& viewportContext,
                                      float& outContainerWidth,
                                      float& outContainerHeight,
                                      float& outBaseWidth,
                                      float& outBaseHeight,
                                      float& outItemWidthPercent,
                                      float& outItemHeightPercent) {
    outContainerWidth = 0.0f;
    outContainerHeight = 0.0f;
    outBaseWidth = 0.0f;
    outBaseHeight = 0.0f;
    outItemWidthPercent = 1.0f;
    outItemHeightPercent = 1.0f;
    if (!ResolveDirectEditOutputContainerSize(group.output, viewportContext, outContainerWidth, outContainerHeight)) {
        return false;
    }

    const platform::config::MirrorGroupItem* firstEnabledItem = nullptr;
    const platform::config::MirrorConfig* itemMirror = nullptr;
    for (const auto& item : group.mirrors) {
        if (!item.enabled) {
            continue;
        }
        for (const auto& mirror : config.mirrors) {
            if (mirror.name == item.mirrorId) {
                firstEnabledItem = &item;
                itemMirror = &mirror;
                break;
            }
        }
        if (firstEnabledItem && itemMirror) {
            break;
        }
    }
    if (!firstEnabledItem || !itemMirror) {
        return false;
    }

    const int border = platform::config::GetMirrorDynamicBorderPadding(itemMirror->border);
    outBaseWidth = static_cast<float>(itemMirror->captureWidth + (2 * border));
    outBaseHeight = static_cast<float>(itemMirror->captureHeight + (2 * border));
    outItemWidthPercent = std::max(0.0001f, firstEnabledItem->widthPercent);
    outItemHeightPercent = std::max(0.0001f, firstEnabledItem->heightPercent);
    return outBaseWidth > 0.0f && outBaseHeight > 0.0f;
}

void UpdateSharedGroupRelativeSizeFromScale(const platform::config::LinuxscreenConfig& config,
                                            platform::config::MirrorGroupConfig& group,
                                            const MirrorDirectEditViewportContext& viewportContext) {
    float containerWidth = 0.0f;
    float containerHeight = 0.0f;
    float baseWidth = 0.0f;
    float baseHeight = 0.0f;
    float itemWidthPercent = 1.0f;
    float itemHeightPercent = 1.0f;
    if (!ResolveSharedGroupScaleReference(config,
                                          group,
                                          viewportContext,
                                          containerWidth,
                                          containerHeight,
                                          baseWidth,
                                          baseHeight,
                                          itemWidthPercent,
                                          itemHeightPercent)) {
        return;
    }

    const float scaleX = group.output.separateScale ? group.output.scaleX : group.output.scale;
    const float scaleY = group.output.separateScale ? group.output.scaleY : group.output.scale;
    const float outputWidth = baseWidth * scaleX / itemWidthPercent;
    const float outputHeight = baseHeight * scaleY / itemHeightPercent;
    group.output.relativeWidth = std::clamp(outputWidth / containerWidth, 0.01f, 20.0f);
    group.output.relativeHeight = std::clamp(outputHeight / containerHeight, 0.01f, 20.0f);
}

void UpdateSharedGroupScaleFromRelativeSize(const platform::config::LinuxscreenConfig& config,
                                            platform::config::MirrorGroupConfig& group,
                                            const MirrorDirectEditViewportContext& viewportContext) {
    float containerWidth = 0.0f;
    float containerHeight = 0.0f;
    float baseWidth = 0.0f;
    float baseHeight = 0.0f;
    float itemWidthPercent = 1.0f;
    float itemHeightPercent = 1.0f;
    if (!ResolveSharedGroupScaleReference(config,
                                          group,
                                          viewportContext,
                                          containerWidth,
                                          containerHeight,
                                          baseWidth,
                                          baseHeight,
                                          itemWidthPercent,
                                          itemHeightPercent)) {
        return;
    }

    const float scaleX = std::clamp((containerWidth * group.output.relativeWidth * itemWidthPercent) / baseWidth, 0.01f, 20.0f);
    const float scaleY = std::clamp((containerHeight * group.output.relativeHeight * itemHeightPercent) / baseHeight, 0.01f, 20.0f);
    if (group.output.preserveAspectRatio) {
        const float uniformScale = std::clamp(ResolveUniformScaleByFitMode(scaleX,
                                                                           scaleY,
                                                                           NormalizeAspectFitMode(group.output.aspectFitMode)),
                                              0.01f,
                                              20.0f);
        group.output.separateScale = false;
        group.output.scale = uniformScale;
        group.output.scaleX = uniformScale;
        group.output.scaleY = uniformScale;
    } else {
        group.output.separateScale = true;
        group.output.scale = scaleX;
        group.output.scaleX = scaleX;
        group.output.scaleY = scaleY;
    }
}

bool GetSharedGroupUniformScale(const platform::config::LinuxscreenConfig& config,
                                const platform::config::MirrorGroupConfig& group,
                                const MirrorDirectEditViewportContext& viewportContext,
                                float& outScale) {
    if (group.output.useRelativeSize) {
        float containerWidth = 0.0f;
        float containerHeight = 0.0f;
        float baseWidth = 0.0f;
        float baseHeight = 0.0f;
        float itemWidthPercent = 1.0f;
        float itemHeightPercent = 1.0f;
        if (!ResolveSharedGroupScaleReference(config,
                                              group,
                                              viewportContext,
                                              containerWidth,
                                              containerHeight,
                                              baseWidth,
                                              baseHeight,
                                              itemWidthPercent,
                                              itemHeightPercent)) {
            return false;
        }
        const float scaleX = (containerWidth * group.output.relativeWidth * itemWidthPercent) / baseWidth;
        const float scaleY = (containerHeight * group.output.relativeHeight * itemHeightPercent) / baseHeight;
        outScale = std::clamp(ResolveUniformScaleByFitMode(scaleX,
                                                           scaleY,
                                                           NormalizeAspectFitMode(group.output.aspectFitMode)),
                              0.01f,
                              20.0f);
        return true;
    }

    const float scaleX = group.output.separateScale ? group.output.scaleX : group.output.scale;
    const float scaleY = group.output.separateScale ? group.output.scaleY : group.output.scale;
    outScale = std::clamp(ResolveUniformScaleByFitMode(scaleX,
                                                       scaleY,
                                                       NormalizeAspectFitMode(group.output.aspectFitMode)),
                          0.01f,
                          20.0f);
    return true;
}

void SetSharedGroupUniformScale(const platform::config::LinuxscreenConfig& config,
                                platform::config::MirrorGroupConfig& group,
                                const MirrorDirectEditViewportContext& viewportContext,
                                float uniformScale) {
    uniformScale = std::clamp(uniformScale, 0.01f, 20.0f);
    if (group.output.useRelativeSize) {
        float containerWidth = 0.0f;
        float containerHeight = 0.0f;
        float baseWidth = 0.0f;
        float baseHeight = 0.0f;
        float itemWidthPercent = 1.0f;
        float itemHeightPercent = 1.0f;
        if (!ResolveSharedGroupScaleReference(config,
                                              group,
                                              viewportContext,
                                              containerWidth,
                                              containerHeight,
                                              baseWidth,
                                              baseHeight,
                                              itemWidthPercent,
                                              itemHeightPercent)) {
            group.output.relativeWidth = uniformScale;
            group.output.relativeHeight = uniformScale;
            return;
        }
        group.output.relativeWidth =
            std::clamp((uniformScale * baseWidth) / (containerWidth * itemWidthPercent), 0.01f, 20.0f);
        group.output.relativeHeight =
            std::clamp((uniformScale * baseHeight) / (containerHeight * itemHeightPercent), 0.01f, 20.0f);
        return;
    }

    group.output.separateScale = false;
    group.output.scale = uniformScale;
    group.output.scaleX = uniformScale;
    group.output.scaleY = uniformScale;
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
    const char* preview = "Unknown";
    if (currentIndex >= 0) {
        preview = kMirrorRelativeToOptions[currentIndex].label;
    }
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

void RenderSharedMirrorGroupEditorSections(platform::config::LinuxscreenConfig& config,
                                           int groupIndex,
                                           const char* idSuffix) {
    if (groupIndex < 0 || groupIndex >= static_cast<int>(config.mirrorGroups.size())) {
        return;
    }

    float displayWidth = 0.0f;
    float displayHeight = 0.0f;
    float framebufferScaleX = 1.0f;
    float framebufferScaleY = 1.0f;
    GetOverlayDisplayMetrics(displayWidth, displayHeight, framebufferScaleX, framebufferScaleY);
    const MirrorDirectEditViewportContext viewportContext =
        BuildMirrorDirectEditViewportContext(config, displayWidth, displayHeight);

    auto& grp = config.mirrorGroups[static_cast<std::size_t>(groupIndex)];
    ImGui::PushID(idSuffix);
    ImGui::PushID(groupIndex);

    if (g_mirrorEditorState.selectedGroupIndex != groupIndex) {
        g_mirrorEditorState.selectedGroupIndex = groupIndex;
        CopyEditorNameToBuffer(g_mirrorEditorState.groupNameBuffer,
                               sizeof(g_mirrorEditorState.groupNameBuffer),
                               grp.name);
        g_mirrorEditorState.groupNameError.clear();
    }

    if (AnimatedCollapsingHeader("General")) {
        HeaderRevealScope headerRevealScope = BeginAnimatedHeaderContentReveal();
        ImGui::Indent();
        if (ImGui::InputText("Group Name",
                             g_mirrorEditorState.groupNameBuffer,
                             sizeof(g_mirrorEditorState.groupNameBuffer))) {
            g_mirrorEditorState.groupNameError.clear();
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            const std::string newName = TrimEditorName(g_mirrorEditorState.groupNameBuffer);
            if (newName.empty()) {
                g_mirrorEditorState.groupNameError = "Group name cannot be empty.";
            } else if (HasDuplicateGroupName(config, newName, groupIndex)) {
                g_mirrorEditorState.groupNameError = "Group name must be unique.";
            } else {
                g_mirrorEditorState.groupNameError.clear();
                if (newName != grp.name) {
                    platform::config::RenameGroup(config, grp.name, newName);
                }
                CopyEditorNameToBuffer(g_mirrorEditorState.groupNameBuffer,
                                       sizeof(g_mirrorEditorState.groupNameBuffer),
                                       newName);
                AutoSaveConfig(config);
            }
        }
        if (!g_mirrorEditorState.groupNameError.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "%s", g_mirrorEditorState.groupNameError.c_str());
        }
        ImGui::Unindent();
    }

    if (AnimatedCollapsingHeader("Group Scaling")) {
        HeaderRevealScope headerRevealScope = BeginAnimatedHeaderContentReveal();
        ImGui::Indent();
        if (ImGui::Checkbox("Relative size to container##grpout_size", &grp.output.useRelativeSize)) {
            if (grp.output.useRelativeSize) {
                UpdateSharedGroupRelativeSizeFromScale(config, grp, viewportContext);
            } else {
                UpdateSharedGroupScaleFromRelativeSize(config, grp, viewportContext);
            }
            AutoSaveConfig(config);
        }
        ImGui::SameLine();
        HelpMarker("When enabled, group output width/height are stored as percentages of the anchor container.\n"
                   "Container is screen for *Screen anchors and mode viewport for *Viewport/Pie anchors.");

        if (ImGui::Checkbox("Preserve aspect ratio##grpout_size", &grp.output.preserveAspectRatio)) {
            if (grp.output.preserveAspectRatio) {
                float uniformScale = 1.0f;
                if (GetSharedGroupUniformScale(config, grp, viewportContext, uniformScale)) {
                    SetSharedGroupUniformScale(config, grp, viewportContext, uniformScale);
                }
            } else if (!grp.output.useRelativeSize) {
                const float currentScale = grp.output.separateScale ? grp.output.scaleX : grp.output.scale;
                grp.output.separateScale = true;
                grp.output.scaleX = currentScale;
                grp.output.scaleY = currentScale;
            }
            AutoSaveConfig(config);
        }

        if (grp.output.preserveAspectRatio &&
            DrawAspectFitModeCombo("Fit Mode##grpout_size", grp.output.aspectFitMode)) {
            grp.output.aspectFitMode = NormalizeAspectFitMode(grp.output.aspectFitMode);
            AutoSaveConfig(config);
        }

        if (grp.output.preserveAspectRatio) {
            float uniformScale = 1.0f;
            if (!GetSharedGroupUniformScale(config, grp, viewportContext, uniformScale)) {
                uniformScale = 1.0f;
            }
            float scalePercent = uniformScale * 100.0f;
            const char* uniformScaleLabel = grp.output.useRelativeSize
                ? "Size % of container##grpout_size"
                : "Scale %##grpout_size";
            if (ImGui::SliderFloat(uniformScaleLabel, &scalePercent, 1.0f, 2000.0f, "%.1f%%")) {
                SetSharedGroupUniformScale(config, grp, viewportContext, scalePercent / 100.0f);
                AutoSaveConfig(config);
            }

            if (grp.output.useRelativeSize) {
                ImGui::TextDisabled("Stored size: %.1f%% width, %.1f%% height",
                                    std::clamp(grp.output.relativeWidth, 0.01f, 20.0f) * 100.0f,
                                    std::clamp(grp.output.relativeHeight, 0.01f, 20.0f) * 100.0f);
            }
        } else if (grp.output.useRelativeSize) {
            float widthPercent = std::clamp(grp.output.relativeWidth, 0.01f, 20.0f) * 100.0f;
            if (ImGui::SliderFloat("Width % of container##grpout_size", &widthPercent, 1.0f, 2000.0f, "%.1f%%")) {
                grp.output.relativeWidth = std::clamp(widthPercent / 100.0f, 0.01f, 20.0f);
                AutoSaveConfig(config);
            }

            float heightPercent = std::clamp(grp.output.relativeHeight, 0.01f, 20.0f) * 100.0f;
            if (ImGui::SliderFloat("Height % of container##grpout_size", &heightPercent, 1.0f, 2000.0f, "%.1f%%")) {
                grp.output.relativeHeight = std::clamp(heightPercent / 100.0f, 0.01f, 20.0f);
                AutoSaveConfig(config);
            }
        } else {
            const float currentScaleX = std::clamp(grp.output.separateScale ? grp.output.scaleX : grp.output.scale, 0.01f, 20.0f);
            const float currentScaleY = std::clamp(grp.output.separateScale ? grp.output.scaleY : grp.output.scale, 0.01f, 20.0f);
            float widthPercent = currentScaleX * 100.0f;
            if (ImGui::SliderFloat("Width %##grpout_size", &widthPercent, 1.0f, 2000.0f, "%.1f%%")) {
                grp.output.separateScale = true;
                grp.output.scaleX = std::clamp(widthPercent / 100.0f, 0.01f, 20.0f);
                grp.output.scale = grp.output.scaleX;
                AutoSaveConfig(config);
            }

            float heightPercent = currentScaleY * 100.0f;
            if (ImGui::SliderFloat("Height %##grpout_size", &heightPercent, 1.0f, 2000.0f, "%.1f%%")) {
                grp.output.separateScale = true;
                grp.output.scaleY = std::clamp(heightPercent / 100.0f, 0.01f, 20.0f);
                AutoSaveConfig(config);
            }
        }
        ImGui::Unindent();
    }

    if (AnimatedCollapsingHeader("Group Position")) {
        HeaderRevealScope headerRevealScope = BeginAnimatedHeaderContentReveal();
        ImGui::Indent();
        if (ImGui::Checkbox("Relative to screen##grpout", &grp.output.useRelativePosition)) {
            if (grp.output.useRelativePosition) {
                UpdateDirectEditRelativeFromPixels(grp.output, viewportContext);
            } else {
                UpdateDirectEditPixelsFromRelative(grp.output, viewportContext);
            }
            AutoSaveConfig(config);
        }
        ImGui::SameLine();
        HelpMarker("When enabled, position is stored as percentages of screen size.\n"
                   "This makes configs portable across different screen resolutions.");

        if (DrawRelativeToCombo("Relative To##grpout", grp.output.relativeTo)) {
            AutoSaveConfig(config);
        }

        if (grp.output.useRelativePosition) {
            float xPercent = grp.output.relativeX * 100.0f;
            if (ImGui::SliderFloat("X %##grpout", &xPercent, -100.0f, 200.0f, "%.1f%%")) {
                grp.output.relativeX = xPercent / 100.0f;
                UpdateDirectEditPixelsFromRelative(grp.output, viewportContext);
                AutoSaveConfig(config);
            }

            float yPercent = grp.output.relativeY * 100.0f;
            if (ImGui::SliderFloat("Y %##grpout", &yPercent, -100.0f, 200.0f, "%.1f%%")) {
                grp.output.relativeY = yPercent / 100.0f;
                UpdateDirectEditPixelsFromRelative(grp.output, viewportContext);
                AutoSaveConfig(config);
            }
        } else {
            if (ImGui::DragInt("X Offset##grpout", &grp.output.x, 1)) {
                AutoSaveConfig(config);
            }
            if (ImGui::DragInt("Y Offset##grpout", &grp.output.y, 1)) {
                AutoSaveConfig(config);
            }
        }
        ImGui::Unindent();
    }

    if (AnimatedCollapsingHeader("Group Mirrors")) {
        HeaderRevealScope headerRevealScope = BeginAnimatedHeaderContentReveal();
        ImGui::Indent();
        int gmRemove = -1;
        int gmDragSource = -1;
        int gmDragTarget = -1;
        bool gmDropAfter = false;
        int gmPreviewRow = -1;
        bool gmPreviewAfter = false;
        ImGui::TextDisabled("Drag to reorder z-index (bottom -> top).");
        if (ImGui::BeginTable("group_mirror_items", 8, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV)) {
            ImGuiTable* groupMirrorTable = ImGui::GetCurrentTable();
            ImGui::TableSetupColumn("###grp_mir_col_drag", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight() + 6.0f);
            ImGui::TableSetupColumn("###grp_mir_col_on", ImGuiTableColumnFlags_WidthFixed, 44.0f);
            ImGui::TableSetupColumn("Mirror###grp_mir_col_mirror", ImGuiTableColumnFlags_WidthStretch, 1.6f);
            ImGui::TableSetupColumn("W%###grp_mir_col_w", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn("H%###grp_mir_col_h", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn("OX###grp_mir_col_ox", ImGuiTableColumnFlags_WidthFixed, 72.0f);
            ImGui::TableSetupColumn("OY###grp_mir_col_oy", ImGuiTableColumnFlags_WidthFixed, 72.0f);
            ImGui::TableSetupColumn("###grp_mir_col_delete", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight() + 6.0f);
            ImGui::TableHeadersRow();

            for (int j = 0; j < static_cast<int>(grp.mirrors.size()); ++j) {
                auto& gi = grp.mirrors[j];
                ImGui::PushID(j);
                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                ImGui::SetNextItemWidth(-1.0f);
                (void)ImGui::SmallButton("::##gm_drag");
                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoDisableHover)) {
                    const int payloadIndex = j;
                    ImGui::SetDragDropPayload("LINUXSCREEN_GROUP_MIRROR_REORDER", &payloadIndex, sizeof(payloadIndex));
                    ImGui::TextUnformatted(gi.mirrorId.empty() ? "[unnamed]" : gi.mirrorId.c_str());
                    ImGui::EndDragDropSource();
                }

                ImGui::TableSetColumnIndex(1);
                if (ImGui::Checkbox("##gmen", &gi.enabled)) {
                    AutoSaveConfig(config);
                }

                ImGui::TableSetColumnIndex(2);
                const std::string mirrorPreview = gi.mirrorId.empty() ? "[Select Mirror]" : gi.mirrorId;
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::BeginCombo("##gmid", mirrorPreview.c_str())) {
                    for (const auto& mirrorConf : config.mirrors) {
                        const bool selected = mirrorConf.name == gi.mirrorId;
                        if (ImGui::Selectable(mirrorConf.name.c_str(), selected)) {
                            gi.mirrorId = mirrorConf.name;
                            AutoSaveConfig(config);
                        }
                        if (selected) {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }

                bool mirrorExists = false;
                for (const auto& mirrorConf : config.mirrors) {
                    if (mirrorConf.name == gi.mirrorId) {
                        mirrorExists = true;
                        break;
                    }
                }
                if (!mirrorExists && !gi.mirrorId.empty()) {
                    ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.45f, 1.0f), "Missing");
                }

                ImGui::TableSetColumnIndex(3);
                ImGui::SetNextItemWidth(-1.0f);
                float widthPct = gi.widthPercent * 100.0f;
                if (ImGui::DragFloat("##gm_w", &widthPct, 1.0f, 10.0f, 1000.0f, "%.0f%%")) {
                    gi.widthPercent = std::clamp(widthPct / 100.0f, 0.1f, 10.0f);
                    AutoSaveConfig(config);
                }

                ImGui::TableSetColumnIndex(4);
                ImGui::SetNextItemWidth(-1.0f);
                float heightPct = gi.heightPercent * 100.0f;
                if (ImGui::DragFloat("##gm_h", &heightPct, 1.0f, 10.0f, 1000.0f, "%.0f%%")) {
                    gi.heightPercent = std::clamp(heightPct / 100.0f, 0.1f, 10.0f);
                    AutoSaveConfig(config);
                }

                ImGui::TableSetColumnIndex(5);
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::DragInt("##gm_ox", &gi.offsetX, 1)) {
                    AutoSaveConfig(config);
                }

                ImGui::TableSetColumnIndex(6);
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::DragInt("##gm_oy", &gi.offsetY, 1)) {
                    AutoSaveConfig(config);
                }

                ImGui::TableSetColumnIndex(7);
                if (ImGui::SmallButton("X##gm")) {
                    gmRemove = j;
                }
                ImRect rowRect = ImGui::TableGetCellBgRect(groupMirrorTable, 0);
                const ImRect rightRect = ImGui::TableGetCellBgRect(groupMirrorTable, 7);
                rowRect.Max.x = rightRect.Max.x;
                const float midY = (rowRect.Min.y + rowRect.Max.y) * 0.5f;
                constexpr int kGroupMirrorTableColumnCount = 8;
                for (int col = 0; col < kGroupMirrorTableColumnCount; ++col) {
                    ImGui::TableSetColumnIndex(col);
                    const ImRect cellRect = ImGui::TableGetCellBgRect(groupMirrorTable, col);
                    ImGui::PushID(col);
                    if (ImGui::BeginDragDropTargetCustom(cellRect, ImGui::GetID("##gm_row_drop_target"))) {
                        const bool dropAfter = ImGui::GetIO().MousePos.y > midY;
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("LINUXSCREEN_GROUP_MIRROR_REORDER",
                                                                                        ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
                            if (payload->DataSize == sizeof(int)) {
                                gmPreviewRow = j;
                                gmPreviewAfter = dropAfter;
                                if (payload->IsDelivery()) {
                                    gmDragSource = *static_cast<const int*>(payload->Data);
                                    gmDragTarget = j;
                                    gmDropAfter = dropAfter;
                                }
                            }
                        }
                        ImGui::EndDragDropTarget();
                    }
                    ImGui::PopID();
                }
                ImGui::TableSetColumnIndex(7);
                if (gmPreviewRow == j && ImGui::GetDragDropPayload() != nullptr) {
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    dl->AddRectFilled(rowRect.Min, rowRect.Max, IM_COL32(88, 166, 236, 34));
                    const float lineY = gmPreviewAfter ? rowRect.Max.y : rowRect.Min.y;
                    dl->AddLine(ImVec2(rowRect.Min.x, lineY),
                                ImVec2(rowRect.Max.x, lineY),
                                IM_COL32(72, 190, 255, 255),
                                2.0f);
                }

                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        if (gmDragSource >= 0 &&
            gmDragTarget >= 0 &&
            gmDragSource < static_cast<int>(grp.mirrors.size()) &&
            gmDragTarget < static_cast<int>(grp.mirrors.size()) &&
            gmDragSource != gmDragTarget) {
            int insertIndex = gmDragTarget + (gmDropAfter ? 1 : 0);
            if (gmDragSource < insertIndex) {
                insertIndex -= 1;
            }
            auto movedItem = std::move(grp.mirrors[static_cast<std::size_t>(gmDragSource)]);
            grp.mirrors.erase(grp.mirrors.begin() + gmDragSource);
            grp.mirrors.insert(grp.mirrors.begin() + insertIndex, std::move(movedItem));
            AutoSaveConfig(config);
        }
        if (gmRemove >= 0) {
            grp.mirrors.erase(grp.mirrors.begin() + gmRemove);
            AutoSaveConfig(config);
        }

        if (ImGui::BeginCombo("Add Mirror##add_mirror_to_group", "[Select Mirror]")) {
            for (const auto& mir : config.mirrors) {
                bool already = false;
                for (const auto& gmi : grp.mirrors) {
                    if (gmi.mirrorId == mir.name) {
                        already = true;
                        break;
                    }
                }
                if (!already && ImGui::Selectable(mir.name.c_str())) {
                    platform::config::MirrorGroupItem item;
                    item.mirrorId = mir.name;
                    grp.mirrors.push_back(item);
                    AutoSaveConfig(config);
                }
            }
            ImGui::EndCombo();
        }
        ImGui::Unindent();
    }

    ImGui::Separator();
    std::vector<std::string> containingModes;
    containingModes.reserve(config.modes.size());
    for (const auto& modeEntry : config.modes) {
        if (!modeEntry.name.empty() && platform::config::IsGroupInMode(modeEntry, grp.name)) {
            containingModes.push_back(modeEntry.name);
        }
    }

    std::string addToModesPreview = "[Select modes]";
    if (containingModes.size() == 1) {
        addToModesPreview = containingModes.front();
    } else if (!containingModes.empty()) {
        addToModesPreview = std::to_string(containingModes.size()) + " modes selected";
    }

    if (ImGui::BeginCombo("Add to Modes##group_refs_add_to_modes", addToModesPreview.c_str())) {
        for (auto& candidateMode : config.modes) {
            if (candidateMode.name.empty()) {
                continue;
            }

            const bool isInMode = platform::config::IsGroupInMode(candidateMode, grp.name);
            if (ImGui::Selectable(candidateMode.name.c_str(),
                                  isInMode,
                                  ImGuiSelectableFlags_DontClosePopups)) {
                if (isInMode) {
                    platform::config::RemoveGroupFromMode(candidateMode, grp.name);
                } else {
                    platform::config::AddGroupToMode(candidateMode, grp.name);
                }
                AutoSaveConfig(config);
            }
        }
        ImGui::EndCombo();
    }

    containingModes.clear();
    for (const auto& modeEntry : config.modes) {
        if (!modeEntry.name.empty() && platform::config::IsGroupInMode(modeEntry, grp.name)) {
            containingModes.push_back(modeEntry.name);
        }
    }

    if (!containingModes.empty()) {
        ImGui::Text("Used in modes:");
        for (const auto& modeName : containingModes) {
            ImGui::BulletText("%s", modeName.c_str());
        }
    } else {
        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.2f, 1.0f), "Not used in any mode");
    }

    ImGui::PopID();
    ImGui::PopID();
}

bool IsMirrorDirectEditActive() {
    return g_mirrorEditorState.directEditActive;
}

void SetMirrorDirectEditActive(bool active, bool hideMainWindow) {
    g_mirrorEditorState.directEditActive = active;
    g_mirrorEditorState.directEditHideMainWindow = hideMainWindow;
    g_mirrorEditorState.directEditFullscreenHovered = false;
    g_mirrorEditorState.directEditFullscreenActive = false;
    g_mirrorEditorState.visualDrag = MirrorEditorState::VisualDragState{};
    if (!active) {
        g_mirrorEditorState.directEditSelection = MirrorEditorState::DirectEditSelection{};
        g_mirrorEditorState.directEditShowGroupInspector = false;
    }
}

void ExitMirrorDirectEditToSettings(bool* inOutGuiVisible) {
    SetMirrorDirectEditActive(false, false);
    g_mirrorEditorState.directEditHideMainWindow = false;
    SetGuiVisible(true);
    if (inOutGuiVisible) {
        *inOutGuiVisible = true;
    }
}

void RenderMirrorDirectEditOverlay(platform::config::LinuxscreenConfig& config,
                                   float displayWidth,
                                   float displayHeight,
                                   bool* inOutGuiVisible) {
    if (!g_mirrorEditorState.directEditActive || !(displayWidth > 0.0f) || !(displayHeight > 0.0f)) {
        g_mirrorEditorState.directEditFullscreenHovered = false;
        g_mirrorEditorState.directEditFullscreenActive = false;
        return;
    }

    if (g_mirrorEditorState.directEditHideMainWindow && inOutGuiVisible) {
        *inOutGuiVisible = false;
    }

    const MirrorDirectEditViewportContext viewportContext =
        BuildMirrorDirectEditViewportContext(config, displayWidth, displayHeight);
    const bool shiftHeld = IsDirectEditShiftHeld();
    const bool altHeld = IsDirectEditAltHeld();
    const MirrorVisualEditorMode effectiveMode = ResolveEffectiveDirectEditMode();
    const bool selectionSupportsCrop =
        g_mirrorEditorState.directEditSelection.kind == MirrorDirectEditSelectionKind::Mirror ||
        g_mirrorEditorState.directEditSelection.kind == MirrorDirectEditSelectionKind::GroupItem;
    const MirrorVisualEditorMode activeVisualMode = g_mirrorEditorState.visualDrag.active
        ? (g_mirrorEditorState.visualDrag.crop ? MirrorVisualEditorMode::Crop : MirrorVisualEditorMode::Layout)
        : effectiveMode;
    const bool freeResizeActive = activeVisualMode == MirrorVisualEditorMode::Layout && shiftHeld;
    const bool cropColorActive =
        (g_mirrorEditorState.visualDrag.active && g_mirrorEditorState.visualDrag.crop) ||
        (!g_mirrorEditorState.visualDrag.active && altHeld && selectionSupportsCrop);
    std::vector<DirectEditResolvedItem> items;
    items.reserve(config.mirrors.size());
    for (const auto& resolved : GetMirrorModeState().GetActiveMirrorRenderList()) {
        ImRect rect;
        if (!ResolveOutputRectForDirectEdit(resolved.config, viewportContext, rect)) {
            continue;
        }
        items.push_back(DirectEditResolvedItem{resolved, rect});
    }
    std::vector<DirectEditGroupTarget> groupTargets;
    for (const auto& group : config.mirrorGroups) {
        if (group.name.empty()) {
            continue;
        }
        ImRect groupRect;
        if (ResolveDirectEditGroupRect(items, group.name, groupRect)) {
            groupTargets.push_back(DirectEditGroupTarget{group.name, groupRect});
        }
    }

    RefreshDirectEditSelectionForCurrentMode(config, items, groupTargets);

    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(displayWidth, displayHeight), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.0f);
    const ImGuiWindowFlags overlayFlags = ImGuiWindowFlags_NoDecoration |
                                          ImGuiWindowFlags_NoSavedSettings |
                                          ImGuiWindowFlags_NoMove |
                                          ImGuiWindowFlags_NoBackground |
                                          ImGuiWindowFlags_NoBringToFrontOnFocus |
                                          ImGuiWindowFlags_NoNav;
    const bool fullscreenBegun = ImGui::Begin("##mirror_direct_edit_fullscreen", nullptr, overlayFlags);
    g_mirrorEditorState.directEditFullscreenActive = fullscreenBegun;
    if (fullscreenBegun) {
        ImGui::SetCursorScreenPos(ImGui::GetWindowPos());
        ImGui::InvisibleButton("##mirror_direct_edit_capture", ImGui::GetWindowSize());
        const bool fullscreenSurfaceHovered = ImGui::IsItemHovered();
        const bool fullscreenWindowHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        g_mirrorEditorState.directEditFullscreenHovered = fullscreenSurfaceHovered || fullscreenWindowHovered;

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImVec2 mousePos = ImGui::GetIO().MousePos;
        int selectedItemIndex = -1;
        for (int i = 0; i < static_cast<int>(items.size()); ++i) {
            const auto& item = items[static_cast<std::size_t>(i)];
            const bool selectedMirror = g_mirrorEditorState.directEditSelection.kind == MirrorDirectEditSelectionKind::Mirror &&
                                        item.resolved.sourceKind == ResolvedMirrorSourceKind::Mirror &&
                                        g_mirrorEditorState.directEditSelection.mirrorId == item.resolved.sourceMirrorId;
            const bool selectedGroupItem = g_mirrorEditorState.directEditSelection.kind == MirrorDirectEditSelectionKind::GroupItem &&
                                           item.resolved.sourceKind == ResolvedMirrorSourceKind::GroupItem &&
                                           g_mirrorEditorState.directEditSelection.mirrorId == item.resolved.sourceMirrorId &&
                                           g_mirrorEditorState.directEditSelection.groupId == item.resolved.sourceGroupId &&
                                           g_mirrorEditorState.directEditSelection.groupItemIndex == item.resolved.sourceGroupItemIndex;
            if (selectedMirror || selectedGroupItem) {
                selectedItemIndex = i;
                break;
            }
        }
        if (selectedItemIndex < 0 &&
            g_mirrorEditorState.directEditSelection.kind == MirrorDirectEditSelectionKind::Group &&
            !g_mirrorEditorState.directEditSelection.groupId.empty()) {
            for (int i = 0; i < static_cast<int>(items.size()); ++i) {
                if (items[static_cast<std::size_t>(i)].resolved.sourceGroupId == g_mirrorEditorState.directEditSelection.groupId) {
                    selectedItemIndex = i;
                    break;
                }
            }
        }

        int hoveredEdgeMask = kMirrorVisualEdgeNone;
        std::vector<DirectEditHitTarget> clickCandidates;
        ImRect selectedInteractionRect;
        const bool selectedGroupDirectly =
            g_mirrorEditorState.directEditSelection.kind == MirrorDirectEditSelectionKind::Group;
        if (selectedItemIndex >= 0) {
            selectedInteractionRect = items[static_cast<std::size_t>(selectedItemIndex)].rect;
            if (selectedGroupDirectly) {
                ImRect groupRect;
                if (ResolveDirectEditGroupRect(items, g_mirrorEditorState.directEditSelection.groupId, groupRect)) {
                    selectedInteractionRect = groupRect;
                }
            }

            const MirrorVisualEditorMode selectedHandleMode =
                selectedGroupDirectly ? MirrorVisualEditorMode::Layout : activeVisualMode;
            hoveredEdgeMask = ResolveHoveredHandleEdgeMask(selectedInteractionRect,
                                                           kDirectEditHandleRadius,
                                                           mousePos,
                                                           selectedHandleMode);
            if ((hoveredEdgeMask & (kMirrorVisualEdgeLeft | kMirrorVisualEdgeRight)) != 0 &&
                       (hoveredEdgeMask & (kMirrorVisualEdgeTop | kMirrorVisualEdgeBottom)) != 0) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNWSE);
            } else if ((hoveredEdgeMask & (kMirrorVisualEdgeLeft | kMirrorVisualEdgeRight)) != 0) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            } else if ((hoveredEdgeMask & (kMirrorVisualEdgeTop | kMirrorVisualEdgeBottom)) != 0) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
            } else if (selectedInteractionRect.Contains(mousePos) &&
                       selectedHandleMode == MirrorVisualEditorMode::Layout) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
            }
        }
        auto pushHitTarget = [&](const DirectEditHitTarget& target) {
            for (const auto& existing : clickCandidates) {
                if (existing.key == target.key) {
                    return;
                }
            }
            clickCandidates.push_back(target);
        };

        for (int i = static_cast<int>(items.size()) - 1; i >= 0; --i) {
            const auto& item = items[static_cast<std::size_t>(i)];
            if (!item.rect.Contains(mousePos)) {
                continue;
            }
            MirrorEditorState::DirectEditSelection selection;
            selection.kind = (item.resolved.sourceKind == ResolvedMirrorSourceKind::GroupItem)
                ? MirrorDirectEditSelectionKind::GroupItem
                : MirrorDirectEditSelectionKind::Mirror;
            selection.mirrorId = item.resolved.sourceMirrorId;
            selection.groupId = item.resolved.sourceGroupId;
            selection.groupItemIndex = item.resolved.sourceGroupItemIndex;
            DirectEditHitTarget target;
            target.kind = DirectEditHitTargetKind::Item;
            target.selection = selection;
            target.rect = item.rect;
            target.itemIndex = i;
            target.key = BuildDirectEditCycleKey(selection);
            pushHitTarget(target);
        }
        for (const auto& groupTarget : groupTargets) {
            if (!groupTarget.rect.Contains(mousePos)) {
                continue;
            }
            MirrorEditorState::DirectEditSelection selection;
            selection.kind = MirrorDirectEditSelectionKind::Group;
            selection.groupId = groupTarget.groupId;
            selection.mirrorId =
                ResolveDirectEditGroupMirrorId(items, g_mirrorEditorState.directEditSelection, groupTarget.groupId);
            selection.groupItemIndex = -1;
            DirectEditHitTarget target;
            target.kind = DirectEditHitTargetKind::Group;
            target.selection = selection;
            target.rect = groupTarget.rect;
            target.itemIndex = -1;
            target.key = BuildDirectEditCycleKey(selection);
            pushHitTarget(target);
        }

        std::string cycleStackKey;
        int previewCycleIndex = -1;
        DirectEditHitTarget previewTarget;
        bool hasPreviewTarget = false;
        if (!clickCandidates.empty()) {
            for (const auto& candidate : clickCandidates) {
                if (!cycleStackKey.empty()) {
                    cycleStackKey += ">";
                }
                cycleStackKey += candidate.key;
            }
            const bool sameCycleStack = cycleStackKey == g_mirrorEditorState.directEditLastCycleStackKey;
            const bool interactingSelectedHandle = hoveredEdgeMask != kMirrorVisualEdgeNone && selectedItemIndex >= 0;
            if (sameCycleStack && !interactingSelectedHandle) {
                const std::string currentSelectionKey =
                    BuildDirectEditCycleKey(g_mirrorEditorState.directEditSelection);
                int currentSelectionCandidate = -1;
                for (int i = 0; i < static_cast<int>(clickCandidates.size()); ++i) {
                    if (clickCandidates[static_cast<std::size_t>(i)].key == currentSelectionKey) {
                        currentSelectionCandidate = i;
                        break;
                    }
                }
                previewCycleIndex =
                    (currentSelectionCandidate >= 0)
                        ? ((currentSelectionCandidate + 1) % static_cast<int>(clickCandidates.size()))
                        : 0;
            } else {
                previewCycleIndex = 0;
            }
            previewTarget = clickCandidates[static_cast<std::size_t>(previewCycleIndex)];
            hasPreviewTarget = true;
        }

        DirectEditHitTarget currentInteractionTarget;
        bool hasCurrentInteractionTarget = false;
        if (selectedItemIndex >= 0) {
            if (selectedGroupDirectly) {
                currentInteractionTarget.kind = DirectEditHitTargetKind::Group;
                currentInteractionTarget.selection.kind = MirrorDirectEditSelectionKind::Group;
                currentInteractionTarget.selection.groupId = g_mirrorEditorState.directEditSelection.groupId;
                currentInteractionTarget.selection.mirrorId = ResolveDirectEditGroupMirrorId(items,
                                                                                             g_mirrorEditorState.directEditSelection,
                                                                                             g_mirrorEditorState.directEditSelection.groupId);
                currentInteractionTarget.selection.groupItemIndex = -1;
                currentInteractionTarget.rect = selectedInteractionRect;
                currentInteractionTarget.itemIndex = selectedItemIndex;
                currentInteractionTarget.key = BuildDirectEditCycleKey(currentInteractionTarget.selection);
                hasCurrentInteractionTarget = true;
            } else {
                const auto& selectedItem = items[static_cast<std::size_t>(selectedItemIndex)];
                currentInteractionTarget.kind = DirectEditHitTargetKind::Item;
                currentInteractionTarget.selection.kind =
                    selectedItem.resolved.sourceKind == ResolvedMirrorSourceKind::GroupItem
                        ? MirrorDirectEditSelectionKind::GroupItem
                        : MirrorDirectEditSelectionKind::Mirror;
                currentInteractionTarget.selection.mirrorId = selectedItem.resolved.sourceMirrorId;
                currentInteractionTarget.selection.groupId = selectedItem.resolved.sourceGroupId;
                currentInteractionTarget.selection.groupItemIndex = selectedItem.resolved.sourceGroupItemIndex;
                currentInteractionTarget.rect = selectedItem.rect;
                currentInteractionTarget.itemIndex = selectedItemIndex;
                currentInteractionTarget.key = BuildDirectEditCycleKey(currentInteractionTarget.selection);
                hasCurrentInteractionTarget = true;
            }
        }

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && fullscreenSurfaceHovered) {
            DirectEditHitTarget clickedTarget;
            bool hasClickedTarget = false;
            const bool currentTargetUnderCursor =
                hasCurrentInteractionTarget &&
                (hoveredEdgeMask != kMirrorVisualEdgeNone || currentInteractionTarget.rect.Contains(mousePos));
            if (currentTargetUnderCursor) {
                clickedTarget = currentInteractionTarget;
                hasClickedTarget = true;
                if (hasPreviewTarget) {
                    g_mirrorEditorState.directEditCycleIndex = std::max(0, previewCycleIndex);
                    g_mirrorEditorState.directEditLastCyclePos = mousePos;
                    g_mirrorEditorState.directEditLastCycleStackKey = cycleStackKey;
                }
                if (hasPreviewTarget && previewTarget.key != currentInteractionTarget.key) {
                    g_mirrorEditorState.directEditPendingClickSelection = previewTarget.selection;
                    g_mirrorEditorState.directEditHasPendingClickSelection = true;
                } else {
                    g_mirrorEditorState.directEditPendingClickSelection = MirrorEditorState::DirectEditSelection{};
                    g_mirrorEditorState.directEditHasPendingClickSelection = false;
                }
            } else if (hasPreviewTarget) {
                g_mirrorEditorState.directEditCycleIndex = std::max(0, previewCycleIndex);
                g_mirrorEditorState.directEditLastCyclePos = mousePos;
                g_mirrorEditorState.directEditLastCycleStackKey = cycleStackKey;
                clickedTarget = previewTarget;
                hasClickedTarget = true;
                g_mirrorEditorState.directEditPendingClickSelection = MirrorEditorState::DirectEditSelection{};
                g_mirrorEditorState.directEditHasPendingClickSelection = false;
            } else {
                g_mirrorEditorState.directEditCycleIndex = 0;
                g_mirrorEditorState.directEditLastCyclePos = mousePos;
                g_mirrorEditorState.directEditLastCycleStackKey.clear();
                g_mirrorEditorState.directEditPendingClickSelection = MirrorEditorState::DirectEditSelection{};
                g_mirrorEditorState.directEditHasPendingClickSelection = false;
            }

            if (hasClickedTarget) {
                const int clickedItemIndex = clickedTarget.itemIndex;
                const auto* clickedItem =
                    (clickedItemIndex >= 0 && clickedItemIndex < static_cast<int>(items.size()))
                        ? &items[static_cast<std::size_t>(clickedItemIndex)]
                        : nullptr;
                const bool clickedGroupTarget = clickedTarget.selection.kind == MirrorDirectEditSelectionKind::Group;
                const MirrorVisualEditorMode clickedTargetMode =
                    clickedGroupTarget ? MirrorVisualEditorMode::Layout : effectiveMode;
                const int clickedEdgeMask =
                    ResolveHoveredHandleEdgeMask(clickedTarget.rect,
                                                 kDirectEditHandleRadius,
                                                 mousePos,
                                                 clickedTargetMode);
                g_mirrorEditorState.directEditSelection = clickedTarget.selection;
                g_mirrorEditorState.directEditShowGroupInspector =
                    clickedTarget.selection.kind == MirrorDirectEditSelectionKind::Group;
                SyncMirrorDirectEditSelectionToSidebar(config);

                const bool cropModeActive =
                    (clickedTargetMode == MirrorVisualEditorMode::Crop) &&
                    g_mirrorEditorState.directEditSelection.kind != MirrorDirectEditSelectionKind::Group;
                const bool shouldStartDrag =
                    clickedEdgeMask != kMirrorVisualEdgeNone || cropModeActive ||
                    clickedTarget.rect.Contains(mousePos);
                g_mirrorEditorState.visualDrag = MirrorEditorState::VisualDragState{};
                if (shouldStartDrag) {
                    g_mirrorEditorState.visualDrag.active = true;
                    g_mirrorEditorState.visualDrag.crop = cropModeActive;
                    g_mirrorEditorState.visualDrag.edgeMask = clickedEdgeMask;
                    g_mirrorEditorState.visualDrag.dragStartMouse = mousePos;
                    g_mirrorEditorState.visualDrag.startOutputWidth = clickedTarget.rect.GetWidth();
                    g_mirrorEditorState.visualDrag.startOutputHeight = clickedTarget.rect.GetHeight();
                    g_mirrorEditorState.visualDrag.startCaptureWidth = clickedItem ? clickedItem->resolved.config.captureWidth : 0.0f;
                    g_mirrorEditorState.visualDrag.startCaptureHeight = clickedItem ? clickedItem->resolved.config.captureHeight : 0.0f;
                    const int dynamicBorder = clickedItem
                        ? platform::config::GetMirrorDynamicBorderPadding(clickedItem->resolved.config.border)
                        : 0;
                    const float baseWidth = std::max(1.0f, g_mirrorEditorState.visualDrag.startCaptureWidth + (2.0f * static_cast<float>(dynamicBorder)));
                    const float baseHeight = std::max(1.0f, g_mirrorEditorState.visualDrag.startCaptureHeight + (2.0f * static_cast<float>(dynamicBorder)));
                    g_mirrorEditorState.visualDrag.startOutputScaleX =
                        std::clamp(g_mirrorEditorState.visualDrag.startOutputWidth / baseWidth,
                                   kDirectEditOutputScaleMin,
                                   kDirectEditOutputScaleMax);
                    g_mirrorEditorState.visualDrag.startOutputScaleY =
                        std::clamp(g_mirrorEditorState.visualDrag.startOutputHeight / baseHeight,
                                   kDirectEditOutputScaleMin,
                                   kDirectEditOutputScaleMax);
                    g_mirrorEditorState.visualDrag.startGroupItemWidthPercent = 1.0f;
                    g_mirrorEditorState.visualDrag.startGroupItemHeightPercent = 1.0f;
                    g_mirrorEditorState.visualDrag.startGroupItemOffsetX = 0;
                    g_mirrorEditorState.visualDrag.startGroupItemOffsetY = 0;
                    g_mirrorEditorState.visualDrag.startGroupOutputX = 0;
                    g_mirrorEditorState.visualDrag.startGroupOutputY = 0;
                    g_mirrorEditorState.visualDrag.startGroupUseRelativeSize = false;
                    g_mirrorEditorState.visualDrag.startGroupRelativeWidth = 1.0f;
                    g_mirrorEditorState.visualDrag.startGroupRelativeHeight = 1.0f;
                    g_mirrorEditorState.visualDrag.startGroupScale = 1.0f;
                    g_mirrorEditorState.visualDrag.startGroupScaleX = 1.0f;
                    g_mirrorEditorState.visualDrag.startGroupScaleY = 1.0f;
                    if (cropModeActive) {
                        platform::config::MirrorConfig* mirror = FindMirrorConfigByName(config, g_mirrorEditorState.directEditSelection.mirrorId);
                        if (mirror) {
                            const int zoneIndex = ResolveDirectEditCaptureZoneIndex(*mirror);
                            if (zoneIndex >= 0 && zoneIndex < static_cast<int>(mirror->input.size())) {
                                float sourceWidth = 0.0f;
                                float sourceHeight = 0.0f;
                                if (ResolveMirrorSourceSizeForDirectEdit(*mirror, viewportContext, sourceWidth, sourceHeight)) {
                                    NormalizeCropZoneAnchorForDirectEdit(*mirror,
                                                                         mirror->input[static_cast<std::size_t>(zoneIndex)],
                                                                         viewportContext,
                                                                         sourceWidth,
                                                                         sourceHeight);
                                    ImRect cropRect;
                                    if (ResolveCropRectInSourceForDirectEdit(*mirror,
                                                                             mirror->input[static_cast<std::size_t>(zoneIndex)],
                                                                             viewportContext,
                                                                             sourceWidth,
                                                                             sourceHeight,
                                                                             cropRect)) {
                                        g_mirrorEditorState.visualDrag.startRect = ImVec4(cropRect.Min.x,
                                                                                          cropRect.Min.y,
                                                                                          cropRect.Max.x,
                                                                                          cropRect.Max.y);
                                    }
                                }
                            }
                        }
                    } else {
                        g_mirrorEditorState.visualDrag.startRect = ImVec4(clickedTarget.rect.Min.x,
                                                                          clickedTarget.rect.Min.y,
                                                                          clickedTarget.rect.Max.x,
                                                                          clickedTarget.rect.Max.y);
                    }
                    if (!g_mirrorEditorState.directEditSelection.groupId.empty()) {
                        platform::config::MirrorGroupConfig* group =
                            FindMirrorGroupByName(config, g_mirrorEditorState.directEditSelection.groupId);
                        if (group) {
                            CaptureDirectEditGroupStartState(g_mirrorEditorState.visualDrag, *group, items);
                            ResolveDirectEditOutputPixels(group->output,
                                                          viewportContext,
                                                          g_mirrorEditorState.visualDrag.startGroupOutputX,
                                                          g_mirrorEditorState.visualDrag.startGroupOutputY);
                            g_mirrorEditorState.visualDrag.startGroupUseRelativeSize = group->output.useRelativeSize;
                            g_mirrorEditorState.visualDrag.startGroupRelativeWidth = group->output.relativeWidth;
                            g_mirrorEditorState.visualDrag.startGroupRelativeHeight = group->output.relativeHeight;
                            g_mirrorEditorState.visualDrag.startGroupScale = group->output.scale;
                            g_mirrorEditorState.visualDrag.startGroupScaleX =
                                group->output.separateScale ? group->output.scaleX : group->output.scale;
                            g_mirrorEditorState.visualDrag.startGroupScaleY =
                                group->output.separateScale ? group->output.scaleY : group->output.scale;
                            if (g_mirrorEditorState.directEditSelection.groupItemIndex >= 0 &&
                                g_mirrorEditorState.directEditSelection.groupItemIndex < static_cast<int>(group->mirrors.size())) {
                                const auto& groupItem =
                                    group->mirrors[static_cast<std::size_t>(g_mirrorEditorState.directEditSelection.groupItemIndex)];
                                g_mirrorEditorState.visualDrag.startGroupItemWidthPercent = groupItem.widthPercent;
                                g_mirrorEditorState.visualDrag.startGroupItemHeightPercent = groupItem.heightPercent;
                                g_mirrorEditorState.visualDrag.startGroupItemOffsetX = groupItem.offsetX;
                                g_mirrorEditorState.visualDrag.startGroupItemOffsetY = groupItem.offsetY;
                            }
                        }
                    }
                }
            }
        }

        if (g_mirrorEditorState.visualDrag.active && selectedItemIndex >= 0) {
            auto applySelectionMutation = [&](auto&& mutator) {
                auto& selection = g_mirrorEditorState.directEditSelection;
                if (selection.kind == MirrorDirectEditSelectionKind::None) {
                    return false;
                }
                platform::config::MirrorConfig* mirror = FindMirrorConfigByName(config, selection.mirrorId);
                if (!mirror) {
                    return false;
                }
                return mutator(*mirror);
            };

            const auto& selectedResolvedItem = items[static_cast<std::size_t>(selectedItemIndex)];
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                const bool preserveAspectRatio = !shiftHeld;
                const ImVec2 delta = mousePos - g_mirrorEditorState.visualDrag.dragStartMouse;
                const float dragDistanceSq = (delta.x * delta.x) + (delta.y * delta.y);
                if (!g_mirrorEditorState.visualDrag.moved &&
                    dragDistanceSq < (kDirectEditDragThreshold * kDirectEditDragThreshold)) {
                    // Keep selection but do not quantize/apply until the pointer has actually moved.
                } else if (!g_mirrorEditorState.visualDrag.crop) {
                    g_mirrorEditorState.visualDrag.moved = true;
                    ImRect updatedRect(ImVec2(g_mirrorEditorState.visualDrag.startRect.x,
                                              g_mirrorEditorState.visualDrag.startRect.y),
                                       ImVec2(g_mirrorEditorState.visualDrag.startRect.z,
                                              g_mirrorEditorState.visualDrag.startRect.w));
                    updatedRect = ApplyDirectEditResizeDelta(updatedRect,
                                                             g_mirrorEditorState.visualDrag.edgeMask,
                                                             delta,
                                                             preserveAspectRatio);
                    float maxWidth = 40000.0f;
                    float maxHeight = 40000.0f;
                    if (g_mirrorEditorState.directEditSelection.kind == MirrorDirectEditSelectionKind::Mirror) {
                        const int dynamicBorder =
                            platform::config::GetMirrorDynamicBorderPadding(selectedResolvedItem.resolved.config.border);
                        maxWidth = static_cast<float>(selectedResolvedItem.resolved.config.captureWidth + (2 * dynamicBorder)) *
                                   kDirectEditOutputScaleMax;
                        maxHeight = static_cast<float>(selectedResolvedItem.resolved.config.captureHeight + (2 * dynamicBorder)) *
                                    kDirectEditOutputScaleMax;
                    } else if (g_mirrorEditorState.directEditSelection.kind == MirrorDirectEditSelectionKind::Group) {
                        const float startWidth =
                            std::max(1.0f, g_mirrorEditorState.visualDrag.startRect.z - g_mirrorEditorState.visualDrag.startRect.x);
                        const float startHeight =
                            std::max(1.0f, g_mirrorEditorState.visualDrag.startRect.w - g_mirrorEditorState.visualDrag.startRect.y);
                        float maxWidthRatio = kDirectEditOutputScaleMax;
                        float maxHeightRatio = kDirectEditOutputScaleMax;
                        if (g_mirrorEditorState.visualDrag.startGroupUseRelativeSize) {
                            maxWidthRatio = kDirectEditOutputScaleMax /
                                            std::max(kDirectEditOutputScaleMin,
                                                     g_mirrorEditorState.visualDrag.startGroupRelativeWidth);
                            maxHeightRatio = kDirectEditOutputScaleMax /
                                             std::max(kDirectEditOutputScaleMin,
                                                      g_mirrorEditorState.visualDrag.startGroupRelativeHeight);
                        } else {
                            maxWidthRatio = kDirectEditOutputScaleMax /
                                            std::max(kDirectEditOutputScaleMin,
                                                     g_mirrorEditorState.visualDrag.startGroupScaleX);
                            maxHeightRatio = kDirectEditOutputScaleMax /
                                             std::max(kDirectEditOutputScaleMin,
                                                      g_mirrorEditorState.visualDrag.startGroupScaleY);
                        }
                        maxWidth = startWidth * std::max(1.0f, maxWidthRatio);
                        maxHeight = startHeight * std::max(1.0f, maxHeightRatio);
                    } else if (g_mirrorEditorState.directEditSelection.kind == MirrorDirectEditSelectionKind::GroupItem) {
                        const float startWidth =
                            std::max(1.0f, g_mirrorEditorState.visualDrag.startRect.z - g_mirrorEditorState.visualDrag.startRect.x);
                        const float startHeight =
                            std::max(1.0f, g_mirrorEditorState.visualDrag.startRect.w - g_mirrorEditorState.visualDrag.startRect.y);
                        maxWidth = startWidth * (kGroupItemPercentMax / std::max(kGroupItemPercentMin, g_mirrorEditorState.visualDrag.startGroupItemWidthPercent));
                        maxHeight = startHeight * (kGroupItemPercentMax / std::max(kGroupItemPercentMin, g_mirrorEditorState.visualDrag.startGroupItemHeightPercent));
                    }
                    updatedRect = ClampDirectEditRectToSizeLimits(
                        ImRect(ImVec2(g_mirrorEditorState.visualDrag.startRect.x,
                                      g_mirrorEditorState.visualDrag.startRect.y),
                               ImVec2(g_mirrorEditorState.visualDrag.startRect.z,
                                      g_mirrorEditorState.visualDrag.startRect.w)),
                        g_mirrorEditorState.visualDrag.edgeMask,
                        updatedRect,
                        12.0f,
                        12.0f,
                        std::max(12.0f, maxWidth),
                        std::max(12.0f, maxHeight),
                        preserveAspectRatio);

                    bool changed = false;
                    if (g_mirrorEditorState.directEditSelection.kind == MirrorDirectEditSelectionKind::Mirror) {
                        changed = applySelectionMutation([&](platform::config::MirrorConfig& mirror) {
                            ApplyOutputRectToMirror(mirror, updatedRect, viewportContext);
                            return true;
                        });
                    } else if (g_mirrorEditorState.directEditSelection.kind == MirrorDirectEditSelectionKind::Group) {
                        platform::config::MirrorGroupConfig* group =
                            FindMirrorGroupByName(config, g_mirrorEditorState.directEditSelection.groupId);
                        if (group) {
                            ApplyOutputRectPositionToRenderConfig(group->output, updatedRect, viewportContext);
                            if (g_mirrorEditorState.visualDrag.edgeMask != kMirrorVisualEdgeNone) {
                                const float startGroupWidth = std::max(1.0f,
                                                                       g_mirrorEditorState.visualDrag.startRect.z -
                                                                           g_mirrorEditorState.visualDrag.startRect.x);
                                const float startGroupHeight = std::max(1.0f,
                                                                        g_mirrorEditorState.visualDrag.startRect.w -
                                                                            g_mirrorEditorState.visualDrag.startRect.y);
                                const float scaleX = updatedRect.GetWidth() / startGroupWidth;
                                const float scaleY = updatedRect.GetHeight() / startGroupHeight;
                                float resizeRatioX = std::clamp(scaleX, kDirectEditOutputScaleMin, kDirectEditOutputScaleMax);
                                float resizeRatioY = std::clamp(scaleY, kDirectEditOutputScaleMin, kDirectEditOutputScaleMax);
                                float uniformRatio = 1.0f;
                                const bool horizontalResize =
                                    (g_mirrorEditorState.visualDrag.edgeMask & (kMirrorVisualEdgeLeft | kMirrorVisualEdgeRight)) != 0;
                                const bool verticalResize =
                                    (g_mirrorEditorState.visualDrag.edgeMask & (kMirrorVisualEdgeTop | kMirrorVisualEdgeBottom)) != 0;
                                if (horizontalResize && verticalResize) {
                                    uniformRatio =
                                        std::clamp((resizeRatioX + resizeRatioY) * 0.5f,
                                                   kDirectEditOutputScaleMin,
                                                   kDirectEditOutputScaleMax);
                                } else if (horizontalResize) {
                                    uniformRatio = resizeRatioX;
                                } else if (verticalResize) {
                                    uniformRatio = resizeRatioY;
                                }
                                if (g_mirrorEditorState.visualDrag.startGroupUseRelativeSize) {
                                    group->output.useRelativeSize = true;
                                    group->output.preserveAspectRatio = preserveAspectRatio;
                                    group->output.separateScale = !preserveAspectRatio;
                                    if (preserveAspectRatio) {
                                        group->output.relativeWidth = std::clamp(
                                            g_mirrorEditorState.visualDrag.startGroupRelativeWidth * uniformRatio,
                                            kDirectEditOutputScaleMin,
                                            kDirectEditOutputScaleMax);
                                        group->output.relativeHeight = std::clamp(
                                            g_mirrorEditorState.visualDrag.startGroupRelativeHeight * uniformRatio,
                                            kDirectEditOutputScaleMin,
                                            kDirectEditOutputScaleMax);
                                        group->output.scale = uniformRatio;
                                        group->output.scaleX = uniformRatio;
                                        group->output.scaleY = uniformRatio;
                                    } else {
                                        group->output.relativeWidth = std::clamp(
                                            g_mirrorEditorState.visualDrag.startGroupRelativeWidth * resizeRatioX,
                                            kDirectEditOutputScaleMin,
                                            kDirectEditOutputScaleMax);
                                        group->output.relativeHeight = std::clamp(
                                            g_mirrorEditorState.visualDrag.startGroupRelativeHeight * resizeRatioY,
                                            kDirectEditOutputScaleMin,
                                            kDirectEditOutputScaleMax);
                                        group->output.scale = g_mirrorEditorState.visualDrag.startGroupScaleX * resizeRatioX;
                                        group->output.scaleX = std::clamp(
                                            g_mirrorEditorState.visualDrag.startGroupScaleX * resizeRatioX,
                                            kDirectEditOutputScaleMin,
                                            kDirectEditOutputScaleMax);
                                        group->output.scaleY = std::clamp(
                                            g_mirrorEditorState.visualDrag.startGroupScaleY * resizeRatioY,
                                            kDirectEditOutputScaleMin,
                                            kDirectEditOutputScaleMax);
                                    }
                                } else {
                                    group->output.useRelativeSize = false;
                                    group->output.preserveAspectRatio = preserveAspectRatio;
                                    group->output.separateScale = !preserveAspectRatio;
                                    if (preserveAspectRatio) {
                                        const float nextScale = std::clamp(
                                            g_mirrorEditorState.visualDrag.startGroupScale * uniformRatio,
                                            kDirectEditOutputScaleMin,
                                            kDirectEditOutputScaleMax);
                                        group->output.scale = nextScale;
                                        group->output.scaleX = nextScale;
                                        group->output.scaleY = nextScale;
                                    } else {
                                        group->output.scaleX = std::clamp(
                                            g_mirrorEditorState.visualDrag.startGroupScaleX * resizeRatioX,
                                            kDirectEditOutputScaleMin,
                                            kDirectEditOutputScaleMax);
                                        group->output.scaleY = std::clamp(
                                            g_mirrorEditorState.visualDrag.startGroupScaleY * resizeRatioY,
                                            kDirectEditOutputScaleMin,
                                            kDirectEditOutputScaleMax);
                                        group->output.scale = group->output.scaleX;
                                    }
                                }
                            }
                            changed = true;
                        }
                    } else if (g_mirrorEditorState.directEditSelection.kind == MirrorDirectEditSelectionKind::GroupItem) {
                        platform::config::MirrorGroupConfig* group = FindMirrorGroupByName(config, g_mirrorEditorState.directEditSelection.groupId);
                        if (group &&
                            g_mirrorEditorState.directEditSelection.groupItemIndex >= 0 &&
                            g_mirrorEditorState.directEditSelection.groupItemIndex < static_cast<int>(group->mirrors.size())) {
                            auto& item = group->mirrors[static_cast<std::size_t>(g_mirrorEditorState.directEditSelection.groupItemIndex)];
                            const float startWidth = std::max(1.0f, g_mirrorEditorState.visualDrag.startRect.z - g_mirrorEditorState.visualDrag.startRect.x);
                            const float startHeight = std::max(1.0f, g_mirrorEditorState.visualDrag.startRect.w - g_mirrorEditorState.visualDrag.startRect.y);
                            const float widthRatio = std::clamp(updatedRect.GetWidth() / startWidth, 0.1f, 10.0f);
                            const float heightRatio = std::clamp(updatedRect.GetHeight() / startHeight, 0.1f, 10.0f);
                            float groupScaleX = 1.0f;
                            float groupScaleY = 1.0f;
                            ResolveDirectEditGroupItemScale(config,
                                                            g_mirrorEditorState.directEditSelection,
                                                            viewportContext,
                                                            g_mirrorEditorState.visualDrag,
                                                            groupScaleX,
                                                            groupScaleY);
                            const float localDeltaX =
                                (updatedRect.Min.x - g_mirrorEditorState.visualDrag.startRect.x) / std::max(0.0001f, groupScaleX);
                            const float localDeltaY =
                                (updatedRect.Min.y - g_mirrorEditorState.visualDrag.startRect.y) / std::max(0.0001f, groupScaleY);

                            item.offsetX = g_mirrorEditorState.visualDrag.startGroupItemOffsetX +
                                           static_cast<int>(std::round(localDeltaX));
                            item.offsetY = g_mirrorEditorState.visualDrag.startGroupItemOffsetY +
                                           static_cast<int>(std::round(localDeltaY));
                            if (preserveAspectRatio) {
                                const float uniformRatio = std::clamp((widthRatio + heightRatio) * 0.5f, 0.1f, 10.0f);
                                item.widthPercent = std::clamp(g_mirrorEditorState.visualDrag.startGroupItemWidthPercent * uniformRatio,
                                                               kGroupItemPercentMin,
                                                               kGroupItemPercentMax);
                                item.heightPercent = std::clamp(g_mirrorEditorState.visualDrag.startGroupItemHeightPercent * uniformRatio,
                                                                kGroupItemPercentMin,
                                                                kGroupItemPercentMax);
                            } else {
                                item.widthPercent = std::clamp(g_mirrorEditorState.visualDrag.startGroupItemWidthPercent * widthRatio,
                                                               kGroupItemPercentMin,
                                                               kGroupItemPercentMax);
                                item.heightPercent = std::clamp(g_mirrorEditorState.visualDrag.startGroupItemHeightPercent * heightRatio,
                                                                kGroupItemPercentMin,
                                                                kGroupItemPercentMax);
                            }
                            changed = true;
                        }
                    }
                    if (changed) {
                        g_mirrorEditorState.visualDrag.dirty = true;
                        platform::config::PublishConfigSnapshot(config);
                    }
                } else {
                    g_mirrorEditorState.visualDrag.moved = true;
                    platform::config::MirrorConfig* mirror = FindMirrorConfigByName(config, g_mirrorEditorState.directEditSelection.mirrorId);
                    if (mirror) {
                        const int zoneIndex = ResolveDirectEditCaptureZoneIndex(*mirror);
                        if (zoneIndex >= 0 &&
                            zoneIndex < static_cast<int>(mirror->input.size()) &&
                            mirror->input[static_cast<std::size_t>(zoneIndex)].enabled &&
                            selectedResolvedItem.rect.GetWidth() > 0.0f &&
                            selectedResolvedItem.rect.GetHeight() > 0.0f) {
                            float sourceWidth = 0.0f;
                            float sourceHeight = 0.0f;
                            if (ResolveMirrorSourceSizeForDirectEdit(*mirror, viewportContext, sourceWidth, sourceHeight)) {
                                ImRect cropRect;
                                if (ResolveCropRectInSourceForDirectEdit(*mirror,
                                                                         mirror->input[static_cast<std::size_t>(zoneIndex)],
                                                                         viewportContext,
                                                                         sourceWidth,
                                                                         sourceHeight,
                                                                         cropRect)) {
                                    const ImRect sourceBounds = ShouldUseViewportRelativeTo(mirror->input[static_cast<std::size_t>(zoneIndex)].relativeTo)
                                        ? ResolveMirrorSourceViewportRectForDirectEdit(*mirror, viewportContext, sourceWidth, sourceHeight)
                                        : ImRect(ImVec2(0.0f, 0.0f), ImVec2(sourceWidth, sourceHeight));
                                    const float sourceDeltaX = delta.x / std::max(g_mirrorEditorState.visualDrag.startOutputScaleX, 0.0001f);
                                    const float sourceDeltaY = delta.y / std::max(g_mirrorEditorState.visualDrag.startOutputScaleY, 0.0001f);
                                    ImRect updatedCrop(ImVec2(g_mirrorEditorState.visualDrag.startRect.x,
                                                              g_mirrorEditorState.visualDrag.startRect.y),
                                                       ImVec2(g_mirrorEditorState.visualDrag.startRect.z,
                                                              g_mirrorEditorState.visualDrag.startRect.w));
                                    if (g_mirrorEditorState.visualDrag.edgeMask == kMirrorVisualEdgeNone) {
                                        updatedCrop.Translate(ImVec2(-sourceDeltaX, -sourceDeltaY));
                                        const float rectWidth = updatedCrop.GetWidth();
                                        const float rectHeight = updatedCrop.GetHeight();
                                        updatedCrop.Min.x = std::clamp(updatedCrop.Min.x, sourceBounds.Min.x, sourceBounds.Max.x - rectWidth);
                                        updatedCrop.Min.y = std::clamp(updatedCrop.Min.y, sourceBounds.Min.y, sourceBounds.Max.y - rectHeight);
                                        updatedCrop.Max.x = updatedCrop.Min.x + rectWidth;
                                        updatedCrop.Max.y = updatedCrop.Min.y + rectHeight;
                                    } else {
                                        updatedCrop = ApplyDirectEditResizeDelta(updatedCrop,
                                                                                 g_mirrorEditorState.visualDrag.edgeMask,
                                                                                 ImVec2(sourceDeltaX, sourceDeltaY),
                                                                                 false);
                                        updatedCrop.Min.x = std::clamp(updatedCrop.Min.x, sourceBounds.Min.x, sourceBounds.Max.x - 1.0f);
                                        updatedCrop.Min.y = std::clamp(updatedCrop.Min.y, sourceBounds.Min.y, sourceBounds.Max.y - 1.0f);
                                        updatedCrop.Max.x = std::clamp(updatedCrop.Max.x, updatedCrop.Min.x + 1.0f, sourceBounds.Max.x);
                                        updatedCrop.Max.y = std::clamp(updatedCrop.Max.y, updatedCrop.Min.y + 1.0f, sourceBounds.Max.y);
                                    }

                                    ApplyCropRectToMirror(*mirror,
                                                          mirror->input[static_cast<std::size_t>(zoneIndex)],
                                                          viewportContext,
                                                          updatedCrop,
                                                          sourceWidth,
                                                          sourceHeight,
                                                          g_mirrorEditorState.visualDrag.edgeMask != kMirrorVisualEdgeNone,
                                                          g_mirrorEditorState.visualDrag.startOutputScaleX,
                                                          g_mirrorEditorState.visualDrag.startOutputScaleY);
                                    g_mirrorEditorState.visualDrag.dirty = true;
                                    platform::config::PublishConfigSnapshot(config);
                                }
                            }
                        }
                    }
                }
            } else {
                if (!g_mirrorEditorState.visualDrag.moved && g_mirrorEditorState.directEditHasPendingClickSelection) {
                    g_mirrorEditorState.directEditSelection = g_mirrorEditorState.directEditPendingClickSelection;
                    g_mirrorEditorState.directEditShowGroupInspector =
                        g_mirrorEditorState.directEditSelection.kind == MirrorDirectEditSelectionKind::Group;
                    SyncMirrorDirectEditSelectionToSidebar(config);
                }
                g_mirrorEditorState.directEditPendingClickSelection = MirrorEditorState::DirectEditSelection{};
                g_mirrorEditorState.directEditHasPendingClickSelection = false;
                if (g_mirrorEditorState.visualDrag.dirty) {
                    AutoSaveConfig(config);
                }
                g_mirrorEditorState.visualDrag = MirrorEditorState::VisualDragState{};
            }
        }

        if (g_mirrorEditorState.directEditShowCaptureGuides &&
            g_mirrorEditorState.directEditSelection.kind != MirrorDirectEditSelectionKind::Group &&
            selectedItemIndex >= 0) {
            platform::config::MirrorConfig* selectedMirrorConfig =
                FindMirrorConfigByName(config, g_mirrorEditorState.directEditSelection.mirrorId);
            if (selectedMirrorConfig) {
                const int zoneIndex = ResolveDirectEditCaptureZoneIndex(*selectedMirrorConfig);
                if (zoneIndex >= 0 && zoneIndex < static_cast<int>(selectedMirrorConfig->input.size())) {
                    DrawCaptureGuides(drawList,
                                      *selectedMirrorConfig,
                                      selectedMirrorConfig->input[static_cast<std::size_t>(zoneIndex)],
                                      viewportContext,
                                      items[static_cast<std::size_t>(selectedItemIndex)].rect);
                }
            }
        }

        const ImU32 normalColor = IM_COL32(255, 255, 255, 88);
        const ImU32 groupNormalColor = IM_COL32(188, 188, 188, 72);
        const ImU32 hoveredColor = IM_COL32(255, 214, 102, 220);
        const ImU32 selectedColor = cropColorActive
            ? IM_COL32(112, 214, 122, 255)
            : (freeResizeActive ? IM_COL32(242, 97, 97, 255) : IM_COL32(92, 194, 255, 255));
        const ImU32 selectedHandleHoverColor = cropColorActive
            ? IM_COL32(173, 242, 179, 255)
            : (freeResizeActive ? IM_COL32(255, 166, 166, 255) : IM_COL32(181, 224, 255, 255));
        for (int i = 0; i < static_cast<int>(items.size()); ++i) {
            const auto& item = items[static_cast<std::size_t>(i)];
            const bool selectedMirror = g_mirrorEditorState.directEditSelection.kind == MirrorDirectEditSelectionKind::Mirror &&
                                        item.resolved.sourceKind == ResolvedMirrorSourceKind::Mirror &&
                                        g_mirrorEditorState.directEditSelection.mirrorId == item.resolved.sourceMirrorId;
            const bool selectedGroupItem = g_mirrorEditorState.directEditSelection.kind == MirrorDirectEditSelectionKind::GroupItem &&
                                           item.resolved.sourceKind == ResolvedMirrorSourceKind::GroupItem &&
                                           g_mirrorEditorState.directEditSelection.mirrorId == item.resolved.sourceMirrorId &&
                                           g_mirrorEditorState.directEditSelection.groupId == item.resolved.sourceGroupId &&
                                           g_mirrorEditorState.directEditSelection.groupItemIndex == item.resolved.sourceGroupItemIndex;
            const bool selected = selectedMirror || selectedGroupItem;
            const bool hovered =
                hasPreviewTarget &&
                previewTarget.kind == DirectEditHitTargetKind::Item &&
                previewTarget.itemIndex == i;
            const ImU32 color = (selected && !selectedGroupDirectly) ? selectedColor : (hovered ? hoveredColor : normalColor);
            drawList->AddRect(item.rect.Min, item.rect.Max, color, 0.0f, 0, selected ? 1.5f : 1.0f);
            const char* label = item.resolved.sourceMirrorId.empty() ? "[Mirror]" : item.resolved.sourceMirrorId.c_str();
            drawList->AddText(item.rect.Min + ImVec2(4.0f, -ImGui::GetTextLineHeight()), color, label);
            if (selected && !selectedGroupDirectly) {
                const float handleRadius = 3.0f;
                const ImVec2 handles[] = {
                    item.rect.Min,
                    ImVec2(item.rect.GetCenter().x, item.rect.Min.y),
                    ImVec2(item.rect.Max.x, item.rect.Min.y),
                    ImVec2(item.rect.Min.x, item.rect.GetCenter().y),
                    ImVec2(item.rect.Max.x, item.rect.GetCenter().y),
                    ImVec2(item.rect.Min.x, item.rect.Max.y),
                    ImVec2(item.rect.GetCenter().x, item.rect.Max.y),
                    item.rect.Max,
                };
                const int activeHandleIndex =
                    (g_mirrorEditorState.visualDrag.active && selected)
                        ? -1
                        : ResolveHoveredHandleIndex(item.rect, kDirectEditHandleRadius, mousePos);
                for (int handleIndex = 0; handleIndex < static_cast<int>(IM_ARRAYSIZE(handles)); ++handleIndex) {
                    if (!ShouldShowHandleIndex(handleIndex, activeVisualMode)) {
                        continue;
                    }
                    const bool handleHovered =
                        (g_mirrorEditorState.visualDrag.active && selected)
                            ? (ResolveHandleEdgeMask(handleIndex, activeVisualMode) == g_mirrorEditorState.visualDrag.edgeMask)
                            : (handleIndex == activeHandleIndex);
                    drawList->AddCircleFilled(handles[handleIndex],
                                              handleHovered ? (handleRadius + 1.0f) : handleRadius,
                                              handleHovered ? selectedHandleHoverColor : selectedColor);
                }
            }
        }
        for (const auto& groupTarget : groupTargets) {
            const bool selectedGroup = g_mirrorEditorState.directEditSelection.kind == MirrorDirectEditSelectionKind::Group &&
                                       g_mirrorEditorState.directEditSelection.groupId == groupTarget.groupId;
            const bool hoveredGroup = hasPreviewTarget &&
                                      previewTarget.kind == DirectEditHitTargetKind::Group &&
                                      previewTarget.selection.groupId == groupTarget.groupId;
            const bool activeGroupContext =
                selectedGroupDirectly && g_mirrorEditorState.directEditSelection.groupId == groupTarget.groupId;
            const ImU32 groupColor = selectedGroup || activeGroupContext
                ? selectedColor
                : (hoveredGroup ? hoveredColor : groupNormalColor);
            const float groupThickness = (selectedGroup || activeGroupContext) ? 1.75f : 1.0f;
            DrawDashedRect(drawList, groupTarget.rect, groupColor, groupThickness);
            DrawCornerBrackets(drawList, groupTarget.rect, groupColor, groupThickness + 0.25f);
            DrawGroupLabelChip(drawList,
                               groupTarget.rect,
                               std::string("Group: ") + groupTarget.groupId,
                               groupColor,
                               IM_COL32(18, 22, 27, 216));
            if (selectedGroup || activeGroupContext) {
                const float handleRadius = 3.0f;
                const ImVec2 handles[] = {
                    groupTarget.rect.Min,
                    ImVec2(groupTarget.rect.GetCenter().x, groupTarget.rect.Min.y),
                    ImVec2(groupTarget.rect.Max.x, groupTarget.rect.Min.y),
                    ImVec2(groupTarget.rect.Min.x, groupTarget.rect.GetCenter().y),
                    ImVec2(groupTarget.rect.Max.x, groupTarget.rect.GetCenter().y),
                    ImVec2(groupTarget.rect.Min.x, groupTarget.rect.Max.y),
                    ImVec2(groupTarget.rect.GetCenter().x, groupTarget.rect.Max.y),
                    groupTarget.rect.Max,
                };
                const int activeHandleIndex =
                    (g_mirrorEditorState.visualDrag.active &&
                     g_mirrorEditorState.directEditSelection.kind == MirrorDirectEditSelectionKind::Group &&
                     g_mirrorEditorState.directEditSelection.groupId == groupTarget.groupId)
                        ? -1
                        : ResolveHoveredHandleIndex(groupTarget.rect, kDirectEditHandleRadius, mousePos);
                for (int handleIndex = 0; handleIndex < static_cast<int>(IM_ARRAYSIZE(handles)); ++handleIndex) {
                    if (!ShouldShowHandleIndex(handleIndex, MirrorVisualEditorMode::Layout)) {
                        continue;
                    }
                    const bool handleHovered =
                        (g_mirrorEditorState.visualDrag.active &&
                         g_mirrorEditorState.directEditSelection.kind == MirrorDirectEditSelectionKind::Group &&
                         g_mirrorEditorState.directEditSelection.groupId == groupTarget.groupId)
                            ? (ResolveHandleEdgeMask(handleIndex, activeVisualMode) == g_mirrorEditorState.visualDrag.edgeMask)
                            : (handleIndex == activeHandleIndex);
                    drawList->AddCircleFilled(handles[handleIndex],
                                              handleHovered ? (handleRadius + 1.0f) : handleRadius,
                                              handleHovered ? selectedHandleHoverColor : selectedColor);
                }
            }
        }
    } else {
        g_mirrorEditorState.directEditFullscreenHovered = false;
    }
    ImGui::End();

    ImGui::SetNextWindowBgAlpha(0.88f);
    ImGui::SetNextWindowPos(ImVec2(16.0f, 16.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(520.0f, 680.0f), ImGuiCond_FirstUseEver);
    const ImGuiWindowFlags panelFlags = ImGuiWindowFlags_NoCollapse;
    if (ImGui::Begin("Mirror Visual Editor", nullptr, panelFlags)) {
        if (AnimatedButton("Exit Editor")) {
            ExitMirrorDirectEditToSettings(inOutGuiVisible);
        }
        ImGui::SameLine();
        const bool settingsHidden = g_mirrorEditorState.directEditHideMainWindow;
        if (AnimatedButton(settingsHidden ? "Show Settings" : "Hide Settings")) {
            const bool nextHidden = !settingsHidden;
            if (inOutGuiVisible) {
                *inOutGuiVisible = !nextHidden;
            }
            SetGuiVisible(!nextHidden);
            g_mirrorEditorState.directEditHideMainWindow = nextHidden;
        }

        const std::string activeModeName = GetMirrorModeState().GetActiveModeName();
        const auto relevantModes = GetRelevantModesForDirectEditSelection(config, g_mirrorEditorState.directEditSelection);
        if (ImGui::BeginCombo("Mode", activeModeName.empty() ? "[No Active Mode]" : activeModeName.c_str())) {
            auto selectMode = [&](const std::string& modeName) {
                g_mirrorEditorState.directEditCycleIndex = 0;
                g_mirrorEditorState.directEditLastCycleStackKey.clear();
                g_mirrorEditorState.directEditPendingClickSelection = MirrorEditorState::DirectEditSelection{};
                g_mirrorEditorState.directEditHasPendingClickSelection = false;
                StartModeSwitchWithTransition(modeName, config, GetMirrorModeState());
            };
            auto isRelevantMode = [&](const std::string& modeName) {
                return std::find(relevantModes.begin(), relevantModes.end(), modeName) != relevantModes.end();
            };
            bool hasOtherModes = false;
            for (const auto& mode : config.modes) {
                if (!mode.name.empty() && !isRelevantMode(mode.name)) {
                    hasOtherModes = true;
                    break;
                }
            }

            ImGui::TextDisabled("Relevant to Selection");
            if (relevantModes.empty()) {
                ImGui::TextDisabled("No modes");
            } else {
                for (const auto& modeName : relevantModes) {
                    const bool selected = modeName == activeModeName;
                    const std::string itemId = modeName + "##direct_edit_relevant_mode";
                    if (ImGui::Selectable(itemId.c_str(), selected)) {
                        selectMode(modeName);
                    }
                    if (selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
            }

            if (hasOtherModes) {
                ImGui::Separator();
                ImGui::TextDisabled("Other Modes");
                for (const auto& mode : config.modes) {
                    if (mode.name.empty() || isRelevantMode(mode.name)) {
                        continue;
                    }
                    const bool selected = mode.name == activeModeName;
                    const std::string itemId = mode.name + "##direct_edit_other_mode";
                    if (ImGui::Selectable(itemId.c_str(), selected)) {
                        selectMode(mode.name);
                    }
                    if (selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
            }
            ImGui::EndCombo();
        }

        platform::config::MirrorConfig* selectedMirror = FindMirrorConfigByName(config, g_mirrorEditorState.directEditSelection.mirrorId);
        platform::config::MirrorGroupConfig* selectedGroup =
            FindMirrorGroupByName(config, g_mirrorEditorState.directEditSelection.groupId);
        const bool selectedGroupDirectly =
            g_mirrorEditorState.directEditSelection.kind == MirrorDirectEditSelectionKind::Group;
        std::string selectedMirrorPreview;
        if (items.empty()) {
            selectedMirrorPreview = "[No visible mirrors in mode]";
        } else if (selectedGroupDirectly && !g_mirrorEditorState.directEditSelection.groupId.empty()) {
            selectedMirrorPreview = "Group: " + g_mirrorEditorState.directEditSelection.groupId;
        } else if (!g_mirrorEditorState.directEditSelection.mirrorId.empty()) {
            selectedMirrorPreview = g_mirrorEditorState.directEditSelection.mirrorId;
            if (!g_mirrorEditorState.directEditSelection.groupId.empty()) {
                selectedMirrorPreview += "  [" + g_mirrorEditorState.directEditSelection.groupId + "]";
            }
        } else {
            selectedMirrorPreview = "[Select visible mirror]";
        }
        if (ImGui::BeginCombo("Selected Mirror", selectedMirrorPreview.c_str())) {
            for (const auto& item : items) {
                const bool selected =
                    g_mirrorEditorState.directEditSelection.kind != MirrorDirectEditSelectionKind::Group &&
                    item.resolved.sourceMirrorId == g_mirrorEditorState.directEditSelection.mirrorId &&
                    item.resolved.sourceGroupId == g_mirrorEditorState.directEditSelection.groupId &&
                    item.resolved.sourceGroupItemIndex == g_mirrorEditorState.directEditSelection.groupItemIndex;
                std::string label = item.resolved.sourceMirrorId;
                if (!item.resolved.sourceGroupId.empty()) {
                    label += "  [" + item.resolved.sourceGroupId + "]";
                }
                const std::string itemId =
                    label + "##direct_edit_item_select_" +
                    item.resolved.sourceGroupId + "_" +
                    std::to_string(item.resolved.sourceGroupItemIndex) + "_" +
                    std::to_string(static_cast<int>(item.resolved.sourceKind));
                if (ImGui::Selectable(itemId.c_str(), selected)) {
                    g_mirrorEditorState.directEditSelection.kind =
                        (item.resolved.sourceKind == ResolvedMirrorSourceKind::GroupItem)
                            ? MirrorDirectEditSelectionKind::GroupItem
                            : MirrorDirectEditSelectionKind::Mirror;
                    g_mirrorEditorState.directEditSelection.mirrorId = item.resolved.sourceMirrorId;
                    g_mirrorEditorState.directEditSelection.groupId = item.resolved.sourceGroupId;
                    g_mirrorEditorState.directEditSelection.groupItemIndex = item.resolved.sourceGroupItemIndex;
                    g_mirrorEditorState.directEditShowGroupInspector = false;
                    SyncMirrorDirectEditSelectionToSidebar(config);
                }
            }
            if (!groupTargets.empty()) {
                ImGui::Separator();
                for (const auto& groupTarget : groupTargets) {
                    const bool selected = selectedGroupDirectly &&
                                          g_mirrorEditorState.directEditSelection.groupId == groupTarget.groupId;
                    const std::string label = "Group: " + groupTarget.groupId;
                    const std::string itemId = label + "##direct_edit_group_select";
                    if (ImGui::Selectable(itemId.c_str(), selected)) {
                        g_mirrorEditorState.directEditSelection.kind = MirrorDirectEditSelectionKind::Group;
                        g_mirrorEditorState.directEditSelection.groupId = groupTarget.groupId;
                        g_mirrorEditorState.directEditSelection.groupItemIndex = -1;
                        g_mirrorEditorState.directEditSelection.mirrorId =
                            ResolveDirectEditGroupMirrorId(items, g_mirrorEditorState.directEditSelection, groupTarget.groupId);
                        g_mirrorEditorState.directEditShowGroupInspector = true;
                        SyncMirrorDirectEditSelectionToSidebar(config);
                    }
                }
            }
            ImGui::EndCombo();
        }

        // Group selections keep a contextual mirrorId for overlap cycling and launcher context,
        // but the panel should treat them as pure group edits.
        if (selectedGroupDirectly) {
            selectedMirror = nullptr;
        }

        const bool showGroupedItemInspectorTabs =
            selectedGroup != nullptr &&
            g_mirrorEditorState.directEditSelection.kind == MirrorDirectEditSelectionKind::GroupItem;
        if (!showGroupedItemInspectorTabs) {
            g_mirrorEditorState.directEditShowGroupInspector = selectedGroupDirectly;
        }
        const bool editGroupFromGroupedItem =
            showGroupedItemInspectorTabs && g_mirrorEditorState.directEditShowGroupInspector;

        const bool canEditCapture =
            selectedMirror &&
            !editGroupFromGroupedItem &&
            g_mirrorEditorState.directEditSelection.kind != MirrorDirectEditSelectionKind::Group &&
            !selectedMirror->input.empty();
        if (canEditCapture) {
            const int selectedZoneIndex = ResolveDirectEditCaptureZoneIndex(*selectedMirror);
            const int zoneCount = static_cast<int>(selectedMirror->input.size());
            const bool singleZone = zoneCount <= 1;
            ImGui::TextUnformatted("Input Zone");
            ImGui::SameLine();
            if (singleZone) {
                ImGui::BeginDisabled();
            }
            if (AnimatedButton("<##direct_edit_zone_prev")) {
                g_mirrorEditorState.directEditSelectedCaptureZoneIndex =
                    (selectedZoneIndex - 1 + zoneCount) % zoneCount;
            }
            if (singleZone) {
                ImGui::EndDisabled();
            }
            ImGui::SameLine();
            ImGui::Text("Input Zone %d / %d", selectedZoneIndex + 1, zoneCount);
            ImGui::SameLine();
            if (singleZone) {
                ImGui::BeginDisabled();
            }
            if (AnimatedButton(">##direct_edit_zone_next")) {
                g_mirrorEditorState.directEditSelectedCaptureZoneIndex =
                    (selectedZoneIndex + 1) % zoneCount;
            }
            if (singleZone) {
                ImGui::EndDisabled();
            }
            ImGui::Checkbox("Show Capture Guides", &g_mirrorEditorState.directEditShowCaptureGuides);
            if (selectedMirror->source.type != platform::config::MirrorSourceType::GameFramebuffer) {
                ImGui::TextDisabled("Capture guides are only shown for game framebuffer mirrors.");
            }
        }

        ImGui::TextColored(ImVec4(92.0f / 255.0f, 194.0f / 255.0f, 1.0f, 1.0f), "Blue: Drag to move or resize");
        ImGui::TextColored(ImVec4(242.0f / 255.0f, 97.0f / 255.0f, 97.0f / 255.0f, 1.0f), "Red: Hold Shift to resize freely");
        if (g_mirrorEditorState.directEditSelection.kind == MirrorDirectEditSelectionKind::Group) {
            ImGui::TextDisabled("Green: Hold Alt to adjust capture area (mirrors only)");
        } else {
            ImGui::TextColored(ImVec4(112.0f / 255.0f, 214.0f / 255.0f, 122.0f / 255.0f, 1.0f),
                               "Green: Hold Alt to adjust capture area");
        }

        if (selectedMirror || selectedGroup) {
            ImGui::Separator();
            if (selectedGroupDirectly && selectedGroup) {
                ImGui::Text("Selected: Group: %s", selectedGroup->name.c_str());
            } else if (selectedMirror) {
                ImGui::Text("Selected: %s", selectedMirror->name.c_str());
                if (selectedGroup && !g_mirrorEditorState.directEditSelection.groupId.empty()) {
                    ImGui::TextDisabled("Inside of Group: %s", selectedGroup->name.c_str());
                }
            }
            if (showGroupedItemInspectorTabs && ImGui::BeginTabBar("##direct_edit_subject_tabs")) {
                if (ImGui::BeginTabItem("Mirror",
                                        nullptr,
                                        !g_mirrorEditorState.directEditShowGroupInspector
                                            ? ImGuiTabItemFlags_SetSelected
                                            : 0)) {
                    g_mirrorEditorState.directEditShowGroupInspector = false;
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Group",
                                        nullptr,
                                        g_mirrorEditorState.directEditShowGroupInspector
                                            ? ImGuiTabItemFlags_SetSelected
                                            : 0)) {
                    g_mirrorEditorState.directEditShowGroupInspector = true;
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
            const bool editMirrorTab =
                selectedMirror != nullptr &&
                g_mirrorEditorState.directEditSelection.kind != MirrorDirectEditSelectionKind::Group &&
                !editGroupFromGroupedItem;
            const bool editGroupTab =
                selectedGroup != nullptr &&
                (g_mirrorEditorState.directEditSelection.kind == MirrorDirectEditSelectionKind::Group ||
                 editGroupFromGroupedItem);
            bool visibleInCurrentMode = false;
            if (selectedMirror) {
                for (const auto& item : items) {
                    if (item.resolved.sourceMirrorId != selectedMirror->name) {
                        continue;
                    }
                    if (!g_mirrorEditorState.directEditSelection.groupId.empty() &&
                        item.resolved.sourceGroupId != g_mirrorEditorState.directEditSelection.groupId &&
                        g_mirrorEditorState.directEditSelection.kind != MirrorDirectEditSelectionKind::Mirror) {
                        continue;
                    }
                    visibleInCurrentMode = true;
                    break;
                }
            }
            if (selectedMirror && !visibleInCurrentMode) {
                ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.35f, 1.0f), "Selected mirror is not visible in the current mode.");
            }

            {
                if (editMirrorTab && AnimatedCollapsingHeader("General", ImGuiTreeNodeFlags_DefaultOpen)) {
                    int fps = selectedMirror->fps;
                    if (ImGui::InputInt("FPS", &fps)) {
                        selectedMirror->fps = std::max(fps, 1);
                        AutoSaveConfig(config);
                    }

                    int captureWidth = selectedMirror->captureWidth;
                    if (ImGui::InputInt("Capture Width", &captureWidth) && captureWidth > 0) {
                        selectedMirror->captureWidth = captureWidth;
                        AutoSaveConfig(config);
                    }
                    int captureHeight = selectedMirror->captureHeight;
                    if (ImGui::InputInt("Capture Height", &captureHeight) && captureHeight > 0) {
                        selectedMirror->captureHeight = captureHeight;
                        AutoSaveConfig(config);
                    }
                }

                if (editGroupTab && selectedGroup) {
                    const int selectedGroupIndex = FindMirrorGroupIndexByName(config, selectedGroup->name);
                    if (selectedGroupIndex >= 0) {
                        RenderSharedMirrorGroupEditorSections(config, selectedGroupIndex, "direct_edit_group");
                    }
                }

                if (editMirrorTab &&
                    g_mirrorEditorState.directEditSelection.kind != MirrorDirectEditSelectionKind::GroupItem &&
                    AnimatedCollapsingHeader("Scaling")) {
                    if (ImGui::Checkbox("Relative size to container##mirror_output_size", &selectedMirror->output.useRelativeSize)) {
                        if (selectedMirror->output.useRelativeSize) {
                            UpdateDirectEditMirrorRelativeSizeFromScale(*selectedMirror, viewportContext);
                        } else {
                            UpdateDirectEditMirrorScaleFromRelativeSize(*selectedMirror, viewportContext);
                        }
                        AutoSaveConfig(config);
                    }
                    if (ImGui::Checkbox("Preserve aspect ratio##mirror_output_size", &selectedMirror->output.preserveAspectRatio)) {
                        if (selectedMirror->output.preserveAspectRatio) {
                            float uniformScale = 1.0f;
                            GetDirectEditMirrorUniformScale(*selectedMirror, uniformScale);
                            SetDirectEditMirrorUniformScale(*selectedMirror, uniformScale);
                        } else if (!selectedMirror->output.useRelativeSize) {
                            const float currentScale = selectedMirror->output.separateScale
                                ? selectedMirror->output.scaleX
                                : selectedMirror->output.scale;
                            selectedMirror->output.separateScale = true;
                            selectedMirror->output.scaleX = currentScale;
                            selectedMirror->output.scaleY = currentScale;
                        }
                        AutoSaveConfig(config);
                    }

                    if (selectedMirror->output.preserveAspectRatio) {
                        if (DrawAspectFitModeCombo("Fit Mode##mirror_output_size", selectedMirror->output.aspectFitMode)) {
                            selectedMirror->output.aspectFitMode = NormalizeAspectFitMode(selectedMirror->output.aspectFitMode);
                            AutoSaveConfig(config);
                        }

                        float uniformScale = 1.0f;
                        GetDirectEditMirrorUniformScale(*selectedMirror, uniformScale);
                        float scalePercent = uniformScale * 100.0f;
                        const char* uniformScaleLabel = selectedMirror->output.useRelativeSize
                            ? "Size % of container##mirror_output_size"
                            : "Scale %##mirror_output_size";
                        if (ImGui::SliderFloat(uniformScaleLabel, &scalePercent, 1.0f, 2000.0f, "%.1f%%")) {
                            SetDirectEditMirrorUniformScale(*selectedMirror, scalePercent / 100.0f);
                            if (selectedMirror->output.useRelativeSize) {
                                UpdateDirectEditMirrorRelativeSizeFromScale(*selectedMirror, viewportContext);
                            }
                            AutoSaveConfig(config);
                        }
                    } else if (selectedMirror->output.useRelativeSize) {
                        float widthPercent =
                            std::clamp(selectedMirror->output.relativeWidth, kDirectEditOutputScaleMin, kDirectEditOutputScaleMax) * 100.0f;
                        if (ImGui::SliderFloat("Width % of container##mirror_output_size", &widthPercent, 1.0f, 10000.0f, "%.1f%%")) {
                            selectedMirror->output.relativeWidth =
                                std::clamp(widthPercent / 100.0f, kDirectEditOutputScaleMin, kDirectEditOutputScaleMax);
                            AutoSaveConfig(config);
                        }
                        float heightPercent =
                            std::clamp(selectedMirror->output.relativeHeight, kDirectEditOutputScaleMin, kDirectEditOutputScaleMax) * 100.0f;
                        if (ImGui::SliderFloat("Height % of container##mirror_output_size", &heightPercent, 1.0f, 10000.0f, "%.1f%%")) {
                            selectedMirror->output.relativeHeight =
                                std::clamp(heightPercent / 100.0f, kDirectEditOutputScaleMin, kDirectEditOutputScaleMax);
                            AutoSaveConfig(config);
                        }
                    } else {
                        float widthPercent =
                            std::clamp(selectedMirror->output.separateScale ? selectedMirror->output.scaleX : selectedMirror->output.scale,
                                       kDirectEditOutputScaleMin,
                                       kDirectEditOutputScaleMax) * 100.0f;
                        if (ImGui::SliderFloat("Width %##mirror_output_size", &widthPercent, 1.0f, 10000.0f, "%.1f%%")) {
                            const float widthScale =
                                std::clamp(widthPercent / 100.0f, kDirectEditOutputScaleMin, kDirectEditOutputScaleMax);
                            selectedMirror->output.separateScale = true;
                            selectedMirror->output.scaleX = widthScale;
                            selectedMirror->output.scale = widthScale;
                            AutoSaveConfig(config);
                        }
                        float heightPercent =
                            std::clamp(selectedMirror->output.separateScale ? selectedMirror->output.scaleY : selectedMirror->output.scale,
                                       kDirectEditOutputScaleMin,
                                       kDirectEditOutputScaleMax) * 100.0f;
                        if (ImGui::SliderFloat("Height %##mirror_output_size", &heightPercent, 1.0f, 10000.0f, "%.1f%%")) {
                            const float heightScale =
                                std::clamp(heightPercent / 100.0f, kDirectEditOutputScaleMin, kDirectEditOutputScaleMax);
                            selectedMirror->output.separateScale = true;
                            selectedMirror->output.scaleY = heightScale;
                            AutoSaveConfig(config);
                        }
                    }
                }

                if (editMirrorTab &&
                    g_mirrorEditorState.directEditSelection.kind != MirrorDirectEditSelectionKind::GroupItem &&
                    AnimatedCollapsingHeader("Position")) {
                    if (ImGui::Checkbox("Relative to screen##mirror_output", &selectedMirror->output.useRelativePosition)) {
                        if (selectedMirror->output.useRelativePosition) {
                            UpdateDirectEditRelativeFromPixels(selectedMirror->output, viewportContext);
                        } else {
                            UpdateDirectEditPixelsFromRelative(selectedMirror->output, viewportContext);
                        }
                        AutoSaveConfig(config);
                    }

                    if (DrawRelativeToCombo("Relative To", selectedMirror->output.relativeTo)) {
                        AutoSaveConfig(config);
                    }

                    if (selectedMirror->output.useRelativePosition) {
                        float xPercent = selectedMirror->output.relativeX * 100.0f;
                        if (ImGui::SliderFloat("X %##mirror_output", &xPercent, -100.0f, 200.0f, "%.1f%%")) {
                            selectedMirror->output.relativeX = xPercent / 100.0f;
                            UpdateDirectEditPixelsFromRelative(selectedMirror->output, viewportContext);
                            AutoSaveConfig(config);
                        }
                        float yPercent = selectedMirror->output.relativeY * 100.0f;
                        if (ImGui::SliderFloat("Y %##mirror_output", &yPercent, -100.0f, 200.0f, "%.1f%%")) {
                            selectedMirror->output.relativeY = yPercent / 100.0f;
                            UpdateDirectEditPixelsFromRelative(selectedMirror->output, viewportContext);
                            AutoSaveConfig(config);
                        }
                    } else {
                        if (ImGui::DragInt("X Offset##mirror_output", &selectedMirror->output.x, 1)) {
                            AutoSaveConfig(config);
                        }
                        if (ImGui::DragInt("Y Offset##mirror_output", &selectedMirror->output.y, 1)) {
                            AutoSaveConfig(config);
                        }
                    }
                }

                if (editMirrorTab && AnimatedCollapsingHeader("Color Filters")) {
                    float sensitivity = selectedMirror->colorSensitivity;
                    if (ImGui::DragFloat("Color Sensitivity", &sensitivity, 0.0001f, 0.0f, 1.0f, "%.4f")) {
                        selectedMirror->colorSensitivity = std::clamp(sensitivity, 0.0f, 1.0f);
                        AutoSaveConfig(config);
                    }

                    float opacity = selectedMirror->opacity;
                    if (ImGui::DragFloat("Opacity", &opacity, 0.01f, 0.0f, 1.0f)) {
                        selectedMirror->opacity = std::clamp(opacity, 0.0f, 1.0f);
                        AutoSaveConfig(config);
                    }

                    if (ImGui::Checkbox("Color Passthrough", &selectedMirror->colorPassthrough)) {
                        AutoSaveConfig(config);
                    }
                    if (ImGui::Checkbox("Raw Output", &selectedMirror->rawOutput)) {
                        AutoSaveConfig(config);
                    }

                    if (selectedMirror->colorPassthrough) {
                        ImGui::BeginDisabled();
                    }
                    float outputColor[4] = {
                        selectedMirror->colors.output.r,
                        selectedMirror->colors.output.g,
                        selectedMirror->colors.output.b,
                        selectedMirror->colors.output.a,
                    };
                    if (ImGui::ColorEdit4("Output Color", outputColor)) {
                        selectedMirror->colors.output = {
                            std::clamp(outputColor[0], 0.0f, 1.0f),
                            std::clamp(outputColor[1], 0.0f, 1.0f),
                            std::clamp(outputColor[2], 0.0f, 1.0f),
                            std::clamp(outputColor[3], 0.0f, 1.0f)
                        };
                        AutoSaveConfig(config);
                    }
                    if (selectedMirror->colorPassthrough) {
                        ImGui::EndDisabled();
                    }

                    ImGui::TextUnformatted("Target Colors:");
                    int targetToRemoveIdx = -1;
                    for (size_t colIdx = 0; colIdx < selectedMirror->colors.targetColors.size(); ++colIdx) {
                        ImGui::PushID(static_cast<int>(colIdx));
                        float tCol[3] = {
                            selectedMirror->colors.targetColors[colIdx].r,
                            selectedMirror->colors.targetColors[colIdx].g,
                            selectedMirror->colors.targetColors[colIdx].b
                        };
                        std::string colLabel = "##col_" + std::to_string(colIdx);
                        if (ImGui::ColorEdit3(colLabel.c_str(), tCol)) {
                            selectedMirror->colors.targetColors[colIdx] = { tCol[0], tCol[1], tCol[2], 1.0f };
                            AutoSaveConfig(config);
                        }
                        ImGui::SameLine();
                        if (AnimatedButton("Remove")) {
                            targetToRemoveIdx = static_cast<int>(colIdx);
                        }
                        ImGui::PopID();
                    }
                    if (targetToRemoveIdx != -1) {
                        selectedMirror->colors.targetColors.erase(selectedMirror->colors.targetColors.begin() + targetToRemoveIdx);
                        AutoSaveConfig(config);
                    }
                    if (selectedMirror->colors.targetColors.size() < 8 && AnimatedButton("+ Add Target Color")) {
                        selectedMirror->colors.targetColors.push_back({ 1.0f, 1.0f, 1.0f, 1.0f });
                        AutoSaveConfig(config);
                    }
                }

                if (editMirrorTab && AnimatedCollapsingHeader("Border")) {
                    const char* borderTypes[] = { "Dynamic (around content)", "Static (shape overlay)" };
                    int currentBorderType = static_cast<int>(selectedMirror->border.type);
                    if (ImGui::Combo("Border Type", &currentBorderType, borderTypes, IM_ARRAYSIZE(borderTypes))) {
                        selectedMirror->border.type = static_cast<platform::config::MirrorBorderType>(currentBorderType);
                        AutoSaveConfig(config);
                    }

                    if (selectedMirror->border.type == platform::config::MirrorBorderType::Dynamic) {
                        if (ImGui::DragInt("Dynamic Thickness", &selectedMirror->border.dynamicThickness, 1, 0, 32)) {
                            selectedMirror->border.dynamicThickness = std::max(selectedMirror->border.dynamicThickness, 0);
                            AutoSaveConfig(config);
                        }
                        if (selectedMirror->border.dynamicThickness > 0) {
                            float dynColor[4] = {
                                selectedMirror->colors.border.r,
                                selectedMirror->colors.border.g,
                                selectedMirror->colors.border.b,
                                selectedMirror->colors.border.a
                            };
                            if (ImGui::ColorEdit4("Border Color##dyn", dynColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar)) {
                                selectedMirror->colors.border = { dynColor[0], dynColor[1], dynColor[2], dynColor[3] };
                                AutoSaveConfig(config);
                            }
                        }
                    } else {
                        if (ImGui::DragInt("Thickness##sb", &selectedMirror->border.staticThickness, 1, 0, 32)) {
                            selectedMirror->border.staticThickness = std::max(selectedMirror->border.staticThickness, 0);
                            AutoSaveConfig(config);
                        }

                        if (selectedMirror->border.staticThickness > 0) {
                            const char* shapes[] = { "Rectangle", "Circle/Ellipse" };
                            int currentShape = static_cast<int>(selectedMirror->border.staticShape);
                            if (ImGui::Combo("Shape", &currentShape, shapes, IM_ARRAYSIZE(shapes))) {
                                selectedMirror->border.staticShape = static_cast<platform::config::MirrorBorderShape>(currentShape);
                                AutoSaveConfig(config);
                            }

                            float staticColor[4] = {
                                selectedMirror->border.staticColor.r,
                                selectedMirror->border.staticColor.g,
                                selectedMirror->border.staticColor.b,
                                selectedMirror->border.staticColor.a
                            };
                            if (ImGui::ColorEdit4("Static Color", staticColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar)) {
                                selectedMirror->border.staticColor = { staticColor[0], staticColor[1], staticColor[2], staticColor[3] };
                                AutoSaveConfig(config);
                            }

                            if (selectedMirror->border.staticShape == platform::config::MirrorBorderShape::Rectangle &&
                                ImGui::DragInt("Radius##sb", &selectedMirror->border.staticRadius, 1, 0, 128)) {
                                selectedMirror->border.staticRadius = std::max(selectedMirror->border.staticRadius, 0);
                                AutoSaveConfig(config);
                            }

                            if (ImGui::DragInt("Offset X##sb", &selectedMirror->border.staticOffsetX, 1)) {
                                AutoSaveConfig(config);
                            }
                            if (ImGui::DragInt("Offset Y##sb", &selectedMirror->border.staticOffsetY, 1)) {
                                AutoSaveConfig(config);
                            }
                            if (ImGui::DragInt("Width##sb", &selectedMirror->border.staticWidth, 1, 0, 4096)) {
                                selectedMirror->border.staticWidth = std::max(selectedMirror->border.staticWidth, 0);
                                AutoSaveConfig(config);
                            }
                            if (ImGui::DragInt("Height##sb", &selectedMirror->border.staticHeight, 1, 0, 4096)) {
                                selectedMirror->border.staticHeight = std::max(selectedMirror->border.staticHeight, 0);
                                AutoSaveConfig(config);
                            }
                        }
                    }
                }

                if (editMirrorTab && AnimatedCollapsingHeader("Capture Zones")) {
                    int zoneToRemoveIdx = -1;
                    if (ImGui::BeginTable("mirror_capture_zones_direct", 5, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV)) {
                        ImGui::TableSetupColumn("###zone_col_visibility", ImGuiTableColumnFlags_WidthFixed, 86.0f);
                        ImGui::TableSetupColumn("X###zone_col_x", ImGuiTableColumnFlags_WidthStretch, 1.0f);
                        ImGui::TableSetupColumn("Y###zone_col_y", ImGuiTableColumnFlags_WidthStretch, 1.0f);
                        ImGui::TableSetupColumn("Relative To###zone_col_relative", ImGuiTableColumnFlags_WidthStretch, 1.0f);
                        ImGui::TableSetupColumn("###delete_zone_col", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight() + 6.0f);
                        ImGui::TableHeadersRow();

                        for (size_t zIdx = 0; zIdx < selectedMirror->input.size(); ++zIdx) {
                            ImGui::PushID(static_cast<int>(zIdx));
                            auto& zone = selectedMirror->input[zIdx];

                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0);
                            if (ImGui::Checkbox("##zone_enabled", &zone.enabled)) {
                                AutoSaveConfig(config);
                            }

                            ImGui::TableSetColumnIndex(1);
                            ImGui::SetNextItemWidth(-1.0f);
                            int zX = zone.x;
                            if (ImGui::InputInt("##zone_x", &zX)) {
                                zone.x = zX;
                                AutoSaveConfig(config);
                            }

                            ImGui::TableSetColumnIndex(2);
                            ImGui::SetNextItemWidth(-1.0f);
                            int zY = zone.y;
                            if (ImGui::InputInt("##zone_y", &zY)) {
                                zone.y = zY;
                                AutoSaveConfig(config);
                            }

                            ImGui::TableSetColumnIndex(3);
                            ImGui::SetNextItemWidth(-1.0f);
                            if (DrawRelativeToCombo("##zone_relative_to", zone.relativeTo)) {
                                AutoSaveConfig(config);
                            }

                            ImGui::TableSetColumnIndex(4);
                            if (selectedMirror->input.size() <= 1) {
                                ImGui::BeginDisabled();
                            }
                            if (ImGui::SmallButton("X###delete_zone_btn")) {
                                zoneToRemoveIdx = static_cast<int>(zIdx);
                            }
                            if (selectedMirror->input.size() <= 1) {
                                ImGui::EndDisabled();
                            }
                            ImGui::PopID();
                        }

                        ImGui::EndTable();
                    }
                    if (zoneToRemoveIdx != -1) {
                        selectedMirror->input.erase(selectedMirror->input.begin() + zoneToRemoveIdx);
                        AutoSaveConfig(config);
                    }
                    if (AnimatedButton("Add New Capture Zone")) {
                        platform::config::MirrorCaptureConfig nZone;
                        nZone.relativeTo = (selectedMirror->source.type != platform::config::MirrorSourceType::GameFramebuffer)
                            ? "topLeftSource"
                            : "centerViewport";
                        selectedMirror->input.push_back(nZone);
                        AutoSaveConfig(config);
                    }
                }
            }
        } else {
            ImGui::Separator();
            ImGui::TextDisabled("Click a mirror to edit it.");
        }
    }
    ImGui::End();

    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        ExitMirrorDirectEditToSettings(inOutGuiVisible);
    }
}

#include "tab_mirrors_editor.cpp"

} // namespace platform::x11
