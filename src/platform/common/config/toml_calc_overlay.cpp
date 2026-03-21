namespace {

const char* CalcOverlayPositionToString(CalcOverlayPosition value) {
    switch (value) {
    case CalcOverlayPosition::TopRight:
        return "top right";
    case CalcOverlayPosition::BottomLeft:
        return "bottom left";
    case CalcOverlayPosition::BottomRight:
        return "bottom right";
    case CalcOverlayPosition::TopLeft:
    default:
        return "top left";
    }
}

CalcOverlayPosition CalcOverlayPositionFromString(const std::string& value) {
    if (value == "top right") return CalcOverlayPosition::TopRight;
    if (value == "bottom left") return CalcOverlayPosition::BottomLeft;
    if (value == "bottom right") return CalcOverlayPosition::BottomRight;
    return CalcOverlayPosition::TopLeft;
}

const char* CalcOverlayHeaderRowToString(CalcOverlayHeaderRow value) {
    switch (value) {
    case CalcOverlayHeaderRow::Nothing:
        return "nothing";
    case CalcOverlayHeaderRow::Text:
        return "show text";
    case CalcOverlayHeaderRow::Icon:
    default:
        return "show icon";
    }
}

CalcOverlayHeaderRow CalcOverlayHeaderRowFromString(const std::string& value) {
    if (value == "nothing") return CalcOverlayHeaderRow::Nothing;
    if (value == "show text") return CalcOverlayHeaderRow::Text;
    return CalcOverlayHeaderRow::Icon;
}

const char* CalcOverlayEyeColumnTypeToString(CalcOverlayEyeColumnType value) {
    switch (value) {
    case CalcOverlayEyeColumnType::Certainty:
        return "certainty";
    case CalcOverlayEyeColumnType::Distance:
        return "distance";
    case CalcOverlayEyeColumnType::NetherCoords:
        return "nether coords";
    case CalcOverlayEyeColumnType::Angle:
        return "angle";
    case CalcOverlayEyeColumnType::OverworldCoords:
    default:
        return "overworld coords";
    }
}

CalcOverlayEyeColumnType CalcOverlayEyeColumnTypeFromString(const std::string& value) {
    if (value == "certainty") return CalcOverlayEyeColumnType::Certainty;
    if (value == "distance") return CalcOverlayEyeColumnType::Distance;
    if (value == "nether coords") return CalcOverlayEyeColumnType::NetherCoords;
    if (value == "angle") return CalcOverlayEyeColumnType::Angle;
    return CalcOverlayEyeColumnType::OverworldCoords;
}

const char* CalcOverlayOverworldCoordsModeToString(CalcOverlayOverworldCoordsMode value) {
    switch (value) {
    case CalcOverlayOverworldCoordsMode::EightEight:
        return "(8, 8)";
    case CalcOverlayOverworldCoordsMode::FourFour:
        return "(4, 4)";
    case CalcOverlayOverworldCoordsMode::Chunk:
    default:
        return "chunk";
    }
}

CalcOverlayOverworldCoordsMode CalcOverlayOverworldCoordsModeFromString(const std::string& value) {
    if (value == "(8, 8)") return CalcOverlayOverworldCoordsMode::EightEight;
    if (value == "(4, 4)") return CalcOverlayOverworldCoordsMode::FourFour;
    return CalcOverlayOverworldCoordsMode::Chunk;
}

const char* CalcOverlayClearOverlayTimeUnitToString(CalcOverlayClearOverlayTimeUnit value) {
    switch (value) {
    case CalcOverlayClearOverlayTimeUnit::Seconds:
        return "seconds";
    case CalcOverlayClearOverlayTimeUnit::Minutes:
        return "minutes";
    case CalcOverlayClearOverlayTimeUnit::Never:
    default:
        return "never";
    }
}

CalcOverlayClearOverlayTimeUnit CalcOverlayClearOverlayTimeUnitFromString(const std::string& value) {
    if (value == "seconds") return CalcOverlayClearOverlayTimeUnit::Seconds;
    if (value == "minutes") return CalcOverlayClearOverlayTimeUnit::Minutes;
    return CalcOverlayClearOverlayTimeUnit::Never;
}

const char* CalcOverlayAngleDisplayModeToString(CalcOverlayAngleDisplayMode value) {
    switch (value) {
    case CalcOverlayAngleDisplayMode::OnlyAngle:
        return "angle";
    case CalcOverlayAngleDisplayMode::OnlyAngleChange:
        return "angle change";
    case CalcOverlayAngleDisplayMode::All:
    default:
        return "all";
    }
}

CalcOverlayAngleDisplayMode CalcOverlayAngleDisplayModeFromString(const std::string& value) {
    if (value == "angle") return CalcOverlayAngleDisplayMode::OnlyAngle;
    if (value == "angle change") return CalcOverlayAngleDisplayMode::OnlyAngleChange;
    return CalcOverlayAngleDisplayMode::All;
}

const char* CalcOverlayAaColumnTypeToString(CalcOverlayAaColumnType value) {
    switch (value) {
    case CalcOverlayAaColumnType::Location:
        return "location";
    case CalcOverlayAaColumnType::NetherCoords:
        return "nether coords";
    case CalcOverlayAaColumnType::Angle:
        return "angle";
    case CalcOverlayAaColumnType::Icons:
    default:
        return "icons";
    }
}

CalcOverlayAaColumnType CalcOverlayAaColumnTypeFromString(const std::string& value) {
    if (value == "location") return CalcOverlayAaColumnType::Location;
    if (value == "nether coords") return CalcOverlayAaColumnType::NetherCoords;
    if (value == "angle") return CalcOverlayAaColumnType::Angle;
    return CalcOverlayAaColumnType::Icons;
}

const char* CalcOverlayAaRowTypeToString(CalcOverlayAaRowType value) {
    switch (value) {
    case CalcOverlayAaRowType::Spawn:
        return "SPAWN";
    case CalcOverlayAaRowType::Outpost:
        return "OUTPOST";
    case CalcOverlayAaRowType::Monument:
        return "MONUMENT";
    case CalcOverlayAaRowType::Stronghold:
    default:
        return "STRONGHOLD";
    }
}

CalcOverlayAaRowType CalcOverlayAaRowTypeFromString(const std::string& value) {
    if (value == "SPAWN") return CalcOverlayAaRowType::Spawn;
    if (value == "OUTPOST") return CalcOverlayAaRowType::Outpost;
    if (value == "MONUMENT") return CalcOverlayAaRowType::Monument;
    return CalcOverlayAaRowType::Stronghold;
}

void CalcOverlayEyeColumnConfigToToml(const CalcOverlayEyeColumnConfig& cfg, toml::table& out) {
    out.insert_or_assign("name", CalcOverlayEyeColumnTypeToString(cfg.type));
    out.insert_or_assign("headerRow", CalcOverlayHeaderRowToString(cfg.headerRow));
    out.insert_or_assign("visible", cfg.visible);
}

CalcOverlayEyeColumnConfig CalcOverlayEyeColumnConfigFromToml(const toml::table& tbl) {
    CalcOverlayEyeColumnConfig cfg;
    cfg.type = CalcOverlayEyeColumnTypeFromString(GetStringOr(tbl, "name", "overworld coords"));
    cfg.headerRow = CalcOverlayHeaderRowFromString(GetStringOr(tbl, "headerRow", "show icon"));
    cfg.visible = GetOr(tbl, "visible", true);
    return cfg;
}

void CalcOverlayAaColumnConfigToToml(const CalcOverlayAaColumnConfig& cfg, toml::table& out) {
    out.insert_or_assign("name", CalcOverlayAaColumnTypeToString(cfg.type));
    out.insert_or_assign("headerRow", CalcOverlayHeaderRowToString(cfg.headerRow));
    out.insert_or_assign("visible", cfg.visible);
}

CalcOverlayAaColumnConfig CalcOverlayAaColumnConfigFromToml(const toml::table& tbl) {
    CalcOverlayAaColumnConfig cfg;
    cfg.type = CalcOverlayAaColumnTypeFromString(GetStringOr(tbl, "name", "icons"));
    cfg.headerRow = CalcOverlayHeaderRowFromString(GetStringOr(tbl, "headerRow", "nothing"));
    cfg.visible = GetOr(tbl, "visible", true);
    return cfg;
}

void CalcOverlayAaRowConfigToToml(const CalcOverlayAaRowConfig& cfg, toml::table& out) {
    out.insert_or_assign("name", CalcOverlayAaRowTypeToString(cfg.type));
    out.insert_or_assign("visible", cfg.visible);
}

CalcOverlayAaRowConfig CalcOverlayAaRowConfigFromToml(const toml::table& tbl) {
    CalcOverlayAaRowConfig cfg;
    cfg.type = CalcOverlayAaRowTypeFromString(GetStringOr(tbl, "name", "STRONGHOLD"));
    cfg.visible = GetOr(tbl, "visible", true);
    return cfg;
}

} // namespace

void CalcOverlayConfigToToml(const CalcOverlayConfig& cfg, toml::table& out) {
    out.insert_or_assign("enabled", cfg.enabled);
    out.insert_or_assign("overlayPosition", CalcOverlayPositionToString(cfg.overlayPosition));
    if (!cfg.fontName.empty()) {
        out.insert_or_assign("fontName", cfg.fontName);
    }
    out.insert_or_assign("fontSize", cfg.fontSize);
    out.insert_or_assign("outlineWidth", cfg.outlineWidth);
    out.insert_or_assign("netherCoordsColor", ColorToTomlArray(cfg.netherCoordsColor));

    toml::table negativeCoordsTbl;
    negativeCoordsTbl.insert_or_assign("use", cfg.negativeCoords.use);
    negativeCoordsTbl.insert_or_assign("color", ColorToTomlArray(cfg.negativeCoords.color));
    out.insert_or_assign("negativeCoords", std::move(negativeCoordsTbl));

    toml::table clearOverlayTbl;
    clearOverlayTbl.insert_or_assign("timeUnit", CalcOverlayClearOverlayTimeUnitToString(cfg.clearOverlayTimeUnit));
    clearOverlayTbl.insert_or_assign("amount", cfg.clearOverlayAmount);
    out.insert_or_assign("clearOverlayAfter", std::move(clearOverlayTbl));

    toml::array eyeColumnsArr;
    for (const auto& column : cfg.eyeColumns) {
        toml::table columnTbl;
        CalcOverlayEyeColumnConfigToToml(column, columnTbl);
        eyeColumnsArr.push_back(std::move(columnTbl));
    }
    out.insert_or_assign("eyeColumns", std::move(eyeColumnsArr));

    out.insert_or_assign("angleDisplay", CalcOverlayAngleDisplayModeToString(cfg.angleDisplay));
    out.insert_or_assign("onlyShowCurrentDimensionCoords", cfg.onlyShowCurrentDimensionCoords);
    out.insert_or_assign("showInfoBar", cfg.showInfoBar);
    out.insert_or_assign("overworldCoordsMode", CalcOverlayOverworldCoordsModeToString(cfg.overworldCoordsMode));
    out.insert_or_assign("shownMeasurements", cfg.shownMeasurements);
    out.insert_or_assign("blindCoordsEnabled", cfg.blindCoordsEnabled);
    out.insert_or_assign("showDirectionAndDistance", cfg.showDirectionAndDistance);
    out.insert_or_assign("allAdvancementsEnabled", cfg.allAdvancementsEnabled);

    toml::table aaTbl;
    toml::array aaColumnsArr;
    for (const auto& column : cfg.allAdvancements.columns) {
        toml::table columnTbl;
        CalcOverlayAaColumnConfigToToml(column, columnTbl);
        aaColumnsArr.push_back(std::move(columnTbl));
    }
    aaTbl.insert_or_assign("columns", std::move(aaColumnsArr));

    toml::array aaRowsArr;
    for (const auto& row : cfg.allAdvancements.rows) {
        toml::table rowTbl;
        CalcOverlayAaRowConfigToToml(row, rowTbl);
        aaRowsArr.push_back(std::move(rowTbl));
    }
    aaTbl.insert_or_assign("rows", std::move(aaRowsArr));
    out.insert_or_assign("allAdvancements", std::move(aaTbl));
}

CalcOverlayConfig CalcOverlayConfigFromToml(const toml::table& tbl) {
    CalcOverlayConfig cfg;
    cfg.enabled = GetOr(tbl, "enabled", false);
    cfg.overlayPosition = CalcOverlayPositionFromString(GetStringOr(tbl, "overlayPosition", "top left"));
    cfg.fontName = GetStringOr(tbl, "fontName", "");
    cfg.fontSize = std::max(1, GetOr(tbl, "fontSize", 48));
    cfg.outlineWidth = std::clamp(GetOr(tbl, "outlineWidth", 3), 0, 20);
    cfg.netherCoordsColor = ColorFromTomlArray(GetArray(tbl, "netherCoordsColor"), {1.0f, 1.0f, 1.0f, 1.0f});

    if (auto negativeCoordsTbl = GetTable(tbl, "negativeCoords")) {
        cfg.negativeCoords.use = GetOr(*negativeCoordsTbl, "use", true);
        cfg.negativeCoords.color = ColorFromTomlArray(GetArray(*negativeCoordsTbl, "color"), {1.0f, 0.7059f, 0.7059f, 1.0f});
    }

    if (auto clearOverlayTbl = GetTable(tbl, "clearOverlayAfter")) {
        cfg.clearOverlayTimeUnit = CalcOverlayClearOverlayTimeUnitFromString(GetStringOr(*clearOverlayTbl, "timeUnit", "never"));
        cfg.clearOverlayAmount = std::max(1, GetOr(*clearOverlayTbl, "amount", 1));
    }

    if (auto eyeColumnsArr = GetArray(tbl, "eyeColumns")) {
        cfg.eyeColumns.clear();
        for (const auto& elem : *eyeColumnsArr) {
            if (auto columnTbl = elem.as_table()) {
                cfg.eyeColumns.push_back(CalcOverlayEyeColumnConfigFromToml(*columnTbl));
            }
        }
    }

    cfg.angleDisplay = CalcOverlayAngleDisplayModeFromString(GetStringOr(tbl, "angleDisplay", "all"));
    cfg.onlyShowCurrentDimensionCoords = GetOr(tbl, "onlyShowCurrentDimensionCoords", false);
    cfg.showInfoBar = GetOr(tbl, "showInfoBar", false);
    cfg.overworldCoordsMode = CalcOverlayOverworldCoordsModeFromString(GetStringOr(tbl, "overworldCoordsMode", "chunk"));
    cfg.shownMeasurements = std::clamp(GetOr(tbl, "shownMeasurements", 3), 1, 5);
    cfg.blindCoordsEnabled = GetOr(tbl, "blindCoordsEnabled", true);
    cfg.showDirectionAndDistance = GetOr(tbl, "showDirectionAndDistance", false);
    cfg.allAdvancementsEnabled = GetOr(tbl, "allAdvancementsEnabled", true);

    if (auto aaTbl = GetTable(tbl, "allAdvancements")) {
        if (auto columnsArr = GetArray(*aaTbl, "columns")) {
            cfg.allAdvancements.columns.clear();
            for (const auto& elem : *columnsArr) {
                if (auto columnTbl = elem.as_table()) {
                    cfg.allAdvancements.columns.push_back(CalcOverlayAaColumnConfigFromToml(*columnTbl));
                }
            }
        }
        if (auto rowsArr = GetArray(*aaTbl, "rows")) {
            cfg.allAdvancements.rows.clear();
            for (const auto& elem : *rowsArr) {
                if (auto rowTbl = elem.as_table()) {
                    cfg.allAdvancements.rows.push_back(CalcOverlayAaRowConfigFromToml(*rowTbl));
                }
            }
        }
    }

    return cfg;
}
