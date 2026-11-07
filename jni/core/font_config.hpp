#pragma once
#include "../third_party/nlohmann/json.hpp"
#include <string>
#include <fstream>
#include <filesystem>
#include <vector>
#include <cstring>
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

inline int32_t inspect_assetbundle_target_platform(const std::string& filepath) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(filepath, ec)) return -1;
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) return -1;

    std::vector<char> buf(65536);
    file.read(buf.data(), buf.size());
    std::streamsize bytes_read = file.gcount();
    if (bytes_read < 64 || memcmp(buf.data(), "UnityFS", 7) != 0) return -1;

    for (std::streamsize i = 64; i + 32 <= bytes_read; ++i) {
        if ((buf[i] == '2' && buf[i + 1] == '0' && buf[i + 2] >= '1' && buf[i + 2] <= '9') ||
            (buf[i] == '6' && buf[i + 1] == '0' && buf[i + 2] == '0' && buf[i + 3] == '0')) {
            std::streamsize null_pos = -1;
            for (std::streamsize j = i; j < i + 32 && j < bytes_read; ++j) {
                if (buf[j] == '\0') { null_pos = j; break; }
            }
            if (null_pos == -1) continue;
            std::streamsize ver_len = null_pos - i;
            if (ver_len < 7 || ver_len > 24) continue;

            bool valid_ver = true;
            for (std::streamsize j = i; j < null_pos; ++j) {
                char c = buf[j];
                if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '.')) {
                    valid_ver = false;
                    break;
                }
            }
            if (!valid_ver) continue;

            std::streamsize plat_pos = null_pos + 1;
            if (plat_pos + 5 > bytes_read) continue;

            uint8_t enable_type_tree = static_cast<uint8_t>(buf[plat_pos + 4]);
            if (enable_type_tree > 1) continue;

            int32_t current_platform = 0;
            memcpy(&current_platform, buf.data() + plat_pos, sizeof(int32_t));
            return current_platform;
        }
    }
    return -1;
}

inline bool adapt_assetbundle_to_android(const std::string& filepath) {
    int32_t current_platform = inspect_assetbundle_target_platform(filepath);
    if (current_platform == 13) return true; // Already Android (BuildTarget.Android)
    if (current_platform == -1) return false;

    std::fstream file(filepath, std::ios::in | std::ios::out | std::ios::binary);
    if (!file.is_open()) return false;

    std::vector<char> buf(65536);
    file.read(buf.data(), buf.size());
    std::streamsize bytes_read = file.gcount();
    if (bytes_read < 64) return false;

    for (std::streamsize i = 64; i + 32 <= bytes_read; ++i) {
        if ((buf[i] == '2' && buf[i + 1] == '0' && buf[i + 2] >= '1' && buf[i + 2] <= '9') ||
            (buf[i] == '6' && buf[i + 1] == '0' && buf[i + 2] == '0' && buf[i + 3] == '0')) {
            std::streamsize null_pos = -1;
            for (std::streamsize j = i; j < i + 32 && j < bytes_read; ++j) {
                if (buf[j] == '\0') { null_pos = j; break; }
            }
            if (null_pos == -1) continue;
            std::streamsize ver_len = null_pos - i;
            if (ver_len < 7 || ver_len > 24) continue;

            bool valid_ver = true;
            for (std::streamsize j = i; j < null_pos; ++j) {
                char c = buf[j];
                if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '.')) {
                    valid_ver = false;
                    break;
                }
            }
            if (!valid_ver) continue;

            std::streamsize plat_pos = null_pos + 1;
            if (plat_pos + 5 > bytes_read) continue;

            uint8_t enable_type_tree = static_cast<uint8_t>(buf[plat_pos + 4]);
            if (enable_type_tree > 1) continue;

            int32_t target_platform = 13; // BuildTarget.Android
            file.clear();
            file.seekp(plat_pos);
            file.write(reinterpret_cast<const char*>(&target_platform), sizeof(target_platform));
            file.flush();
            return true;
        }
    }
    return false;
}

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
