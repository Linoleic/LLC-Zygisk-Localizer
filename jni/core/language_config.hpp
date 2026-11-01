#pragma once

#include <string>
#include <fstream>
#include <filesystem>
#include <unistd.h>
#include "../third_party/nlohmann/json.hpp"

namespace language_config {

using Json = nlohmann::json;

constexpr const char* config_path = "/data/user/0/com.ProjectMoon.LimbusCompany/files/language_config.json";

// Limbus Company LOCALIZE_LANGUAGE enum mappings
enum class GameLanguage : int {
    Unknown = -1,
    KR = 0,
    EN = 1,
    JP = 2
};

inline int lang_code_to_int(const std::string& code) {
    if (code == "kr" || code == "KR" || code == "korean") return 0;
    if (code == "en" || code == "EN" || code == "english") return 1;
    if (code == "jp" || code == "JP" || code == "japanese") return 2;
    return -1;
}

inline std::string int_to_lang_code(int id) {
    switch (id) {
        case 0: return "kr";
        case 1: return "en";
        case 2: return "jp";
        default: return "unknown";
    }
}

inline std::string int_to_lang_name(int id) {
    switch (id) {
        case 0: return "韩语 (KR)";
        case 1: return "英语 (EN)";
        case 2: return "日语 (JP)";
        default: return "未知";
    }
}

struct LanguageSelection {
    std::string mode = "all";           // "all" (全部替换) or "selective" (仅替换特定语言)
    std::string target_language = "kr"; // "kr", "en", "jp"
};

inline LanguageSelection resolve_language_selection(const std::string& custom_config_file = config_path) {
    LanguageSelection result; // defaults to "all", "kr"

    std::error_code ec;
    if (!std::filesystem::is_regular_file(custom_config_file, ec) || access(custom_config_file.c_str(), R_OK) != 0) {
        return result;
    }

    try {
        std::ifstream f(custom_config_file);
        if (!f.is_open()) return result;

        Json j = Json::parse(f, nullptr, false);
        if (j.is_discarded() || !j.is_object()) return result;

        if (j.contains("mode") && j["mode"].is_string()) {
            std::string m = j["mode"].get<std::string>();
            if (m == "all" || m == "selective") {
                result.mode = m;
            }
        }

        if (j.contains("target_language") && j["target_language"].is_string()) {
            std::string lang = j["target_language"].get<std::string>();
            if (lang == "kr" || lang == "en" || lang == "jp") {
                result.target_language = lang;
            }
        }
    } catch (...) {
        // Any parse failure safely falls back to defaults
    }

    return result;
}

inline bool should_apply_localization(const LanguageSelection& sel, int current_game_language) {
    if (sel.mode == "all") {
        return true;
    }
    // If game language is unknown (-1), allow localization by default so initialization isn't stalled
    if (current_game_language < 0) {
        return true;
    }
    return lang_code_to_int(sel.target_language) == current_game_language;
}

} // namespace language_config
