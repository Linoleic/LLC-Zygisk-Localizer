#include "ui_text_replacer.hpp"
#include "ui_overrides.hpp"
#include "font_injector.hpp"
#include <mutex>
#include <unordered_map>
#include <vector>
#include <string>

namespace {

Il2CppString* get_pinned_ui_string(const std::string& text) {
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

void set_component_text_and_font(Il2CppObject* comp,
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

} // namespace

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

        // 3. Evaluate replacement via data-driven overrides & UI key mappings
        std::string target_replacement;
        if (evaluate_ui_replacement(obj_path, utf8_text, target_replacement)) {
            set_component_text_and_font(comp, target_replacement, set_txt_mi, SetText_mi, set_font_mi, set_dirty_mi, force_mesh_update_mi);
            replaced_count++;
            LOGI("[LLC-FONT] Replaced '%s' -> '%s' (path: %s)",
                 utf8_text.c_str(), target_replacement.c_str(), obj_path.c_str());
            utf8_text = target_replacement;
            has_cjk = utf8_has_cjk(utf8_text);
        }

        // Detect Title screen or Auth dialog presence
        if (is_title_auth_element(obj_path, trimmed_text) || is_title_auth_element(obj_path, trim_copy(utf8_text))) {
            result.title_or_auth_dialog_active = true;
        }

        // 4. Content-aware & safe font swapping for title UI text
        if (localization_active && g_shs_font && get_font_mi && set_font_mi && get_name_mi) {
            bool should_unify = is_force_font_unify_enabled() || has_cjk;
            if (should_unify) {
                Il2CppObject* cur_f = il2cpp_runtime_invoke_fn(get_font_mi, comp, nullptr, &exc);
                if (cur_f && !exc && cur_f != g_shs_font) {
                    Il2CppObject* fn_obj = il2cpp_runtime_invoke_fn(get_name_mi, cur_f, nullptr, &exc);
                    if (fn_obj && !exc) {
                        const uint16_t* fu = il2cpp_string_chars_fn((Il2CppString*)fn_obj);
                        char fnbuf[64] = {0};
                        for (int x = 0; x < 63 && fu[x]; ++x) fnbuf[x] = (char)fu[x];

                        bool is_target = is_cjk_body_font_name(fnbuf);
                        if (is_force_font_unify_enabled()) {
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
