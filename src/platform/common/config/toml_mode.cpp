void ModeBackgroundConfigToToml(const ModeBackgroundConfig& cfg, toml::table& out) {
    out.is_inline(false);

    out.insert("selectedMode", NormalizeBackgroundSelectedMode(cfg.selectedMode));
    out.insert("image", cfg.image);
    out.insert("color", ColorToTomlArray(cfg.color));

    toml::array stopsArr;
    for (const auto& stop : cfg.gradientStops) {
        toml::table stopTbl;
        stopTbl.insert("color", ColorToTomlArray(stop.color));
        stopTbl.insert("position", static_cast<double>(std::clamp(stop.position, 0.0f, 1.0f)));
        stopsArr.push_back(stopTbl);
    }
    out.insert("gradientStops", stopsArr);

    out.insert("gradientAngle", static_cast<double>(cfg.gradientAngle));
    out.insert("gradientAnimation", GradientAnimationTypeToString(cfg.gradientAnimation));
    out.insert("gradientAnimationSpeed", static_cast<double>(cfg.gradientAnimationSpeed));
    out.insert("gradientColorFade", cfg.gradientColorFade);
}

ModeBackgroundConfig ModeBackgroundConfigFromToml(const toml::table& tbl) {
    ModeBackgroundConfig cfg;

    cfg.selectedMode = NormalizeBackgroundSelectedMode(GetStringOr(tbl, "selectedMode", "color"));
    cfg.image = GetStringOr(tbl, "image", "");
    cfg.color = ColorFromTomlArray(GetArray(tbl, "color"), {0.0f, 0.0f, 0.0f, 1.0f});

    cfg.gradientStops.clear();
    if (auto arr = GetArray(tbl, "gradientStops")) {
        for (const auto& elem : *arr) {
            if (auto stopTbl = elem.as_table()) {
                GradientStop stop;
                stop.color = ColorFromTomlArray(GetArray(*stopTbl, "color"), {0.0f, 0.0f, 0.0f, 1.0f});
                stop.position = static_cast<float>(GetOr(*stopTbl, "position", 0.0));
                stop.position = std::clamp(stop.position, 0.0f, 1.0f);
                cfg.gradientStops.push_back(stop);
            }
        }
    }

    if (cfg.gradientStops.size() < 2 || cfg.gradientStops.size() > 8) {
        cfg.gradientStops.clear();
        cfg.gradientStops.push_back(GradientStop{Color{0.0f, 0.0f, 0.0f, 1.0f}, 0.0f});
        cfg.gradientStops.push_back(GradientStop{Color{1.0f, 1.0f, 1.0f, 1.0f}, 1.0f});
    }
    std::stable_sort(cfg.gradientStops.begin(),
                     cfg.gradientStops.end(),
                     [](const GradientStop& a, const GradientStop& b) { return a.position < b.position; });

    cfg.gradientAngle = static_cast<float>(GetOr(tbl, "gradientAngle", 0.0));
    cfg.gradientAnimation = StringToGradientAnimationType(GetStringOr(tbl, "gradientAnimation", "None"));
    cfg.gradientAnimationSpeed = static_cast<float>(GetOr(tbl, "gradientAnimationSpeed", 0.5));
    if (cfg.gradientAnimationSpeed <= 0.0f) {
        cfg.gradientAnimationSpeed = 0.5f;
    }
    cfg.gradientColorFade = GetOr(tbl, "gradientColorFade", false);

    return cfg;
}

static const char* ModeLayerTypeToString(ModeLayerType layerType) {
    switch (layerType) {
    case ModeLayerType::Group:
        return "group";
    case ModeLayerType::Mirror:
    default:
        return "mirror";
    }
}

static ModeLayerType StringToModeLayerType(const std::string& value) {
    if (value == "group" || value == "Group") {
        return ModeLayerType::Group;
    }
    return ModeLayerType::Mirror;
}

static std::vector<ModeLayerConfig> BuildModeLayersFromLegacyLists(const std::vector<std::string>& mirrorIds,
                                                                   const std::vector<std::string>& groupIds) {
    std::vector<ModeLayerConfig> layers;
    layers.reserve(mirrorIds.size() + groupIds.size());
    for (const auto& mirrorId : mirrorIds) {
        if (mirrorId.empty()) {
            continue;
        }
        layers.push_back(ModeLayerConfig{ModeLayerType::Mirror, mirrorId, true});
    }
    for (const auto& groupId : groupIds) {
        if (groupId.empty()) {
            continue;
        }
        layers.push_back(ModeLayerConfig{ModeLayerType::Group, groupId, true});
    }
    return layers;
}

static void BuildLegacyListsFromModeLayers(const std::vector<ModeLayerConfig>& layers,
                                           std::vector<std::string>& outMirrorIds,
                                           std::vector<std::string>& outGroupIds) {
    outMirrorIds.clear();
    outGroupIds.clear();
    outMirrorIds.reserve(layers.size());
    outGroupIds.reserve(layers.size());
    for (const auto& layer : layers) {
        if (layer.id.empty()) {
            continue;
        }
        if (layer.type == ModeLayerType::Mirror) {
            outMirrorIds.push_back(layer.id);
        } else {
            outGroupIds.push_back(layer.id);
        }
    }
}

void ModeConfigToToml(const ModeConfig& cfg, toml::table& out) {
    out = cfg.windowsPassthrough;
    out.insert_or_assign("id", cfg.name);
    out.erase("name");

    std::vector<ModeLayerConfig> modeLayers = cfg.layers;
    if (modeLayers.empty() && (!cfg.mirrorIds.empty() || !cfg.groupIds.empty())) {
        modeLayers = BuildModeLayersFromLegacyLists(cfg.mirrorIds, cfg.groupIds);
    }

    if (!modeLayers.empty()) {
        toml::array layersArr;
        for (const auto& layer : modeLayers) {
            if (layer.id.empty()) {
                continue;
            }
            toml::table layerTbl;
            layerTbl.insert("type", ModeLayerTypeToString(layer.type));
            layerTbl.insert("id", layer.id);
            layerTbl.insert("enabled", layer.enabled);
            layersArr.push_back(layerTbl);
        }
        out.insert("layers", layersArr);
    }

    std::vector<std::string> legacyMirrorIds;
    std::vector<std::string> legacyGroupIds;
    if (!modeLayers.empty()) {
        BuildLegacyListsFromModeLayers(modeLayers, legacyMirrorIds, legacyGroupIds);
    } else {
        legacyMirrorIds = cfg.mirrorIds;
        legacyGroupIds = cfg.groupIds;
    }

    toml::array mirrorIdsArr;
    for (const auto& id : legacyMirrorIds) {
        mirrorIdsArr.push_back(id);
    }
    out.insert_or_assign("mirrorIds", mirrorIdsArr);

    toml::array mirrorGroupIdsArr;
    for (const auto& id : legacyGroupIds) {
        mirrorGroupIdsArr.push_back(id);
    }
    out.insert_or_assign("mirrorGroupIds", mirrorGroupIdsArr);
    out.erase("groupIds");

    toml::array imageIdsArr;
    for (const auto& id : cfg.imageIds) {
        imageIdsArr.push_back(id);
    }
    out.insert_or_assign("imageIds", imageIdsArr);

    toml::array windowOverlayIdsArr;
    for (const auto& id : cfg.windowOverlayIds) {
        windowOverlayIdsArr.push_back(id);
    }
    out.insert_or_assign("windowOverlayIds", windowOverlayIdsArr);

    if (!cfg.widthExpr.empty()) {
        out.insert_or_assign("width", cfg.widthExpr);
    } else if (cfg.useRelativeSize) {
        out.insert_or_assign("width", static_cast<double>(cfg.relativeWidth));
    } else {
        out.insert_or_assign("width", cfg.width);
    }

    if (!cfg.heightExpr.empty()) {
        out.insert_or_assign("height", cfg.heightExpr);
    } else if (cfg.useRelativeSize) {
        out.insert_or_assign("height", static_cast<double>(cfg.relativeHeight));
    } else {
        out.insert_or_assign("height", cfg.height);
    }

    out.insert_or_assign("useRelativeSize", cfg.useRelativeSize);
    out.insert_or_assign("relativeWidth", static_cast<double>(cfg.relativeWidth));
    out.insert_or_assign("relativeHeight", static_cast<double>(cfg.relativeHeight));
    out.insert_or_assign("widthExpr", cfg.widthExpr);
    out.insert_or_assign("heightExpr", cfg.heightExpr);
    out.insert_or_assign("sensitivityOverrideEnabled", cfg.sensitivityOverrideEnabled);
    out.insert_or_assign("modeSensitivity", static_cast<double>(cfg.modeSensitivity));
    out.insert_or_assign("separateXYSensitivity", cfg.separateXYSensitivity);
    out.insert_or_assign("modeSensitivityX", static_cast<double>(cfg.modeSensitivityX));
    out.insert_or_assign("modeSensitivityY", static_cast<double>(cfg.modeSensitivityY));

    toml::table borderTbl;
    BorderConfigToToml(cfg.border, borderTbl);
    out.insert_or_assign("border", borderTbl);

    out.insert_or_assign("x", cfg.x);
    out.insert_or_assign("y", cfg.y);
    out.insert_or_assign("positionPreset", cfg.positionPreset);

    toml::table backgroundTbl;
    ModeBackgroundConfigToToml(cfg.background, backgroundTbl);
    out.insert_or_assign("background", backgroundTbl);
}

ModeConfig ModeConfigFromToml(const toml::table& tbl) {
    ModeConfig cfg;
    cfg.windowsPassthrough = tbl;
    cfg.name = GetStringOr(tbl, "id", GetStringOr(tbl, "name", ""));
    EraseKeys(cfg.windowsPassthrough,
              { "id",
                "name",
                "mirrorIds",
                "mirrorGroupIds",
                "groupIds",
                "imageIds",
                "windowOverlayIds",
                "layers",
                "width",
                "height",
                "useRelativeSize",
                "relativeWidth",
                "relativeHeight",
                "widthExpr",
                "heightExpr",
                "matchWindowSize",
                "sensitivityOverrideEnabled",
                "modeSensitivity",
                "separateXYSensitivity",
                "modeSensitivityX",
                "modeSensitivityY",
                "border",
                "x",
                "y",
                "positionPreset",
                "renderOffscreen",
                "background" });

    std::vector<std::string> legacyMirrorIds;
    std::vector<std::string> legacyGroupIds;
    if (auto arr = GetArray(tbl, "mirrorIds")) {
        for (const auto& elem : *arr) {
            if (auto val = elem.value<std::string>()) {
                legacyMirrorIds.push_back(*val);
            }
        }
    }

    const toml::array* groupIdsArr = GetArray(tbl, "mirrorGroupIds");
    if (!groupIdsArr) {
        groupIdsArr = GetArray(tbl, "groupIds");
    }
    if (groupIdsArr) {
        for (const auto& elem : *groupIdsArr) {
            if (auto val = elem.value<std::string>()) {
                legacyGroupIds.push_back(*val);
            }
        }
    }

    if (auto arr = GetArray(tbl, "imageIds")) {
        for (const auto& elem : *arr) {
            if (auto val = elem.value<std::string>()) {
                cfg.imageIds.push_back(*val);
            }
        }
    }

    if (auto arr = GetArray(tbl, "windowOverlayIds")) {
        for (const auto& elem : *arr) {
            if (auto val = elem.value<std::string>()) {
                cfg.windowOverlayIds.push_back(*val);
            }
        }
    }

    if (auto arr = GetArray(tbl, "layers")) {
        for (const auto& elem : *arr) {
            if (const auto* layerTbl = elem.as_table()) {
                ModeLayerConfig layer;
                layer.type = StringToModeLayerType(GetStringOr(*layerTbl, "type", "mirror"));
                layer.id = GetStringOr(*layerTbl, "id", "");
                layer.enabled = GetOr(*layerTbl, "enabled", true);
                if (!layer.id.empty()) {
                    cfg.layers.push_back(std::move(layer));
                }
            }
        }
    }

    if (cfg.layers.empty()) {
        cfg.layers = BuildModeLayersFromLegacyLists(legacyMirrorIds, legacyGroupIds);
    }
    BuildLegacyListsFromModeLayers(cfg.layers, cfg.mirrorIds, cfg.groupIds);

    bool widthWasRelative = false;
    bool heightWasRelative = false;

    if (auto widthNode = tbl.get("width")) {
        if (auto widthStr = widthNode->value<std::string>()) {
            cfg.widthExpr = *widthStr;
        } else if (widthNode->is_floating_point()) {
            const double widthValue = widthNode->as_floating_point()->get();
            if (widthValue >= 0.0 && widthValue <= 1.0) {
                cfg.relativeWidth = static_cast<float>(widthValue);
                widthWasRelative = true;
            } else {
                cfg.width = static_cast<int>(widthValue);
            }
        } else if (widthNode->is_integer()) {
            cfg.width = static_cast<int>(widthNode->as_integer()->get());
        }
    }

    if (auto heightNode = tbl.get("height")) {
        if (auto heightStr = heightNode->value<std::string>()) {
            cfg.heightExpr = *heightStr;
        } else if (heightNode->is_floating_point()) {
            const double heightValue = heightNode->as_floating_point()->get();
            if (heightValue >= 0.0 && heightValue <= 1.0) {
                cfg.relativeHeight = static_cast<float>(heightValue);
                heightWasRelative = true;
            } else {
                cfg.height = static_cast<int>(heightValue);
            }
        } else if (heightNode->is_integer()) {
            cfg.height = static_cast<int>(heightNode->as_integer()->get());
        }
    }

    cfg.useRelativeSize = GetOr(tbl, "useRelativeSize", widthWasRelative || heightWasRelative);
    cfg.relativeWidth = static_cast<float>(GetOr(tbl, "relativeWidth", static_cast<double>(cfg.relativeWidth)));
    cfg.relativeHeight = static_cast<float>(GetOr(tbl, "relativeHeight", static_cast<double>(cfg.relativeHeight)));
    cfg.widthExpr = GetStringOr(tbl, "widthExpr", cfg.widthExpr);
    cfg.heightExpr = GetStringOr(tbl, "heightExpr", cfg.heightExpr);
    cfg.enforceModeSize = true;
    cfg.matchWindowSize = GetOr(tbl, "matchWindowSize", false);
    cfg.sensitivityOverrideEnabled = GetOr(tbl, "sensitivityOverrideEnabled", false);
    cfg.modeSensitivity = static_cast<float>(GetOr(tbl, "modeSensitivity", 1.0));
    cfg.separateXYSensitivity = GetOr(tbl, "separateXYSensitivity", false);
    cfg.modeSensitivityX = static_cast<float>(GetOr(tbl, "modeSensitivityX", 1.0));
    cfg.modeSensitivityY = static_cast<float>(GetOr(tbl, "modeSensitivityY", 1.0));
    if (auto borderTbl = GetTable(tbl, "border")) {
        cfg.border = BorderConfigFromToml(*borderTbl);
    }

    cfg.relativeWidth = std::clamp(cfg.relativeWidth, 0.0f, 1.0f);
    cfg.relativeHeight = std::clamp(cfg.relativeHeight, 0.0f, 1.0f);

    cfg.x = GetOr(tbl, "x", 0);
    cfg.y = GetOr(tbl, "y", 0);
    cfg.positionPreset = GetStringOr(tbl, "positionPreset", "centerScreen");
    if (cfg.positionPreset == "custom") {
        cfg.positionPreset = "topLeftScreen";
    }

    if (cfg.matchWindowSize) {
        cfg.useRelativeSize = true;
        cfg.relativeWidth = 1.0f;
        cfg.relativeHeight = 1.0f;
        cfg.widthExpr.clear();
        cfg.heightExpr.clear();
        cfg.positionPreset = "topLeftScreen";
        cfg.x = 0;
        cfg.y = 0;
        cfg.matchWindowSize = false;
    }

    if (auto backgroundTbl = GetTable(tbl, "background")) {
        cfg.background = ModeBackgroundConfigFromToml(*backgroundTbl);
    }
    
    return cfg;
}
