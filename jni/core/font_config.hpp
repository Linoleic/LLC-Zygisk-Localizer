#pragma once
#include "../third_party/nlohmann/json.hpp"
#include <string>
#include <fstream>
#include <filesystem>
#include <unistd.h>

namespace font_config {

inline constexpr const char* default_font_path = "/data/user/0/com.ProjectMoon.LimbusCompany/files/tmpchinesefont";
inline constexpr const char* config_path = "/data/user/0/com.ProjectMoon.LimbusCompany/files/font_config.json";
inline constexpr const char* fonts_dir_user = "/data/user/0/com.ProjectMoon.LimbusCompany/files/fonts";
inline constexpr const char* fonts_dir_sdcard = "/sdcard/Android/data/com.ProjectMoon.LimbusCompany/files/fonts";

struct FontSelection {
    std::string selected_name = "default";
    std::string font_path = default_font_path;
    bool is_custom = false;
};

inline FontSelection resolve_font_selection(const std::string& custom_config_file = config_path,
                                           const std::string& fallback_font = default_font_path) {
    FontSelection result;
    result.font_path = fallback_font;

    std::ifstream file(custom_config_file);
    if (!file.is_open()) {
        return result;
    }

    try {
        nlohmann::json j;
        file >> j;
        if (!j.is_object()) return result;

        std::string path;
        if (j.contains("font_path") && j["font_path"].is_string()) {
            path = j["font_path"].get<std::string>();
        }

        std::string name;
        if (j.contains("selected_font") && j["selected_font"].is_string()) {
            name = j["selected_font"].get<std::string>();
        }

        if (path.empty() || path == "default" || name == "default") {
            return result;
        }

        // Validate that the target font bundle exists and is readable
        std::error_code ec;
        if (std::filesystem::is_regular_file(path, ec) && access(path.c_str(), R_OK) == 0) {
            result.font_path = path;
            result.selected_name = name.empty() ? std::filesystem::path(path).filename().string() : name;
            result.is_custom = true;
            return result;
        }
    } catch (...) {
        // Any parse failure safely falls back to default
    }

    return result;
}

inline std::string resolve_active_font_bundle_path(const std::string& custom_config_file = config_path,
                                                  const std::string& fallback_font = default_font_path) {
    return resolve_font_selection(custom_config_file, fallback_font).font_path;
}

} // namespace font_config
