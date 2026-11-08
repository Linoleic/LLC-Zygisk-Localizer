#include "font_injector.hpp"
#include "font_config.hpp"
#include "managed_fields.hpp"
#include "string_table_patcher.hpp"
#include "storage.hpp"
#include "../../third_party/nlohmann/json.hpp"
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sys/stat.h>
#include <time.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>
#include <atomic>
#include "main_thread_bridge.hpp"

Il2CppObject* g_shs_font = nullptr;
Il2CppObject* g_shs_material = nullptr;
Il2CppObject* g_shs_atlas = nullptr;
Il2CppObject* g_empty_font = nullptr;
Il2CppObject* g_empty_material = nullptr;

struct UIOverrideData {
    bool force_font_unify = false;
    std::unordered_map<std::string, std::string> direct_strings;
    std::vector<std::pair<std::string, std::string>> direct_paths;
    std::unordered_map<std::string, std::string> key_strings;
    std::vector<std::pair<std::string, std::string>> key_paths;
};

static UIOverrideData s_ui_overrides;
static time_t s_last_base_mtime = 0;
static time_t s_last_custom_mtime = 0;
static time_t s_last_check_time = 0;
static std::string s_active_base_path;
static std::string s_active_custom_path;

static void parse_override_json(const nlohmann::json& j, UIOverrideData& out, bool is_custom) {
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

static void check_and_reload_ui_overrides() {
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
        if (stat(base_candidate_paths[i], &base_st) == 0 && S_ISREG(base_st.st_mode)) {
            found_base = base_candidate_paths[i];
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

static inline std::string trim_copy(const std::string& s) {
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

static inline std::string to_lower_ascii(const std::string& s) {
    std::string res = s;
    for (char& c : res) {
        if (c >= 'A' && c <= 'Z') c = (char)(c + ('a' - 'A'));
    }
    return res;
}

static inline bool is_title_or_login_obj_path(const std::string& path) {
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

static inline bool is_cjk_codepoint(uint16_t c) {
    return (c >= 0x4E00 && c <= 0x9FFF) || // CJK Unified Ideographs
           (c >= 0x3400 && c <= 0x4DBF) || // CJK Extension A
           (c >= 0xF900 && c <= 0xFAFF) || // CJK Compatibility
           (c >= 0x3000 && c <= 0x303F) || // CJK Symbols and Punctuation
           (c >= 0xFF00 && c <= 0xFFEF);   // Halfwidth and Fullwidth Forms
}

static inline bool utf8_has_cjk(const std::string& str) {
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

static inline bool is_cjk_body_font_name(const char* fnbuf) {
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

static void log_exception(const char* context, Il2CppException* exc) {
    if (!exc) return;
    Il2CppClass* exc_class = il2cpp_object_get_class_fn((Il2CppObject*)exc);
    const char* exc_name = exc_class ? il2cpp_class_get_name_fn(exc_class) : "UnknownException";
    const MethodInfo* to_string_mi = find_method_in_hierarchy(exc_class, "ToString", 0);
    Il2CppException* sub_exc = nullptr;
    Il2CppObject* str_obj = to_string_mi ? il2cpp_runtime_invoke_fn(to_string_mi, exc, nullptr, &sub_exc) : nullptr;
    char buf[512] = {0};
    if (str_obj) {
        const uint16_t* u16 = il2cpp_string_chars_fn((Il2CppString*)str_obj);
        for (int k = 0; k < 511 && u16[k]; ++k) buf[k] = (char)u16[k];
    }
    LOGE("[LLC-FONT] Exception in %s: [%s] %s", context, exc_name, buf);
}

static Il2CppString* get_pinned_ui_string(const std::string& text) {
    static std::mutex s_ui_str_mutex;
    static std::unordered_map<std::string, uintptr_t> s_ui_handles;
    if (!il2cpp_string_new_fn) return nullptr;

    std::lock_guard<std::mutex> lock(s_ui_str_mutex);
    auto it = s_ui_handles.find(text);
    if (it != s_ui_handles.end() && it->second && il2cpp_gchandle_get_target_fn) {
        Il2CppObject* obj = il2cpp_gchandle_get_target_fn(it->second);
        if (obj) return (Il2CppString*)obj;
    }

    Il2CppString* s = il2cpp_string_new_fn(text.c_str());
    if (!s) return nullptr;
    if (il2cpp_gchandle_new_fn) {
        uintptr_t h = il2cpp_gchandle_new_fn((Il2CppObject*)s, true);
        s_ui_handles[text] = h;
    }
    return s;
}

static void set_component_text_and_font(Il2CppObject* comp,
                                        const std::string& new_text,
                                        const MethodInfo* set_txt_mi,
                                        const MethodInfo* SetText_mi,
                                        const MethodInfo* set_font_mi,
                                        const MethodInfo* set_dirty_mi,
                                        const MethodInfo* force_mesh_update_mi) {
    Il2CppException* exc = nullptr;
    Il2CppString* new_s = get_pinned_ui_string(new_text);
    if (!new_s) return;

    void* st_args[1] = { new_s };
    if (SetText_mi) {
        il2cpp_runtime_invoke_fn(SetText_mi, comp, st_args, &exc);
        if (exc) {
            log_exception("SetText_mi", exc);
            exc = nullptr;
        }
    }
    if (set_txt_mi) {
        il2cpp_runtime_invoke_fn(set_txt_mi, comp, st_args, &exc);
        if (exc) {
            log_exception("set_txt_mi", exc);
            exc = nullptr;
        }
    }

    Il2CppClass* comp_k = il2cpp_object_get_class_fn(comp);
    void* m_text_field = comp_k ? il2cpp_class_get_field_from_name_fn(comp_k, "m_text") : nullptr;
    if (!m_text_field && comp_k) {
        for (Il2CppClass* pk = il2cpp_class_get_parent_fn(comp_k); pk; pk = il2cpp_class_get_parent_fn(pk)) {
            m_text_field = il2cpp_class_get_field_from_name_fn(pk, "m_text");
            if (m_text_field) break;
        }
    }
    if (m_text_field) {
        if (il2cpp_field_set_value_fn) {
            il2cpp_field_set_value_fn(comp, m_text_field, &new_s);
        }
        if (il2cpp_gc_wbarrier_set_field_fn) {
            uint32_t offset = il2cpp_field_get_offset_fn(m_text_field);
            il2cpp_gc_wbarrier_set_field_fn(comp, (void**)((char*)comp + offset), new_s);
        }
    }

    if (g_shs_font && set_font_mi && utf8_has_cjk(new_text)) {
        void* sf_args[1] = { g_shs_font };
        il2cpp_runtime_invoke_fn(set_font_mi, comp, sf_args, &exc);
    }

    if (set_dirty_mi) {
        il2cpp_runtime_invoke_fn(set_dirty_mi, comp, nullptr, &exc);
    }
    if (force_mesh_update_mi) {
        il2cpp_runtime_invoke_fn(force_mesh_update_mi, comp, nullptr, &exc);
    }
}

static std::unordered_set<Il2CppObject*> g_patched_fonts;



bool load_chinese_font_bundle(const char* font_bundle_path) {
    Il2CppClass* ab_class = nullptr;
    Il2CppClass* async_op_class = nullptr;
    Il2CppClass* font_asset_class = nullptr;

    for (int retry = 0; retry < 100; ++retry) {
        ab_class = find_class_any("UnityEngine", "AssetBundle");
        async_op_class = find_class_any("UnityEngine", "AsyncOperation");
        font_asset_class = find_class_any("TMPro", "TMP_FontAsset");
        if (ab_class && async_op_class && font_asset_class) break;
        usleep(100000);
    }

    if (!ab_class || !async_op_class) {
        LOGE("[LLC-FONT] Failed to resolve required classes (AssetBundle / AsyncOperation)!");
        return false;
    }

    void* ab_iter = nullptr;
    const MethodInfo* mi = nullptr;
    const MethodInfo* load_async_mi = nullptr;
    const MethodInfo* load_all_async_type_mi = nullptr;

    while ((mi = il2cpp_class_get_methods_fn(ab_class, &ab_iter))) {
        const char* mname = il2cpp_method_get_name_fn(mi);
        if (!mname) continue;
        uint32_t pcount = il2cpp_method_get_param_count_fn(mi);
        if (strcmp(mname, "LoadFromFileAsync") == 0 && pcount == 1) {
            load_async_mi = mi;
        } else if (strcmp(mname, "LoadAllAssetsAsync") == 0 && pcount == 1) {
            load_all_async_type_mi = mi;
        }
    }

    if (!load_async_mi || !load_all_async_type_mi) {
        LOGE("[LLC-FONT] Failed to resolve AssetBundle load methods!");
        return false;
    }

    const MethodInfo* is_done_mi = il2cpp_class_get_method_from_name_fn(async_op_class, "get_isDone", 0);
    if (!is_done_mi) {
        LOGE("[LLC-FONT] Failed to resolve AsyncOperation.get_isDone!");
        return false;
    }

    if (font_bundle_path) {
        font_config::adapt_assetbundle_to_android(font_bundle_path);
    }

    LOGI("[LLC-FONT] Loading Chinese Font AssetBundle from: %s", font_bundle_path);

    Il2CppString* font_path_str = il2cpp_string_new_fn(font_bundle_path);
    void* args[1] = { font_path_str };
    Il2CppException* exc = nullptr;
    Il2CppObject* req_obj = il2cpp_runtime_invoke_fn(load_async_mi, nullptr, args, &exc);

    if (!req_obj || exc) {
        log_exception("LoadFromFileAsync", exc);
        return false;
    }

    for (int retry = 0; retry < 150; ++retry) {
        Il2CppObject* res = il2cpp_runtime_invoke_fn(is_done_mi, req_obj, nullptr, &exc);
        if (res && !exc && *(bool*)il2cpp_object_unbox_fn(res)) break;
        usleep(50000);
    }

    Il2CppClass* req_class = il2cpp_object_get_class_fn(req_obj);
    const MethodInfo* get_ab_mi = req_class ? find_method_in_hierarchy(req_class, "get_assetBundle", 0) : nullptr;
    Il2CppObject* ab_obj = get_ab_mi ? il2cpp_runtime_invoke_fn(get_ab_mi, req_obj, nullptr, &exc) : nullptr;

    if (!ab_obj) {
        LOGE("[LLC-FONT] AssetBundle object is null after async load!");
        return false;
    }

    LOGI("[LLC-FONT] AssetBundle loaded: %p", ab_obj);
    Il2CppObject* assets_array_obj = nullptr;

    Il2CppClass* unity_obj_class = find_class_any("UnityEngine", "Object");
    const Il2CppType* obj_type = unity_obj_class ? il2cpp_class_get_type_fn(unity_obj_class) : nullptr;
    Il2CppObject* obj_type_obj = obj_type ? il2cpp_type_get_object_fn(obj_type) : nullptr;

    if (obj_type_obj) {
        void* abr_args[1] = { obj_type_obj };
        Il2CppObject* abr_obj = il2cpp_runtime_invoke_fn(load_all_async_type_mi, ab_obj, abr_args, &exc);
        if (exc) {
            log_exception("LoadAllAssetsAsync(Type)", exc);
        }
        if (abr_obj && !exc) {
            for (int retry = 0; retry < 150; ++retry) {
                Il2CppObject* res = il2cpp_runtime_invoke_fn(is_done_mi, abr_obj, nullptr, &exc);
                if (res && !exc && *(bool*)il2cpp_object_unbox_fn(res)) break;
                usleep(50000);
            }
            Il2CppClass* abr_class = il2cpp_object_get_class_fn(abr_obj);
            const MethodInfo* get_all_assets_mi = abr_class ? find_method_in_hierarchy(abr_class, "get_allAssets", 0) : nullptr;
            if (get_all_assets_mi) {
                assets_array_obj = il2cpp_runtime_invoke_fn(get_all_assets_mi, abr_obj, nullptr, &exc);
                if (exc) {
                    log_exception("get_allAssets", exc);
                }
            }
        }
    }

    if (!assets_array_obj) {
        LOGE("[LLC-FONT] Failed to retrieve assets array from font bundle!");
        return false;
    }

    uintptr_t asset_count = il2cpp_array_length_fn(assets_array_obj);
    const MethodInfo* dont_destroy_mi = unity_obj_class ? find_method_in_hierarchy(unity_obj_class, "DontDestroyOnLoad", 1) : nullptr;
    const MethodInfo* get_name_mi = unity_obj_class ? find_method_in_hierarchy(unity_obj_class, "get_name", 0) : nullptr;

    Il2CppObject* first_font_asset = nullptr;
    Il2CppObject* first_material = nullptr;
    Il2CppObject* first_texture = nullptr;

    LOGI("[LLC-FONT] Total assets loaded from font bundle: %lu", (unsigned long)asset_count);
    Il2CppObject** items = (Il2CppObject**)((char*)assets_array_obj + sizeof(void*)*4);
    for (uintptr_t i = 0; i < asset_count; ++i) {
        Il2CppObject* asset = items[i];
        if (!asset) continue;
        Il2CppClass* klass = il2cpp_object_get_class_fn(asset);

        if (dont_destroy_mi) {
            void* dd_args[1] = { asset };
            il2cpp_runtime_invoke_fn(dont_destroy_mi, nullptr, dd_args, &exc);
        }

        Il2CppObject* name_obj = get_name_mi ? il2cpp_runtime_invoke_fn(get_name_mi, asset, nullptr, &exc) : nullptr;
        char buf[128] = {0};
        if (name_obj) {
            const uint16_t* u16 = il2cpp_string_chars_fn((Il2CppString*)name_obj);
            for (int k = 0; k < 127 && u16[k]; ++k) buf[k] = (char)u16[k];
        }

        const char* kname = klass ? il2cpp_class_get_name_fn(klass) : "";
        if (strcmp(kname, "TMP_FontAsset") == 0) {
            if (strstr(buf, "SourceHanSans")) {
                g_shs_font = asset;
                LOGI("[LLC-FONT] Selected Chinese Font: %s (%p)", buf, g_shs_font);
            } else if (strstr(buf, "empty")) {
                g_empty_font = asset;
                LOGI("[LLC-FONT] Selected Empty Font: %s (%p)", buf, g_empty_font);
            }
        } else if (strcmp(kname, "Material") == 0) {
            if (strstr(buf, "SourceHanSans")) {
                g_shs_material = asset;
                LOGI("[LLC-FONT] Selected Chinese Material: %s (%p)", buf, g_shs_material);
            } else if (strstr(buf, "empty")) {
                g_empty_material = asset;
                LOGI("[LLC-FONT] Selected Empty Material: %s (%p)", buf, g_empty_material);
            }
        } else if (strcmp(kname, "Texture2D") == 0) {
            if (strstr(buf, "SourceHanSans") && !g_shs_atlas) {
                g_shs_atlas = asset;
                LOGI("[LLC-FONT] Selected Chinese Atlas Texture: %s (%p)", buf, g_shs_atlas);
            }
        }
    }

    if (!g_shs_font && first_font_asset) {
        g_shs_font = first_font_asset;
        LOGI("[LLC-FONT] Selected Custom Font Asset: %p", g_shs_font);
    }
    if (!g_shs_material && first_material) {
        g_shs_material = first_material;
        LOGI("[LLC-FONT] Selected Custom Material: %p", g_shs_material);
    }
    if (!g_shs_atlas && first_texture) {
        g_shs_atlas = first_texture;
        LOGI("[LLC-FONT] Selected Custom Atlas Texture: %p", g_shs_atlas);
    }

    return (g_shs_font != nullptr);
}

Il2CppObject* find_game_font_shader() {
    Il2CppClass* resources_class = find_class_any("UnityEngine", "Resources");
    Il2CppClass* font_asset_class = find_class_any("TMPro", "TMP_FontAsset");
    Il2CppClass* material_class = find_class_any("UnityEngine", "Material");
    if (!resources_class || !font_asset_class || !material_class) return nullptr;

    const MethodInfo* find_objs_mi = il2cpp_class_get_method_from_name_fn(resources_class, "FindObjectsOfTypeAll", 1);
    const MethodInfo* get_mat_mi = find_method_in_hierarchy(font_asset_class, "get_material", 0);
    const MethodInfo* get_shader_mi = find_method_in_hierarchy(material_class, "get_shader", 0);
    if (!find_objs_mi || !get_mat_mi || !get_shader_mi) return nullptr;

    Il2CppException* exc = nullptr;
    const Il2CppType* font_type = il2cpp_class_get_type_fn(font_asset_class);
    Il2CppObject* font_type_obj = il2cpp_type_get_object_fn(font_type);
    void* f_args[1] = { font_type_obj };
    Il2CppObject* font_arr_obj = il2cpp_runtime_invoke_fn(find_objs_mi, nullptr, f_args, &exc);
    if (!font_arr_obj || exc) return nullptr;

    uintptr_t total_fonts = il2cpp_array_length_fn(font_arr_obj);
    Il2CppObject** font_items = (Il2CppObject**)((char*)font_arr_obj + sizeof(void*)*4);
    for (uintptr_t i = 0; i < total_fonts; ++i) {
        if (!font_items[i] || font_items[i] == g_shs_font || font_items[i] == g_empty_font) continue;
        Il2CppObject* mat = il2cpp_runtime_invoke_fn(get_mat_mi, font_items[i], nullptr, &exc);
        if (mat && !exc) {
            Il2CppObject* shader = il2cpp_runtime_invoke_fn(get_shader_mi, mat, nullptr, &exc);
            if (shader && !exc) {
                Il2CppClass* unity_obj_class = find_class_any("UnityEngine", "Object");
                const MethodInfo* get_name_mi = unity_obj_class ? find_method_in_hierarchy(unity_obj_class, "get_name", 0) : nullptr;
                Il2CppObject* sname = get_name_mi ? il2cpp_runtime_invoke_fn(get_name_mi, shader, nullptr, &exc) : nullptr;
                char buf[128] = {0};
                if (sname) {
                    const uint16_t* u16 = il2cpp_string_chars_fn((Il2CppString*)sname);
                    for (int k = 0; k < 127 && u16[k]; ++k) buf[k] = (char)u16[k];
                }
                LOGI("[LLC-FONT] Found valid game font shader: '%s' (%p)", buf, shader);
                return shader;
            }
        }
    }
    return nullptr;
}

bool ensure_font_material_valid() {
    if (!g_shs_font) return false;
    Il2CppClass* font_asset_class = find_class_any("TMPro", "TMP_FontAsset");
    Il2CppClass* material_class = find_class_any("UnityEngine", "Material");
    if (!font_asset_class || !material_class) return false;

    const MethodInfo* font_get_mat_mi = find_method_in_hierarchy(font_asset_class, "get_material", 0);
    const MethodInfo* font_set_mat_mi = find_method_in_hierarchy(font_asset_class, "set_material", 1);
    const MethodInfo* mat_set_shader_mi = find_method_in_hierarchy(material_class, "set_shader", 1);
    const MethodInfo* mat_set_main_tex_mi = find_method_in_hierarchy(material_class, "set_mainTexture", 1);

    Il2CppException* exc = nullptr;
    Il2CppObject* cur_mat = font_get_mat_mi ? il2cpp_runtime_invoke_fn(font_get_mat_mi, g_shs_font, nullptr, &exc) : nullptr;

    if (!cur_mat && g_shs_material) {
        if (font_set_mat_mi) {
            void* m_args[1] = { g_shs_material };
            il2cpp_runtime_invoke_fn(font_set_mat_mi, g_shs_font, m_args, &exc);
            if (!exc) cur_mat = g_shs_material;
        }
        void* mat_field = il2cpp_class_get_field_from_name_fn(font_asset_class, "m_material");
        if (!mat_field) mat_field = il2cpp_class_get_field_from_name_fn(font_asset_class, "material");
        if (mat_field && cur_mat) {
            il2cpp_field_set_value_fn(g_shs_font, mat_field, cur_mat);
        }
    }

    if (!cur_mat) return false;

    Il2CppObject* game_shader = find_game_font_shader();
    if (!game_shader) return false;

    if (mat_set_shader_mi) {
        void* s_args[1] = { game_shader };
        il2cpp_runtime_invoke_fn(mat_set_shader_mi, cur_mat, s_args, &exc);
        LOGI("[LLC-FONT] Successfully bound Android Vulkan TMP shader (%p) to Chinese font material!", game_shader);
    }

    if (g_empty_material && mat_set_shader_mi) {
        void* s_args[1] = { game_shader };
        il2cpp_runtime_invoke_fn(mat_set_shader_mi, g_empty_material, s_args, &exc);
    }

    if (g_shs_atlas && mat_set_main_tex_mi) {
        void* t_args[1] = { g_shs_atlas };
        il2cpp_runtime_invoke_fn(mat_set_main_tex_mi, cur_mat, t_args, &exc);
        LOGI("[LLC-FONT] Injected atlas texture into g_shs_font material.");
    }

    return true;
}

static int strip_cjk_from_font(Il2CppObject* font_asset) {
    if (!font_asset || font_asset == g_shs_font || font_asset == g_empty_font) return 0;

    Il2CppClass* unity_obj_class = find_class_any("UnityEngine", "Object");
    const MethodInfo* get_name_mi = unity_obj_class ? find_method_in_hierarchy(unity_obj_class, "get_name", 0) : nullptr;
    Il2CppException* exc = nullptr;
    char fname[128] = {0};
    if (get_name_mi) {
        Il2CppObject* nobj = il2cpp_runtime_invoke_fn(get_name_mi, font_asset, nullptr, &exc);
        if (nobj && !exc) {
            const uint16_t* u = il2cpp_string_chars_fn((Il2CppString*)nobj);
            for (int k = 0; k < 127 && u[k]; ++k) fname[k] = (char)u[k];
        }
    }
    // Never strip from our Chinese fonts
    if (strstr(fname, "SourceHanSans") || strstr(fname, "Chinese")) return 0;

    Il2CppClass* f_class = il2cpp_object_get_class_fn(font_asset);
    const MethodInfo* get_lookup_mi = find_method_in_hierarchy(f_class, "get_characterLookupTable", 0);
    if (!get_lookup_mi) return 0;

    Il2CppObject* dict = il2cpp_runtime_invoke_fn(get_lookup_mi, font_asset, nullptr, &exc);
    if (!dict || exc) return 0;

    Il2CppObject* entries = string_table::reference(dict, "_entries");
    if (!entries) {
        entries = *(Il2CppObject**)((char*)dict + 24);
    }
    int count = 0;
    if (!string_table::integer(dict, "_count", count) && !string_table::integer(dict, "count", count)) {
        count = *(int*)((char*)dict + 32);
    }
    if (!entries || count <= 0 || static_cast<uintptr_t>(count) > il2cpp_array_length_fn(entries)) return 0;

    Il2CppClass* dict_class = il2cpp_object_get_class_fn(dict);
    const MethodInfo* remove_mi = il2cpp_class_get_method_from_name_fn(dict_class, "Remove", 1);
    if (!remove_mi) return 0;

    std::vector<uint32_t> cjk_keys;
    char* items_base = (char*)entries + sizeof(void*) * 4;
    for (int i = 0; i < count; ++i) {
        char* entry_ptr = items_base + i * 24;
        int32_t hash = *(int32_t*)entry_ptr;
        if (hash < 0) continue; // deleted
        uint32_t key = *(uint32_t*)(entry_ptr + 8);
        // CJK Unified Ideographs (0x4E00 - 0x9FFF), Ext A (0x3400 - 0x4DBF), Compatibility (0xF900 - 0xFAFF)
        if ((key >= 0x4E00 && key <= 0x9FFF) ||
            (key >= 0x3400 && key <= 0x4DBF) ||
            (key >= 0xF900 && key <= 0xFAFF)) {
            cjk_keys.push_back(key);
        }
    }

    if (cjk_keys.empty()) return 0;

    int removed = 0;
    for (uint32_t k : cjk_keys) {
        void* args[1] = { &k };
        il2cpp_runtime_invoke_fn(remove_mi, dict, args, &exc);
        if (!exc) removed++;
        else exc = nullptr;
    }

    LOGI("[LLC-FONT] Stripped %d CJK glyphs from base font '%s' (%p) -> unified to Chinese fallback!",
         removed, fname, font_asset);
    return removed;
}

void inject_fallback_font_into(Il2CppObject* font_asset, bool high_priority) {
    if (!font_asset || !g_shs_font || font_asset == g_shs_font) return;

    Il2CppClass* f_class = il2cpp_object_get_class_fn(font_asset);
    const MethodInfo* get_tbl_mi = find_method_in_hierarchy(f_class, "get_fallbackFontAssetTable", 0);
    if (!get_tbl_mi) {
        get_tbl_mi = find_method_in_hierarchy(f_class, "get_fallbackFontAssets", 0);
    }
    if (!get_tbl_mi) return;

    Il2CppException* exc = nullptr;
    Il2CppObject* tbl = il2cpp_runtime_invoke_fn(get_tbl_mi, font_asset, nullptr, &exc);
    if (!tbl || exc) return;

    Il2CppObject* ditems = string_table::reference(tbl, "_items");
    if (!ditems) {
        ditems = *(Il2CppObject**)((char*)tbl + 16);
    }
    int dsize = 0;
    if (!string_table::integer(tbl, "_size", dsize) && !string_table::integer(tbl, "size", dsize)) {
        dsize = *(int*)((char*)tbl + 24);
    }

    if (high_priority) {
        if (ditems && dsize > 0) {
            Il2CppObject** arr = (Il2CppObject**)((char*)ditems + sizeof(void*)*4);
            if (arr[0] == g_shs_font) {
                g_patched_fonts.insert(font_asset);
                strip_cjk_from_font(font_asset);
                return; // Already present at priority index 0
            }
            for (int k = 1; k < dsize; ++k) {
                if (arr[k] == g_shs_font) {
                    // Remove from lower-priority index so we can insert at index 0
                    const MethodInfo* remove_at_mi = find_method_in_hierarchy(il2cpp_object_get_class_fn(tbl), "RemoveAt", 1);
                    if (remove_at_mi) {
                        void* rm_args[1] = { &k };
                        il2cpp_runtime_invoke_fn(remove_at_mi, tbl, rm_args, &exc);
                    }
                    break;
                }
            }
        }

        // Insert(0, g_shs_font) to give Chinese SourceHanSans top priority over Japanese fonts
        const MethodInfo* insert_mi = find_method_in_hierarchy(il2cpp_object_get_class_fn(tbl), "Insert", 2);
        if (insert_mi) {
            int idx = 0;
            void* ins_args[2] = { &idx, g_shs_font };
            il2cpp_runtime_invoke_fn(insert_mi, tbl, ins_args, &exc);
            if (!exc) {
                g_patched_fonts.insert(font_asset);
                Il2CppClass* unity_obj_class = find_class_any("UnityEngine", "Object");
                const MethodInfo* get_name_mi = unity_obj_class ? find_method_in_hierarchy(unity_obj_class, "get_name", 0) : nullptr;
                Il2CppObject* name_obj = get_name_mi ? il2cpp_runtime_invoke_fn(get_name_mi, font_asset, nullptr, &exc) : nullptr;
                char buf[128] = {0};
                if (name_obj) {
                    const uint16_t* u16 = il2cpp_string_chars_fn((Il2CppString*)name_obj);
                    for (int k = 0; k < 127 && u16[k]; ++k) buf[k] = (char)u16[k];
                }
                LOGI("[LLC-FONT] Injected Chinese fallback at INDEX 0 (high priority) into font: '%s' (%p)", buf, font_asset);
                strip_cjk_from_font(font_asset);
                return;
            }
        }

        const MethodInfo* add_mi = find_method_in_hierarchy(il2cpp_object_get_class_fn(tbl), "Add", 1);
        if (add_mi) {
            void* add_args[1] = { g_shs_font };
            il2cpp_runtime_invoke_fn(add_mi, tbl, add_args, &exc);
            if (!exc) {
                g_patched_fonts.insert(font_asset);
                strip_cjk_from_font(font_asset);
            }
        }
    } else {
        // Low priority (e.g. Japanese mode: keep Chinese font at the end so Japanese fonts have top priority)
        if (ditems && dsize > 0) {
            Il2CppObject** arr = (Il2CppObject**)((char*)ditems + sizeof(void*)*4);
            if (arr[0] == g_shs_font) {
                // If present at index 0, remove it so Japanese font has top priority
                int zero = 0;
                const MethodInfo* remove_at_mi = find_method_in_hierarchy(il2cpp_object_get_class_fn(tbl), "RemoveAt", 1);
                if (remove_at_mi) {
                    void* rm_args[1] = { &zero };
                    il2cpp_runtime_invoke_fn(remove_at_mi, tbl, rm_args, &exc);
                }
                // Append to end
                const MethodInfo* add_mi = find_method_in_hierarchy(il2cpp_object_get_class_fn(tbl), "Add", 1);
                if (add_mi) {
                    void* add_args[1] = { g_shs_font };
                    il2cpp_runtime_invoke_fn(add_mi, tbl, add_args, &exc);
                }
                g_patched_fonts.insert(font_asset);
                LOGI("[LLC-FONT] Moved Chinese fallback from INDEX 0 to END (low priority) for pristine original font rendering");
                return;
            }
            // Check if already in list at k > 0
            for (int k = 1; k < dsize; ++k) {
                if (arr[k] == g_shs_font) {
                    g_patched_fonts.insert(font_asset);
                    return; // Already present at lower priority
                }
            }
        }

        // Not present yet, append to end
        const MethodInfo* add_mi = find_method_in_hierarchy(il2cpp_object_get_class_fn(tbl), "Add", 1);
        if (add_mi) {
            void* add_args[1] = { g_shs_font };
            il2cpp_runtime_invoke_fn(add_mi, tbl, add_args, &exc);
            if (!exc) {
                g_patched_fonts.insert(font_asset);
            }
        }
    }
}

int get_patched_font_count() {
    return (int)g_patched_fonts.size();
}

int inject_fallback_to_all_loaded_fonts(bool high_priority) {
    Il2CppClass* resources_class = find_class_any("UnityEngine", "Resources");
    Il2CppClass* font_asset_class = find_class_any("TMPro", "TMP_FontAsset");
    if (!resources_class || !font_asset_class || !g_shs_font) return (int)g_patched_fonts.size();

    const MethodInfo* find_objs_mi = il2cpp_class_get_method_from_name_fn(resources_class, "FindObjectsOfTypeAll", 1);
    if (!find_objs_mi) return (int)g_patched_fonts.size();

    Il2CppException* exc = nullptr;
    const Il2CppType* font_type = il2cpp_class_get_type_fn(font_asset_class);
    Il2CppObject* font_type_obj = il2cpp_type_get_object_fn(font_type);
    void* f_args[1] = { font_type_obj };
    Il2CppObject* font_arr_obj = il2cpp_runtime_invoke_fn(find_objs_mi, nullptr, f_args, &exc);
    if (!font_arr_obj || exc) return (int)g_patched_fonts.size();

    uintptr_t total_fonts = il2cpp_array_length_fn(font_arr_obj);
    Il2CppObject** font_items = (Il2CppObject**)((char*)font_arr_obj + sizeof(void*)*4);
    for (uintptr_t i = 0; i < total_fonts; ++i) {
        if (font_items[i]) {
            inject_fallback_font_into(font_items[i], high_priority);
        }
    }
    return (int)g_patched_fonts.size();
}

void refresh_title_screen_loaders() {
    static Il2CppClass* resources_class = nullptr;
    static Il2CppClass* utdl_class = nullptr;
    static const MethodInfo* find_objs_mi = nullptr;
    static const MethodInfo* utdl_set_text_mi = nullptr;
    static Il2CppObject* utdl_type_obj = nullptr;
    static bool resolved = false;

    if (!resolved) {
        resources_class = find_class_any("UnityEngine", "Resources");
        utdl_class = find_class_any("", "UITextDataLoader");
        if (resources_class && utdl_class) {
            find_objs_mi = il2cpp_class_get_method_from_name_fn(resources_class, "FindObjectsOfTypeAll", 1);
            utdl_set_text_mi = il2cpp_class_get_method_from_name_fn(utdl_class, "SetText", 0);
            const Il2CppType* utdl_type = il2cpp_class_get_type_fn(utdl_class);
            if (utdl_type && il2cpp_type_get_object_fn) {
                utdl_type_obj = il2cpp_type_get_object_fn(utdl_type);
            }
            resolved = true;
        }
    }
    if (!find_objs_mi || !utdl_set_text_mi || !utdl_type_obj) return;

    Il2CppException* exc = nullptr;
    void* u_args[1] = { utdl_type_obj };
    Il2CppObject* arr_obj = il2cpp_runtime_invoke_fn(find_objs_mi, nullptr, u_args, &exc);
    if (!arr_obj || exc) return;

    uintptr_t total = il2cpp_array_length_fn(arr_obj);
    Il2CppObject** items = (Il2CppObject**)((char*)arr_obj + sizeof(void*)*4);
    int refreshed = 0;

    for (uintptr_t i = 0; i < total; ++i) {
        if (!items[i]) continue;
        il2cpp_runtime_invoke_fn(utdl_set_text_mi, items[i], nullptr, &exc);
        if (!exc) refreshed++;
    }
    LOGI("[LLC-FONT] Refreshed %d/%lu title screen UITextDataLoaders.", refreshed, (unsigned long)total);
}

// Built-in rule mapping for standard title & auth UI elements.
// Resolves dynamically against g_ui_map (loaded from LoginUIText.json),
// with an embedded fallback string if the language pack has not populated the key.
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

static inline std::string resolve_ui_target(const char* ui_key, const char* default_text) {
    if (ui_key && ui_key[0] != '\0') {
        auto it = g_ui_map.find(ui_key);
        if (it != g_ui_map.end() && !it->second.empty()) {
            return it->second;
        }
    }
    return (default_text && default_text[0] != '\0') ? default_text : "";
}

UIRefreshResult refresh_active_ui_texts(bool localization_active) {
    UIRefreshResult result;
    check_and_reload_ui_overrides();
    static Il2CppClass* resources_class = nullptr;
    static Il2CppClass* tmp_text_class = nullptr;
    static Il2CppClass* unity_obj_class = nullptr;
    static Il2CppClass* comp_class = nullptr;
    static Il2CppClass* transform_class = nullptr;
    static const MethodInfo* find_objs_mi = nullptr;
    static const MethodInfo* get_txt_mi = nullptr;
    static const MethodInfo* set_txt_mi = nullptr;
    static const MethodInfo* SetText_mi = nullptr;
    static const MethodInfo* get_font_mi = nullptr;
    static const MethodInfo* set_font_mi = nullptr;
    static const MethodInfo* set_dirty_mi = nullptr;
    static const MethodInfo* force_mesh_update_mi = nullptr;
    static const MethodInfo* get_name_mi = nullptr;
    static const MethodInfo* get_transform_mi = nullptr;
    static const MethodInfo* get_parent_mi = nullptr;
    static Il2CppObject* text_type_obj = nullptr;
    static bool resolved = false;

    if (!resolved) {
        resources_class = find_class_any("UnityEngine", "Resources");
        tmp_text_class = find_class_any("TMPro", "TMP_Text");
        unity_obj_class = find_class_any("UnityEngine", "Object");
        comp_class = find_class_any("UnityEngine", "Component");
        transform_class = find_class_any("UnityEngine", "Transform");
        if (resources_class && tmp_text_class) {
            find_objs_mi = il2cpp_class_get_method_from_name_fn(resources_class, "FindObjectsOfTypeAll", 1);
            get_txt_mi = find_method_in_hierarchy(tmp_text_class, "get_text", 0);
            set_txt_mi = find_method_in_hierarchy(tmp_text_class, "set_text", 1);
            SetText_mi = find_method_in_hierarchy(tmp_text_class, "SetText", 1);
            get_font_mi = find_method_in_hierarchy(tmp_text_class, "get_font", 0);
            set_font_mi = find_method_in_hierarchy(tmp_text_class, "set_font", 1);
            set_dirty_mi = find_method_in_hierarchy(tmp_text_class, "SetAllDirty", 0);
            force_mesh_update_mi = find_method_in_hierarchy(tmp_text_class, "ForceMeshUpdate", 0);
            get_name_mi = unity_obj_class ? find_method_in_hierarchy(unity_obj_class, "get_name", 0) : nullptr;
            get_transform_mi = comp_class ? find_method_in_hierarchy(comp_class, "get_transform", 0) : nullptr;
            get_parent_mi = transform_class ? find_method_in_hierarchy(transform_class, "get_parent", 0) : nullptr;
            const Il2CppType* text_type = il2cpp_class_get_type_fn(tmp_text_class);
            if (text_type && il2cpp_type_get_object_fn) {
                text_type_obj = il2cpp_type_get_object_fn(text_type);
            }
            resolved = true;
        }
    }
    if (!find_objs_mi || !get_txt_mi || !set_txt_mi || !text_type_obj) return result;

    Il2CppException* exc = nullptr;
    void* t_args[1] = { text_type_obj };
    Il2CppObject* text_arr_obj = il2cpp_runtime_invoke_fn(find_objs_mi, nullptr, t_args, &exc);
    if (!text_arr_obj || exc) return result;

    uintptr_t total_texts = il2cpp_array_length_fn(text_arr_obj);
    Il2CppObject** text_items = (Il2CppObject**)((char*)text_arr_obj + sizeof(void*)*4);

    int replaced_count = 0;
    int font_swapped_count = 0;

    for (uintptr_t i = 0; i < total_texts; ++i) {
        Il2CppObject* comp = text_items[i];
        if (!comp) continue;

        // 1. Build hierarchy obj_path FIRST
        std::string obj_path;
        if (get_transform_mi && get_name_mi) {
            Il2CppObject* cur_t = il2cpp_runtime_invoke_fn(get_transform_mi, comp, nullptr, &exc);
            std::vector<std::string> path_nodes;
            while (cur_t && !exc) {
                Il2CppObject* n_str = il2cpp_runtime_invoke_fn(get_name_mi, cur_t, nullptr, &exc);
                if (n_str && !exc) {
                    const uint16_t* nu = il2cpp_string_chars_fn((Il2CppString*)n_str);
                    char nbuf[128] = {0};
                    for (int k = 0; k < 127 && nu[k]; ++k) nbuf[k] = (char)nu[k];
                    path_nodes.push_back(nbuf);
                }
                if (!get_parent_mi) break;
                cur_t = il2cpp_runtime_invoke_fn(get_parent_mi, cur_t, nullptr, &exc);
            }
            for (int p = (int)path_nodes.size() - 1; p >= 0; --p) {
                obj_path += "/";
                obj_path += path_nodes[p];
            }
        }

        // STRICT SCOPE PROTECTION:
        // refresh_active_ui_texts must ONLY EVER operate on Title Screen and Auth/Login popups.
        // In-game lobby, manuals, tutorials, gacha, formation, battle, etc. are localized purely via TextDataSet.
        if (!is_title_or_login_obj_path(obj_path)) {
            continue;
        }

        // 2. Read current text and check for CJK characters
        std::string utf8_text;
        bool has_cjk = false;
        if (get_txt_mi) {
            Il2CppObject* txt_str = il2cpp_runtime_invoke_fn(get_txt_mi, comp, nullptr, &exc);
            if (txt_str && !exc) {
                const uint16_t* u16 = il2cpp_string_chars_fn((Il2CppString*)txt_str);
                if (u16) {
                    for (int k = 0; u16[k] && k < 512; ++k) {
                        uint16_t c = u16[k];
                        if (is_cjk_codepoint(c)) {
                            has_cjk = true;
                        }
                        if (c < 0x80) {
                            if (c == '\r') continue;
                            utf8_text += (char)c;
                        } else if (c < 0x800) {
                            utf8_text += (char)(0xC0 | (c >> 6));
                            utf8_text += (char)(0x80 | (c & 0x3F));
                        } else {
                            utf8_text += (char)(0xE0 | (c >> 12));
                            utf8_text += (char)(0x80 | ((c >> 6) & 0x3F));
                            utf8_text += (char)(0x80 | (c & 0x3F));
                        }
                    }
                }
            }
        }

        std::string trimmed_text = trim_copy(utf8_text);
        std::string lower_trimmed = to_lower_ascii(trimmed_text);

        // 3. Evaluate replacement via data-driven overrides & UI key mappings
        bool text_replaced = false;
        std::string target_replacement;

        auto lookup_ui_key = [](const std::string& key) -> std::string {
            if (key.empty()) return "";
            auto it = g_ui_map.find(key);
            if (it != g_ui_map.end() && !it->second.empty()) {
                return it->second;
            }
            return "";
        };

        // 3a. Direct path overrides (configured in static_overrides.json or custom_overrides.json)
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

        // 3b. Direct string overrides (raw text -> translated text) - EXACT TRIMMED MATCH or CASE-INSENSITIVE
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

        // 3c. Key-mapped overrides from JSON
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

        // 3d. Built-in path mappings (resolves via LoginUIText g_ui_map, with embedded fallback)
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

        // 3e. Built-in string mappings (resolves via LoginUIText g_ui_map, with embedded fallback)
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

        // 3f. Fallback fuzzy heuristics for Auth & Login buttons (resolving via LoginUIText keys)
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

        // Detect Title screen or Auth dialog presence
        if (obj_path.find("Button_SwitchAccount") != std::string::npos ||
            obj_path.find("Button_AllCacheClear") != std::string::npos ||
            obj_path.find("SwitchAccount") != std::string::npos ||
            obj_path.find("UserAgreement") != std::string::npos ||
            obj_path.find("ClearCache") != std::string::npos) {
            result.title_or_auth_dialog_active = true;
        }
        if (trimmed_text == "切换账号" || trimmed_text == "清除所有缓存" ||
            trimmed_text == "Google登录" || trimmed_text == "Apple登录" ||
            trimmed_text == "以游客登录" || trimmed_text == "游客登录" ||
            trimmed_text == "Steam登录" || trimmed_text == "点击开始" ||
            trimmed_text == "TOUCH TO START") {
            result.title_or_auth_dialog_active = true;
        }

        // 4. Apply text replacement if needed
        if (text_replaced && !target_replacement.empty()) {
            set_component_text_and_font(comp, target_replacement, set_txt_mi, SetText_mi, set_font_mi, set_dirty_mi, force_mesh_update_mi);
            replaced_count++;
            LOGI("[LLC-FONT] Replaced '%s' -> '%s' (path: %s)",
                 utf8_text.c_str(), target_replacement.c_str(), obj_path.c_str());
            utf8_text = target_replacement;
            has_cjk = utf8_has_cjk(utf8_text);
        }

        // 5. Content-aware & safe font swapping for title UI text
        if (localization_active && g_shs_font && get_font_mi && set_font_mi && get_name_mi) {
            bool should_unify = s_ui_overrides.force_font_unify || has_cjk;
            if (should_unify) {
                Il2CppObject* cur_f = il2cpp_runtime_invoke_fn(get_font_mi, comp, nullptr, &exc);
                if (cur_f && !exc && cur_f != g_shs_font) {
                    Il2CppObject* fn_obj = il2cpp_runtime_invoke_fn(get_name_mi, cur_f, nullptr, &exc);
                    if (fn_obj && !exc) {
                        const uint16_t* fu = il2cpp_string_chars_fn((Il2CppString*)fn_obj);
                        char fnbuf[64] = {0};
                        for (int x = 0; x < 63 && fu[x]; ++x) fnbuf[x] = (char)fu[x];

                        bool is_target = is_cjk_body_font_name(fnbuf);
                        if (s_ui_overrides.force_font_unify) {
                            if (strstr(fnbuf, "Liberation") || strstr(fnbuf, "Excelsior") ||
                                strstr(fnbuf, "Mikodacs") || strstr(fnbuf, "BebasKai")) {
                                is_target = true;
                            }
                        }

                        if (is_target) {
                            void* sf_args[1] = { g_shs_font };
                            il2cpp_runtime_invoke_fn(set_font_mi, comp, sf_args, &exc);
                            if (set_dirty_mi) il2cpp_runtime_invoke_fn(set_dirty_mi, comp, nullptr, &exc);
                            font_swapped_count++;
                        }
                    }
                }
            }
        }
    }

    result.replaced_count = replaced_count;
    result.font_swapped_count = font_swapped_count;

    if (font_swapped_count > 0 || replaced_count > 0) {
        LOGI("[LLC-FONT] Active UI Text Sync: %d fonts unified to Chinese font, %d title strings translated (dialog active: %d).",
             font_swapped_count, replaced_count, result.title_or_auth_dialog_active ? 1 : 0);
    }
    return result;
}

void dump_all_fonts_status(const char* output_file) {
    inject_fallback_to_all_loaded_fonts();
    Il2CppClass* resources_class = find_class_any("UnityEngine", "Resources");
    Il2CppClass* font_asset_class = find_class_any("TMPro", "TMP_FontAsset");
    if (!resources_class || !font_asset_class) return;

    const MethodInfo* find_objs_mi = il2cpp_class_get_method_from_name_fn(resources_class, "FindObjectsOfTypeAll", 1);
    if (!find_objs_mi) return;

    Il2CppException* exc = nullptr;
    const Il2CppType* font_type = il2cpp_class_get_type_fn(font_asset_class);
    Il2CppObject* font_type_obj = il2cpp_type_get_object_fn(font_type);
    void* f_args[1] = { font_type_obj };
    Il2CppObject* font_arr_obj = il2cpp_runtime_invoke_fn(find_objs_mi, nullptr, f_args, &exc);
    if (!font_arr_obj || exc) return;

    uintptr_t total_fonts = il2cpp_array_length_fn(font_arr_obj);
    Il2CppObject** font_items = (Il2CppObject**)((char*)font_arr_obj + sizeof(void*)*4);

    FILE* fp = output_file ? fopen(output_file, "w") : nullptr;
    if (fp) {
        fprintf(fp, "================================================================================\n");
        fprintf(fp, "Limbus Company Font Asset & Fallback Audit (Total: %lu)\n", (unsigned long)total_fonts);
        fprintf(fp, "================================================================================\n");

        fprintf(fp, "\n--- TMP_FontAsset Fields ---\n");
        void* fiter = nullptr;
        while (void* fi = il2cpp_class_get_fields_fn(font_asset_class, &fiter)) {
            const char* fn = il2cpp_field_get_name_fn(fi);
            const Il2CppType* ft = il2cpp_field_get_type_fn(fi);
            Il2CppClass* fc = ft ? il2cpp_class_from_type_fn(ft) : nullptr;
            fprintf(fp, "Field: %s (type: %s)\n", fn ? fn : "?", fc ? il2cpp_class_get_name_fn(fc) : "?");
        }

        fprintf(fp, "\n--- TMP_FontAsset Methods ---\n");
        void* miter = nullptr;
        while (const MethodInfo* mi = il2cpp_class_get_methods_fn(font_asset_class, &miter)) {
            const char* mn = il2cpp_method_get_name_fn(mi);
            fprintf(fp, "Method: %s (params: %d)\n", mn ? mn : "?", il2cpp_method_get_param_count_fn(mi));
        }
        fprintf(fp, "\n================================================================================\n");
    }

    LOGI("[LLC-FONT-AUDIT] =================== Font Fallback Audit ===================");
    Il2CppClass* unity_obj_class = find_class_any("UnityEngine", "Object");
    const MethodInfo* get_name_mi = unity_obj_class ? find_method_in_hierarchy(unity_obj_class, "get_name", 0) : nullptr;
    const MethodInfo* get_tbl_mi = find_method_in_hierarchy(font_asset_class, "get_fallbackFontAssetTable", 0);
    if (!get_tbl_mi) get_tbl_mi = find_method_in_hierarchy(font_asset_class, "get_fallbackFontAssets", 0);

    for (uintptr_t i = 0; i < total_fonts; ++i) {
        Il2CppObject* font = font_items[i];
        if (!font) continue;

        char fname[128] = "UnknownFont";
        if (get_name_mi) {
            Il2CppObject* nobj = il2cpp_runtime_invoke_fn(get_name_mi, font, nullptr, &exc);
            if (nobj && !exc) {
                const uint16_t* u = il2cpp_string_chars_fn((Il2CppString*)nobj);
                for (int k = 0; k < 127 && u[k]; ++k) fname[k] = (char)u[k];
            }
        }

        bool has_fallback = false;
        int fallback_count = 0;
        std::string fallback_names;
        if (get_tbl_mi) {
            Il2CppObject* tbl = il2cpp_runtime_invoke_fn(get_tbl_mi, font, nullptr, &exc);
            if (tbl && !exc) {
                Il2CppClass* l_class = il2cpp_object_get_class_fn(tbl);
                const MethodInfo* count_mi = find_method_in_hierarchy(l_class, "get_Count", 0);
                if (count_mi) {
                    Il2CppObject* cnt_obj = il2cpp_runtime_invoke_fn(count_mi, tbl, nullptr, &exc);
                    if (cnt_obj && !exc) fallback_count = *(int*)il2cpp_object_unbox_fn(cnt_obj);
                }
                Il2CppObject* ditems = string_table::reference(tbl, "_items");
                if (!ditems) {
                    ditems = *(Il2CppObject**)((char*)tbl + 16);
                }
                int dsize = 0;
                if (!string_table::integer(tbl, "_size", dsize) && !string_table::integer(tbl, "size", dsize)) {
                    dsize = *(int*)((char*)tbl + 24);
                }
                if (ditems && dsize > 0) {
                    Il2CppObject** arr = (Il2CppObject**)((char*)ditems + sizeof(void*)*4);
                    for (int k = 0; k < dsize && k < 10; ++k) {
                        Il2CppObject* item = arr[k];
                        if (!item) continue;
                        if (item == g_shs_font) has_fallback = true;
                        if (get_name_mi) {
                            Il2CppObject* iname = il2cpp_runtime_invoke_fn(get_name_mi, item, nullptr, &exc);
                            if (iname && !exc) {
                                const uint16_t* iu = il2cpp_string_chars_fn((Il2CppString*)iname);
                                char ibuf[64] = {0};
                                for (int x = 0; x < 63 && iu[x]; ++x) ibuf[x] = (char)iu[x];
                                if (!fallback_names.empty()) fallback_names += ", ";
                                fallback_names += ibuf;
                                if (strstr(ibuf, "SourceHanSans")) has_fallback = true;
                            }
                        }
                    }
                }
            }
        }

        LOGI("[LLC-FONT-AUDIT] [%lu/%lu] Font: '%s' | Fallbacks (%d): [%s] | SourceHanSans: %s",
             (unsigned long)(i + 1), (unsigned long)total_fonts, fname, fallback_count, fallback_names.c_str(), has_fallback ? "YES" : "NO");
        if (fp) {
            fprintf(fp, "[%lu/%lu] Font: '%s' | Fallbacks (%d): [%s] | SourceHanSans: %s\n",
                    (unsigned long)(i + 1), (unsigned long)total_fonts, fname, fallback_count, fallback_names.c_str(), has_fallback ? "YES" : "NO");
        }
    }
    LOGI("[LLC-FONT-AUDIT] ===========================================================");
    if (fp) {
        fprintf(fp, "================================================================================\n");
        fclose(fp);
    }
}

void dump_active_tmp_texts(const char* output_file) {
    Il2CppClass* resources_class = find_class_any("UnityEngine", "Resources");
    Il2CppClass* tmp_text_class = find_class_any("TMPro", "TMP_Text");
    if (!resources_class || !tmp_text_class) return;

    const MethodInfo* find_objs_mi = il2cpp_class_get_method_from_name_fn(resources_class, "FindObjectsOfTypeAll", 1);
    if (!find_objs_mi) return;

    Il2CppException* exc = nullptr;
    const Il2CppType* text_type = il2cpp_class_get_type_fn(tmp_text_class);
    Il2CppObject* text_type_obj = il2cpp_type_get_object_fn(text_type);
    void* t_args[1] = { text_type_obj };
    Il2CppObject* text_arr_obj = il2cpp_runtime_invoke_fn(find_objs_mi, nullptr, t_args, &exc);
    if (!text_arr_obj || exc) return;

    uintptr_t total_texts = il2cpp_array_length_fn(text_arr_obj);
    Il2CppObject** text_items = (Il2CppObject**)((char*)text_arr_obj + sizeof(void*)*4);

    const MethodInfo* get_txt_mi = find_method_in_hierarchy(tmp_text_class, "get_text", 0);
    const MethodInfo* get_font_mi = find_method_in_hierarchy(tmp_text_class, "get_font", 0);
    Il2CppClass* comp_class = find_class_any("UnityEngine", "Component");
    Il2CppClass* go_class = find_class_any("UnityEngine", "GameObject");
    Il2CppClass* transform_class = find_class_any("UnityEngine", "Transform");
    Il2CppClass* unity_obj_class = find_class_any("UnityEngine", "Object");

    const MethodInfo* get_go_mi = comp_class ? find_method_in_hierarchy(comp_class, "get_gameObject", 0) : nullptr;
    const MethodInfo* get_transform_mi = comp_class ? find_method_in_hierarchy(comp_class, "get_transform", 0) : nullptr;
    const MethodInfo* active_mi = go_class ? find_method_in_hierarchy(go_class, "get_activeInHierarchy", 0) : nullptr;
    const MethodInfo* get_name_mi = unity_obj_class ? find_method_in_hierarchy(unity_obj_class, "get_name", 0) : nullptr;
    const MethodInfo* get_parent_mi = transform_class ? find_method_in_hierarchy(transform_class, "get_parent", 0) : nullptr;

    int active_count = 0;
    int zh_count = 0;
    int en_count = 0;
    int ko_count = 0;
    int other_count = 0;

    struct DumpEntry {
        std::string path;
        std::string font;
        std::string text;
        std::string lang;
    };
    std::vector<DumpEntry> entries;

    for (uintptr_t i = 0; i < total_texts; ++i) {
        Il2CppObject* comp = text_items[i];
        if (!comp) continue;

        if (get_go_mi && active_mi) {
            Il2CppObject* go = il2cpp_runtime_invoke_fn(get_go_mi, comp, nullptr, &exc);
            if (go && !exc) {
                Il2CppObject* a_res = il2cpp_runtime_invoke_fn(active_mi, go, nullptr, &exc);
                if (a_res && !exc && !(*(bool*)il2cpp_object_unbox_fn(a_res))) {
                    continue; // Skip inactive objects
                }
            }
        }

        if (!get_txt_mi) continue;
        Il2CppObject* txt_str = il2cpp_runtime_invoke_fn(get_txt_mi, comp, nullptr, &exc);
        if (!txt_str || exc) continue;

        const uint16_t* u16 = il2cpp_string_chars_fn((Il2CppString*)txt_str);
        if (!u16 || !u16[0]) continue;

        bool has_cjk = false;
        bool has_hangul = false;
        bool has_alpha = false;

        std::string utf8_text;
        for (int k = 0; u16[k]; ++k) {
            uint16_t c = u16[k];
            if ((c >= 0x4E00 && c <= 0x9FFF) || (c >= 0x3400 && c <= 0x4DBF)) {
                has_cjk = true;
            } else if ((c >= 0xAC00 && c <= 0xD7AF) || (c >= 0x1100 && c <= 0x11FF)) {
                has_hangul = true;
            } else if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
                has_alpha = true;
            }

            if (c < 0x80) {
                if (c == '\n') utf8_text += "\\n";
                else if (c == '\r') {}
                else utf8_text += (char)c;
            } else if (c < 0x800) {
                utf8_text += (char)(0xC0 | (c >> 6));
                utf8_text += (char)(0x80 | (c & 0x3F));
            } else {
                utf8_text += (char)(0xE0 | (c >> 12));
                utf8_text += (char)(0x80 | ((c >> 6) & 0x3F));
                utf8_text += (char)(0x80 | (c & 0x3F));
            }
        }

        if (utf8_text.empty()) continue;

        std::string lang_tag;
        if (has_cjk) {
            lang_tag = "ZH";
            zh_count++;
        } else if (has_hangul) {
            lang_tag = "KO";
            ko_count++;
        } else if (has_alpha) {
            lang_tag = "EN";
            en_count++;
        } else {
            lang_tag = "NUM/SYM";
            other_count++;
        }
        active_count++;

        std::string full_path;
        if (get_transform_mi && get_name_mi) {
            Il2CppObject* cur_t = il2cpp_runtime_invoke_fn(get_transform_mi, comp, nullptr, &exc);
            std::vector<std::string> path_nodes;
            while (cur_t && !exc) {
                Il2CppObject* n_str = il2cpp_runtime_invoke_fn(get_name_mi, cur_t, nullptr, &exc);
                if (n_str && !exc) {
                    const uint16_t* nu = il2cpp_string_chars_fn((Il2CppString*)n_str);
                    char nbuf[128] = {0};
                    for (int k = 0; k < 127 && nu[k]; ++k) nbuf[k] = (char)nu[k];
                    path_nodes.push_back(nbuf);
                }
                if (!get_parent_mi) break;
                cur_t = il2cpp_runtime_invoke_fn(get_parent_mi, cur_t, nullptr, &exc);
            }
            for (int p = (int)path_nodes.size() - 1; p >= 0; --p) {
                full_path += "/";
                full_path += path_nodes[p];
            }
        }

        std::string font_name = "Default";
        if (get_font_mi && get_name_mi) {
            Il2CppObject* fobj = il2cpp_runtime_invoke_fn(get_font_mi, comp, nullptr, &exc);
            if (fobj && !exc) {
                Il2CppObject* fn_str = il2cpp_runtime_invoke_fn(get_name_mi, fobj, nullptr, &exc);
                if (fn_str && !exc) {
                    const uint16_t* fu = il2cpp_string_chars_fn((Il2CppString*)fn_str);
                    char fbuf[128] = {0};
                    for (int k = 0; k < 127 && fu[k]; ++k) fbuf[k] = (char)fu[k];
                    font_name = fbuf;
                }
            }
        }

        DumpEntry de;
        de.path = full_path;
        de.font = font_name;
        de.text = utf8_text;
        de.lang = lang_tag;
        entries.push_back(de);
    }

    LOGI("[LLC-TEXT-DUMP] ================= ACTIVE TMP_TEXT DUMP =================");
    LOGI("[LLC-TEXT-DUMP] Total Active Texts: %d (ZH: %d, EN: %d, KO: %d, Other: %d)",
         active_count, zh_count, en_count, ko_count, other_count);

    FILE* fp = output_file ? fopen(output_file, "w") : nullptr;
    if (fp) {
        fprintf(fp, "================================================================================\n");
        fprintf(fp, "Limbus Company Active TMP_Text Dump\n");
        fprintf(fp, "Total Active Texts: %d\n", active_count);
        fprintf(fp, "  - Chinese (ZH): %d (%.1f%%)\n", zh_count, active_count > 0 ? (zh_count * 100.0 / active_count) : 0.0);
        fprintf(fp, "  - English (EN): %d (%.1f%%)\n", en_count, active_count > 0 ? (en_count * 100.0 / active_count) : 0.0);
        fprintf(fp, "  - Korean  (KO): %d (%.1f%%)\n", ko_count, active_count > 0 ? (ko_count * 100.0 / active_count) : 0.0);
        fprintf(fp, "  - Other/Sym   : %d (%.1f%%)\n", other_count, active_count > 0 ? (other_count * 100.0 / active_count) : 0.0);
        fprintf(fp, "================================================================================\n\n");
    }

    for (size_t idx = 0; idx < entries.size(); ++idx) {
        const auto& e = entries[idx];
        if (fp) {
            fprintf(fp, "[#%03zu] [%s] [%s]\n  Path: %s\n  Text: \"%s\"\n\n",
                    idx + 1, e.lang.c_str(), e.font.c_str(), e.path.c_str(), e.text.c_str());
        }
        if (e.lang == "EN" || e.lang == "KO") {
            LOGI("[LLC-TEXT-DUMP] [%s] %s | Text: '%s'", e.lang.c_str(), e.path.c_str(), e.text.c_str());
        }
    }

    if (fp) {
        fclose(fp);
        LOGI("[LLC-TEXT-DUMP] Full text dump written to: %s", output_file);
    }
    LOGI("[LLC-TEXT-DUMP] ========================================================");
}

static std::atomic<bool> s_ui_refresh_pending{false};
static std::atomic<bool> s_title_auth_dialog_active{false};
static std::atomic<bool> s_font_fallback_pending{false};
static std::atomic<bool> s_mat_check_pending{false};
static std::atomic<bool> s_shader_bound{false};

void schedule_ui_refresh(bool localization_active) {
    if (s_ui_refresh_pending.exchange(true)) return;
    main_thread_bridge::post_task([localization_active]() {
        s_ui_refresh_pending.store(false);
        refresh_title_screen_loaders();
        UIRefreshResult res = refresh_active_ui_texts(localization_active);
        if (res.title_or_auth_dialog_active) {
            s_title_auth_dialog_active.store(true);
        }
    });
}

bool is_title_auth_dialog_active() {
    return s_title_auth_dialog_active.exchange(false);
}

void schedule_font_fallback(bool high_priority) {
    if (s_font_fallback_pending.exchange(true)) return;
    main_thread_bridge::post_task([high_priority]() {
        s_font_fallback_pending.store(false);
        inject_fallback_to_all_loaded_fonts(high_priority);
    });
}

void schedule_ensure_material_valid() {
    if (s_shader_bound.load()) return;
    if (s_mat_check_pending.exchange(true)) return;
    main_thread_bridge::post_task([]() {
        s_mat_check_pending.store(false);
        if (ensure_font_material_valid()) {
            s_shader_bound.store(true);
            LOGI("[LLC-CORE] Font material and Android Vulkan shader successfully configured on main thread.");
            inject_fallback_to_all_loaded_fonts(true);
        }
    });
}

bool is_shader_bound() {
    return s_shader_bound.load();
}

void schedule_dump_active_tmp_texts(const char* output_file) {
    std::string path = output_file ? output_file : "";
    main_thread_bridge::post_task([path]() {
        dump_active_tmp_texts(path.empty() ? nullptr : path.c_str());
    });
}

void schedule_dump_all_fonts_status(const char* output_file) {
    std::string path = output_file ? output_file : "";
    main_thread_bridge::post_task([path]() {
        dump_all_fonts_status(path.empty() ? nullptr : path.c_str());
    });
}

