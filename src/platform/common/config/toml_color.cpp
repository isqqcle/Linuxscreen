toml::array ColorToTomlArray(const Color& color) {
    toml::array arr;
    arr.push_back(static_cast<int64_t>(std::round(color.r * 255.0f)));
    arr.push_back(static_cast<int64_t>(std::round(color.g * 255.0f)));
    arr.push_back(static_cast<int64_t>(std::round(color.b * 255.0f)));
    if (color.a < 1.0f - 0.001f) {
        arr.push_back(static_cast<int64_t>(std::round(color.a * 255.0f)));
    }
    return arr;
}

Color ColorFromTomlArray(const toml::array* arr, const Color& defaultColor) {
    Color color = defaultColor;
    if (!arr || arr->size() < 3) {
        return color;
    }

    auto readComponent01 = [&](size_t idx, float fallback01) -> float {
        if (idx >= arr->size()) {
            return fallback01;
        }

        if (auto vInt = (*arr)[idx].value<int64_t>()) {
            return static_cast<float>(*vInt) / 255.0f;
        }

        if (auto vDbl = (*arr)[idx].value<double>()) {
            const double v = *vDbl;
            if (v <= 1.0) {
                return static_cast<float>(v);
            }
            return static_cast<float>(v / 255.0);
        }

        return fallback01;
    };

    color.r = readComponent01(0, defaultColor.r);
    color.g = readComponent01(1, defaultColor.g);
    color.b = readComponent01(2, defaultColor.b);
    color.a = (arr->size() >= 4) ? readComponent01(3, defaultColor.a) : 1.0f;

    color.r = std::max(0.0f, std::min(1.0f, color.r));
    color.g = std::max(0.0f, std::min(1.0f, color.g));
    color.b = std::max(0.0f, std::min(1.0f, color.b));
    color.a = std::max(0.0f, std::min(1.0f, color.a));
    
    return color;
}

void BorderConfigToToml(const BorderConfig& cfg, toml::table& out) {
    out.insert("enabled", cfg.enabled);
    out.insert("color", ColorToTomlArray(cfg.color));
    out.insert("width", cfg.width);
    out.insert("radius", cfg.radius);
}

BorderConfig BorderConfigFromToml(const toml::table& tbl) {
    BorderConfig cfg;
    cfg.enabled = GetOr(tbl, "enabled", false);
    cfg.color = ColorFromTomlArray(GetArray(tbl, "color"), {1.0f, 1.0f, 1.0f, 1.0f});
    cfg.width = GetOr(tbl, "width", 4);
    cfg.radius = GetOr(tbl, "radius", 0);
    return cfg;
}

void MirrorBorderConfigToToml(const MirrorBorderConfig& cfg, toml::table& out) {
    out.insert("type", MirrorBorderTypeToString(cfg.type));
    out.insert("dynamicThickness", cfg.dynamicThickness);
    out.insert("staticShape", MirrorBorderShapeToString(cfg.staticShape));
    out.insert("staticColor", ColorToTomlArray(cfg.staticColor));
    out.insert("staticThickness", cfg.staticThickness);
    out.insert("staticRadius", cfg.staticRadius);
    out.insert("staticOffsetX", cfg.staticOffsetX);
    out.insert("staticOffsetY", cfg.staticOffsetY);
    out.insert("staticWidth", cfg.staticWidth);
    out.insert("staticHeight", cfg.staticHeight);
}

MirrorBorderConfig MirrorBorderConfigFromToml(const toml::table& tbl) {
    MirrorBorderConfig cfg;
    cfg.type = StringToMirrorBorderType(GetStringOr(tbl, "type", "Dynamic"));
    cfg.dynamicThickness = GetOr(tbl, "dynamicThickness", 1);
    cfg.staticShape = StringToMirrorBorderShape(GetStringOr(tbl, "staticShape", "Rectangle"));
    cfg.staticColor = ColorFromTomlArray(GetArray(tbl, "staticColor"), {1.0f, 1.0f, 1.0f, 1.0f});
    cfg.staticThickness = GetOr(tbl, "staticThickness", 2);
    cfg.staticRadius = GetOr(tbl, "staticRadius", 0);
    cfg.staticOffsetX = GetOr(tbl, "staticOffsetX", 0);
    cfg.staticOffsetY = GetOr(tbl, "staticOffsetY", 0);
    cfg.staticWidth = GetOr(tbl, "staticWidth", 0);
    cfg.staticHeight = GetOr(tbl, "staticHeight", 0);
    return cfg;
}
