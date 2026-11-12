#include "ui_overrides.hpp"
#include "storage.hpp"
#include "security_boundary.hpp"
#include "../../third_party/nlohmann/json.hpp"
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sys/stat.h>
#include <time.h>
#include <unordered_map>
#include <vector>
#include <string>
#include <mutex>

#ifndef LOGI
#ifdef ANDROID
#include <android/log.h>
#define LOG_TAG_CORE "LLC_CORE"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG_CORE, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG_CORE, __VA_ARGS__)
#else
#define LOGI(...) ((void)0)
#define LOGE(...) ((void)0)
#endif
#endif

namespace {

struct UIOverrideData {
    bool force_font_unify = false;
    std::unordered_map<std::string, std::string> direct_strings;
    std::vector<std::pair<std::string, std::string>> direct_paths;
    std::unordered_map<std::string, std::string> key_strings;
    std::vector<std::pair<std::string, std::string>> key_paths;
};

static std::mutex s_override_mutex;
static UIOverrideData s_ui_overrides;
static time_t s_last_base_mtime = 0;
static time_t s_last_custom_mtime = 0;
static time_t s_last_check_time = 0;
static std::string s_active_base_path;
static std::string s_active_custom_path;

void parse_override_json(const nlohmann::json& j, UIOverrideData& out, bool is_custom) {
    if (!j.is_object()) return;
    if (j.contains("force_font_unify") && j["force_font_unify"].is_boolean()) {
        out.force_font_unify = j["force_font_unify"].get<bool>();
    }

    auto parse_string_map = [&](const nlohmann::json& node) {
        if (!node.is_object()) return;
        for (auto& item : node.items()) {
            if (item.value().is_string()) {
                out.direct_strings[item.key()] = item.value().get<std::string>();
            }
        }
    };
    auto parse_path_map = [&](const nlohmann::json& node) {
        if (!node.is_object()) return;
        for (auto& item : node.items()) {
            if (item.value().is_string()) {
                if (is_custom) {
                    out.direct_paths.insert(out.direct_paths.begin(), {item.key(), item.value().get<std::string>()});
                } else {
                    out.direct_paths.emplace_back(item.key(), item.value().get<std::string>());
                }
            }
        }
    };

    // Approach A: concise missing_strings / direct_strings
    if (j.contains("missing_strings")) parse_string_map(j["missing_strings"]);
    if (j.contains("direct_strings")) parse_string_map(j["direct_strings"]);
    if (j.contains("strings")) parse_string_map(j["strings"]);
    if (j.contains("paths")) parse_path_map(j["paths"]);

    // Legacy direct_replacements / key_mappings
    if (j.contains("direct_replacements") && j["direct_replacements"].is_object()) {
        const auto& dr = j["direct_replacements"];
        if (dr.contains("strings")) parse_string_map(dr["strings"]);
        if (dr.contains("paths")) parse_path_map(dr["paths"]);
    }
    if (j.contains("key_mappings") && j["key_mappings"].is_object()) {
        const auto& km = j["key_mappings"];
        if (km.contains("strings") && km["strings"].is_object()) {
            for (auto& item : km["strings"].items()) {
                if (item.value().is_string()) out.key_strings[item.key()] = item.value().get<std::string>();
            }
        }
        if (km.contains("paths") && km["paths"].is_object()) {
            for (auto& item : km["paths"].items()) {
                if (item.value().is_string()) {
                    if (is_custom) out.key_paths.insert(out.key_paths.begin(), {item.key(), item.value().get<std::string>()});
                    else out.key_paths.emplace_back(item.key(), item.value().get<std::string>());
                }
            }
        }
    }
}

struct BuiltinPathMapping {
    const char* path_substring;
    const char* ui_key;
    const char* default_text;
};

static const BuiltinPathMapping s_builtin_path_mappings[] = {
    { "Button_SwitchAccount", "switch_account", "切换账号" },
    { "Button_AllCacheClear", "switch_allcacheclear", "清除所有缓存" },
    { "Button_OldCacheClear", "switch_oldcacheclear", "清除旧缓存" },
    { "Button_Guest", "loginui_sign_in_with_guest", "游客登录" },
    { "Btn_Guest", "loginui_sign_in_with_guest", "游客登录" },
    { "Button_Google", "loginui_sign_in_with_google", "Google登录" },
    { "Button_Apple", "loginui_sign_in_with_apple", "Apple登录" },
    { "Button_Steam", "loginui_sign_in_with_steam", "Steam登录" },
    { "Button_Logout", "loginui_sign_out_current_account", "登出" },
    { nullptr, nullptr, nullptr }
};

struct BuiltinStringMapping {
    const char* pattern;       // Matched against trimmed original text (case-insensitive)
    const char* ui_key;        // Key in g_ui_map (from LoginUIText.json), or empty if unkeyed
    const char* default_text;  // Default Chinese translation
};

static const BuiltinStringMapping s_builtin_string_mappings[] = {
    // Missing strings (not keyed in standard LoginUIText.json)
    { "touch to start", "", "点击开始" },
    { "터치하여 시작", "", "点击开始" },
    { "터치하여 시작하기", "", "点击开始" },
    { "タップしてスタート", "", "点击开始" },
    { "画面をタップしてスタート", "", "点击开始" },

    { "customer support", "", "客服中心" },
    { "customer service", "", "客服中心" },
    { "support", "", "客服中心" },
    { "고객센터", "", "客服中心" },
    { "고객 센터", "", "客服中心" },
    { "문의하기", "", "客服中心" },
    { "サポート", "", "客服中心" },
    { "カスタマーサポート", "", "客服中心" },
    { "お問い合わせ", "", "客服中心" },

    { "terms of service", "user_agreements", "服务条款" },
    { "terms of use", "user_agreements", "服务条款" },
    { "user agreement", "user_agreements", "服务条款" },
    { "이용약관", "user_agreements", "服务条款" },
    { "이용 약관", "user_agreements", "服务条款" },
    { "利用規約", "user_agreements", "服务条款" },

    { "privacy policy", "", "隐私政策" },
    { "개인정보 처리방침", "", "隐私政策" },
    { "プライバシーポリシー", "", "隐私政策" },

    { "download all", "", "全部下载" },
    { "전체 다운로드", "", "全部下载" },
    { "전체다운로드", "", "全部下载" },
    { "一括ダウンロード", "", "全部下载" },

    { "notice", "", "公告" },
    { "공지사항", "", "公告" },
    { "공지 사항", "", "公告" },
    { "お知らせ", "", "公告" },

    // Standard Login & Account actions (keyed in LoginUIText.json)
    { "switch account", "switch_account", "切换账号" },
    { "change account", "switch_account", "切换账号" },
    { "계정 변경", "switch_account", "切换账号" },
    { "계정변경", "switch_account", "切换账号" },
    { "계정 전환", "switch_account", "切换账号" },
    { "アカウント変更", "switch_account", "切换账号" },
    { "アカウント切り替え", "switch_account", "切换账号" },
    { "アカウント切替", "switch_account", "切换账号" },

    { "clear all caches", "switch_allcacheclear", "清除所有缓存" },
    { "clear all cache", "switch_allcacheclear", "清除所有缓存" },
    { "전체 캐시 삭제", "switch_allcacheclear", "清除所有缓存" },
    { "전체 캐시를 삭제", "switch_allcacheclear", "清除所有缓存" },
    { "전체캐시삭제", "switch_allcacheclear", "清除所有缓存" },
    { "모든 캐시 삭제", "switch_allcacheclear", "清除所有缓存" },
    { "全キャッシュを削除", "switch_allcacheclear", "清除所有缓存" },
    { "全キャッシュ削除", "switch_allcacheclear", "清除所有缓存" },
    { "すべてのキャッシュを削除", "switch_allcacheclear", "清除所有缓存" },

    { "clear old caches", "switch_oldcacheclear", "清除旧缓存" },
    { "clear old cache", "switch_oldcacheclear", "清除旧缓存" },
    { "이전 캐시 삭제", "switch_oldcacheclear", "清除旧缓存" },
    { "옛날 캐시 삭제", "switch_oldcacheclear", "清除旧缓存" },
    { "旧キャッシュ削除", "switch_oldcacheclear", "清除旧缓存" },
    { "古いキャッシュの削除", "switch_oldcacheclear", "清除旧缓存" },

    { "sign in with google", "loginui_sign_in_with_google", "Google登录" },
    { "sign in with google account", "loginui_sign_in_with_google", "Google登录" },
    { "google 계정으로 로그인", "loginui_sign_in_with_google", "Google登录" },
    { "google로 로그인", "loginui_sign_in_with_google", "Google登录" },
    { "googleアカウントでログイン", "loginui_sign_in_with_google", "Google登录" },

    { "sign in with apple", "loginui_sign_in_with_apple", "Apple登录" },
    { "sign in with apple account", "loginui_sign_in_with_apple", "Apple登录" },
    { "apple 계정으로 로그인", "loginui_sign_in_with_apple", "Apple登录" },
    { "apple로 로그인", "loginui_sign_in_with_apple", "Apple登录" },
    { "appleアカウントでログイン", "loginui_sign_in_with_apple", "Apple登录" },

    { "sign in with steam", "loginui_sign_in_with_steam", "Steam登录" },
    { "steam 계정으로 로그인", "loginui_sign_in_with_steam", "Steam登录" },
    { "steam으로 로그인", "loginui_sign_in_with_steam", "Steam登录" },
    { "steamアカウントでログイン", "loginui_sign_in_with_steam", "Steam登录" },

    { "sign in as guest", "loginui_sign_in_with_guest", "游客登录" },
    { "sign in as a guest", "loginui_sign_in_with_guest", "游客登录" },
    { "sign in with guest", "loginui_sign_in_with_guest", "游客登录" },
    { "sign in as guest account", "loginui_sign_in_with_guest", "游客登录" },
    { "guest login", "loginui_sign_in_with_guest", "游客登录" },
    { "log in as guest", "loginui_sign_in_with_guest", "游客登录" },
    { "login as guest", "loginui_sign_in_with_guest", "游客登录" },
    { "continue as guest", "loginui_sign_in_with_guest", "游客登录" },
    { "play as guest", "loginui_sign_in_with_guest", "游客登录" },
    { "guest account", "loginui_sign_in_with_guest", "游客登录" },
    { "게스트 로그인", "loginui_sign_in_with_guest", "游客登录" },
    { "게스트로 로그인", "loginui_sign_in_with_guest", "游客登录" },
    { "게스트 계정으로 로그인", "loginui_sign_in_with_guest", "游客登录" },
    { "게스트로 계속하기", "loginui_sign_in_with_guest", "游客登录" },
    { "ゲストログイン", "loginui_sign_in_with_guest", "游客登录" },
    { "ゲストでログイン", "loginui_sign_in_with_guest", "游客登录" },

    { "logout", "loginui_sign_out_current_account", "登出" },
    { "log out", "loginui_sign_out_current_account", "登出" },
    { "sign out", "loginui_sign_out_current_account", "登出" },
    { "로그아웃", "loginui_sign_out_current_account", "登出" },
    { "ログアウト", "loginui_sign_out_current_account", "登出" },

    { "combat tips", "loginui_loading_battlehint", "战斗小贴士" },
    { "combat tip", "loginui_loading_battlehint", "战斗小贴士" },
    { "battle hint", "loginui_loading_battlehint", "战斗小贴士" },
    { "battle tips", "loginui_loading_battlehint", "战斗小贴士" },
    { "tips", "loginui_loading_battlehint", "战斗小贴士" },
    { "전투 팁", "loginui_loading_battlehint", "战斗小贴士" },
    { "전투팁", "loginui_loading_battlehint", "战斗小贴士" },
    { "팁", "loginui_loading_battlehint", "战斗小贴士" },
    { "ヒント", "loginui_loading_battlehint", "战斗小贴士" },

    { "a server error has occurred.", "servererror_content", "发生服务器错误。\n请稍后重试。" },
    { "서버 오류가 발생했습니다.", "servererror_content", "发生服务器错误。\n请稍后重试。" },
    { "サーバーエラーが発生しました。", "servererror_content", "发生服务器错误。\n请稍后重试。" },

    { nullptr, nullptr, nullptr }
};

inline std::string resolve_ui_target(const char* ui_key, const char* default_text) {
    if (ui_key && ui_key[0] != '\0') {
        const auto snapshot = ui_translation_snapshot();
        if (!snapshot) return default_text ? default_text : "";
        auto it = snapshot->find(ui_key);
        if (it != snapshot->end() && !it->second.empty()) {
            return it->second;
        }
    }
    return (default_text && default_text[0] != '\0') ? default_text : "";
}

} // namespace

std::string trim_copy(const std::string& s) {
    size_t start = 0;
    while (start < s.size() && (s[start] == ' ' || s[start] == '\t' || s[start] == '\r' || s[start] == '\n')) {
        start++;
    }
    if (start >= s.size()) return "";
    size_t end = s.size() - 1;
    while (end > start && (s[end] == ' ' || s[end] == '\t' || s[end] == '\r' || s[end] == '\n')) {
        end--;
    }
    return s.substr(start, end - start + 1);
}

std::string to_lower_ascii(const std::string& s) {
    std::string res = s;
    for (char& c : res) {
        if (c >= 'A' && c <= 'Z') c = (char)(c + ('a' - 'A'));
    }
    return res;
}

bool is_title_or_login_obj_path(const std::string& path) {
    if (path.empty()) return false;

    // Explicit blacklist of known in-game UI hierarchies that must NEVER be touched by title screen overrides
    if (path.find("Manual") != std::string::npos ||
        path.find("Tutorial") != std::string::npos ||
        path.find("Formation") != std::string::npos ||
        path.find("Gacha") != std::string::npos ||
        path.find("Inventory") != std::string::npos ||
        path.find("Railway") != std::string::npos ||
        path.find("StageMap") != std::string::npos ||
        path.find("Dungeon") != std::string::npos ||
        path.find("MainLobby") != std::string::npos ||
        path.find("MainUI") != std::string::npos ||
        path.find("Battle") != std::string::npos ||
        path.find("Personality") != std::string::npos ||
        path.find("Story") != std::string::npos) {
        return false;
    }

    // Whitelist for Title Screen and Authentication / Login / Network UI
    if (path.find("LoginSceneManager") != std::string::npos ||
        path.find("Title") != std::string::npos ||
        path.find("title") != std::string::npos ||
        path.find("SwitchAccount") != std::string::npos ||
        path.find("Button_SwitchAccount") != std::string::npos ||
        path.find("Button_AllCacheClear") != std::string::npos ||
        path.find("ClearCache") != std::string::npos ||
        path.find("UserAgreement") != std::string::npos ||
        path.find("DownloadPopup") != std::string::npos ||
        path.find("DownloadUI") != std::string::npos ||
        path.find("NetworkingUI") != std::string::npos ||
        path.find("AddressableNetworkingUI") != std::string::npos ||
        path.find("APIRequester") != std::string::npos ||
        path.find("TextOkPopup") != std::string::npos) {
        return true;
    }

    return false;
}

bool is_cjk_codepoint(uint16_t c) {
    return (c >= 0x4E00 && c <= 0x9FFF) || // CJK Unified Ideographs
           (c >= 0x3400 && c <= 0x4DBF) || // CJK Extension A
           (c >= 0xF900 && c <= 0xFAFF) || // CJK Compatibility
           (c >= 0x3000 && c <= 0x303F) || // CJK Symbols and Punctuation
           (c >= 0xFF00 && c <= 0xFFEF);   // Halfwidth and Fullwidth Forms
}

bool utf8_has_cjk(const std::string& str) {
    for (size_t i = 0; i < str.size(); ) {
        unsigned char c = (unsigned char)str[i];
        if (c < 0x80) {
            i += 1;
        } else if ((c & 0xE0) == 0xC0) {
            i += 2;
        } else if ((c & 0xF0) == 0xE0) {
            if (i + 2 < str.size()) {
                uint16_t code = (uint16_t)(((c & 0x0F) << 12) |
                                          (((unsigned char)str[i+1] & 0x3F) << 6) |
                                          ((unsigned char)str[i+2] & 0x3F));
                if (is_cjk_codepoint(code)) return true;
            }
            i += 3;
        } else {
            i += 4;
        }
    }
    return false;
}

bool is_cjk_body_font_name(const char* fnbuf) {
    if (!fnbuf || !*fnbuf) return false;
    return (strstr(fnbuf, "Pretendard") ||
            strstr(fnbuf, "HigashiOme") ||
            strstr(fnbuf, "Corporate") ||
            strstr(fnbuf, "SCDream") ||
            strstr(fnbuf, "NotoSans") ||
            strstr(fnbuf, "KOTRA") ||
            strstr(fnbuf, "Mary_Jane") ||
            strstr(fnbuf, "ZenAntique") ||
            strstr(fnbuf, "HinaMincho"));
}

void reset_ui_overrides() {
    std::lock_guard<std::mutex> lock(s_override_mutex);
    s_ui_overrides = UIOverrideData{};
    s_last_base_mtime = 0;
    s_last_custom_mtime = 0;
    s_last_check_time = 0;
    s_active_base_path.clear();
    s_active_custom_path.clear();
}

void load_ui_overrides_from_json_string(const std::string& json_str, bool is_custom) {
    std::lock_guard<std::mutex> lock(s_override_mutex);
    try {
        nlohmann::json j = nlohmann::json::parse(json_str, nullptr, false);
        if (!j.is_discarded() && j.is_object()) {
            parse_override_json(j, s_ui_overrides, is_custom);
        }
    } catch (...) {}
}

void check_and_reload_ui_overrides() {
    time_t now = time(nullptr);
    if (now - s_last_check_time < 2) return;
    s_last_check_time = now;

    static const char* base_candidate_paths[] = {
        "/data/user/0/com.ProjectMoon.LimbusCompany/files/Lang/static_overrides.json",
        "/data/data/com.ProjectMoon.LimbusCompany/files/Lang/static_overrides.json",
        "/data/user/0/com.ProjectMoon.LimbusCompany/files/Lang/LLC_zh-CN/static_overrides.json",
        "/data/data/com.ProjectMoon.LimbusCompany/files/Lang/LLC_zh-CN/static_overrides.json",
        "/data/user/0/com.ProjectMoon.LimbusCompany/files/static_overrides.json",
        "/data/data/com.ProjectMoon.LimbusCompany/files/static_overrides.json",
        "/data/adb/modules/llc_zygisk/config/static_overrides.json",
        "/data/adb/modules/llc_zygisk/static_overrides.json",
        "/data/local/tmp/llc/static_overrides.json",
        nullptr
    };

    static const char* custom_candidate_paths[] = {
        "/data/user/0/com.ProjectMoon.LimbusCompany/files/Lang/custom_overrides.json",
        "/data/data/com.ProjectMoon.LimbusCompany/files/Lang/custom_overrides.json",
        "/sdcard/Android/data/com.ProjectMoon.LimbusCompany/files/custom_overrides.json",
        "/data/adb/modules/llc_zygisk/custom_overrides.json",
        nullptr
    };

    const char* found_base = nullptr;
    struct stat base_st;
    for (int i = 0; base_candidate_paths[i]; ++i) {
        const char* p = base_candidate_paths[i];
        if (stat(p, &base_st) == 0 && S_ISREG(base_st.st_mode)) {
            if (!llc::security::is_safe_file_or_candidate(p, "/data/local/tmp/")) continue;
            found_base = p;
            break;
        }
    }

    const char* found_custom = nullptr;
    struct stat custom_st;
    for (int i = 0; custom_candidate_paths[i]; ++i) {
        if (stat(custom_candidate_paths[i], &custom_st) == 0 && S_ISREG(custom_st.st_mode)) {
            found_custom = custom_candidate_paths[i];
            break;
        }
    }

    std::lock_guard<std::mutex> lock(s_override_mutex);
    bool base_changed = found_base ? (s_active_base_path != found_base || base_st.st_mtime != s_last_base_mtime) : !s_active_base_path.empty();
    bool custom_changed = found_custom ? (s_active_custom_path != found_custom || custom_st.st_mtime != s_last_custom_mtime) : !s_active_custom_path.empty();

    if (!base_changed && !custom_changed) return;

    UIOverrideData merged;
    if (found_base) {
        try {
            std::ifstream f(found_base);
            if (f.is_open()) {
                nlohmann::json j = nlohmann::json::parse(f, nullptr, false);
                if (!j.is_discarded() && j.is_object()) {
                    parse_override_json(j, merged, false);
                    s_active_base_path = found_base;
                    s_last_base_mtime = base_st.st_mtime;
                }
            }
        } catch (...) {}
    } else {
        s_active_base_path.clear();
        s_last_base_mtime = 0;
    }

    if (found_custom) {
        try {
            std::ifstream f(found_custom);
            if (f.is_open()) {
                nlohmann::json j = nlohmann::json::parse(f, nullptr, false);
                if (!j.is_discarded() && j.is_object()) {
                    parse_override_json(j, merged, true);
                    s_active_custom_path = found_custom;
                    s_last_custom_mtime = custom_st.st_mtime;
                }
            }
        } catch (...) {}
    } else {
        s_active_custom_path.clear();
        s_last_custom_mtime = 0;
    }

    s_ui_overrides = std::move(merged);
    LOGI("[LLC-OVERRIDE] Synced UI overrides (base: %s, custom: %s) -> %zu direct_strings, %zu key_strings, %zu direct_paths, %zu key_paths, force_font_unify=%s",
         found_base ? found_base : "none",
         found_custom ? found_custom : "none",
         s_ui_overrides.direct_strings.size(),
         s_ui_overrides.key_strings.size(),
         s_ui_overrides.direct_paths.size(),
         s_ui_overrides.key_paths.size(),
         s_ui_overrides.force_font_unify ? "true" : "false");
}

bool is_force_font_unify_enabled() {
    std::lock_guard<std::mutex> lock(s_override_mutex);
    return s_ui_overrides.force_font_unify;
}

bool evaluate_ui_replacement(const std::string& obj_path, const std::string& utf8_text, std::string& out_replacement) {
    std::string trimmed_text = trim_copy(utf8_text);
    std::string lower_trimmed = to_lower_ascii(trimmed_text);

    bool text_replaced = false;
    std::string target_replacement;

    auto lookup_ui_key = [](const std::string& key) -> std::string {
        if (key.empty()) return "";
        const auto snapshot = ui_translation_snapshot();
        if (!snapshot) return {};
        auto it = snapshot->find(key);
        if (it != snapshot->end() && !it->second.empty()) {
            return it->second;
        }
        return "";
    };

    std::lock_guard<std::mutex> lock(s_override_mutex);

    // 1. Direct path overrides (configured in static_overrides.json or custom_overrides.json)
    bool path_matched = false;
    for (const auto& p : s_ui_overrides.direct_paths) {
        if (!p.first.empty() && obj_path.find(p.first) != std::string::npos) {
            path_matched = true;
            if (trimmed_text != p.second && utf8_text != p.second) {
                target_replacement = p.second;
                text_replaced = true;
            }
            break;
        }
    }

    // 2. Direct string overrides (raw text -> translated text) - EXACT TRIMMED MATCH or CASE-INSENSITIVE
    bool string_matched = false;
    if (!text_replaced && !path_matched && !trimmed_text.empty()) {
        auto it = s_ui_overrides.direct_strings.find(trimmed_text);
        if (it != s_ui_overrides.direct_strings.end()) {
            string_matched = true;
            if (trimmed_text != it->second) {
                target_replacement = it->second;
                text_replaced = true;
            }
        } else {
            for (const auto& kv : s_ui_overrides.direct_strings) {
                if (lower_trimmed == to_lower_ascii(kv.first)) {
                    string_matched = true;
                    if (trimmed_text != kv.second) {
                        target_replacement = kv.second;
                        text_replaced = true;
                    }
                    break;
                }
            }
        }
    }

    // 3. Key-mapped overrides from JSON
    if (!text_replaced && !path_matched) {
        for (const auto& kp : s_ui_overrides.key_paths) {
            if (!kp.first.empty() && obj_path.find(kp.first) != std::string::npos) {
                path_matched = true;
                std::string mapped = lookup_ui_key(kp.second);
                if (!mapped.empty() && trimmed_text != mapped && utf8_text != mapped) {
                    target_replacement = mapped;
                    text_replaced = true;
                }
                break;
            }
        }
    }
    if (!text_replaced && !path_matched && !string_matched && !trimmed_text.empty()) {
        for (const auto& kv : s_ui_overrides.key_strings) {
            if (lower_trimmed == to_lower_ascii(kv.first)) {
                string_matched = true;
                std::string mapped = lookup_ui_key(kv.second);
                if (!mapped.empty() && trimmed_text != mapped) {
                    target_replacement = mapped;
                    text_replaced = true;
                }
                break;
            }
        }
    }

    // 4. Built-in path mappings (resolves via LoginUIText g_ui_map, with embedded fallback)
    if (!text_replaced && !path_matched) {
        for (int r = 0; s_builtin_path_mappings[r].path_substring; ++r) {
            if (obj_path.find(s_builtin_path_mappings[r].path_substring) != std::string::npos) {
                path_matched = true;
                std::string resolved = resolve_ui_target(s_builtin_path_mappings[r].ui_key, s_builtin_path_mappings[r].default_text);
                if (!resolved.empty() && trimmed_text != resolved && utf8_text != resolved) {
                    target_replacement = resolved;
                    text_replaced = true;
                }
                break;
            }
        }
    }

    // 5. Built-in string mappings (resolves via LoginUIText g_ui_map, with embedded fallback)
    if (!text_replaced && !path_matched && !string_matched && !trimmed_text.empty()) {
        for (int r = 0; s_builtin_string_mappings[r].pattern; ++r) {
            if (lower_trimmed == s_builtin_string_mappings[r].pattern || trimmed_text == s_builtin_string_mappings[r].pattern) {
                string_matched = true;
                std::string resolved = resolve_ui_target(s_builtin_string_mappings[r].ui_key, s_builtin_string_mappings[r].default_text);
                if (!resolved.empty() && trimmed_text != resolved && utf8_text != resolved) {
                    target_replacement = resolved;
                    text_replaced = true;
                }
                break;
            }
        }
    }

    // 6. Fallback fuzzy heuristics for Auth & Login buttons (resolving via LoginUIText keys)
    if (!text_replaced && !trimmed_text.empty()) {
        if (obj_path.find("accountType") != std::string::npos) {
            // Do not replace account type label badge with login button text
        } else if (lower_trimmed.find("guest") != std::string::npos) {
            if (lower_trimmed.find("sign") != std::string::npos ||
                lower_trimmed.find("login") != std::string::npos ||
                lower_trimmed.find("log in") != std::string::npos ||
                lower_trimmed.find("continue") != std::string::npos ||
                lower_trimmed.find("play") != std::string::npos ||
                (lower_trimmed == "guest" && (obj_path.find("Button") != std::string::npos || obj_path.find("Btn") != std::string::npos))) {
                target_replacement = resolve_ui_target("loginui_sign_in_with_guest", "游客登录");
                text_replaced = true;
            }
        } else if (lower_trimmed.find("google") != std::string::npos &&
            (lower_trimmed.find("sign") != std::string::npos || lower_trimmed.find("login") != std::string::npos || lower_trimmed.find("log in") != std::string::npos || lower_trimmed.find("account") != std::string::npos)) {
            target_replacement = resolve_ui_target("loginui_sign_in_with_google", "Google登录");
            text_replaced = true;
        } else if (lower_trimmed.find("apple") != std::string::npos &&
            (lower_trimmed.find("sign") != std::string::npos || lower_trimmed.find("login") != std::string::npos || lower_trimmed.find("log in") != std::string::npos || lower_trimmed.find("account") != std::string::npos)) {
            target_replacement = resolve_ui_target("loginui_sign_in_with_apple", "Apple登录");
            text_replaced = true;
        } else if (lower_trimmed.find("steam") != std::string::npos &&
            (lower_trimmed.find("sign") != std::string::npos || lower_trimmed.find("login") != std::string::npos || lower_trimmed.find("log in") != std::string::npos || lower_trimmed.find("account") != std::string::npos)) {
            target_replacement = resolve_ui_target("loginui_sign_in_with_steam", "Steam登录");
            text_replaced = true;
        }
    }

    if (text_replaced && !target_replacement.empty()) {
        out_replacement = std::move(target_replacement);
        return true;
    }

    return false;
}

bool is_title_auth_element(const std::string& obj_path, const std::string& trimmed_text) {
    if (obj_path.find("Button_SwitchAccount") != std::string::npos ||
        obj_path.find("Button_AllCacheClear") != std::string::npos ||
        obj_path.find("SwitchAccount") != std::string::npos ||
        obj_path.find("UserAgreement") != std::string::npos ||
        obj_path.find("ClearCache") != std::string::npos) {
        return true;
    }
    if (trimmed_text == "切换账号" || trimmed_text == "清除所有缓存" ||
        trimmed_text == "Google登录" || trimmed_text == "Apple登录" ||
        trimmed_text == "以游客登录" || trimmed_text == "游客登录" ||
        trimmed_text == "Steam登录" || trimmed_text == "点击开始" ||
        trimmed_text == "TOUCH TO START") {
        return true;
    }
    return false;
}
