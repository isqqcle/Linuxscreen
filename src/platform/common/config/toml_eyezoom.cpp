namespace {

const char* EyeZoomOverlayDisplayModeToString(EyeZoomOverlayDisplayMode mode) {
    switch (mode) {
    case EyeZoomOverlayDisplayMode::Manual:
        return "manual";
    case EyeZoomOverlayDisplayMode::Stretch:
        return "stretch";
    case EyeZoomOverlayDisplayMode::Fit:
    default:
        return "fit";
    }
}

EyeZoomOverlayDisplayMode StringToEyeZoomOverlayDisplayMode(const std::string& value) {
    if (value == "manual") {
        return EyeZoomOverlayDisplayMode::Manual;
    }
    if (value == "stretch") {
        return EyeZoomOverlayDisplayMode::Stretch;
    }
    return EyeZoomOverlayDisplayMode::Fit;
}

} // namespace

void EyeZoomConfigToToml(const EyeZoomConfig& cfg, toml::table& out) {
    out = cfg.windowsPassthrough;
    int windowsZoomAreaWidth = cfg.zoomAreaWidth;
    int windowsZoomAreaHeight = cfg.zoomAreaHeight;
    int windowsPositionX = cfg.positionX;
    int windowsPositionY = cfg.positionY;
    bool windowsUseCustomSizePosition = cfg.useCustomSizePosition;

    if (!cfg.outputUseRelativePosition) {
        windowsPositionX = cfg.outputX;
        windowsPositionY = cfg.outputY;
        windowsUseCustomSizePosition = true;
    }
    if (!cfg.outputUseRelativeSize) {
        if (cfg.stretchWidth > 0) {
            windowsZoomAreaWidth = cfg.stretchWidth;
            windowsUseCustomSizePosition = true;
        }
        if (cfg.outputHeight > 0) {
            windowsZoomAreaHeight = cfg.outputHeight;
            windowsUseCustomSizePosition = true;
        }
    }

    out.insert_or_assign("cloneWidth", cfg.cloneWidth);
    out.insert_or_assign("overlayWidth", cfg.overlayWidth);
    out.insert_or_assign("cloneHeight", cfg.cloneHeight);
    out.insert_or_assign("stretchWidth", cfg.stretchWidth);
    out.insert_or_assign("windowWidth", cfg.windowWidth);
    out.insert_or_assign("windowHeight", cfg.windowHeight);
    out.insert_or_assign("zoomAreaWidth", windowsZoomAreaWidth);
    out.insert_or_assign("zoomAreaHeight", windowsZoomAreaHeight);
    out.insert_or_assign("useCustomSizePosition", windowsUseCustomSizePosition);
    out.insert_or_assign("positionX", windowsPositionX);
    out.insert_or_assign("positionY", windowsPositionY);
    out.insert_or_assign("horizontalMargin", cfg.horizontalMargin);
    out.insert_or_assign("verticalMargin", cfg.verticalMargin);
    out.insert_or_assign("outputRelativeTo", cfg.outputRelativeTo);
    if (cfg.outputUseRelativePosition) {
        out.insert_or_assign("outputX", static_cast<double>(cfg.outputRelativeX));
        out.insert_or_assign("outputY", static_cast<double>(cfg.outputRelativeY));
    } else {
        out.insert_or_assign("outputX", cfg.outputX);
        out.insert_or_assign("outputY", cfg.outputY);
    }
    out.insert_or_assign("outputUseRelativePosition", cfg.outputUseRelativePosition);
    out.insert_or_assign("outputRelativeX", static_cast<double>(cfg.outputRelativeX));
    out.insert_or_assign("outputRelativeY", static_cast<double>(cfg.outputRelativeY));
    out.insert_or_assign("outputUseRelativeSize", cfg.outputUseRelativeSize);
    out.insert_or_assign("outputRelativeWidth", static_cast<double>(cfg.outputRelativeWidth));
    out.insert_or_assign("outputRelativeHeight", static_cast<double>(cfg.outputRelativeHeight));
    out.insert_or_assign("outputPreserveAspectRatio", cfg.outputPreserveAspectRatio);
    out.insert_or_assign("outputAspectFitMode", NormalizeAspectFitMode(cfg.outputAspectFitMode));
    out.insert_or_assign("outputHeight", cfg.outputHeight);
    out.insert_or_assign("autoFontSize", cfg.autoFontSize);
    out.insert_or_assign("textFontSize", cfg.textFontSize);
    out.insert_or_assign("textFontPath", cfg.textFontPath);
    out.insert_or_assign("rectHeight", cfg.rectHeight);
    out.insert_or_assign("linkRectToFont", cfg.linkRectToFont);
    out.insert_or_assign("gridColor1", ColorToTomlArray(cfg.gridColor1));
    out.insert_or_assign("gridColor1Opacity", cfg.gridColor1Opacity);
    out.insert_or_assign("gridColor2", ColorToTomlArray(cfg.gridColor2));
    out.insert_or_assign("gridColor2Opacity", cfg.gridColor2Opacity);
    out.insert_or_assign("centerLineColor", ColorToTomlArray(cfg.centerLineColor));
    out.insert_or_assign("centerLineColorOpacity", cfg.centerLineColorOpacity);
    out.insert_or_assign("textColor", ColorToTomlArray(cfg.textColor));
    out.insert_or_assign("textColorOpacity", cfg.textColorOpacity);
    out.insert_or_assign("slideZoomIn", cfg.slideZoomIn);
    out.insert_or_assign("slideMirrorsIn", cfg.slideMirrorsIn);
    out.insert_or_assign("activeOverlayIndex", cfg.activeOverlayIndex);

    toml::array overlayArr;
    for (const auto& overlay : cfg.overlays) {
        toml::table overlayTbl;
        overlayTbl.insert("name", overlay.name);
        overlayTbl.insert("path", overlay.path);
        overlayTbl.insert("displayMode", EyeZoomOverlayDisplayModeToString(overlay.displayMode));
        overlayTbl.insert("manualWidth", overlay.manualWidth);
        overlayTbl.insert("manualHeight", overlay.manualHeight);
        overlayTbl.insert("opacity", static_cast<double>(overlay.opacity));
        overlayArr.push_back(std::move(overlayTbl));
    }
    out.insert_or_assign("overlay", overlayArr);
}

EyeZoomConfig EyeZoomConfigFromToml(const toml::table& tbl) {
    EyeZoomConfig cfg;
    cfg.windowsPassthrough = tbl;
    EraseKeys(cfg.windowsPassthrough,
              { "cloneWidth",
                "overlayWidth",
                "cloneHeight",
                "stretchWidth",
                "windowWidth",
                "windowHeight",
                "zoomAreaWidth",
                "zoomAreaHeight",
                "useCustomSizePosition",
                "positionX",
                "positionY",
                "horizontalMargin",
                "verticalMargin",
                "outputRelativeTo",
                "outputX",
                "outputY",
                "outputUseRelativePosition",
                "outputRelativeX",
                "outputRelativeY",
                "outputUseRelativeSize",
                "outputRelativeWidth",
                "outputRelativeHeight",
                "outputPreserveAspectRatio",
                "outputAspectFitMode",
                "outputHeight",
                "autoFontSize",
                "textFontSize",
                "textFontPath",
                "rectHeight",
                "linkRectToFont",
                "gridColor1",
                "gridColor1Opacity",
                "gridColor2",
                "gridColor2Opacity",
                "centerLineColor",
                "centerLineColorOpacity",
                "textColor",
                "textColorOpacity",
                "slideZoomIn",
                "slideMirrorsIn",
                "activeOverlayIndex",
                "overlay" });

    cfg.cloneWidth = GetOr(tbl, "cloneWidth", 24);
    if (cfg.cloneWidth < 2) cfg.cloneWidth = 2;
    if (cfg.cloneWidth % 2 != 0) cfg.cloneWidth = (cfg.cloneWidth / 2) * 2;

    const int overlayDefaultSentinel = -1;
    const int overlayWidth = GetOr(tbl, "overlayWidth", overlayDefaultSentinel);
    if (overlayWidth == overlayDefaultSentinel) {
        cfg.overlayWidth = cfg.cloneWidth / 2;
    } else {
        cfg.overlayWidth = overlayWidth;
    }
    if (cfg.overlayWidth < 0) cfg.overlayWidth = 0;
    const int maxOverlay = cfg.cloneWidth / 2;
    if (cfg.overlayWidth > maxOverlay) cfg.overlayWidth = maxOverlay;

    cfg.cloneHeight = GetOr(tbl, "cloneHeight", 2080);
    if (cfg.cloneHeight < 1) cfg.cloneHeight = 1;
    cfg.stretchWidth = GetOr(tbl, "stretchWidth", 810);
    if (cfg.stretchWidth < 0) cfg.stretchWidth = 0;
    cfg.windowWidth = GetOr(tbl, "windowWidth", 384);
    cfg.windowHeight = GetOr(tbl, "windowHeight", 16384);
    cfg.zoomAreaWidth = GetOr(tbl, "zoomAreaWidth", 0);
    cfg.zoomAreaHeight = GetOr(tbl, "zoomAreaHeight", 0);
    cfg.useCustomSizePosition = GetOr(tbl, "useCustomSizePosition", GetOr(tbl, "useCustomPosition", false));
    cfg.positionX = GetOr(tbl, "positionX", 0);
    cfg.positionY = GetOr(tbl, "positionY", 0);
    cfg.horizontalMargin = GetOr(tbl, "horizontalMargin", 0);
    cfg.verticalMargin = GetOr(tbl, "verticalMargin", 0);
    cfg.outputRelativeTo = GetStringOr(tbl, "outputRelativeTo", "middleLeftScreen");
    cfg.outputUseRelativePosition = GetOr(tbl, "outputUseRelativePosition", false);
    cfg.outputRelativeX = static_cast<float>(GetOr(tbl, "outputRelativeX", 0.0));
    cfg.outputRelativeY = static_cast<float>(GetOr(tbl, "outputRelativeY", 0.0));
    cfg.outputUseRelativeSize = GetOr(tbl, "outputUseRelativeSize", false);
    cfg.outputRelativeWidth = static_cast<float>(GetOr(tbl, "outputRelativeWidth", 0.5));
    cfg.outputRelativeHeight = static_cast<float>(GetOr(tbl, "outputRelativeHeight", 0.5));
    cfg.outputPreserveAspectRatio = GetOr(tbl, "outputPreserveAspectRatio", true);
    cfg.outputAspectFitMode = NormalizeAspectFitMode(GetStringOr(tbl, "outputAspectFitMode", "contain"));
    cfg.outputX = cfg.horizontalMargin;
    cfg.outputY = 0;

    bool xIsPercentage = false;
    bool yIsPercentage = false;

    if (auto outputXNode = tbl.get("outputX")) {
        if (outputXNode->is_floating_point()) {
            const double outputXVal = outputXNode->as_floating_point()->get();
            if (cfg.outputUseRelativePosition || (outputXVal >= 0.0 && outputXVal <= 1.0)) {
                cfg.outputRelativeX = static_cast<float>(outputXVal);
                xIsPercentage = true;
            } else {
                cfg.outputX = static_cast<int>(outputXVal);
            }
        } else if (outputXNode->is_integer()) {
            cfg.outputX = static_cast<int>(outputXNode->as_integer()->get());
        }
    }

    if (auto outputYNode = tbl.get("outputY")) {
        if (outputYNode->is_floating_point()) {
            const double outputYVal = outputYNode->as_floating_point()->get();
            if (cfg.outputUseRelativePosition || (outputYVal >= 0.0 && outputYVal <= 1.0)) {
                cfg.outputRelativeY = static_cast<float>(outputYVal);
                yIsPercentage = true;
            } else {
                cfg.outputY = static_cast<int>(outputYVal);
            }
        } else if (outputYNode->is_integer()) {
            cfg.outputY = static_cast<int>(outputYNode->as_integer()->get());
        }
    }

    if (!tbl.contains("outputUseRelativePosition") && xIsPercentage && yIsPercentage) {
        cfg.outputUseRelativePosition = true;
    }

    cfg.outputRelativeX = std::clamp(cfg.outputRelativeX, -1.0f, 2.0f);
    cfg.outputRelativeY = std::clamp(cfg.outputRelativeY, -1.0f, 2.0f);
    cfg.outputRelativeWidth = std::clamp(cfg.outputRelativeWidth, 0.01f, 20.0f);
    cfg.outputRelativeHeight = std::clamp(cfg.outputRelativeHeight, 0.01f, 20.0f);
    cfg.outputHeight = GetOr(tbl, "outputHeight", 0);
    if (cfg.outputHeight < 0) cfg.outputHeight = 0;
    cfg.autoFontSize = GetOr(tbl, "autoFontSize", true);
    cfg.textFontSize = GetOr(tbl, "textFontSize", 24);
    if (cfg.textFontSize < 1) cfg.textFontSize = 1;
    cfg.textFontPath = GetStringOr(tbl, "textFontPath", "");
    cfg.rectHeight = GetOr(tbl, "rectHeight", 24);
    if (cfg.rectHeight < 1) cfg.rectHeight = 1;
    cfg.linkRectToFont = GetOr(tbl, "linkRectToFont", true);

    cfg.gridColor1 = ColorFromTomlArray(GetArray(tbl, "gridColor1"), {1.0f, 0.714f, 0.757f, 1.0f});
    cfg.gridColor1Opacity = static_cast<float>(GetOr(tbl, "gridColor1Opacity", 1.0));
    cfg.gridColor2 = ColorFromTomlArray(GetArray(tbl, "gridColor2"), {0.678f, 0.847f, 0.902f, 1.0f});
    cfg.gridColor2Opacity = static_cast<float>(GetOr(tbl, "gridColor2Opacity", 1.0));
    cfg.centerLineColor = ColorFromTomlArray(GetArray(tbl, "centerLineColor"), {1.0f, 1.0f, 1.0f, 1.0f});
    cfg.centerLineColorOpacity = static_cast<float>(GetOr(tbl, "centerLineColorOpacity", 1.0));
    cfg.textColor = ColorFromTomlArray(GetArray(tbl, "textColor"), {0.0f, 0.0f, 0.0f, 1.0f});
    cfg.textColorOpacity = static_cast<float>(GetOr(tbl, "textColorOpacity", 1.0));
    cfg.slideZoomIn = GetOr(tbl, "slideZoomIn", false);
    cfg.slideMirrorsIn = GetOr(tbl, "slideMirrorsIn", false);
    cfg.activeOverlayIndex = GetOr(tbl, "activeOverlayIndex", -1);

    if (auto overlayArr = GetArray(tbl, "overlay")) {
        for (const auto& elem : *overlayArr) {
            if (const auto* overlayTbl = elem.as_table()) {
                EyeZoomOverlayConfig overlay;
                overlay.name = GetStringOr(*overlayTbl, "name", "");
                overlay.path = GetStringOr(*overlayTbl, "path", "");
                overlay.displayMode = StringToEyeZoomOverlayDisplayMode(GetStringOr(*overlayTbl, "displayMode", "fit"));
                overlay.manualWidth = GetOr(*overlayTbl, "manualWidth", 100);
                overlay.manualHeight = GetOr(*overlayTbl, "manualHeight", 100);
                overlay.opacity = static_cast<float>(GetOr(*overlayTbl, "opacity", 1.0));
                cfg.overlays.push_back(std::move(overlay));
            }
        }
    }
    if (cfg.activeOverlayIndex < -1 || cfg.activeOverlayIndex >= static_cast<int>(cfg.overlays.size())) {
        cfg.activeOverlayIndex = -1;
    }

    const bool hasLinuxOutputFields =
        tbl.contains("outputRelativeTo") ||
        tbl.contains("outputX") ||
        tbl.contains("outputY") ||
        tbl.contains("outputUseRelativePosition") ||
        tbl.contains("outputRelativeX") ||
        tbl.contains("outputRelativeY") ||
        tbl.contains("outputUseRelativeSize") ||
        tbl.contains("outputRelativeWidth") ||
        tbl.contains("outputRelativeHeight") ||
        tbl.contains("outputHeight");
    const bool hasWindowsPlacementFields =
        tbl.contains("positionX") ||
        tbl.contains("positionY") ||
        tbl.contains("zoomAreaWidth") ||
        tbl.contains("zoomAreaHeight") ||
        tbl.contains("useCustomSizePosition");

    if (!hasLinuxOutputFields && hasWindowsPlacementFields) {
        cfg.outputRelativeTo = "topLeftScreen";
        cfg.outputUseRelativePosition = false;
        cfg.outputUseRelativeSize = false;
        cfg.outputX = cfg.positionX;
        cfg.outputY = cfg.positionY;
        if (cfg.zoomAreaWidth > 0) {
            cfg.stretchWidth = cfg.zoomAreaWidth;
        }
        if (cfg.zoomAreaHeight > 0) {
            cfg.outputHeight = cfg.zoomAreaHeight;
        }
    }

    return cfg;
}
