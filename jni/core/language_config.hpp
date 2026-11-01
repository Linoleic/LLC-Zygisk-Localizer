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

inline int parse_language_from_playerprefs_content(const std::string& content) {
    if (content.empty()) return -1;
    // Look for LocalSave.LocalGameOptionData
    size_t opt_pos = content.find("LocalSave.LocalGameOptionData");
    if (opt_pos == std::string::npos) {
        opt_pos = 0;
    }
    const char* patterns[] = {
        "%22_language%22%3A",
        "%22_language%22:",
        "\"_language\":",
        "\"_language\" :"
    };
    for (const char* pat : patterns) {
        size_t match = content.find(pat, opt_pos);
        if (match != std::string::npos) {
            size_t idx = match + strlen(pat);
            while (idx < content.size() && (content[idx] == ' ' || content[idx] == '\t')) {
                idx++;
            }
            if (idx < content.size() && content[idx] >= '0' && content[idx] <= '9') {
                return content[idx] - '0';
            }
        }
    }
    return -1;
}

inline int query_language_from_playerprefs(const std::string& custom_prefs_path = "") {
    if (!custom_prefs_path.empty()) {
        std::ifstream f(custom_prefs_path);
        if (f.is_open()) {
            std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            return parse_language_from_playerprefs_content(content);
        }
        return -1;
    }

    const char* paths[] = {
        "/data/data/com.ProjectMoon.LimbusCompany/shared_prefs/com.ProjectMoon.LimbusCompany.v2.playerprefs.xml",
        "/data/user/0/com.ProjectMoon.LimbusCompany/shared_prefs/com.ProjectMoon.LimbusCompany.v2.playerprefs.xml"
    };
    for (const char* path : paths) {
        std::ifstream f(path);
        if (!f.is_open()) continue;
        std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        int lang = parse_language_from_playerprefs_content(content);
        if (lang >= 0) return lang;
    }
    return -1;
}

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
    // If game language is unknown (-1), selective mode defaults to true only if the user explicitly targeted "kr"
    // (since Korean is Limbus Company's native fallback/first-boot language).
    // Never allow selective mode to forcefully inject onto undetected foreign languages (EN/JP).
    if (current_game_language < 0) {
        return sel.target_language == "kr";
    }
    return lang_code_to_int(sel.target_language) == current_game_language;
}

} // namespace language_config
