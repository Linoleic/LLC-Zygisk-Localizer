#include "font_injector.hpp"
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <unordered_set>
#include <vector>
#include <string>

Il2CppObject* g_shs_font = nullptr;
Il2CppObject* g_shs_material = nullptr;
Il2CppObject* g_shs_atlas = nullptr;
Il2CppObject* g_empty_font = nullptr;
Il2CppObject* g_empty_material = nullptr;

static std::unordered_set<Il2CppObject*> g_patched_fonts;

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

void inject_fallback_font_into(Il2CppObject* font_asset) {
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

    Il2CppObject* ditems = *(Il2CppObject**)((char*)tbl + 16);
    int dsize = *(int*)((char*)tbl + 24);
    if (ditems && dsize > 0) {
        Il2CppObject** arr = (Il2CppObject**)((char*)ditems + sizeof(void*)*4);
        for (int k = 0; k < dsize; ++k) {
            if (arr[k] == g_shs_font) {
                g_patched_fonts.insert(font_asset);
                return; // Already present
            }
        }
    }

    const MethodInfo* add_mi = find_method_in_hierarchy(il2cpp_object_get_class_fn(tbl), "Add", 1);
    if (add_mi) {
        void* add_args[1] = { g_shs_font };
        il2cpp_runtime_invoke_fn(add_mi, tbl, add_args, &exc);
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
            LOGI("[LLC-FONT] Injected Chinese fallback into font: '%s' (%p)", buf, font_asset);
        }
    }
}

int get_patched_font_count() {
    return (int)g_patched_fonts.size();
}

int inject_fallback_to_all_loaded_fonts() {
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
            inject_fallback_font_into(font_items[i]);
        }
    }
    return (int)g_patched_fonts.size();
}

void refresh_title_screen_loaders() {
    Il2CppClass* resources_class = find_class_any("UnityEngine", "Resources");
    Il2CppClass* utdl_class = find_class_any("", "UITextDataLoader");
    if (!resources_class || !utdl_class) return;

    const MethodInfo* find_objs_mi = il2cpp_class_get_method_from_name_fn(resources_class, "FindObjectsOfTypeAll", 1);
    if (!find_objs_mi) return;

    const MethodInfo* utdl_set_text_mi = il2cpp_class_get_method_from_name_fn(utdl_class, "SetText", 0);
    if (!utdl_set_text_mi) return;

    Il2CppException* exc = nullptr;
    const Il2CppType* utdl_type = il2cpp_class_get_type_fn(utdl_class);
    Il2CppObject* utdl_type_obj = il2cpp_type_get_object_fn(utdl_type);
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

void refresh_active_ui_texts() {
    Il2CppClass* resources_class = find_class_any("UnityEngine", "Resources");
    Il2CppClass* tmp_text_class = find_class_any("TMPro", "TMP_Text");
    if (!resources_class || !tmp_text_class) return;

    const MethodInfo* find_objs_mi = il2cpp_class_get_method_from_name_fn(resources_class, "FindObjectsOfTypeAll", 1);
    if (!find_objs_mi) return;

    const MethodInfo* get_txt_mi = find_method_in_hierarchy(tmp_text_class, "get_text", 0);
    const MethodInfo* set_txt_mi = find_method_in_hierarchy(tmp_text_class, "set_text", 1);
    const MethodInfo* set_font_mi = find_method_in_hierarchy(tmp_text_class, "set_font", 1);
    const MethodInfo* set_dirty_mi = find_method_in_hierarchy(tmp_text_class, "SetAllDirty", 0);
    if (!get_txt_mi || !set_txt_mi) return;

    Il2CppException* exc = nullptr;
    const Il2CppType* text_type = il2cpp_class_get_type_fn(tmp_text_class);
    Il2CppObject* text_type_obj = il2cpp_type_get_object_fn(text_type);
    void* t_args[1] = { text_type_obj };
    Il2CppObject* text_arr_obj = il2cpp_runtime_invoke_fn(find_objs_mi, nullptr, t_args, &exc);
    if (!text_arr_obj || exc) return;

    uintptr_t total_texts = il2cpp_array_length_fn(text_arr_obj);
    Il2CppObject** text_items = (Il2CppObject**)((char*)text_arr_obj + sizeof(void*)*4);

    static const struct {
        const char* pattern;
        const char* replacement;
    } s_title_replacements[] = {
        { "Change Account", "切换账号" },
        { "Clear all caches", "清除所有缓存" },
        { "Clear old caches", "清除旧缓存" },
        { "TOUCH TO START", "点击开始" },
        { "Touch To Start", "点击开始" },
        { "Touch to start", "点击开始" },
        { "A server error has occurred.", "发生服务器错误。\n请稍后再试。" },
        { "Please try again in a moment.", "" },
        { "Close", "关闭" },
        { "Retry", "重试" },
        { "Terms of Service", "服务条款" }
    };

    int replaced_count = 0;
    for (uintptr_t i = 0; i < total_texts; ++i) {
        Il2CppObject* comp = text_items[i];
        if (!comp) continue;

        Il2CppObject* txt_str = il2cpp_runtime_invoke_fn(get_txt_mi, comp, nullptr, &exc);
        if (!txt_str || exc) continue;

        const uint16_t* u16 = il2cpp_string_chars_fn((Il2CppString*)txt_str);
        if (!u16) continue;

        char t_buf[256] = {0};
        for (int k = 0; k < 255 && u16[k]; ++k) t_buf[k] = (char)u16[k];
        if (t_buf[0] == '\0') continue;


        for (const auto& r : s_title_replacements) {
            if (strcmp(t_buf, r.pattern) == 0 || (strlen(r.pattern) > 5 && strstr(t_buf, r.pattern) != nullptr)) {
                if (r.replacement[0] != '\0') {
                    Il2CppString* new_s = il2cpp_string_new_fn(r.replacement);
                    void* st_args[1] = { new_s };
                    il2cpp_runtime_invoke_fn(set_txt_mi, comp, st_args, &exc);
                    if (g_shs_font && set_font_mi) {
                        void* sf_args[1] = { g_shs_font };
                        il2cpp_runtime_invoke_fn(set_font_mi, comp, sf_args, &exc);
                    }
                    if (set_dirty_mi) {
                        il2cpp_runtime_invoke_fn(set_dirty_mi, comp, nullptr, &exc);
                    }
                    replaced_count++;
                    LOGI("[LLC-FONT] Replaced '%s' -> '%s' (comp: %p)", t_buf, r.replacement, comp);
                }
                break;
            }
        }
    }

    if (replaced_count > 0) {
        LOGI("[LLC-FONT] Replaced %d title screen TMP_Text strings with Chinese!", replaced_count);
    }
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
                Il2CppObject* ditems = *(Il2CppObject**)((char*)tbl + 16);
                int dsize = *(int*)((char*)tbl + 24);
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

