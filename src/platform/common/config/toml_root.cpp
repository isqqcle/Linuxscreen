namespace {

toml::table MakeAppearanceTable(const LinuxscreenConfig& cfg) {
    toml::table appearanceTbl;
    appearanceTbl.insert("theme", cfg.guiTheme);

    toml::table colorsTbl;
    for (const auto& [name, rgba] : cfg.guiCustomColors) {
        toml::array colorArr;
        colorArr.push_back(static_cast<double>(rgba[0]));
        colorArr.push_back(static_cast<double>(rgba[1]));
        colorArr.push_back(static_cast<double>(rgba[2]));
        colorArr.push_back(static_cast<double>(rgba[3]));
        colorsTbl.insert(name, std::move(colorArr));
    }
    appearanceTbl.insert("customColors", std::move(colorsTbl));
    return appearanceTbl;
}

void LoadAppearanceTable(const toml::table& tbl, LinuxscreenConfig& cfg) {
    cfg.guiTheme = GetStringOr(tbl, "theme", cfg.guiTheme);
    cfg.guiCustomColors.clear();

    if (auto customColorsTbl = GetTable(tbl, "customColors")) {
        for (const auto& [key, value] : *customColorsTbl) {
            if (auto arr = value.as_array()) {
                const Color parsed = ColorFromTomlArray(arr, {0.0f, 0.0f, 0.0f, 1.0f});
                cfg.guiCustomColors[std::string(key.str())] = {parsed.r, parsed.g, parsed.b, parsed.a};
            }
        }
    }
}

} // namespace

toml::table LinuxscreenConfigToToml(const LinuxscreenConfig& cfg) {
    toml::table out = cfg.windowsPassthroughRoot;
    out.insert_or_assign("configVersion", cfg.configVersion);
    out.insert_or_assign("defaultMode", cfg.defaultMode);
    
    toml::array guiHotkeyArr;
    for (auto key : cfg.guiHotkey) {
        guiHotkeyArr.push_back(static_cast<int64_t>(key));
    }
    out.insert_or_assign("guiHotkey", guiHotkeyArr);
    out.erase("rebindToggleHotkey");
    
    toml::array mirrorsArr;
    for (const auto& mirror : cfg.mirrors) {
        toml::table mirrorTbl;
        MirrorConfigToToml(mirror, mirrorTbl);
        mirrorsArr.push_back(mirrorTbl);
    }
    out.insert_or_assign("mirror", mirrorsArr);
    
    toml::array mirrorGroupsArr;
    for (const auto& group : cfg.mirrorGroups) {
        toml::table groupTbl;
        MirrorGroupConfigToToml(group, groupTbl);
        mirrorGroupsArr.push_back(groupTbl);
    }
    out.insert_or_assign("mirrorGroup", mirrorGroupsArr);
    
    toml::array modesArr;
    for (const auto& mode : cfg.modes) {
        toml::table modeTbl;
        ModeConfigToToml(mode, modeTbl);
        modesArr.push_back(modeTbl);
    }
    out.insert_or_assign("mode", modesArr);
    
    toml::array hotkeysArr;
    for (const auto& hotkey : cfg.hotkeys) {
        toml::table hotkeyTbl;
        HotkeyConfigToToml(hotkey, hotkeyTbl);
        hotkeysArr.push_back(hotkeyTbl);
    }
    out.insert_or_assign("hotkey", hotkeysArr);

    toml::array sensitivityHotkeysArr;
    for (const auto& sensitivityHotkey : cfg.sensitivityHotkeys) {
        toml::table sensitivityHotkeyTbl;
        SensitivityHotkeyConfigToToml(sensitivityHotkey, sensitivityHotkeyTbl);
        sensitivityHotkeysArr.push_back(sensitivityHotkeyTbl);
    }
    out.insert_or_assign("sensitivityHotkey", sensitivityHotkeysArr);

    out.insert_or_assign("mouseSensitivity", static_cast<double>(cfg.mouseSensitivity));
    out.insert_or_assign("windowsMouseSpeed", cfg.windowsMouseSpeed);
    out.insert_or_assign("keyRepeatStartDelay", cfg.keyRepeatStartDelay);
    out.insert_or_assign("keyRepeatDelay", cfg.keyRepeatDelay);
    out.insert_or_assign("keyRepeatAffectsMouseButtons", cfg.keyRepeatAffectsMouseButtons);

    toml::table keyRebindsTbl;
    KeyRebindsConfig keyRebindsCfg = cfg.keyRebinds;
    keyRebindsCfg.toggleHotkey = cfg.rebindToggleHotkey;
    KeyRebindsConfigToToml(keyRebindsCfg, keyRebindsTbl);
    out.insert_or_assign("keyRebinds", keyRebindsTbl);
    
    toml::table eyezoomTbl;
    EyeZoomConfigToToml(cfg.eyezoom, eyezoomTbl);
    out.insert_or_assign("eyezoom", eyezoomTbl);

    out.insert_or_assign("guiScale", static_cast<double>(cfg.guiScale));
    out.insert_or_assign("guiTheme", cfg.guiTheme);
    out.insert_or_assign("guiWidth", cfg.guiWidth);
    out.insert_or_assign("guiHeight", cfg.guiHeight);
    out.insert_or_assign("guiFontPath", cfg.guiFontPath);
    out.insert_or_assign("guiFontSize", cfg.guiFontSize);
    out.insert_or_assign("guiOpacity", static_cast<double>(cfg.guiOpacity));
    out.insert_or_assign("appearance", MakeAppearanceTable(cfg));
    
    return out;
}

LinuxscreenConfig LinuxscreenConfigFromToml(const toml::table& tbl) {
    LinuxscreenConfig cfg;
    cfg.windowsPassthroughRoot = tbl;
    cfg.configVersion = GetOr(tbl, "configVersion", 1);
    cfg.defaultMode = GetStringOr(tbl, "defaultMode", "");
    
    if (auto arr = GetArray(tbl, "guiHotkey")) {
        for (const auto& elem : *arr) {
            if (auto val = elem.value<int64_t>()) {
                cfg.guiHotkey.push_back(static_cast<uint32_t>(*val));
            }
        }
    }

    if (auto arr = GetArray(tbl, "rebindToggleHotkey")) {
        for (const auto& elem : *arr) {
            if (auto val = elem.value<int64_t>()) {
                cfg.rebindToggleHotkey.push_back(static_cast<uint32_t>(*val));
            }
        }
    }
    
    if (auto arr = GetArray(tbl, "mirror")) {
        for (const auto& elem : *arr) {
            if (auto mirrorTbl = elem.as_table()) {
                cfg.mirrors.push_back(MirrorConfigFromToml(*mirrorTbl));
            }
        }
    }
    
    if (auto arr = GetArray(tbl, "mirrorGroup")) {
        for (const auto& elem : *arr) {
            if (auto groupTbl = elem.as_table()) {
                cfg.mirrorGroups.push_back(MirrorGroupConfigFromToml(*groupTbl));
            }
        }
    }
    
    if (auto arr = GetArray(tbl, "mode")) {
        for (const auto& elem : *arr) {
            if (auto modeTbl = elem.as_table()) {
                cfg.modes.push_back(ModeConfigFromToml(*modeTbl));
            }
        }
    }
    
    if (auto arr = GetArray(tbl, "hotkey")) {
        for (const auto& elem : *arr) {
            if (auto hotkeyTbl = elem.as_table()) {
                cfg.hotkeys.push_back(HotkeyConfigFromToml(*hotkeyTbl));
            }
        }
    }

    const toml::array* sensitivityHotkeysArr = GetArray(tbl, "sensitivityHotkey");
    if (!sensitivityHotkeysArr) {
        sensitivityHotkeysArr = GetArray(tbl, "sensitivityHotkeys");
    }
    if (sensitivityHotkeysArr) {
        for (const auto& elem : *sensitivityHotkeysArr) {
            if (auto sensitivityHotkeyTbl = elem.as_table()) {
                cfg.sensitivityHotkeys.push_back(SensitivityHotkeyConfigFromToml(*sensitivityHotkeyTbl));
            }
        }
    }

    cfg.mouseSensitivity = static_cast<float>(GetOr(tbl, "mouseSensitivity", 1.0));
    cfg.windowsMouseSpeed = GetOr(tbl, "windowsMouseSpeed", 0);
    cfg.keyRepeatStartDelay = std::clamp(GetOr(tbl, "keyRepeatStartDelay", 0), 0, 500);
    cfg.keyRepeatDelay = std::clamp(GetOr(tbl, "keyRepeatDelay", 0), 0, 500);
    cfg.keyRepeatAffectsMouseButtons = GetOr(tbl, "keyRepeatAffectsMouseButtons", false);

    if (auto keyRebindsTbl = GetTable(tbl, "keyRebinds")) {
        cfg.keyRebinds = KeyRebindsConfigFromToml(*keyRebindsTbl);
    }
    if (cfg.rebindToggleHotkey.empty()) {
        cfg.rebindToggleHotkey = cfg.keyRebinds.toggleHotkey;
    }
    cfg.keyRebinds.toggleHotkey = cfg.rebindToggleHotkey;
    
    if (auto eyezoomTbl = GetTable(tbl, "eyezoom")) {
        cfg.eyezoom = EyeZoomConfigFromToml(*eyezoomTbl);
    }

    cfg.guiScale = static_cast<float>(GetOr(tbl, "guiScale", 1.0));
    cfg.guiTheme = GetStringOr(tbl, "guiTheme", "Dark");
    cfg.guiWidth = GetOr(tbl, "guiWidth", 800);
    cfg.guiHeight = GetOr(tbl, "guiHeight", 600);
    cfg.guiFontPath = GetStringOr(tbl, "guiFontPath", "");
    cfg.guiFontSize = GetOr(tbl, "guiFontSize", 13);
    cfg.guiOpacity = static_cast<float>(GetOr(tbl, "guiOpacity", 1.0));
    if (auto appearanceTbl = GetTable(tbl, "appearance")) {
        LoadAppearanceTable(*appearanceTbl, cfg);
    }

    EraseKeys(cfg.windowsPassthroughRoot,
              { "configVersion",
                "defaultMode",
                "guiHotkey",
                "rebindToggleHotkey",
                "mirror",
                "mirrorGroup",
                "mode",
                "hotkey",
                "sensitivityHotkey",
                "sensitivityHotkeys",
                "mouseSensitivity",
                "windowsMouseSpeed",
                "keyRepeatStartDelay",
                "keyRepeatDelay",
                "keyRepeatAffectsMouseButtons",
                "keyRebinds",
                "eyezoom",
                "guiScale",
                "guiTheme",
                "guiWidth",
                "guiHeight",
                "guiFontPath",
                "guiFontSize",
                "guiOpacity",
                "appearance" });
    
    return cfg;
}

} // namespace platform::config
