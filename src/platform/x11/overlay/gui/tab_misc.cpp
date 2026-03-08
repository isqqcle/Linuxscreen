#include "../overlay_internal.h"
#include "imgui_overlay_helpers.h"
#include "tab_misc.h"

namespace platform::x11 {

void RenderMiscTab(platform::config::LinuxscreenConfig& config) {
    auto lowercaseForSearch = [](std::string value) {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        return value;
    };

    auto applyThemePresetAndSave = [&](const char* themeName) {
        config.guiTheme = themeName;
        config.guiCustomColors.clear();

        const ImGuiStyle startStyle = ImGui::GetStyle();
        ApplyThemePreset(config.guiTheme);
        const ImGuiStyle targetStyle = ImGui::GetStyle();
        ImGui::GetStyle() = startStyle;
        StartThemeTransitionToStyle(targetStyle);

        platform::config::SaveThemeFile(config.guiTheme, config.guiCustomColors);
        AutoSaveConfig(config);
    };

    struct ThemeColorEntry {
        const char* label;
        ImGuiCol_ colorId;
        const char* key;
    };

    static const ThemeColorEntry kWindowColors[] = {
        {"Window Background", ImGuiCol_WindowBg, "WindowBg"},
        {"Child Background", ImGuiCol_ChildBg, "ChildBg"},
        {"Popup Background", ImGuiCol_PopupBg, "PopupBg"},
        {"Border", ImGuiCol_Border, "Border"},
    };
    static const ThemeColorEntry kTextColors[] = {
        {"Text", ImGuiCol_Text, "Text"},
        {"Text Disabled", ImGuiCol_TextDisabled, "TextDisabled"},
    };
    static const ThemeColorEntry kFrameColors[] = {
        {"Frame Background", ImGuiCol_FrameBg, "FrameBg"},
        {"Frame Hovered", ImGuiCol_FrameBgHovered, "FrameBgHovered"},
        {"Frame Active", ImGuiCol_FrameBgActive, "FrameBgActive"},
    };
    static const ThemeColorEntry kTitleColors[] = {
        {"Title Background", ImGuiCol_TitleBg, "TitleBg"},
        {"Title Active", ImGuiCol_TitleBgActive, "TitleBgActive"},
        {"Title Collapsed", ImGuiCol_TitleBgCollapsed, "TitleBgCollapsed"},
    };
    static const ThemeColorEntry kButtonColors[] = {
        {"Button", ImGuiCol_Button, "Button"},
        {"Button Hovered", ImGuiCol_ButtonHovered, "ButtonHovered"},
        {"Button Active", ImGuiCol_ButtonActive, "ButtonActive"},
    };
    static const ThemeColorEntry kHeaderColors[] = {
        {"Header", ImGuiCol_Header, "Header"},
        {"Header Hovered", ImGuiCol_HeaderHovered, "HeaderHovered"},
        {"Header Active", ImGuiCol_HeaderActive, "HeaderActive"},
    };
    static const ThemeColorEntry kTabColors[] = {
        {"Tab", ImGuiCol_Tab, "Tab"},
        {"Tab Hovered", ImGuiCol_TabHovered, "TabHovered"},
        {"Tab Selected", ImGuiCol_TabSelected, "TabSelected"},
    };
    static const ThemeColorEntry kSliderScrollColors[] = {
        {"Slider Grab", ImGuiCol_SliderGrab, "SliderGrab"},
        {"Slider Grab Active", ImGuiCol_SliderGrabActive, "SliderGrabActive"},
        {"Scrollbar Background", ImGuiCol_ScrollbarBg, "ScrollbarBg"},
        {"Scrollbar Grab", ImGuiCol_ScrollbarGrab, "ScrollbarGrab"},
        {"Scrollbar Grab Hovered", ImGuiCol_ScrollbarGrabHovered, "ScrollbarGrabHovered"},
        {"Scrollbar Grab Active", ImGuiCol_ScrollbarGrabActive, "ScrollbarGrabActive"},
    };
    static const ThemeColorEntry kSelectionColors[] = {
        {"Check Mark", ImGuiCol_CheckMark, "CheckMark"},
        {"Text Selected Background", ImGuiCol_TextSelectedBg, "TextSelectedBg"},
    };
    static const ThemeColorEntry kSeparatorResizeColors[] = {
        {"Separator", ImGuiCol_Separator, "Separator"},
        {"Separator Hovered", ImGuiCol_SeparatorHovered, "SeparatorHovered"},
        {"Separator Active", ImGuiCol_SeparatorActive, "SeparatorActive"},
        {"Resize Grip", ImGuiCol_ResizeGrip, "ResizeGrip"},
        {"Resize Grip Hovered", ImGuiCol_ResizeGripHovered, "ResizeGripHovered"},
        {"Resize Grip Active", ImGuiCol_ResizeGripActive, "ResizeGripActive"},
    };

    struct ThemeColorCategory {
        const char* name;
        const ThemeColorEntry* entries;
        std::size_t count;
    };

    static const ThemeColorCategory kThemeCategories[] = {
        {"Window", kWindowColors, sizeof(kWindowColors) / sizeof(kWindowColors[0])},
        {"Text", kTextColors, sizeof(kTextColors) / sizeof(kTextColors[0])},
        {"Frame (Input Fields)", kFrameColors, sizeof(kFrameColors) / sizeof(kFrameColors[0])},
        {"Title Bar", kTitleColors, sizeof(kTitleColors) / sizeof(kTitleColors[0])},
        {"Buttons", kButtonColors, sizeof(kButtonColors) / sizeof(kButtonColors[0])},
        {"Headers", kHeaderColors, sizeof(kHeaderColors) / sizeof(kHeaderColors[0])},
        {"Tabs", kTabColors, sizeof(kTabColors) / sizeof(kTabColors[0])},
        {"Sliders & Scrollbars", kSliderScrollColors, sizeof(kSliderScrollColors) / sizeof(kSliderScrollColors[0])},
        {"Checkboxes & Selections", kSelectionColors, sizeof(kSelectionColors) / sizeof(kSelectionColors[0])},
        {"Separators & Resize Grips", kSeparatorResizeColors, sizeof(kSeparatorResizeColors) / sizeof(kSeparatorResizeColors[0])},
    };

    static int s_selectedThemeColorCategory = 0;

    ImGuiStyle& style = ImGui::GetStyle();
    auto saveCustomThemeColor = [&](const ThemeColorEntry& entry) {
        const ImVec4& color = style.Colors[entry.colorId];
        config.guiCustomColors[entry.key] = {color.x, color.y, color.z, color.w};
        config.guiTheme = "Custom";
        platform::config::SaveThemeFile(config.guiTheme, config.guiCustomColors);
        AutoSaveConfig(config);
    };

    if (ImGui::BeginTabBar("##misc_sections")) {
        if (ImGui::BeginTabItem("Theme")) {
            ImGui::SeparatorText("Theme Presets");
            ImGui::Text("Preset Themes:");
            ImGui::SameLine();
            HelpMarker("Select a preset theme or customize individual colors below.");

            static const char* kThemePresetNames[] = {
                "Dark", "Light", "Classic", "Dracula",
                "Nord", "Solarized", "Monokai", "Catppuccin",
                "One Dark", "Gruvbox", "Tokyo Night", "Purple",
                "Pink", "Blue", "Teal", "Red",
                "Green", "Balanced",
            };
            constexpr int kPresetColumns = 4;
            constexpr std::size_t kPresetCount = sizeof(kThemePresetNames) / sizeof(kThemePresetNames[0]);
            for (std::size_t i = 0; i < kPresetCount; ++i) {
                std::string buttonLabel = std::string(kThemePresetNames[i]) + "##preset";
                if (AnimatedButton(buttonLabel.c_str())) {
                    applyThemePresetAndSave(kThemePresetNames[i]);
                }
                const bool endOfRow = ((i + 1) % kPresetColumns) == 0;
                if (!endOfRow && i + 1 < kPresetCount) {
                    ImGui::SameLine();
                }
            }

            ImGui::Spacing();
            ImGui::TextDisabled("Active: %s", config.guiTheme.c_str());

            ImGui::Spacing();
            ImGui::SeparatorText("Custom Colors");
            HelpMarker("Pick a color category on the left, then edit its colors on the right.");

            s_selectedThemeColorCategory = std::clamp(s_selectedThemeColorCategory,
                                                      0,
                                                      static_cast<int>((sizeof(kThemeCategories) / sizeof(kThemeCategories[0])) - 1));
            const ImGuiTableFlags splitPaneFlags = ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV;
            if (ImGui::BeginTable("##theme_color_split", 2, splitPaneFlags)) {
                ImGui::TableSetupColumn("Category", ImGuiTableColumnFlags_WidthFixed, 260.0f);
                ImGui::TableSetupColumn("Editor", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                ImGui::BeginChild("##theme_color_category_list");
                for (std::size_t i = 0; i < (sizeof(kThemeCategories) / sizeof(kThemeCategories[0])); ++i) {
                    const bool selected = static_cast<int>(i) == s_selectedThemeColorCategory;
                    std::string listLabel = std::string(kThemeCategories[i].name) + "###theme_color_cat_" + std::to_string(i);
                    if (ImGui::Selectable(listLabel.c_str(), selected)) {
                        s_selectedThemeColorCategory = static_cast<int>(i);
                    }
                    if (i + 1 < (sizeof(kThemeCategories) / sizeof(kThemeCategories[0]))) {
                        ImGui::Separator();
                    }
                }
                ImGui::EndChild();

                ImGui::TableSetColumnIndex(1);
                ImGui::BeginChild("##theme_color_editor");
                const ThemeColorCategory& category = kThemeCategories[static_cast<std::size_t>(s_selectedThemeColorCategory)];
                ImGui::TextUnformatted(category.name);
                ImGui::Separator();
                for (std::size_t i = 0; i < category.count; ++i) {
                    const ThemeColorEntry& entry = category.entries[i];
                    std::string colorLabel = std::string(entry.label) + "##theme_" + entry.key;
                    if (ImGui::ColorEdit4(colorLabel.c_str(), (float*)&style.Colors[entry.colorId])) {
                        saveCustomThemeColor(entry);
                    }
                }
                ImGui::EndChild();

                ImGui::EndTable();
            }

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Fonts")) {
            ImGui::SeparatorText("Font Source");
            ImGui::Text("Font Path or Name:");
            static char fontPathBuf[512];
            std::strncpy(fontPathBuf, config.guiFontPath.c_str(), sizeof(fontPathBuf) - 1);
            fontPathBuf[sizeof(fontPathBuf) - 1] = '\0';
            if (ImGui::InputText("##FontPath", fontPathBuf, sizeof(fontPathBuf))) {
                config.guiFontPath = fontPathBuf;
                AutoSaveConfig(config);
            }

            if (AnimatedButton("Scan for Fonts")) {
                g_discoveredFonts = platform::common::ScanForFonts();
                g_fontsScanned = true;
            }

            if (g_fontsScanned && !g_discoveredFonts.empty()) {
                ImGui::SameLine();
                ImGui::SetNextItemWidth(260.0f);
                if (ImGui::BeginCombo("##font_dropdown", "Discover Fonts...")) {
                    ImGui::InputTextWithHint("##filter", "Search...", g_fontSearchFilter, sizeof(g_fontSearchFilter));

                    std::string filter = lowercaseForSearch(g_fontSearchFilter);

                    for (const auto& [name, path] : g_discoveredFonts) {
                        std::string nameLower = lowercaseForSearch(name);

                        if (filter.empty() || nameLower.find(filter) != std::string::npos) {
                            const bool isSelected = (config.guiFontPath == name);
                            if (ImGui::Selectable(name.c_str(), isSelected)) {
                                config.guiFontPath = name;
                                AutoSaveConfig(config);
                            }
                            if (ImGui::IsItemHovered()) {
                                ImGui::SetTooltip("%s", path.c_str());
                            }
                        }
                    }
                    ImGui::EndCombo();
                }
            }

            ImGui::Spacing();
            ImGui::SeparatorText("Font Size");
            int fontSize = config.guiFontSize;
            if (ImGui::DragInt("##FontSize", &fontSize, 1, 8, 36)) {
                config.guiFontSize = fontSize;
                AutoSaveConfig(config);
            }

            ImGui::Spacing();
            ImGui::SeparatorText("Font Actions");
            if (AnimatedButton("Apply Font Changes")) {
                g_resetOverlayRequested = true;
            }
            ImGui::SameLine();
            if (AnimatedButton("Reset Font")) {
                config.guiFontPath = "";
                config.guiFontSize = 13;
                AutoSaveConfig(config);
                g_resetOverlayRequested = true;
            }

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Interface")) {
            ImGui::SeparatorText("GUI Scale");
            float scale = config.guiScale;
            if (ImGui::DragFloat("Scale##GuiScale", &scale, 0.01f, 0.5f, 2.0f, "%.2fx")) {
                config.guiScale = scale;
                AutoSaveConfig(config);
            }
            ImGui::SameLine();
            if (AnimatedButton("Reset##Scale")) {
                config.guiScale = 1.0f;
                AutoSaveConfig(config);
            }

            ImGui::Spacing();
            ImGui::SeparatorText("GUI Opacity");
            float opacity = config.guiOpacity;
            if (ImGui::DragFloat("Opacity##GuiOpacity", &opacity, 0.01f, 0.1f, 1.0f, "%.2f")) {
                config.guiOpacity = opacity;
                ImGui::GetStyle().Alpha = opacity;
                AutoSaveConfig(config);
            }
            ImGui::SameLine();
            HelpMarker("Controls the transparency of the entire overlay UI.");

            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }
}

} // namespace platform::x11
