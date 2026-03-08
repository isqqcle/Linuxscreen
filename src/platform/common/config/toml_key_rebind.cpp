namespace {

void ParseKeyRebindUnicodeField(const toml::table& tbl, const char* fieldName, std::uint32_t& outCodepoint) {
    outCodepoint = 0;
    if (auto unicodeNode = tbl.get(fieldName)) {
        if (auto unicodeInt = unicodeNode->value<int64_t>()) {
            const std::uint64_t raw = static_cast<std::uint64_t>(*unicodeInt);
            if (raw <= 0x10FFFFull) {
                const std::uint32_t cp = static_cast<std::uint32_t>(raw);
                if (IsValidUnicodeScalar(cp)) {
                    outCodepoint = cp;
                }
            }
        } else if (auto unicodeStr = unicodeNode->value<std::string>()) {
            std::uint32_t cp = 0;
            if (TryParseUnicodeCodepointString(*unicodeStr, cp)) {
                outCodepoint = cp;
            }
        }
    }
}

} // namespace

void KeyRebindToToml(const KeyRebind& cfg, toml::table& out) {
    out.insert("fromKey", static_cast<int64_t>(cfg.fromKey));
    out.insert("toKey", static_cast<int64_t>(cfg.toKey));
    out.insert("enabled", cfg.enabled);
    out.insert("consumeSourceInput", cfg.consumeSourceInput);
    out.insert("name", cfg.name);
    out.insert("useCustomOutput", cfg.useCustomOutput);
    out.insert("customOutputVK", static_cast<int64_t>(cfg.customOutputVK));
    out.insert("customOutputUnicode", static_cast<int64_t>(cfg.customOutputUnicode));
    out.insert("customOutputShiftUnicode", static_cast<int64_t>(cfg.customOutputShiftUnicode));
    out.insert("customOutputScanCode", static_cast<int64_t>(cfg.customOutputScanCode));
    out.insert("keyRepeatDisabled", cfg.keyRepeatDisabled);
    out.insert("keyRepeatStartDelay", cfg.keyRepeatStartDelay);
    out.insert("keyRepeatDelay", cfg.keyRepeatDelay);
}

KeyRebind KeyRebindFromToml(const toml::table& tbl) {
    KeyRebind cfg;
    cfg.fromKey = static_cast<uint32_t>(GetOr<int64_t>(tbl, "fromKey", 0));
    cfg.toKey = static_cast<uint32_t>(GetOr<int64_t>(tbl, "toKey", 0));
    cfg.enabled = GetOr(tbl, "enabled", true);
    cfg.consumeSourceInput = GetOr(tbl, "consumeSourceInput", false);
    cfg.name = GetStringOr(tbl, "name", "");
    cfg.useCustomOutput = GetOr(tbl, "useCustomOutput", false);
    cfg.customOutputVK = static_cast<uint32_t>(GetOr<int64_t>(tbl, "customOutputVK", 0));
    cfg.customOutputScanCode = static_cast<uint32_t>(GetOr<int64_t>(tbl, "customOutputScanCode", 0));
    cfg.keyRepeatDisabled = GetOr(tbl, "keyRepeatDisabled", false);
    cfg.keyRepeatStartDelay = std::clamp(GetOr(tbl, "keyRepeatStartDelay", 0), 0, 500);
    cfg.keyRepeatDelay = std::clamp(GetOr(tbl, "keyRepeatDelay", 0), 0, 500);
    ParseKeyRebindUnicodeField(tbl, "customOutputUnicode", cfg.customOutputUnicode);
    ParseKeyRebindUnicodeField(tbl, "customOutputShiftUnicode", cfg.customOutputShiftUnicode);

    return cfg;
}

void KeyRebindsConfigToToml(const KeyRebindsConfig& cfg, toml::table& out) {
    out.insert("enabled", cfg.enabled);
    out.insert("resolveRebindTargetsForHotkeys", cfg.resolveRebindTargetsForHotkeys);

    toml::array toggleHotkeyArr;
    for (const auto key : cfg.toggleHotkey) {
        toggleHotkeyArr.push_back(static_cast<int64_t>(key));
    }
    out.insert("toggleHotkey", toggleHotkeyArr);

    toml::array rebindsArr;
    for (const auto& rebind : cfg.rebinds) {
        toml::table rebindTbl;
        KeyRebindToToml(rebind, rebindTbl);
        rebindsArr.push_back(rebindTbl);
    }

    out.insert("rebinds", rebindsArr);
}

KeyRebindsConfig KeyRebindsConfigFromToml(const toml::table& tbl) {
    KeyRebindsConfig cfg;
    cfg.enabled = GetOr(tbl, "enabled", false);
    cfg.resolveRebindTargetsForHotkeys = GetOr(tbl, "resolveRebindTargetsForHotkeys", true);

    if (auto toggleHotkeyArr = GetArray(tbl, "toggleHotkey")) {
        for (const auto& elem : *toggleHotkeyArr) {
            if (auto val = elem.value<int64_t>()) {
                cfg.toggleHotkey.push_back(static_cast<uint32_t>(*val));
            }
        }
    }

    if (auto rebindsArr = GetArray(tbl, "rebinds")) {
        for (const auto& elem : *rebindsArr) {
            if (auto rebindTbl = elem.as_table()) {
                cfg.rebinds.push_back(KeyRebindFromToml(*rebindTbl));
            }
        }
    }

    return cfg;
}
