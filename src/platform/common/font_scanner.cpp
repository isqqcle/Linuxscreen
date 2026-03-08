#include "font_scanner.h"

#include <filesystem>
#include <vector>
#include <algorithm>
#include <wordexp.h>

namespace platform::common {

namespace {

std::string ExpandTilde(const std::string& path) {
    if (path.empty() || path[0] != '~') {
        return path;
    }

    wordexp_t exp_result;
    if (wordexp(path.c_str(), &exp_result, 0) != 0) {
        return path;
    }

    std::string expanded;
    if (exp_result.we_wordc > 0) {
        expanded = exp_result.we_wordv[0];
    } else {
        expanded = path;
    }

    wordfree(&exp_result);
    return expanded;
}

} // namespace

std::map<std::string, std::string> ScanForFonts() {
    std::map<std::string, std::string> discoveredFonts;
    
    std::vector<std::string> searchPaths = {
        "/usr/share/fonts",
        "/usr/local/share/fonts",
        "~/.local/share/fonts",
        "~/.fonts"
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
