#include "font_injector.hpp"
#include "font_config.hpp"
#include "string_table_patcher.hpp"
#include "main_thread_bridge.hpp"
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <unordered_set>
#include <vector>
#include <string>
#include <atomic>

Il2CppObject* g_shs_font = nullptr;
Il2CppObject* g_shs_material = nullptr;
Il2CppObject* g_shs_atlas = nullptr;
Il2CppObject* g_empty_font = nullptr;
Il2CppObject* g_empty_material = nullptr;

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
    static uintptr_t s_font_bundle_handle = 0;
    if (ab_obj && il2cpp_gchandle_new_fn) {
        if (s_font_bundle_handle && il2cpp_gchandle_free_fn) il2cpp_gchandle_free_fn(s_font_bundle_handle);
        s_font_bundle_handle = il2cpp_gchandle_new_fn(ab_obj, false);
    }
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
    const MethodInfo* set_hide_flags_mi = unity_obj_class ? find_method_in_hierarchy(unity_obj_class, "set_hideFlags", 1) : nullptr;
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

        // HideFlags.DontUnloadUnusedAsset (32) | DontSave (20) = 52
        if (set_hide_flags_mi) {
            int hf = 52;
            void* hf_args[1] = { &hf };
            il2cpp_runtime_invoke_fn(set_hide_flags_mi, asset, hf_args, &exc);
            exc = nullptr;
        }

        if (dont_destroy_mi) {
            void* dd_args[1] = { asset };
            il2cpp_runtime_invoke_fn(dont_destroy_mi, nullptr, dd_args, &exc);
            exc = nullptr;
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

    static uintptr_t s_font_asset_handle = 0;
    static uintptr_t s_font_mat_handle = 0;
    static uintptr_t s_font_atlas_handle = 0;
    static uintptr_t s_font_empty_handle = 0;
    static uintptr_t s_font_empty_mat_handle = 0;

    if (g_shs_font && il2cpp_gchandle_new_fn) {
        if (s_font_asset_handle && il2cpp_gchandle_free_fn) il2cpp_gchandle_free_fn(s_font_asset_handle);
        s_font_asset_handle = il2cpp_gchandle_new_fn(g_shs_font, false);

        Il2CppClass* shs_k = il2cpp_object_get_class_fn(g_shs_font);
        const MethodInfo* read_def_mi = shs_k ? find_method_in_hierarchy(shs_k, "ReadFontAssetDefinition", 0) : nullptr;
        if (read_def_mi) {
            Il2CppException* rexc = nullptr;
            il2cpp_runtime_invoke_fn(read_def_mi, g_shs_font, nullptr, &rexc);
            if (rexc) log_exception("ReadFontAssetDefinition(g_shs_font)", rexc);
            else LOGI("[LLC-FONT] Successfully initialized character lookup tables on g_shs_font via ReadFontAssetDefinition.");
        }
    }
    if (g_shs_material && il2cpp_gchandle_new_fn) {
        if (s_font_mat_handle && il2cpp_gchandle_free_fn) il2cpp_gchandle_free_fn(s_font_mat_handle);
        s_font_mat_handle = il2cpp_gchandle_new_fn(g_shs_material, false);
    }
    if (g_shs_atlas && il2cpp_gchandle_new_fn) {
        if (s_font_atlas_handle && il2cpp_gchandle_free_fn) il2cpp_gchandle_free_fn(s_font_atlas_handle);
        s_font_atlas_handle = il2cpp_gchandle_new_fn(g_shs_atlas, false);
    }
    if (g_empty_font && il2cpp_gchandle_new_fn) {
        if (s_font_empty_handle && il2cpp_gchandle_free_fn) il2cpp_gchandle_free_fn(s_font_empty_handle);
        s_font_empty_handle = il2cpp_gchandle_new_fn(g_empty_font, false);
    }
    if (g_empty_material && il2cpp_gchandle_new_fn) {
        if (s_font_empty_mat_handle && il2cpp_gchandle_free_fn) il2cpp_gchandle_free_fn(s_font_empty_mat_handle);
        s_font_empty_mat_handle = il2cpp_gchandle_new_fn(g_empty_material, false);
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

    static Il2CppObject* s_main_thread_pinned_font = nullptr;
    if (s_main_thread_pinned_font != g_shs_font) {
        Il2CppClass* unity_obj_class = find_class_any("UnityEngine", "Object");
        const MethodInfo* set_hide_flags_mi = unity_obj_class ? find_method_in_hierarchy(unity_obj_class, "set_hideFlags", 1) : nullptr;
        const MethodInfo* dont_destroy_mi = unity_obj_class ? find_method_in_hierarchy(unity_obj_class, "DontDestroyOnLoad", 1) : nullptr;

        auto protect_on_main_thread = [&](Il2CppObject* obj) {
            if (!obj) return;
            Il2CppException* pexc = nullptr;
            if (set_hide_flags_mi) {
                int hf = 52; // HideFlags.DontUnloadUnusedAsset (32) | DontSave (20)
                void* hf_args[1] = { &hf };
                il2cpp_runtime_invoke_fn(set_hide_flags_mi, obj, hf_args, &pexc);
                pexc = nullptr;
            }
            if (dont_destroy_mi) {
                void* dd_args[1] = { obj };
                il2cpp_runtime_invoke_fn(dont_destroy_mi, nullptr, dd_args, &pexc);
            }
        };

        protect_on_main_thread(g_shs_font);
        protect_on_main_thread(g_shs_material);
        protect_on_main_thread(g_shs_atlas);
        protect_on_main_thread(g_empty_font);
        protect_on_main_thread(g_empty_material);

        const MethodInfo* read_def_mi = find_method_in_hierarchy(font_asset_class, "ReadFontAssetDefinition", 0);
        if (read_def_mi) {
            Il2CppException* rexc = nullptr;
            il2cpp_runtime_invoke_fn(read_def_mi, g_shs_font, nullptr, &rexc);
            if (rexc) log_exception("ReadFontAssetDefinition(g_shs_font, main_thread)", rexc);
        }
        s_main_thread_pinned_font = g_shs_font;
    }

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
