std::string GetConfigPathInternal() {
    const char* envPath = std::getenv("LINUXSCREEN_X11_CONFIG_FILE");
    if (envPath && envPath[0] != '\0') {
        return std::string(envPath);
    }

    const char* home = std::getenv("HOME");
    if (home && home[0] != '\0') {
        return std::string(home) + "/.config/linuxscreen/config.toml";
    }
    
    return "/tmp/linuxscreen_config.toml";
}

} // namespace

std::string GetConfigPath() {
    return GetConfigPathInternal();
}

std::string GetConfigDirectoryPath() {
    std::filesystem::path configPath(GetConfigPath());
    std::filesystem::path parent = configPath.parent_path();
    if (parent.empty()) {
        parent = ".";
    }
    return parent.lexically_normal().string();
}

std::string ResolvePathFromConfigDir(const std::string& path) {
    if (path.empty()) {
        return {};
    }

    try {
        std::filesystem::path input(path);
        if (input.is_absolute()) {
            return input.lexically_normal().string();
        }

        std::filesystem::path base(GetConfigDirectoryPath());
        return (base / input).lexically_normal().string();
    } catch (...) {
        return path;
    }
}

std::string NormalizePathForConfig(const std::string& path) {
    if (path.empty()) {
        return {};
    }

    try {
        std::filesystem::path input(path);
        if (!input.is_absolute()) {
            return input.lexically_normal().string();
        }

        std::filesystem::path absoluteInput = input.lexically_normal();
        std::filesystem::path baseAbs = std::filesystem::absolute(GetConfigDirectoryPath()).lexically_normal();
        std::filesystem::path relative = absoluteInput.lexically_relative(baseAbs);
        bool escapesBase = false;
        if (!relative.empty()) {
            auto it = relative.begin();
            if (it != relative.end() && *it == std::filesystem::path("..")) {
                escapesBase = true;
            }
        }
        if (!relative.empty() && !relative.is_absolute() && !escapesBase) {
            return relative.lexically_normal().string();
        }
        return absoluteInput.string();
    } catch (...) {
        return path;
    }
}
