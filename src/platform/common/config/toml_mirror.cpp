void MirrorCaptureConfigToToml(const MirrorCaptureConfig& cfg, toml::table& out) {
    out.insert("x", cfg.x);
    out.insert("y", cfg.y);
    out.insert("relativeTo", cfg.relativeTo);
    out.insert("enabled", cfg.enabled);
}

MirrorCaptureConfig MirrorCaptureConfigFromToml(const toml::table& tbl) {
    MirrorCaptureConfig cfg;
    cfg.x = GetOr(tbl, "x", 0);
    cfg.y = GetOr(tbl, "y", 0);
    cfg.relativeTo = GetStringOr(tbl, "relativeTo", "topLeftScreen");
    if (cfg.relativeTo == "custom") {
        cfg.relativeTo = "topLeftScreen";
    }
    cfg.enabled = GetOr(tbl, "enabled", true);
    return cfg;
}

void MirrorRenderConfigToToml(const MirrorRenderConfig& cfg, toml::table& out) {
    if (cfg.useRelativePosition) {
        out.insert("x", static_cast<double>(cfg.relativeX));
        out.insert("y", static_cast<double>(cfg.relativeY));
    } else {
        out.insert("x", cfg.x);
        out.insert("y", cfg.y);
    }
    out.insert("useRelativePosition", cfg.useRelativePosition);
    out.insert("relativeX", static_cast<double>(cfg.relativeX));
    out.insert("relativeY", static_cast<double>(cfg.relativeY));
    out.insert("useRelativeSize", cfg.useRelativeSize);
    out.insert("relativeWidth", static_cast<double>(cfg.relativeWidth));
    out.insert("relativeHeight", static_cast<double>(cfg.relativeHeight));
    out.insert("preserveAspectRatio", cfg.preserveAspectRatio);
    out.insert("aspectFitMode", NormalizeAspectFitMode(cfg.aspectFitMode));
    out.insert("scale", static_cast<double>(cfg.scale));
    out.insert("separateScale", cfg.separateScale);
    out.insert("scaleX", static_cast<double>(cfg.scaleX));
    out.insert("scaleY", static_cast<double>(cfg.scaleY));
    out.insert("relativeTo", (cfg.relativeTo == "custom") ? "topLeftScreen" : cfg.relativeTo);
}

MirrorRenderConfig MirrorRenderConfigFromToml(const toml::table& tbl) {
    MirrorRenderConfig cfg;
    cfg.useRelativePosition = GetOr(tbl, "useRelativePosition", false);
    cfg.relativeX = static_cast<float>(GetOr(tbl, "relativeX", 0.5));
    cfg.relativeY = static_cast<float>(GetOr(tbl, "relativeY", 0.5));
    cfg.useRelativeSize = GetOr(tbl, "useRelativeSize", false);
    cfg.relativeWidth = static_cast<float>(GetOr(tbl, "relativeWidth", 0.5));
    cfg.relativeHeight = static_cast<float>(GetOr(tbl, "relativeHeight", 0.5));
    cfg.preserveAspectRatio = GetOr(tbl, "preserveAspectRatio", true);
    cfg.aspectFitMode = NormalizeAspectFitMode(GetStringOr(tbl, "aspectFitMode", "contain"));

    bool xIsPercentage = false;
    bool yIsPercentage = false;

    if (auto xNode = tbl.get("x")) {
        if (xNode->is_floating_point()) {
            const double xVal = xNode->as_floating_point()->get();
            if (cfg.useRelativePosition || (xVal >= 0.0 && xVal <= 1.0)) {
                cfg.relativeX = static_cast<float>(xVal);
                xIsPercentage = true;
            } else {
                cfg.x = static_cast<int>(xVal);
            }
        } else if (xNode->is_integer()) {
            cfg.x = static_cast<int>(xNode->as_integer()->get());
        }
    }

    if (auto yNode = tbl.get("y")) {
        if (yNode->is_floating_point()) {
            const double yVal = yNode->as_floating_point()->get();
            if (cfg.useRelativePosition || (yVal >= 0.0 && yVal <= 1.0)) {
                cfg.relativeY = static_cast<float>(yVal);
                yIsPercentage = true;
            } else {
                cfg.y = static_cast<int>(yVal);
            }
        } else if (yNode->is_integer()) {
            cfg.y = static_cast<int>(yNode->as_integer()->get());
        }
    }

    if (!tbl.contains("useRelativePosition") && xIsPercentage && yIsPercentage) {
        cfg.useRelativePosition = true;
    }

    cfg.relativeX = std::clamp(cfg.relativeX, -1.0f, 2.0f);
    cfg.relativeY = std::clamp(cfg.relativeY, -1.0f, 2.0f);
    cfg.relativeWidth = std::clamp(cfg.relativeWidth, 0.01f, 20.0f);
    cfg.relativeHeight = std::clamp(cfg.relativeHeight, 0.01f, 20.0f);
    cfg.scale = static_cast<float>(GetOr(tbl, "scale", 1.0));
    cfg.separateScale = GetOr(tbl, "separateScale", false);
    cfg.scaleX = static_cast<float>(GetOr(tbl, "scaleX", static_cast<double>(cfg.scale)));
    cfg.scaleY = static_cast<float>(GetOr(tbl, "scaleY", static_cast<double>(cfg.scale)));
    cfg.relativeTo = GetStringOr(tbl, "relativeTo", "bottomLeftScreen");
    if (cfg.relativeTo == "custom") {
        cfg.relativeTo = "topLeftScreen";
    }
    return cfg;
}

void MirrorColorsToToml(const MirrorColors& cfg, toml::table& out) {
    toml::array targetColorsArr;
    for (const auto& color : cfg.targetColors) {
        targetColorsArr.push_back(ColorToTomlArray(color));
    }
    out.insert("targetColors", targetColorsArr);
    out.insert("output", ColorToTomlArray(cfg.output));
    out.insert("border", ColorToTomlArray(cfg.border));
}

MirrorColors MirrorColorsFromToml(const toml::table& tbl) {
    MirrorColors cfg;
    
    if (auto arr = GetArray(tbl, "targetColors")) {
        for (const auto& elem : *arr) {
            if (auto colorArr = elem.as_array()) {
                cfg.targetColors.push_back(ColorFromTomlArray(colorArr));
            }
        }
    }
    
    cfg.output = ColorFromTomlArray(GetArray(tbl, "output"), {0.0f, 0.0f, 0.0f, 1.0f});
    cfg.border = ColorFromTomlArray(GetArray(tbl, "border"), {1.0f, 1.0f, 1.0f, 1.0f});
    
    return cfg;
}

void MirrorConfigToToml(const MirrorConfig& cfg, toml::table& out) {
    out = cfg.windowsPassthrough;
    out.insert_or_assign("name", cfg.name);
    out.insert_or_assign("captureWidth", cfg.captureWidth);
    out.insert_or_assign("captureHeight", cfg.captureHeight);
    out.insert_or_assign("colorSensitivity", static_cast<double>(cfg.colorSensitivity));
    out.insert_or_assign("fps", cfg.fps);
    out.insert_or_assign("opacity", static_cast<double>(cfg.opacity));
    out.insert_or_assign("rawOutput", cfg.rawOutput);
    out.insert_or_assign("colorPassthrough", cfg.colorPassthrough);
    out.insert_or_assign("onlyOnMyScreen", cfg.onlyOnMyScreen);
    if (!cfg.gammaMode.empty()) {
        out.insert_or_assign("gammaMode", cfg.gammaMode);
    }
    
    toml::table borderTbl;
    MirrorBorderConfigToToml(cfg.border, borderTbl);
    out.insert_or_assign("border", borderTbl);
    
    toml::array inputArr;
    for (const auto& input : cfg.input) {
        toml::table inputTbl;
        MirrorCaptureConfigToToml(input, inputTbl);
        inputArr.push_back(inputTbl);
    }
    out.insert_or_assign("input", inputArr);
    
    toml::table outputTbl;
    MirrorRenderConfigToToml(cfg.output, outputTbl);
    out.insert_or_assign("output", outputTbl);
    
    toml::table colorsTbl;
    MirrorColorsToToml(cfg.colors, colorsTbl);
    out.insert_or_assign("colors", colorsTbl);
}

MirrorConfig MirrorConfigFromToml(const toml::table& tbl) {
    MirrorConfig cfg;
    cfg.windowsPassthrough = tbl;
    EraseKeys(cfg.windowsPassthrough,
              { "name",
                "captureWidth",
                "captureHeight",
                "colorSensitivity",
                "fps",
                "opacity",
                "rawOutput",
                "colorPassthrough",
                "onlyOnMyScreen",
                "gammaMode",
                "border",
                "dynamicBorderThickness",
                "borderThickness",
                "input",
                "output",
                "colors" });
    cfg.name = GetStringOr(tbl, "name", "");
    cfg.captureWidth = GetOr(tbl, "captureWidth", 50);
    cfg.captureHeight = GetOr(tbl, "captureHeight", 50);
    cfg.colorSensitivity = static_cast<float>(GetOr(tbl, "colorSensitivity", 0.001));
    cfg.fps = GetOr(tbl, "fps", 30);
    cfg.opacity = static_cast<float>(GetOr(tbl, "opacity", 1.0));
    cfg.rawOutput = GetOr(tbl, "rawOutput", false);
    cfg.colorPassthrough = GetOr(tbl, "colorPassthrough", false);
    cfg.onlyOnMyScreen = GetOr(tbl, "onlyOnMyScreen", false);
    cfg.gammaMode = GetStringOr(tbl, "gammaMode", "");
    
    // Parse border table (with backward compatibility for old dynamicBorderThickness)
    if (auto borderTbl = GetTable(tbl, "border")) {
        cfg.border = MirrorBorderConfigFromToml(*borderTbl);
    } else {
        // Backward compatibility: read old flat fields
        cfg.border.dynamicThickness = GetOr(tbl, "dynamicBorderThickness", GetOr(tbl, "borderThickness", 1));
    }
    
    if (auto inputArr = GetArray(tbl, "input")) {
        for (const auto& elem : *inputArr) {
            if (auto inputTbl = elem.as_table()) {
                cfg.input.push_back(MirrorCaptureConfigFromToml(*inputTbl));
            }
        }
    }
    
    if (auto outputTbl = GetTable(tbl, "output")) {
        cfg.output = MirrorRenderConfigFromToml(*outputTbl);
    }
    
    if (auto colorsTbl = GetTable(tbl, "colors")) {
        cfg.colors = MirrorColorsFromToml(*colorsTbl);
    }
    
    return cfg;
}

void MirrorGroupItemToToml(const MirrorGroupItem& cfg, toml::table& out) {
    out.insert("mirrorId", cfg.mirrorId);
    out.insert("enabled", cfg.enabled);
    out.insert("widthPercent", static_cast<double>(cfg.widthPercent));
    out.insert("heightPercent", static_cast<double>(cfg.heightPercent));
    out.insert("offsetX", cfg.offsetX);
    out.insert("offsetY", cfg.offsetY);
}

MirrorGroupItem MirrorGroupItemFromToml(const toml::table& tbl) {
    MirrorGroupItem cfg;
    cfg.mirrorId = GetStringOr(tbl, "mirrorId", "");
    cfg.enabled = GetOr(tbl, "enabled", true);
    cfg.widthPercent = static_cast<float>(GetOr(tbl, "widthPercent", 1.0));
    cfg.heightPercent = static_cast<float>(GetOr(tbl, "heightPercent", 1.0));
    cfg.offsetX = GetOr(tbl, "offsetX", 0);
    cfg.offsetY = GetOr(tbl, "offsetY", 0);
    return cfg;
}

void MirrorGroupConfigToToml(const MirrorGroupConfig& cfg, toml::table& out) {
    out.insert("name", cfg.name);
    
    toml::table outputTbl;
    MirrorRenderConfigToToml(cfg.output, outputTbl);
    out.insert("output", outputTbl);
    
    toml::array mirrorsArr;
    for (const auto& item : cfg.mirrors) {
        toml::table itemTbl;
        MirrorGroupItemToToml(item, itemTbl);
        mirrorsArr.push_back(itemTbl);
    }
    out.insert("mirrors", mirrorsArr);
}

MirrorGroupConfig MirrorGroupConfigFromToml(const toml::table& tbl) {
    MirrorGroupConfig cfg;
    cfg.name = GetStringOr(tbl, "name", "");
    
    if (auto outputTbl = GetTable(tbl, "output")) {
        cfg.output = MirrorRenderConfigFromToml(*outputTbl);
    }
    
    if (auto mirrorsArr = GetArray(tbl, "mirrors")) {
        for (const auto& elem : *mirrorsArr) {
            if (auto itemTbl = elem.as_table()) {
                cfg.mirrors.push_back(MirrorGroupItemFromToml(*itemTbl));
            }
        }
    }

    if (cfg.mirrors.empty()) {
        if (auto mirrorIdsArr = GetArray(tbl, "mirrorIds")) {
            for (const auto& elem : *mirrorIdsArr) {
                if (auto mirrorId = elem.value<std::string>()) {
                    MirrorGroupItem item;
                    item.mirrorId = *mirrorId;
                    cfg.mirrors.push_back(item);
                }
            }
        }
    }
    
    return cfg;
}
