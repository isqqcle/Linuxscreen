#include "io_detail.h"
#include "toml_detail.h"

namespace {

constexpr const char* kProfileRegistryFileName = "profiles.toml";
constexpr const char* kDefaultProfileId = "default";
constexpr const char* kDefaultProfileName = "Default";

std::string SanitizeProfileNameToId(const std::string& name) {
    std::string out;
    out.reserve(name.size());
    for (char c : name) {
        unsigned char uc = static_cast<unsigned char>(c);
        if (std::isalnum(uc)) {
            out.push_back(static_cast<char>(std::tolower(uc)));
        } else if (c == ' ' || c == '-' || c == '_' || c == '.') {
            if (!out.empty() && out.back() != '-') {
                out.push_back('-');
            }
        }
    }
    while (!out.empty() && out.back() == '-') {
        out.pop_back();
    }
    if (out.empty()) {
        out = "profile";
    }
    return out;
}

std::string BuildUniqueProfileId(const ConfigProfileRegistry& registry, const std::string& desiredName) {
    const std::string base = SanitizeProfileNameToId(desiredName);
    auto exists = [&](const std::string& id) {
        for (const auto& profile : registry.profiles) {
            if (profile.id == id) {
                return true;
            }
        }
        return false;
    };
    if (!exists(base)) {
        return base;
    }
    int suffix = 2;
    for (;;) {
        const std::string candidate = base + "-" + std::to_string(suffix);
        if (!exists(candidate)) {
            return candidate;
        }
        ++suffix;
    }
}

ConfigProfile MakeDefaultProfile() {
    ConfigProfile profile;
    profile.id = kDefaultProfileId;
    profile.name = kDefaultProfileName;

    const std::string basePath = GetBaseConfigPathInternal();
    const std::filesystem::path rootPath(GetConfigRootDirectoryPath());
    const std::filesystem::path baseParent = std::filesystem::path(basePath).parent_path().lexically_normal();
    if (!rootPath.empty() && rootPath == baseParent) {
        profile.path = "config.toml";
    } else {
        profile.path = basePath;
    }
    return profile;
}

std::string ResolveProfilePath(const ConfigProfile& profile) {
    return ResolvePathFromConfigRootDir(profile.path);
}

ConfigProfileRegistry EnsureRegistryValid(ConfigProfileRegistry registry) {
    registry.profiles.erase(std::remove_if(registry.profiles.begin(),
                                           registry.profiles.end(),
                                           [](const ConfigProfile& profile) {
                                               return profile.id.empty();
                                           }),
                            registry.profiles.end());

    std::set<std::string> seenIds;
    std::set<std::string> seenPaths;
    std::vector<ConfigProfile> deduped;
    deduped.reserve(registry.profiles.size());
    for (const auto& rawProfile : registry.profiles) {
        ConfigProfile profile = rawProfile;
        if (profile.name.empty()) {
            profile.name = profile.id;
        }
        if (profile.path.empty()) {
            profile.path = profile.id + ".toml";
        }

        const std::string resolvedPath = ResolveProfilePath(profile);
        if (!seenIds.insert(profile.id).second) {
            continue;
        }
        if (!seenPaths.insert(resolvedPath).second) {
            continue;
        }

        deduped.push_back(std::move(profile));
    }
    registry.profiles = std::move(deduped);

    if (registry.profiles.empty()) {
        registry.profiles.push_back(MakeDefaultProfile());
    }

    bool activeFound = false;
    for (const auto& profile : registry.profiles) {
        if (profile.id == registry.activeProfileId) {
            activeFound = true;
            break;
        }
    }
    if (!activeFound) {
        registry.activeProfileId = registry.profiles.front().id;
    }
    return registry;
}

toml::table ProfileRegistryToToml(const ConfigProfileRegistry& registry) {
    toml::table out;
    out.insert_or_assign("activeProfile", registry.activeProfileId);

    toml::array profilesArr;
    for (const auto& profile : registry.profiles) {
        toml::table profileTbl;
        profileTbl.insert_or_assign("id", profile.id);
        profileTbl.insert_or_assign("name", profile.name);
        profileTbl.insert_or_assign("path", profile.path);
        profilesArr.push_back(std::move(profileTbl));
    }
    out.insert_or_assign("profile", std::move(profilesArr));
    return out;
}

ConfigProfileRegistry ProfileRegistryFromToml(const toml::table& tbl) {
    ConfigProfileRegistry registry;
    registry.activeProfileId = detail::GetStringOr(tbl, "activeProfile", "");

    if (const toml::array* profilesArr = detail::GetArray(tbl, "profile")) {
        for (const auto& elem : *profilesArr) {
            const toml::table* profileTbl = elem.as_table();
            if (!profileTbl) {
                continue;
            }
            ConfigProfile profile;
            profile.id = detail::GetStringOr(*profileTbl, "id", "");
            profile.name = detail::GetStringOr(*profileTbl, "name", profile.id);
            profile.path = detail::GetStringOr(*profileTbl, "path", "");
            if (!profile.id.empty()) {
                registry.profiles.push_back(std::move(profile));
            }
        }
    }

    if (registry.profiles.empty()) {
        ConfigProfile legacy = MakeDefaultProfile();
        if (const std::string legacyPath = detail::GetStringOr(tbl, "activeConfigPath", ""); !legacyPath.empty()) {
            legacy.path = legacyPath;
        }
        registry.profiles.push_back(std::move(legacy));
    }
    return EnsureRegistryValid(std::move(registry));
}

bool EnsureDirectoryForFilePath(const std::string& path) {
    try {
        const std::filesystem::path fsPath(path);
        const std::filesystem::path parent = fsPath.parent_path();
        if (!parent.empty() && !std::filesystem::exists(parent)) {
            std::filesystem::create_directories(parent);
        }
        return true;
    } catch (const std::exception& e) {
        LogWarning("Failed to create profile registry directory for %s: %s", path.c_str(), e.what());
        return false;
    }
}

const ConfigProfile* FindProfileById(const ConfigProfileRegistry& registry, const std::string& profileId) {
    for (const auto& profile : registry.profiles) {
        if (profile.id == profileId) {
            return &profile;
        }
    }
    return nullptr;
}

void EnsureProfileConfigFileExists(const ConfigProfile& profile) {
    const std::string resolvedPath = ResolveProfilePath(profile);
    if (std::filesystem::exists(resolvedPath)) {
        return;
    }

    LinuxscreenConfig defaultConfig = LoadEmbeddedDefaultConfig();
    SaveLinuxscreenConfigAtPathImmediate(defaultConfig, resolvedPath);
}

ConfigProfileRegistry g_profileRegistryCache;
std::mutex g_profileRegistryCacheMutex;
std::string g_activeProfileId;

ConfigProfileRegistry LoadRegistryCached() {
    std::lock_guard<std::mutex> lock(g_profileRegistryCacheMutex);
    if (g_profileRegistryCache.profiles.empty()) {
        g_profileRegistryCache = LoadOrCreateConfigProfileRegistry();
        g_activeProfileId = g_profileRegistryCache.activeProfileId;
    }
    return g_profileRegistryCache;
}

void StoreRegistryCached(ConfigProfileRegistry registry) {
    std::lock_guard<std::mutex> lock(g_profileRegistryCacheMutex);
    g_profileRegistryCache = EnsureRegistryValid(std::move(registry));
    if (!g_profileRegistryCache.activeProfileId.empty()) {
        g_activeProfileId = g_profileRegistryCache.activeProfileId;
    }
}

std::string GetActiveProfileIdCached() {
    std::lock_guard<std::mutex> lock(g_profileRegistryCacheMutex);
    if (!g_activeProfileId.empty()) {
        return g_activeProfileId;
    }
    if (g_profileRegistryCache.profiles.empty()) {
        return {};
    }
    return g_profileRegistryCache.activeProfileId;
}

} // namespace

std::string GetProfileRegistryPath() {
    const std::filesystem::path root(GetConfigRootDirectoryPath());
    return (root / kProfileRegistryFileName).lexically_normal().string();
}

ConfigProfileRegistry LoadConfigProfileRegistry() {
    const std::string path = GetProfileRegistryPath();
    if (!std::filesystem::exists(path)) {
        ConfigProfileRegistry registry;
        registry.profiles.push_back(MakeDefaultProfile());
        registry.activeProfileId = registry.profiles.front().id;
        return registry;
    }

    try {
        const toml::table parsed = toml::parse_file(path);
        return ProfileRegistryFromToml(parsed);
    } catch (const std::exception& e) {
        LogWarning("Failed to parse profile registry %s: %s", path.c_str(), e.what());
        ConfigProfileRegistry registry;
        registry.profiles.push_back(MakeDefaultProfile());
        registry.activeProfileId = registry.profiles.front().id;
        return registry;
    }
}

bool SaveConfigProfileRegistry(const ConfigProfileRegistry& registry) {
    const ConfigProfileRegistry validRegistry = EnsureRegistryValid(registry);
    const std::string path = GetProfileRegistryPath();
    if (!EnsureDirectoryForFilePath(path)) {
        return false;
    }

    try {
        const toml::table out = ProfileRegistryToToml(validRegistry);
        std::ofstream file(path);
        if (!file) {
            LogWarning("Failed to open profile registry for writing: %s", path.c_str());
            return false;
        }
        file << out;
        return true;
    } catch (const std::exception& e) {
        LogWarning("Failed to save profile registry %s: %s", path.c_str(), e.what());
        return false;
    }
}

ConfigProfileRegistry LoadOrCreateConfigProfileRegistry() {
    ConfigProfileRegistry registry = LoadConfigProfileRegistry();
    registry = EnsureRegistryValid(std::move(registry));
    (void)SaveConfigProfileRegistry(registry);
    return registry;
}

bool InitializeActiveConfigProfile(std::string& outError) {
    outError.clear();
    ConfigProfileRegistry registry = LoadOrCreateConfigProfileRegistry();
    const ConfigProfile* activeProfile = FindProfileById(registry, registry.activeProfileId);
    if (!activeProfile) {
        outError = "Active profile is missing.";
        return false;
    }

    EnsureProfileConfigFileExists(*activeProfile);
    SetConfigPathOverride(ResolveProfilePath(*activeProfile));
    StoreRegistryCached(std::move(registry));
    return true;
}

ConfigProfileRegistry GetConfigProfileRegistry() {
    return LoadRegistryCached();
}

std::string GetActiveConfigProfileId() {
    return GetActiveProfileIdCached();
}

bool SwitchActiveConfigProfile(const std::string& profileId,
                               LinuxscreenConfig& outConfig,
                               std::string& outError) {
    outError.clear();

    ConfigProfileRegistry registry = LoadRegistryCached();
    const ConfigProfile* targetProfile = FindProfileById(registry, profileId);
    if (!targetProfile) {
        outError = "Profile not found.";
        return false;
    }

    WaitForConfigSaveQueueIdle(3000);
    EnsureProfileConfigFileExists(*targetProfile);

    SetConfigPathOverride(ResolveProfilePath(*targetProfile));
    outConfig = LoadLinuxscreenConfig();
    PublishConfigSnapshot(outConfig);

    registry.activeProfileId = targetProfile->id;
    if (!SaveConfigProfileRegistry(registry)) {
        outError = "Switched profile, but failed to persist active profile selection.";
    }
    StoreRegistryCached(std::move(registry));
    return true;
}

bool CreateConfigProfile(const std::string& name,
                         bool duplicateCurrent,
                         ConfigProfile& outProfile,
                         std::string& outError) {
    outError.clear();

    ConfigProfileRegistry registry = LoadRegistryCached();
    const std::string profileName = name.empty() ? "Profile" : name;

    outProfile.id = BuildUniqueProfileId(registry, profileName);
    outProfile.name = profileName;
    outProfile.path = outProfile.id + ".toml";

    LinuxscreenConfig profileConfig;
    if (duplicateCurrent) {
        if (const auto current = GetConfigSnapshot()) {
            profileConfig = *current;
        } else {
            profileConfig = LoadEmbeddedDefaultConfig();
        }
    } else {
        profileConfig = LoadEmbeddedDefaultConfig();
    }

    const std::string resolvedPath = ResolveProfilePath(outProfile);
    if (!EnsureDirectoryForFilePath(resolvedPath)) {
        outError = "Failed to prepare profile directory.";
        return false;
    }
    SaveLinuxscreenConfigAtPathImmediate(profileConfig, resolvedPath);

    registry.profiles.push_back(outProfile);
    if (!SaveConfigProfileRegistry(registry)) {
        outError = "Failed to save profile registry.";
        return false;
    }

    StoreRegistryCached(std::move(registry));
    return true;
}
