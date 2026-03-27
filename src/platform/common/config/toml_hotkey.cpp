void HotkeyConditionsToToml(const HotkeyConditions& cfg, toml::table& out) {
    toml::array gameStateArr;
    for (const auto& state : cfg.gameState) {
        gameStateArr.push_back(state);
    }
    out.insert("gameState", gameStateArr);

    if (!cfg.exclusions.empty()) {
        out.insert("exclusions", BindingKeysToToml(cfg.exclusions));
    }
}

HotkeyConditions HotkeyConditionsFromToml(const toml::table& tbl) {
    HotkeyConditions cfg;

    if (auto gameStateArr = GetArray(tbl, "gameState")) {
        for (const auto& elem : *gameStateArr) {
            if (auto val = elem.value<std::string>()) {
                cfg.gameState.push_back(*val);
            }
        }
    }

    cfg.exclusions = BindingKeysFromToml(tbl, "exclusions");

    return cfg;
}

void AltSecondaryModeToToml(const AltSecondaryModeConfig& cfg, toml::table& out) {
    if (!cfg.keys.empty()) {
        out.insert("keys", BindingKeysToToml(cfg.keys));
    }
    out.insert("mode", cfg.mode);
}

AltSecondaryModeConfig AltSecondaryModeFromToml(const toml::table& tbl) {
    AltSecondaryModeConfig cfg;

    cfg.keys = BindingKeysFromToml(tbl, "keys");

    cfg.mode = GetStringOr(tbl, "mode", "");
    return cfg;
}

void HotkeyConfigToToml(const HotkeyConfig& cfg, toml::table& out) {
    if (!cfg.keys.empty()) {
        out.insert("keys", BindingKeysToToml(cfg.keys));
    }
    out.insert("mainMode", cfg.mainMode);
    out.insert("secondaryMode", cfg.secondaryMode);
    out.insert("returnMode", cfg.returnMode);

    toml::array altSecondaryArr;
    for (const auto& alt : cfg.altSecondaryModes) {
        toml::table altTbl;
        AltSecondaryModeToToml(alt, altTbl);
        altSecondaryArr.push_back(altTbl);
    }
    out.insert("altSecondaryModes", altSecondaryArr);

    out.insert("debounce", cfg.debounce);
    out.insert("triggerOnRelease", cfg.triggerOnRelease);
    out.insert("triggerOnHold", cfg.triggerOnHold);
    out.insert("blockKeyFromGame", cfg.blockKeyFromGame);
    out.insert("returnToDefaultOnRepeat", cfg.returnToDefaultOnRepeat);
    out.insert("allowExitToFullscreenRegardlessOfGameState", cfg.allowExitToFullscreenRegardlessOfGameState);

    toml::table conditionsTbl;
    HotkeyConditionsToToml(cfg.conditions, conditionsTbl);
    out.insert("conditions", conditionsTbl);
}

HotkeyConfig HotkeyConfigFromToml(const toml::table& tbl) {
    HotkeyConfig cfg;
    
    cfg.keys = BindingKeysFromToml(tbl, "keys");
    
    cfg.mainMode = GetStringOr(tbl, "mainMode", "");
    cfg.secondaryMode = GetStringOr(tbl, "secondaryMode", "");
    cfg.returnMode = GetStringOr(tbl, "returnMode", "");

    if (auto altSecondaryArr = GetArray(tbl, "altSecondaryModes")) {
        for (const auto& elem : *altSecondaryArr) {
            if (auto altTbl = elem.as_table()) {
                cfg.altSecondaryModes.push_back(AltSecondaryModeFromToml(*altTbl));
            }
        }
    }

    cfg.debounce = GetOr(tbl, "debounce", 100);
    cfg.triggerOnRelease = GetOr(tbl, "triggerOnRelease", false);
    cfg.triggerOnHold = GetOr(tbl, "triggerOnHold", false);
    cfg.blockKeyFromGame = GetOr(tbl, "blockKeyFromGame", false);
    cfg.returnToDefaultOnRepeat = GetOr(tbl, "returnToDefaultOnRepeat", true);
    cfg.allowExitToFullscreenRegardlessOfGameState = GetOr(tbl, "allowExitToFullscreenRegardlessOfGameState", false);

    if (auto conditionsTbl = GetTable(tbl, "conditions")) {
        cfg.conditions = HotkeyConditionsFromToml(*conditionsTbl);
    }
    
    return cfg;
}

void SensitivityHotkeyConfigToToml(const SensitivityHotkeyConfig& cfg, toml::table& out) {
    if (!cfg.keys.empty()) {
        out.insert("keys", BindingKeysToToml(cfg.keys));
    }
    out.insert("sensitivity", static_cast<double>(cfg.sensitivity));
    out.insert("separateXY", cfg.separateXY);
    out.insert("sensitivityX", static_cast<double>(cfg.sensitivityX));
    out.insert("sensitivityY", static_cast<double>(cfg.sensitivityY));
    out.insert("toggle", cfg.toggle);
    out.insert("triggerOnHold", cfg.triggerOnHold);
    out.insert("debounce", cfg.debounce);

    toml::table conditionsTbl;
    HotkeyConditionsToToml(cfg.conditions, conditionsTbl);
    out.insert("conditions", conditionsTbl);
}

SensitivityHotkeyConfig SensitivityHotkeyConfigFromToml(const toml::table& tbl) {
    SensitivityHotkeyConfig cfg;

    cfg.keys = BindingKeysFromToml(tbl, "keys");

    cfg.sensitivity = static_cast<float>(GetOr(tbl, "sensitivity", 1.0));
    cfg.separateXY = GetOr(tbl, "separateXY", false);
    cfg.sensitivityX = static_cast<float>(GetOr(tbl, "sensitivityX", 1.0));
    cfg.sensitivityY = static_cast<float>(GetOr(tbl, "sensitivityY", 1.0));
    cfg.toggle = GetOr(tbl, "toggle", false);
    cfg.triggerOnHold = GetOr(tbl, "triggerOnHold", false);
    cfg.debounce = GetOr(tbl, "debounce", 100);

    if (auto conditionsTbl = GetTable(tbl, "conditions")) {
        cfg.conditions = HotkeyConditionsFromToml(*conditionsTbl);
    }

    return cfg;
}
