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

void ParseKeyRebindVkHintField(const toml::table& tbl, const char* fieldName, platform::input::VkCode& outVkHint) {
    outVkHint = platform::input::VK_NONE;
    if (auto vkNode = tbl.get(fieldName)) {
        if (auto vkInt = vkNode->value<int64_t>()) {
            const std::uint64_t raw = static_cast<std::uint64_t>(*vkInt);
            if (raw <= static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max())) {
                outVkHint = static_cast<platform::input::VkCode>(raw);
            }
        }
    }
}

} // namespace

void KeyRebindToToml(const KeyRebind& cfg, toml::table& out) {
    if (input::IsValidBindingKey(cfg.fromInput)) {
        out.insert("fromInput", BindingKeyToToml(cfg.fromInput));
    }
    if (cfg.fromVkHint != input::VK_NONE) {
        out.insert("fromVkHint", static_cast<int64_t>(cfg.fromVkHint));
    }
    if (input::IsValidBindingKey(cfg.toInput)) {
        out.insert("toInput", BindingKeyToToml(cfg.toInput));
    }
    if (cfg.toVkHint != input::VK_NONE) {
        out.insert("toVkHint", static_cast<int64_t>(cfg.toVkHint));
    }
    out.insert("enabled", cfg.enabled);
    out.insert("consumeSourceInput", cfg.consumeSourceInput);
    out.insert("suppressWithF3", cfg.suppressWithF3);
    out.insert("name", cfg.name);
    out.insert("useCustomOutput", cfg.useCustomOutput);
    if (input::IsValidBindingKey(cfg.customOutputKey)) {
        out.insert("customOutputKey", BindingKeyToToml(cfg.customOutputKey));
    }
    if (cfg.customOutputVkHint != input::VK_NONE) {
        out.insert("customOutputVkHint", static_cast<int64_t>(cfg.customOutputVkHint));
    }
    out.insert("customOutputUnicode", static_cast<int64_t>(cfg.customOutputUnicode));
    out.insert("customOutputShiftUnicode", static_cast<int64_t>(cfg.customOutputShiftUnicode));
    out.insert("keyRepeatDisabled", cfg.keyRepeatDisabled);
    out.insert("keyRepeatStartDelay", cfg.keyRepeatStartDelay);
    out.insert("keyRepeatDelay", cfg.keyRepeatDelay);
}

KeyRebind KeyRebindFromToml(const toml::table& tbl) {
    KeyRebind cfg;
    if (const toml::node* fromInputNode = tbl.get("fromInput")) {
        cfg.fromInput = BindingKeyFromTomlNode(*fromInputNode);
    }
    ParseKeyRebindVkHintField(tbl, "fromVkHint", cfg.fromVkHint);
    if (const toml::node* toInputNode = tbl.get("toInput")) {
        cfg.toInput = BindingKeyFromTomlNode(*toInputNode);
    }
    ParseKeyRebindVkHintField(tbl, "toVkHint", cfg.toVkHint);
    cfg.enabled = GetOr(tbl, "enabled", true);
    cfg.consumeSourceInput = GetOr(tbl, "consumeSourceInput", false);
    cfg.suppressWithF3 = GetOr(tbl, "suppressWithF3", false);
    cfg.name = GetStringOr(tbl, "name", "");
    cfg.useCustomOutput = GetOr(tbl, "useCustomOutput", false);
    if (const toml::node* customOutputNode = tbl.get("customOutputKey")) {
        cfg.customOutputKey = BindingKeyFromTomlNode(*customOutputNode);
    }
    ParseKeyRebindVkHintField(tbl, "customOutputVkHint", cfg.customOutputVkHint);
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

    if (!cfg.toggleHotkey.empty()) {
        out.insert("toggleHotkey", BindingKeysToToml(cfg.toggleHotkey));
    }

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

    cfg.toggleHotkey = BindingKeysFromToml(tbl, "toggleHotkey");

    if (auto rebindsArr = GetArray(tbl, "rebinds")) {
        for (const auto& elem : *rebindsArr) {
            if (auto rebindTbl = elem.as_table()) {
                cfg.rebinds.push_back(KeyRebindFromToml(*rebindTbl));
            }
        }
    }

    return cfg;
}
