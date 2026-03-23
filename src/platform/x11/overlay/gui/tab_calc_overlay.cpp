#include "../overlay_internal.h"
#include "../../../common/config_io.h"
#include "../../calc_overlay_runtime.h"
#include "imgui_overlay_helpers.h"
#include "tab_calc_overlay.h"
#include "tab_mirrors_state.h"

#include "../../../common/font_scanner.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <map>
#include <string>

namespace platform::x11 {

namespace {

const char* GetEyeColumnDisplay(platform::config::CalcOverlayEyeColumnType type) {
    switch (type) {
    case platform::config::CalcOverlayEyeColumnType::Certainty: return "Certainty";
    case platform::config::CalcOverlayEyeColumnType::Distance: return "Distance";
    case platform::config::CalcOverlayEyeColumnType::NetherCoords: return "Nether Coords";
    case platform::config::CalcOverlayEyeColumnType::Angle: return "Angle";
    case platform::config::CalcOverlayEyeColumnType::OverworldCoords:
    default: return "Overworld Coords";
    }
}

const char* GetAaColumnDisplay(platform::config::CalcOverlayAaColumnType type) {
    switch (type) {
    case platform::config::CalcOverlayAaColumnType::Location: return "Location";
    case platform::config::CalcOverlayAaColumnType::NetherCoords: return "Nether Coords";
    case platform::config::CalcOverlayAaColumnType::Angle: return "Angle";
    case platform::config::CalcOverlayAaColumnType::Icons:
    default: return "Icons";
    }
}

const char* GetAaRowDisplay(platform::config::CalcOverlayAaRowType type) {
    switch (type) {
    case platform::config::CalcOverlayAaRowType::Spawn: return "Shulker";
    case platform::config::CalcOverlayAaRowType::Outpost: return "Outpost";
    case platform::config::CalcOverlayAaRowType::Monument: return "Monument";
    case platform::config::CalcOverlayAaRowType::Stronghold:
    default: return "Stronghold";
    }
}

const char* GetHeaderRowDisplay(platform::config::CalcOverlayHeaderRow value) {
    switch (value) {
    case platform::config::CalcOverlayHeaderRow::Nothing: return "Nothing";
    case platform::config::CalcOverlayHeaderRow::Text: return "Text";
    case platform::config::CalcOverlayHeaderRow::Icon:
    default: return "Icon";
    }
}

bool DrawHeaderRowCombo(const char* label,
                        platform::config::CalcOverlayHeaderRow& value,
                        bool allowNothing,
                        bool allowIcon,
                        bool allowText) {
    const char* preview = GetHeaderRowDisplay(value);
    bool changed = false;
    if (ImGui::BeginCombo(label, preview)) {
        if (allowNothing) {
            const bool selected = value == platform::config::CalcOverlayHeaderRow::Nothing;
            if (ImGui::Selectable("Nothing", selected)) {
                value = platform::config::CalcOverlayHeaderRow::Nothing;
                changed = true;
            }
        }
        if (allowIcon) {
            const bool selected = value == platform::config::CalcOverlayHeaderRow::Icon;
            if (ImGui::Selectable("Icon", selected)) {
                value = platform::config::CalcOverlayHeaderRow::Icon;
                changed = true;
            }
        }
        if (allowText) {
            const bool selected = value == platform::config::CalcOverlayHeaderRow::Text;
            if (ImGui::Selectable("Text", selected)) {
                value = platform::config::CalcOverlayHeaderRow::Text;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

// Perform an insert-based reorder: move item at srcIndex to dstIndex position.
template <typename T>
void ReorderInsert(std::vector<T>& items, int srcIndex, int dstIndex, bool after) {
    if (srcIndex < 0 || dstIndex < 0 ||
        srcIndex >= static_cast<int>(items.size()) ||
        dstIndex >= static_cast<int>(items.size()) ||
        srcIndex == dstIndex) {
        return;
    }
    int insertIndex = dstIndex + (after ? 1 : 0);
    if (srcIndex < insertIndex) {
        insertIndex -= 1;
    }
    auto movedItem = std::move(items[static_cast<std::size_t>(srcIndex)]);
    items.erase(items.begin() + srcIndex);
    items.insert(items.begin() + insertIndex, std::move(movedItem));
}

// Draw a copy-to-clipboard button with a tooltip showing the full path.
void PathCopyButton(const char* label, const char* path, const char* fallback = "[unavailable]") {
    const bool available = path != nullptr && path[0] != '\0';
    ImGui::BeginDisabled(!available);
    if (AnimatedButton(label)) {
        if (available) {
            ImGui::SetClipboardText(path);
        }
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s", available ? path : fallback);
    }
}

std::string MakeUniqueCalcOverlayMirrorName(const platform::config::LinuxscreenConfig& config) {
    const std::string stem = "Calc Overlay";
    auto exists = [&](const std::string& name) {
        for (const auto& mirror : config.mirrors) {
            if (mirror.name == name) {
                return true;
            }
        }
        return false;
    };

    if (!exists(stem)) {
        return stem;
    }

    std::string candidate = stem + " (Copy)";
    int suffix = 2;
    while (exists(candidate)) {
        candidate = stem + " (Copy " + std::to_string(suffix) + ")";
        ++suffix;
    }
    return candidate;
}

std::string NormalizePathForComparison(const std::string& path) {
    if (path.empty()) {
        return {};
    }

    const std::filesystem::path input(path);
    std::error_code ec;
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(input, ec);
    return ec ? input.lexically_normal().string() : canonical.lexically_normal().string();
}

std::string ResolveMirrorImagePathForCalcOverlayWarning(const platform::config::MirrorSourceConfig& source) {
    if (source.type == platform::config::MirrorSourceType::CalcOverlay) {
        return GetCalcOverlayImagePath();
    }
    if (source.type == platform::config::MirrorSourceType::Image && !source.image.empty()) {
        return platform::config::ResolvePathFromConfigDir(source.image);
    }
    return {};
}

bool HasExistingCalcOverlayMirror(const platform::config::LinuxscreenConfig& config,
                                  const CalcOverlayRuntimeStatus& runtimeStatus) {
    const std::string calcOverlayImagePath = NormalizePathForComparison(runtimeStatus.imagePath);
    if (calcOverlayImagePath.empty()) {
        return false;
    }

    return std::any_of(config.mirrors.begin(), config.mirrors.end(), [&](const auto& mirror) {
        return NormalizePathForComparison(ResolveMirrorImagePathForCalcOverlayWarning(mirror.source)) ==
               calcOverlayImagePath;
    });
}

platform::config::MirrorConfig BuildCalcOverlayMirror(const platform::config::LinuxscreenConfig& config,
                                                      const CalcOverlayRuntimeStatus& runtimeStatus) {
    platform::config::MirrorConfig mirror;
    mirror.name = MakeUniqueCalcOverlayMirrorName(config);
    mirror.output.relativeTo = "topLeftScreen";
    mirror.colorSensitivity = 1.0f;
    mirror.rawOutput = true;
    mirror.border.dynamicThickness = 0;
    mirror.source.type = platform::config::MirrorSourceType::CalcOverlay;
    mirror.source.image = runtimeStatus.imagePath;
    mirror.source.useImageSize = true;
    mirror.source.imageReloadPollMs = 1;

    platform::config::MirrorCaptureConfig zone;
    zone.relativeTo = "centerViewport";
    mirror.input.push_back(zone);
    return mirror;
}

void CreateCalcOverlayMirror(platform::config::LinuxscreenConfig& config,
                             const CalcOverlayRuntimeStatus& runtimeStatus) {
    config.mirrors.push_back(BuildCalcOverlayMirror(config, runtimeStatus));
    g_mirrorEditorState.mirrorListSelectionIndex = static_cast<int>(config.mirrors.size()) - 1;
    g_mirrorEditorState.selectedMirrorIndex = -1;
    g_mirrorEditorState.nameBuffer[0] = '\0';
    g_mirrorEditorState.mirrorNameError.clear();
    AutoSaveConfig(config);
}

// ── Transposed column table ──
// Draws a table where each TABLE COLUMN = one overlay column item,
// with a narrow row-header column on the left.
// Rows: Reorder (:: drag + visible checkbox) | Name | Header
// Drag left/right to reorder with vertical line preview.
// Columns with visible=false are shown with a dimmed/disabled style.
template <typename T, typename DisplayFn>
void DrawTransposedColumnTable(const char* tableId,
                               const char* dndType,
                               std::vector<T>& items,
                               DisplayFn getDisplay,
                               bool allowIcon,
                               bool& changed) {
    const int numItems = static_cast<int>(items.size());
    if (numItems == 0) return;

    int dragSource = -1;
    int dragTarget = -1;
    bool dropAfter = false;
    int previewCol = -1;
    bool previewAfter = false;

    // +1 for the row-header label column on the left
    const int totalCols = 1 + numItems;

    // Measure the widest row label to size the header column correctly
    const float labelPad = ImGui::GetStyle().CellPadding.x * 2.0f + 4.0f;
    const float labelColWidth = ImGui::CalcTextSize("Visible").x + labelPad;

    constexpr ImGuiTableFlags tableFlags =
        ImGuiTableFlags_BordersInnerH |
        ImGuiTableFlags_BordersInnerV |
        ImGuiTableFlags_BordersOuterH |
        ImGuiTableFlags_PadOuterX |
        ImGuiTableFlags_SizingStretchSame;

    if (ImGui::BeginTable(tableId, totalCols, tableFlags)) {
        ImGuiTable* table = ImGui::GetCurrentTable();

        // Row-header column (fixed, auto-sized to widest label)
        ImGui::TableSetupColumn("##label", ImGuiTableColumnFlags_WidthFixed, labelColWidth);
        for (int i = 0; i < numItems; ++i) {
            ImGui::TableSetupColumn(getDisplay(items[static_cast<std::size_t>(i)]));
        }

        float tableTopY = 0.0f;
        float tableBottomY = 0.0f;

        // Alternating column backgrounds — match ImGui's RowBg / RowBgAlt colors
        const ImU32 colBgEven = ImGui::GetColorU32(ImGuiCol_TableRowBg);
        const ImU32 colBgOdd  = ImGui::GetColorU32(ImGuiCol_TableRowBgAlt);

        // Helper lambda: register per-cell drop targets for a given row
        // (mirrors the pattern used in mirror groups — per-cell so rects don't overlap)
        auto registerRowDropTargets = [&](int rowIdx) {
            for (int i = 0; i < numItems; ++i) {
                const int col = i + 1;
                ImGui::TableSetColumnIndex(col);
                const ImRect cellRect = ImGui::TableGetCellBgRect(table, col);
                ImGui::PushID(rowIdx * numItems + i + 4000);
                if (ImGui::BeginDragDropTargetCustom(cellRect, ImGui::GetID("##col_drop"))) {
                    const float midX = (cellRect.Min.x + cellRect.Max.x) * 0.5f;
                    const bool isAfter = ImGui::GetIO().MousePos.x > midX;
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(
                            dndType, ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
                        if (payload->DataSize == sizeof(int)) {
                            previewCol = i;
                            previewAfter = isAfter;
                            if (payload->IsDelivery()) {
                                dragSource = *static_cast<const int*>(payload->Data);
                                dragTarget = i;
                                dropAfter = isAfter;
                            }
                        }
                    }
                    ImGui::EndDragDropTarget();
                }
                ImGui::PopID();
            }
        };

        // ── Row 0: Drag handle + Visible checkbox (combined) ──
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        tableTopY = ImGui::TableGetCellBgRect(table, 0).Min.y;
        ImGui::TextDisabled("Visible");
        for (int i = 0; i < numItems; ++i) {
            const int col = i + 1;
            ImGui::TableSetColumnIndex(col);
            ImGui::PushID(i);

            // Wide :: drag button followed by visible checkbox on same line
            const float colWidth = ImGui::GetColumnWidth();
            const float cbWidth = ImGui::GetFrameHeight();
            const float spacing = ImGui::GetStyle().ItemSpacing.x;
            const float dragBtnWidth = colWidth - cbWidth - spacing;
            if (dragBtnWidth > 10.0f) {
                (void)ImGui::Button("::##col_drag", ImVec2(dragBtnWidth, 0));
            } else {
                (void)ImGui::SmallButton("::##col_drag");
            }
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoDisableHover)) {
                ImGui::SetDragDropPayload(dndType, &i, sizeof(i));
                ImGui::TextUnformatted(getDisplay(items[static_cast<std::size_t>(i)]));
                ImGui::EndDragDropSource();
            }
            ImGui::SameLine();
            ImGui::PushID(1000);
            if (ImGui::Checkbox("##vis", &items[static_cast<std::size_t>(i)].visible)) {
                changed = true;
            }
            ImGui::PopID();

            ImGui::PopID();
        }
        registerRowDropTargets(0);

        // ── Row 1: Names ──
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextDisabled("Name");
        for (int i = 0; i < numItems; ++i) {
            ImGui::TableSetColumnIndex(i + 1);
            const bool dimmed = !items[static_cast<std::size_t>(i)].visible;
            if (dimmed) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.4f);
            ImGui::TextUnformatted(getDisplay(items[static_cast<std::size_t>(i)]));
            if (dimmed) ImGui::PopStyleVar();
        }
        registerRowDropTargets(1);

        // ── Row 2: Header combos ──
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextDisabled("Header");
        for (int i = 0; i < numItems; ++i) {
            ImGui::TableSetColumnIndex(i + 1);
            ImGui::PushID(i + 2000);
            const bool dimmed = !items[static_cast<std::size_t>(i)].visible;
            if (dimmed) ImGui::BeginDisabled();
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (DrawHeaderRowCombo("##hdr", items[static_cast<std::size_t>(i)].headerRow,
                                   true, allowIcon, true)) {
                changed = true;
            }
            if (dimmed) ImGui::EndDisabled();
            ImGui::PopID();
        }
        registerRowDropTargets(2);

        // Get bottom Y from the last row
        tableBottomY = ImGui::TableGetCellBgRect(table, 0).Max.y;

        // ── Alternating column backgrounds + preview ──
        ImDrawList* dl = ImGui::GetWindowDrawList();
        for (int i = 0; i < numItems; ++i) {
            const int col = i + 1;
            const ImRect cellRect = ImGui::TableGetCellBgRect(table, col);
            ImRect colRect;
            colRect.Min = ImVec2(cellRect.Min.x, tableTopY);
            colRect.Max = ImVec2(cellRect.Max.x, tableBottomY);

            // Alternating column background
            dl->AddRectFilled(colRect.Min, colRect.Max, (i % 2 == 0) ? colBgEven : colBgOdd);

            // Draw preview for this column
            if (previewCol == i && ImGui::GetDragDropPayload() != nullptr) {
                dl->AddRectFilled(colRect.Min, colRect.Max, IM_COL32(88, 166, 236, 34));
                const float lineX = previewAfter ? colRect.Max.x : colRect.Min.x;
                dl->AddLine(
                    ImVec2(lineX, colRect.Min.y),
                    ImVec2(lineX, colRect.Max.y),
                    IM_COL32(72, 190, 255, 255),
                    2.0f);
            }
        }

        ImGui::EndTable();
    }

    // Apply reorder
    if (dragSource >= 0 && dragTarget >= 0 &&
        dragSource < numItems && dragTarget < numItems &&
        dragSource != dragTarget) {
        ReorderInsert(items, dragSource, dragTarget, dropAfter);
        changed = true;
    }
}

// ── Row drag-and-drop table ──
// Draws rows in a table with :: drag handles, following the mirror groups pattern.
template <typename T, typename DisplayFn, typename SettingsFn>
void DrawDragReorderTable(const char* tableId,
                          const char* dndType,
                          std::vector<T>& items,
                          DisplayFn getDisplay,
                          SettingsFn drawSettings,
                          int numExtraColumns,
                          bool& changed) {
    int dragSource = -1;
    int dragTarget = -1;
    bool dropAfter = false;
    int previewRow = -1;
    bool previewAfter = false;

    const int totalColumns = 2 + numExtraColumns; // drag + name + extras

    constexpr ImGuiTableFlags tableFlags =
        ImGuiTableFlags_BordersInnerH |
        ImGuiTableFlags_BordersInnerV |
        ImGuiTableFlags_RowBg |
        ImGuiTableFlags_SizingStretchProp |
        ImGuiTableFlags_PadOuterX;

    ImGuiTable* table = nullptr;
    if (ImGui::BeginTable(tableId, totalColumns, tableFlags)) {
        table = ImGui::GetCurrentTable();
        ImGui::TableSetupColumn("##drag", ImGuiTableColumnFlags_WidthFixed, 24.0f);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);

        for (std::size_t i = 0; i < items.size(); ++i) {
            auto& item = items[i];
            const int idx = static_cast<int>(i);
            ImGui::PushID(idx);
            ImGui::TableNextRow();

            // Drag handle
            ImGui::TableSetColumnIndex(0);
            (void)ImGui::SmallButton("::##drag");
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoDisableHover)) {
                ImGui::SetDragDropPayload(dndType, &idx, sizeof(idx));
                ImGui::TextUnformatted(getDisplay(item));
                ImGui::EndDragDropSource();
            }

            // Name
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(getDisplay(item));

            // Extra columns (visible, header, etc.)
            drawSettings(item, i);

            // Full-row drop target across all columns
            if (table != nullptr) {
                ImRect rowRect = ImGui::TableGetCellBgRect(table, 0);
                const ImRect rightRect = ImGui::TableGetCellBgRect(table, totalColumns - 1);
                rowRect.Max.x = rightRect.Max.x;
                const float midY = (rowRect.Min.y + rowRect.Max.y) * 0.5f;

                for (int col = 0; col < totalColumns; ++col) {
                    ImGui::TableSetColumnIndex(col);
                    const ImRect cellRect = ImGui::TableGetCellBgRect(table, col);
                    ImGui::PushID(col);
                    if (ImGui::BeginDragDropTargetCustom(cellRect, ImGui::GetID("##row_drop"))) {
                        const bool isAfter = ImGui::GetIO().MousePos.y > midY;
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(
                                dndType, ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
                            if (payload->DataSize == sizeof(int)) {
                                previewRow = idx;
                                previewAfter = isAfter;
                                if (payload->IsDelivery()) {
                                    dragSource = *static_cast<const int*>(payload->Data);
                                    dragTarget = idx;
                                    dropAfter = isAfter;
                                }
                            }
                        }
                        ImGui::EndDragDropTarget();
                    }
                    ImGui::PopID();
                }

                // Draw preview highlight
                ImGui::TableSetColumnIndex(totalColumns - 1);
                if (previewRow == idx && ImGui::GetDragDropPayload() != nullptr) {
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    dl->AddRectFilled(rowRect.Min, rowRect.Max, IM_COL32(88, 166, 236, 34));
                    const float lineY = previewAfter ? rowRect.Max.y : rowRect.Min.y;
                    dl->AddLine(ImVec2(rowRect.Min.x, lineY),
                                ImVec2(rowRect.Max.x, lineY),
                                IM_COL32(72, 190, 255, 255),
                                2.0f);
                }
            }

            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    // Apply reorder after table ends
    if (dragSource >= 0 && dragTarget >= 0 &&
        dragSource < static_cast<int>(items.size()) &&
        dragTarget < static_cast<int>(items.size()) &&
        dragSource != dragTarget) {
        ReorderInsert(items, dragSource, dragTarget, dropAfter);
        changed = true;
    }
}

} // namespace

void RenderCalcOverlayTab(platform::config::LinuxscreenConfig& config) {
    auto& overlay = config.calcOverlay;
    bool changed = false;
    const CalcOverlayRuntimeStatus runtimeStatus = GetCalcOverlayRuntimeStatus();

    ImGui::Checkbox("Enable Calc Overlay", &overlay.enabled);
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        changed = true;
    }
    ImGui::SameLine();
    HelpMarker("Starts or stops the headless Java helper that renders calc-overlay.png from Ninjabrain Bot data.");

    if (AnimatedButton("Create Mirror")) {
        if (HasExistingCalcOverlayMirror(config, runtimeStatus)) {
            ImGui::OpenPopup("##calc_overlay_existing_mirror_confirm");
        } else {
            CreateCalcOverlayMirror(config, runtimeStatus);
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Creates a mirror backed by the Calc Overlay image path.");
    }
    if (ImGui::BeginPopupModal("##calc_overlay_existing_mirror_confirm", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("A mirror polling calc-overlay.png already exists");
        ImGui::Separator();
        if (AnimatedButton("Create Anyway")) {
            CreateCalcOverlayMirror(config, runtimeStatus);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (AnimatedButton("Cancel")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (runtimeStatus.running) {
        ImGui::TextColored(ImVec4(0.55f, 0.9f, 0.55f, 1.0f), "Status: running");
    } else if (overlay.enabled) {
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.35f, 1.0f), "Status: waiting");
    } else {
        ImGui::TextDisabled("Status: disabled");
    }

    // Paths — compact copy buttons with hover tooltips
    ImGui::SeparatorText("Paths");
    PathCopyButton("Copy Java Path", runtimeStatus.javaPath.c_str());
    ImGui::SameLine();
    PathCopyButton("Copy Helper Jar", runtimeStatus.jarPath.c_str());
    ImGui::SameLine();
    PathCopyButton("Copy Image Path", runtimeStatus.imagePath.c_str());
    ImGui::SameLine();
    PathCopyButton("Copy Config Dir", runtimeStatus.configDir.c_str());

    ImGui::Spacing();
    ImGui::Spacing();

    if (ImGui::BeginTabBar("##calc_overlay_sections")) {
        // ── General ──
        if (ImGui::BeginTabItem("General")) {
            ImGui::SeparatorText("Display");

            const char* positions[] = { "Top Left", "Top Right", "Bottom Left", "Bottom Right" };
            int currentPosition = static_cast<int>(overlay.overlayPosition);
            if (ImGui::Combo("Overlay Position", &currentPosition, positions, IM_ARRAYSIZE(positions))) {
                overlay.overlayPosition = static_cast<platform::config::CalcOverlayPosition>(currentPosition);
                changed = true;
            }

            int outlineWidth = overlay.outlineWidth;
            if (ImGui::SliderInt("Outline Width", &outlineWidth, 0, 20)) {
                overlay.outlineWidth = outlineWidth;
                changed = true;
            }

            const char* clearUnits[] = { "Never", "Seconds", "Minutes" };
            int clearUnit = static_cast<int>(overlay.clearOverlayTimeUnit);
            if (ImGui::Combo("Clear Overlay After", &clearUnit, clearUnits, IM_ARRAYSIZE(clearUnits))) {
                overlay.clearOverlayTimeUnit = static_cast<platform::config::CalcOverlayClearOverlayTimeUnit>(clearUnit);
                changed = true;
            }
            if (overlay.clearOverlayTimeUnit != platform::config::CalcOverlayClearOverlayTimeUnit::Never) {
                int amount = overlay.clearOverlayAmount;
                if (ImGui::SliderInt("Amount", &amount, 1, 60)) {
                    overlay.clearOverlayAmount = amount;
                    changed = true;
                }
            }

            ImGui::SeparatorText("Font");
            static char fontNameBuffer[256];
            static std::string fontNameKey;
            if (fontNameKey != overlay.fontName) {
                fontNameKey = overlay.fontName;
                std::snprintf(fontNameBuffer, sizeof(fontNameBuffer), "%s", overlay.fontName.c_str());
            }
            if (ImGui::InputText("Font Name", fontNameBuffer, sizeof(fontNameBuffer))) {
                overlay.fontName = fontNameBuffer;
                fontNameKey = overlay.fontName;
                changed = true;
            }

            static bool fontsScanned = false;
            static std::map<std::string, std::string> discoveredFonts;
            if (AnimatedButton("Scan Fonts")) {
                discoveredFonts = platform::common::ScanForFonts();
                fontsScanned = true;
            }
            if (fontsScanned && !discoveredFonts.empty()) {
                ImGui::SameLine();
                std::string previewLabel = "Select Font";
                if (!overlay.fontName.empty()) {
                    previewLabel = std::filesystem::path(overlay.fontName).filename().string();
                    if (previewLabel.empty()) {
                        previewLabel = overlay.fontName;
                    }
                }
                const char* preview = previewLabel.c_str();
                if (ImGui::BeginCombo("##calc_overlay_font", preview)) {
                    for (const auto& [name, path] : discoveredFonts) {
                        const bool selected = overlay.fontName == path || overlay.fontName == name;
                        if (ImGui::Selectable(name.c_str(), selected)) {
                            overlay.fontName = path;
                            fontNameKey = overlay.fontName;
                            std::snprintf(fontNameBuffer, sizeof(fontNameBuffer), "%s", overlay.fontName.c_str());
                            changed = true;
                        }
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("%s", path.c_str());
                        }
                    }
                    ImGui::EndCombo();
                }
            }

            int fontSize = overlay.fontSize;
            if (ImGui::SliderInt("Font Size", &fontSize, 8, 96)) {
                overlay.fontSize = fontSize;
                changed = true;
            }

            ImGui::SeparatorText("Colors");
            float netherColor[3] = {
                overlay.netherCoordsColor.r, overlay.netherCoordsColor.g, overlay.netherCoordsColor.b
            };
            if (ImGui::ColorEdit3("Nether Coords Color", netherColor, ImGuiColorEditFlags_NoInputs)) {
                overlay.netherCoordsColor = { netherColor[0], netherColor[1], netherColor[2], 1.0f };
                changed = true;
            }

            if (ImGui::Checkbox("Use Negative Coords Color", &overlay.negativeCoords.use)) {
                changed = true;
            }
            if (overlay.negativeCoords.use) {
                float negativeColor[3] = {
                    overlay.negativeCoords.color.r,
                    overlay.negativeCoords.color.g,
                    overlay.negativeCoords.color.b
                };
                if (ImGui::ColorEdit3("Negative Coords Color", negativeColor, ImGuiColorEditFlags_NoInputs)) {
                    overlay.negativeCoords.color = { negativeColor[0], negativeColor[1], negativeColor[2], 1.0f };
                    changed = true;
                }
            }

            ImGui::EndTabItem();
        }

        // ── Eye Throws ──
        if (ImGui::BeginTabItem("Eye Throws")) {
            ImGui::SeparatorText("Display");

            int shownMeasurements = overlay.shownMeasurements;
            if (ImGui::SliderInt("Shown Measurements", &shownMeasurements, 1, 5)) {
                overlay.shownMeasurements = shownMeasurements;
                changed = true;
            }

            const char* overworldModes[] = { "Chunk", "(8, 8)", "(4, 4)" };
            int overworldMode = static_cast<int>(overlay.overworldCoordsMode);
            if (ImGui::Combo("Overworld Coords", &overworldMode, overworldModes, IM_ARRAYSIZE(overworldModes))) {
                overlay.overworldCoordsMode = static_cast<platform::config::CalcOverlayOverworldCoordsMode>(overworldMode);
                changed = true;
            }

            if (ImGui::Checkbox("Show Overworld/Nether Coords Based on Dimension",
                                &overlay.onlyShowCurrentDimensionCoords)) {
                changed = true;
            }
            if (ImGui::Checkbox("Show Information Bar", &overlay.showInfoBar)) {
                changed = true;
            }

            const char* angleDisplays[] = {
                "Angle (and angle change)",
                "Angle only",
                "Angle change only",
            };
            int angleDisplay = static_cast<int>(overlay.angleDisplay);
            if (ImGui::Combo("Angle Display", &angleDisplay, angleDisplays, IM_ARRAYSIZE(angleDisplays))) {
                overlay.angleDisplay = static_cast<platform::config::CalcOverlayAngleDisplayMode>(angleDisplay);
                changed = true;
            }

            // Transposed column table: each table column = one overlay column
            ImGui::SeparatorText("Columns");
            DrawTransposedColumnTable(
                "##eye_col_table",
                "CALC_EYE_COL",
                overlay.eyeColumns,
                [](const auto& col) { return GetEyeColumnDisplay(col.type); },
                true, // allow Icon header
                changed);

            ImGui::EndTabItem();
        }

        // ── Blind Coords ──
        if (ImGui::BeginTabItem("Blind Coords")) {
            ImGui::SeparatorText("Settings");

            if (ImGui::Checkbox("Enable Blind Coords On Overlay", &overlay.blindCoordsEnabled)) {
                changed = true;
            }
            if (ImGui::Checkbox("Show Direction And Distance To Good Blind Coords",
                                &overlay.showDirectionAndDistance)) {
                changed = true;
            }
            
            ImGui::EndTabItem();
        }

        // ── All Advancements ──
        if (ImGui::BeginTabItem("All Advancements")) {
            if (ImGui::Checkbox("Enable All Advancements On Overlay", &overlay.allAdvancementsEnabled)) {
                changed = true;
            }

            // Transposed column table: each table column = one overlay column
            ImGui::SeparatorText("Columns");
            DrawTransposedColumnTable(
                "##aa_col_table",
                "CALC_AA_COL",
                overlay.allAdvancements.columns,
                [](const auto& col) { return GetAaColumnDisplay(col.type); },
                false, // no Icon header option for AA columns
                changed);

            // Rows table with drag reordering
            ImGui::SeparatorText("Rows");
            DrawDragReorderTable(
                "##aa_rows_table",
                "CALC_AA_ROW",
                overlay.allAdvancements.rows,
                [](const auto& row) { return GetAaRowDisplay(row.type); },
                [&changed](auto& row, [[maybe_unused]] std::size_t i) {
                    ImGui::TableSetColumnIndex(2);
                    if (ImGui::Checkbox("##vis", &row.visible)) {
                        changed = true;
                    }
                },
                1, // 1 extra column (Visible)
                changed);

            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    if (changed) {
        AutoSaveConfig(config);
    }
}

} // namespace platform::x11
