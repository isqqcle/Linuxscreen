#include "font_scanner.h"

#include <filesystem>
#include <vector>
#include <algorithm>
#include <pwd.h>
#include <unistd.h>

namespace platform::common {

namespace {

std::string GetHomeDir() {
    const char* home = getenv("HOME");
    if (home && home[0] != '\0') {
        return home;
    }
    struct passwd* pw = getpwuid(getuid());
    if (pw && pw->pw_dir) {
        return pw->pw_dir;
    }
    return "";
}

std::string ExpandTilde(const std::string& path) {
    if (path.empty() || path[0] != '~') {
        return path;
    }
    std::string home = GetHomeDir();
    if (home.empty()) {
        return path;
    }
    return home + path.substr(1);
}

} // namespace

std::map<std::string, std::string> ScanForFonts() {
    std::map<std::string, std::string> discoveredFonts;
    
    std::vector<std::string> searchPaths = {
#ifdef __APPLE__
        "/System/Library/Fonts",
        "/Library/Fonts",
        "~/Library/Fonts",
#else
        "/usr/share/fonts",
        "/usr/local/share/fonts",
        "~/.local/share/fonts",
        "~/.fonts",
#endif
    };

    for (const auto& rawPath : searchPaths) {
        std::string path = ExpandTilde(rawPath);
        std::filesystem::path fsPath(path);

        if (!std::filesystem::exists(fsPath) || !std::filesystem::is_directory(fsPath)) {
            continue;
        }

        try {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(fsPath, std::filesystem::directory_options::skip_permission_denied)) {
                if (!entry.is_regular_file()) {
                    continue;
                }

                auto ext = entry.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

                if (ext == ".ttf" || ext == ".otf") {
                    std::string fontName = entry.path().stem().string();
                    if (discoveredFonts.find(fontName) == discoveredFonts.end() || rawPath[0] == '~') {
                        discoveredFonts[fontName] = std::filesystem::absolute(entry.path()).string();
                    }
                }
            }
        } catch (const std::exception& e) {
            continue;
        }
    }

    return discoveredFonts;
}

} // namespace platform::common
