#include "font_diagnostics.hpp"
#include "font_injector.hpp"
#include "string_table_patcher.hpp"
#include "il2cpp_api.hpp"
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>

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

        fprintf(fp, "\n--- Diagnostic for g_shs_font (%p) ---\n", g_shs_font);
        if (g_shs_font) {
            Il2CppClass* shs_k = il2cpp_object_get_class_fn(g_shs_font);
            fprintf(fp, "Class: %s::%s\n", shs_k ? il2cpp_class_get_namespace_fn(shs_k) : "?", shs_k ? il2cpp_class_get_name_fn(shs_k) : "?");
            Il2CppClass* unity_obj_class = find_class_any("UnityEngine", "Object");
            const MethodInfo* get_n_mi = unity_obj_class ? find_method_in_hierarchy(unity_obj_class, "get_name", 0) : nullptr;
            Il2CppException* e = nullptr;
            Il2CppObject* name_obj = get_n_mi ? il2cpp_runtime_invoke_fn(get_n_mi, g_shs_font, nullptr, &e) : nullptr;
            if (e) {
                log_exception("dump get_name", e);
                fprintf(fp, "get_name exception thrown: %p\n", e);
            } else if (name_obj) {
                const uint16_t* u = il2cpp_string_chars_fn((Il2CppString*)name_obj);
                char nb[128] = {0};
                for (int k = 0; k < 127 && u[k]; ++k) nb[k] = (char)u[k];
                fprintf(fp, "Name: '%s'\n", nb);
            }

            const MethodInfo* has_char_mi = font_asset_class ? find_method_in_hierarchy(font_asset_class, "HasCharacter", 1) : nullptr;
            if (has_char_mi) {
                uint32_t c1 = 0x4E00;
                void* a1[1] = { &c1 };
                e = nullptr;
                Il2CppObject* r1 = il2cpp_runtime_invoke_fn(has_char_mi, g_shs_font, a1, &e);
                fprintf(fp, "HasCharacter(0x4E00): res=%d, exc=%p\n", (r1 && !e) ? (*(bool*)il2cpp_object_unbox_fn(r1) ? 1 : 0) : -1, e);

                uint32_t c2 = 0x8FDE;
                void* a2[1] = { &c2 };
                e = nullptr;
                Il2CppObject* r2 = il2cpp_runtime_invoke_fn(has_char_mi, g_shs_font, a2, &e);
                fprintf(fp, "HasCharacter(0x8FDE '连'): res=%d, exc=%p\n", (r2 && !e) ? (*(bool*)il2cpp_object_unbox_fn(r2) ? 1 : 0) : -1, e);

                uint32_t c3 = 0x6E05;
                void* a3[1] = { &c3 };
                e = nullptr;
                Il2CppObject* r3 = il2cpp_runtime_invoke_fn(has_char_mi, g_shs_font, a3, &e);
                fprintf(fp, "HasCharacter(0x6E05 '清'): res=%d, exc=%p\n", (r3 && !e) ? (*(bool*)il2cpp_object_unbox_fn(r3) ? 1 : 0) : -1, e);
            }

            const MethodInfo* read_def_mi = font_asset_class ? find_method_in_hierarchy(font_asset_class, "ReadFontAssetDefinition", 0) : nullptr;
            if (read_def_mi) {
                e = nullptr;
                il2cpp_runtime_invoke_fn(read_def_mi, g_shs_font, nullptr, &e);
                fprintf(fp, "Called ReadFontAssetDefinition(): exc=%p\n", e);
            }

            if (has_char_mi) {
                uint32_t c2 = 0x8FDE;
                void* a2[1] = { &c2 };
                e = nullptr;
                Il2CppObject* r2 = il2cpp_runtime_invoke_fn(has_char_mi, g_shs_font, a2, &e);
                fprintf(fp, "HasCharacter(0x8FDE '连') AFTER ReadDef: res=%d, exc=%p\n", (r2 && !e) ? (*(bool*)il2cpp_object_unbox_fn(r2) ? 1 : 0) : -1, e);
            }
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
                            Il2CppException* iexc = nullptr;
                            Il2CppObject* iname = il2cpp_runtime_invoke_fn(get_name_mi, item, nullptr, &iexc);
                            if (iexc) {
                                if (!fallback_names.empty()) fallback_names += ", ";
                                fallback_names += "<EXC:";
                                fallback_names += std::to_string((uintptr_t)item);
                                fallback_names += ">";
                            } else if (iname) {
                                const uint16_t* iu = il2cpp_string_chars_fn((Il2CppString*)iname);
                                char ibuf[64] = {0};
                                for (int x = 0; x < 63 && iu[x]; ++x) ibuf[x] = (char)iu[x];
                                if (!fallback_names.empty()) fallback_names += ", ";
                                fallback_names += ibuf;
                                if (strstr(ibuf, "SourceHanSans")) has_fallback = true;
                            } else {
                                if (!fallback_names.empty()) fallback_names += ", ";
                                fallback_names += "<NULL_NAME>";
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
