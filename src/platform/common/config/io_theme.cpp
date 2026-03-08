std::string GetThemePath() {
    const std::string configPath = GetConfigPath();
    const std::filesystem::path fsPath(configPath);
    return (fsPath.parent_path() / "theme.toml").string();
}

void SaveThemeFile(const std::string& theme,
                   const std::map<std::string, std::array<float, 4>>& customColors) {
    const std::string path = GetThemePath();
    try {
        std::filesystem::path fsPath(path);
        std::filesystem::path parentPath = fsPath.parent_path();
        if (!parentPath.empty() && !std::filesystem::exists(parentPath)) {
            std::filesystem::create_directories(parentPath);
        }

        toml::table out;
        out.insert("theme", theme);

        if (!customColors.empty()) {
            toml::table colorsTbl;
            for (const auto& [key, rgba] : customColors) {
                toml::array arr;
                arr.push_back(static_cast<double>(rgba[0]));
                arr.push_back(static_cast<double>(rgba[1]));
                arr.push_back(static_cast<double>(rgba[2]));
                arr.push_back(static_cast<double>(rgba[3]));
                colorsTbl.insert(key, arr);
            }
            out.insert("customColors", colorsTbl);
        }

        std::ofstream file(path);
        if (file) {
            file << out;
            LogDebug("Theme saved to %s", path.c_str());
        } else {
            LogWarning("Failed to open theme file for writing: %s", path.c_str());
        }
    } catch (const std::exception& e) {
        LogWarning("Failed to save theme file: %s", e.what());
    }
}

bool LoadThemeFile(std::string& theme,
                   std::map<std::string, std::array<float, 4>>& customColors) {
    const std::string path = GetThemePath();
    if (!std::filesystem::exists(path)) {
        return false;
    }
    try {
        auto tbl = toml::parse_file(path);

        if (auto themeVal = tbl.get("theme")) {
            if (auto s = themeVal->value<std::string>()) {
                theme = *s;
            }
        }

        if (auto colorsTbl = tbl.get("customColors")) {
            if (auto ct = colorsTbl->as_table()) {
                for (const auto& [key, node] : *ct) {
                    if (auto arr = node.as_array()) {
                        if (arr->size() >= 4) {
                            std::array<float, 4> rgba{};
                            for (int i = 0; i < 4; ++i) {
                                if (auto v = (*arr)[i].value<double>()) {
                                    rgba[i] = static_cast<float>(*v);
                                }
                            }
                            customColors[std::string(key)] = rgba;
                        }
                    }
                }
            }
        }

        LogDebug("Theme loaded from %s (theme=%s, %zu custom colors)",
                 path.c_str(), theme.c_str(), customColors.size());
        return true;
    } catch (const std::exception& e) {
        LogWarning("Failed to parse theme file %s: %s", path.c_str(), e.what());
        return false;
    }
}

} // namespace platform::config
