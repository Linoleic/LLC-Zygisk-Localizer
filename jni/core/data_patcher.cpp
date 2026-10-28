#include "data_patcher.hpp"
#include "storage.hpp"
#include "json_loader.hpp"
#include <unistd.h>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <string>
#include <unordered_map>

int patch_ui_list(Il2CppObject* ui_list_obj, const char* list_name) {
    if (!ui_list_obj) return 0;
    Il2CppObject* dic = *(Il2CppObject**)((char*)ui_list_obj + 16);
    if (!dic) return 0;

    Il2CppClass* dic_class = il2cpp_object_get_class_fn(dic);
    const MethodInfo* set_item_mi = dic_class ? il2cpp_class_get_method_from_name_fn(dic_class, "set_Item", 2) : nullptr;
    if (!set_item_mi) {
        LOGE("[LLC-DATA] Failed to resolve set_Item on %s._dic", list_name);
        return 0;
    }

    // Step 1: Update existing entries in-place
    Il2CppObject* entries = *(Il2CppObject**)((char*)dic + 24);
    int count = *(int*)((char*)dic + 32);
    int updated_in_place = 0;

    if (entries && count > 0) {
        char* items_base = (char*)entries + sizeof(void*) * 4;
        for (int e = 0; e < count; ++e) {
            char* entry_ptr = items_base + e * 24;
            Il2CppString* k = *(Il2CppString**)(entry_ptr + 8);
            Il2CppObject* v = *(Il2CppObject**)(entry_ptr + 16);
            if (!k || !v) continue;
            char kbuf[128] = {0};
            const uint16_t* u = il2cpp_string_chars_fn(k);
            for (int x = 0; x < 127 && u[x]; ++x) kbuf[x] = (char)u[x];

            auto it = g_ui_map.find(kbuf);
            if (it != g_ui_map.end()) {
                *(Il2CppString**)((char*)v + 24) = il2cpp_string_new_fn(it->second.c_str());
                updated_in_place++;
            }
        }
    }

    // Step 2: Determine element class for new insertions
    Il2CppClass* luil_class = il2cpp_object_get_class_fn(ui_list_obj);
    const MethodInfo* get_data_mi = luil_class ? il2cpp_class_get_method_from_name_fn(luil_class, "GetData", 1) : nullptr;
    Il2CppClass* elem_class = nullptr;
    if (get_data_mi) {
        Il2CppString* dummy_k = il2cpp_string_new_fn("__dummy_lookup__");
        void* args[1] = { dummy_k };
        Il2CppException* exc = nullptr;
        Il2CppObject* dummy_item = il2cpp_runtime_invoke_fn(get_data_mi, ui_list_obj, args, &exc);
        if (dummy_item) elem_class = il2cpp_object_get_class_fn(dummy_item);
    }

    // Step 3: Insert all remaining UI keys into dictionary
    int inserted = 0;
    if (elem_class) {
        Il2CppException* exc = nullptr;
        for (const auto& kv : g_ui_map) {
            Il2CppObject* elem = il2cpp_object_new_fn(elem_class);
            Il2CppString* k_str = il2cpp_string_new_fn(kv.first.c_str());
            Il2CppString* c_str = il2cpp_string_new_fn(kv.second.c_str());
            *(Il2CppString**)((char*)elem + 16) = k_str;
            *(Il2CppString**)((char*)elem + 24) = c_str;
            void* si_args[2] = { k_str, elem };
            il2cpp_runtime_invoke_fn(set_item_mi, dic, si_args, &exc);
            if (!exc) inserted++;
        }
    }

    LOGI("[LLC-DATA] %s synchronized: %d in-place updated, %d total keys inserted in _dic.",
         list_name, updated_in_place, inserted);
    return updated_in_place + inserted;
}

int patch_personality_list(Il2CppObject* pers_list_obj) {
    if (!pers_list_obj) return 0;
    Il2CppObject* dic = *(Il2CppObject**)((char*)pers_list_obj + 16);
    if (!dic) return 0;

    Il2CppObject* entries = *(Il2CppObject**)((char*)dic + 24);
    int count = *(int*)((char*)dic + 32);
    if (!entries || count <= 0) return 0;

    int patched = 0;
    char* items_base = (char*)entries + sizeof(void*) * 4;
    for (int e = 0; e < count; ++e) {
        char* entry_ptr = items_base + e * 24;
        Il2CppObject* v = *(Il2CppObject**)(entry_ptr + 16);
        if (!v) continue;

        int id = *(int*)((char*)v + 16);
        int key_int = *(int*)(entry_ptr + 8);
        if (id == 0 && key_int != 0) id = key_int;

        if (g_personalities.find(id) == g_personalities.end()) {
            Il2CppString* k_str = *(Il2CppString**)(entry_ptr + 8);
            if (k_str && (uintptr_t)k_str > 0x10000) {
                const uint16_t* u = il2cpp_string_chars_fn(k_str);
                char kbuf[32] = {0};
                for (int x = 0; x < 31 && u[x]; ++x) kbuf[x] = (char)u[x];
                int parsed_id = atoi(kbuf);
                if (parsed_id != 0) id = parsed_id;
            }
        }

        auto it = g_personalities.find(id);
        if (it != g_personalities.end()) {
            if (!it->second.name.empty()) {
                *(Il2CppString**)((char*)v + 24) = il2cpp_string_new_fn(it->second.name.c_str());
            }
            if (!it->second.desc.empty()) {
                *(Il2CppString**)((char*)v + 32) = il2cpp_string_new_fn(it->second.desc.c_str());
            }
            if (!it->second.title.empty()) {
                *(Il2CppString**)((char*)v + 40) = il2cpp_string_new_fn(it->second.title.c_str());
            }
            patched++;
        }
    }
    LOGI("[LLC-DATA] _personalityList patched: %d / %d entries updated with Chinese translations!", patched, count);
    return patched;
}

int patch_passive_list(Il2CppObject* passive_list_obj) {
    if (!passive_list_obj) return 0;
    Il2CppObject* dic = *(Il2CppObject**)((char*)passive_list_obj + 16);
    if (!dic) return 0;

    Il2CppObject* entries = *(Il2CppObject**)((char*)dic + 24);
    int count = *(int*)((char*)dic + 32);
    if (!entries || count <= 0) return 0;

    int patched = 0;
    char* items_base = (char*)entries + sizeof(void*) * 4;
    for (int e = 0; e < count; ++e) {
        char* entry_ptr = items_base + e * 24;
        Il2CppObject* v = *(Il2CppObject**)(entry_ptr + 16);
        if (!v) continue;

        int id = 0;
        Il2CppString* id_str = *(Il2CppString**)((char*)v + 16);
        if (id_str && (uintptr_t)id_str > 0x10000) {
            const uint16_t* u = il2cpp_string_chars_fn(id_str);
            char buf[32] = {0};
            for (int x = 0; x < 31 && u[x]; ++x) buf[x] = (char)u[x];
            id = atoi(buf);
        }
        if (id == 0) {
            Il2CppString* k_str = *(Il2CppString**)(entry_ptr + 8);
            if (k_str && (uintptr_t)k_str > 0x10000) {
                const uint16_t* u = il2cpp_string_chars_fn(k_str);
                char buf[32] = {0};
                for (int x = 0; x < 31 && u[x]; ++x) buf[x] = (char)u[x];
                id = atoi(buf);
            }
        }
        if (id == 0) {
            id = *(int*)(entry_ptr + 8);
        }

        auto it = g_passives.find(id);
        if (it != g_passives.end()) {
            if (!it->second.name.empty()) {
                *(Il2CppString**)((char*)v + 24) = il2cpp_string_new_fn(it->second.name.c_str());
            }
            if (!it->second.desc.empty()) {
                *(Il2CppString**)((char*)v + 32) = il2cpp_string_new_fn(it->second.desc.c_str());
            }
            if (!it->second.summary.empty()) {
                *(Il2CppString**)((char*)v + 40) = il2cpp_string_new_fn(it->second.summary.c_str());
            }
            if (!it->second.flavor.empty()) {
                *(Il2CppString**)((char*)v + 48) = il2cpp_string_new_fn(it->second.flavor.c_str());
            }
            patched++;
        }
    }
    LOGI("[LLC-DATA] _passiveList patched: %d / %d entries updated with Chinese translations!", patched, count);
    return patched;
}

int patch_buf_list(Il2CppObject* buf_list_obj, const char* name_prefix) {
    if (!buf_list_obj) return 0;
    Il2CppObject* dic = *(Il2CppObject**)((char*)buf_list_obj + 16);
    if (!dic) return 0;

    Il2CppObject* entries = *(Il2CppObject**)((char*)dic + 24);
    int count = *(int*)((char*)dic + 32);
    if (!entries || count <= 0) return 0;

    int patched = 0;
    char* items_base = (char*)entries + sizeof(void*) * 4;
    for (int e = 0; e < count; ++e) {
        char* entry_ptr = items_base + e * 24;
        Il2CppObject* v = *(Il2CppObject**)(entry_ptr + 16);
        if (!v) continue;

        std::string id_str_val;
        Il2CppString* id_str = *(Il2CppString**)((char*)v + 16);
        if (id_str && (uintptr_t)id_str > 0x10000) {
            const uint16_t* u = il2cpp_string_chars_fn(id_str);
            for (int x = 0; x < 127 && u[x]; ++x) id_str_val.push_back((char)u[x]);
        }
        if (id_str_val.empty()) {
            Il2CppString* k_str = *(Il2CppString**)(entry_ptr + 8);
            if (k_str && (uintptr_t)k_str > 0x10000) {
                const uint16_t* u = il2cpp_string_chars_fn(k_str);
                for (int x = 0; x < 127 && u[x]; ++x) id_str_val.push_back((char)u[x]);
            }
        }

        auto it = g_bufs.find(id_str_val);
        if (it == g_bufs.end()) {
            it = g_battle_keywords.find(id_str_val);
        }

        if (it != g_bufs.end() && it != g_battle_keywords.end()) {
            if (!it->second.name.empty()) {
                *(Il2CppString**)((char*)v + 24) = il2cpp_string_new_fn(it->second.name.c_str());
            }
            if (!it->second.desc.empty()) {
                *(Il2CppString**)((char*)v + 32) = il2cpp_string_new_fn(it->second.desc.c_str());
            }
            if (!it->second.summary.empty()) {
                *(Il2CppString**)((char*)v + 40) = il2cpp_string_new_fn(it->second.summary.c_str());
            }
            if (!it->second.flavor.empty()) {
                *(Il2CppString**)((char*)v + 48) = il2cpp_string_new_fn(it->second.flavor.c_str());
            }
            patched++;
        }
    }
    LOGI("[LLC-DATA] %s patched: %d / %d entries updated with Chinese translations!", name_prefix, patched, count);
    return patched;
}

int patch_ego_list(Il2CppObject* ego_list_obj) {
    if (!ego_list_obj) return 0;
    Il2CppObject* dic = *(Il2CppObject**)((char*)ego_list_obj + 16);
    if (!dic) return 0;

    Il2CppObject* entries = *(Il2CppObject**)((char*)dic + 24);
    int count = *(int*)((char*)dic + 32);
    if (!entries || count <= 0) return 0;

    int patched = 0;
    char* items_base = (char*)entries + sizeof(void*) * 4;
    for (int e = 0; e < count; ++e) {
        char* entry_ptr = items_base + e * 24;
        Il2CppObject* v = *(Il2CppObject**)(entry_ptr + 16);
        if (!v) continue;

        int id = 0;
        Il2CppString* id_str = *(Il2CppString**)((char*)v + 16);
        if (id_str && (uintptr_t)id_str > 0x10000) {
            const uint16_t* u = il2cpp_string_chars_fn(id_str);
            char buf[32] = {0};
            for (int x = 0; x < 31 && u[x]; ++x) buf[x] = (char)u[x];
            id = atoi(buf);
        }
        if (id == 0) {
            Il2CppString* k_str = *(Il2CppString**)(entry_ptr + 8);
            if (k_str && (uintptr_t)k_str > 0x10000) {
                const uint16_t* u = il2cpp_string_chars_fn(k_str);
                char buf[32] = {0};
                for (int x = 0; x < 31 && u[x]; ++x) buf[x] = (char)u[x];
                id = atoi(buf);
            }
        }
        if (id == 0) {
            id = *(int*)(entry_ptr + 8);
        }

        auto it = g_egos.find(id);
        if (it != g_egos.end()) {
            if (!it->second.name.empty()) {
                *(Il2CppString**)((char*)v + 24) = il2cpp_string_new_fn(it->second.name.c_str());
            }
            if (!it->second.desc.empty()) {
                *(Il2CppString**)((char*)v + 32) = il2cpp_string_new_fn(it->second.desc.c_str());
            }
            patched++;
        }
    }
    LOGI("[LLC-DATA] _egoList patched: %d / %d entries updated with Chinese translations!", patched, count);
    return patched;
}

int patch_character_list(Il2CppObject* char_list_obj) {
    if (!char_list_obj) return 0;
    Il2CppObject* dic = *(Il2CppObject**)((char*)char_list_obj + 16);
    if (!dic) return 0;

    Il2CppObject* entries = *(Il2CppObject**)((char*)dic + 24);
    int count = *(int*)((char*)dic + 32);
    if (!entries || count <= 0) return 0;

    int patched = 0;
    char* items_base = (char*)entries + sizeof(void*) * 4;
    for (int e = 0; e < count; ++e) {
        char* entry_ptr = items_base + e * 24;
        Il2CppObject* v = *(Il2CppObject**)(entry_ptr + 16);
        if (!v) continue;

        int id = 0;
        Il2CppString* id_str = *(Il2CppString**)((char*)v + 16);
        if (id_str && (uintptr_t)id_str > 0x10000) {
            const uint16_t* u = il2cpp_string_chars_fn(id_str);
            char buf[32] = {0};
            for (int x = 0; x < 31 && u[x]; ++x) buf[x] = (char)u[x];
            id = atoi(buf);
        }
        if (id == 0) {
            id = *(int*)(entry_ptr + 8);
        }

        auto it = g_characters.find(id);
        if (it != g_characters.end()) {
            if (!it->second.name.empty()) {
                *(Il2CppString**)((char*)v + 24) = il2cpp_string_new_fn(it->second.name.c_str());
            }
            patched++;
        }
    }
    LOGI("[LLC-DATA] _characterList patched: %d / %d entries updated with Chinese translations!", patched, count);
    return patched;
}

int patch_skill_list(Il2CppObject* skill_list_obj) {
    if (!skill_list_obj) return 0;
    Il2CppObject* dic = *(Il2CppObject**)((char*)skill_list_obj + 16);
    if (!dic) return 0;

    Il2CppObject* entries = *(Il2CppObject**)((char*)dic + 24);
    int count = *(int*)((char*)dic + 32);
    if (!entries || count <= 0) return 0;

    int patched = 0;
    char* items_base = (char*)entries + sizeof(void*) * 4;
    for (int e = 0; e < count; ++e) {
        char* entry_ptr = items_base + e * 24;
        Il2CppObject* v = *(Il2CppObject**)(entry_ptr + 16);
        if (!v) continue;

        int id = 0;
        Il2CppString* id_str = *(Il2CppString**)((char*)v + 16);
        if (id_str && (uintptr_t)id_str > 0x10000) {
            const uint16_t* u = il2cpp_string_chars_fn(id_str);
            char buf[32] = {0};
            for (int x = 0; x < 31 && u[x]; ++x) buf[x] = (char)u[x];
            id = atoi(buf);
        }
        if (id == 0) {
            Il2CppString* k_str = *(Il2CppString**)(entry_ptr + 8);
            if (k_str && (uintptr_t)k_str > 0x10000) {
                const uint16_t* u = il2cpp_string_chars_fn(k_str);
                char buf[32] = {0};
                for (int x = 0; x < 31 && u[x]; ++x) buf[x] = (char)u[x];
                id = atoi(buf);
            }
        }
        if (id == 0) {
            id = *(int*)(entry_ptr + 8);
        }

        auto it = g_skills.find(id);
        if (it != g_skills.end()) {
            Il2CppObject* level_list = *(Il2CppObject**)((char*)v + 24);
            if (level_list) {
                Il2CppObject* ll_items = *(Il2CppObject**)((char*)level_list + 16);
                int ll_size = *(int*)((char*)level_list + 24);
                if (ll_items && ll_size > 0) {
                    Il2CppObject** lvl_arr = (Il2CppObject**)((char*)ll_items + sizeof(void*)*4);
                    for (int l = 0; l < ll_size && l < (int)it->second.levelList.size(); ++l) {
                        Il2CppObject* lvl_obj = lvl_arr[l];
                        if (!lvl_obj) continue;
                        const auto& s_lvl = it->second.levelList[l];

                        if (!s_lvl.name.empty()) {
                            *(Il2CppString**)((char*)lvl_obj + 40) = il2cpp_string_new_fn(s_lvl.name.c_str());
                        }
                        if (!s_lvl.desc.empty()) {
                            *(Il2CppString**)((char*)lvl_obj + 32) = il2cpp_string_new_fn(s_lvl.desc.c_str());
                        }
                        if (!s_lvl.flavor.empty()) {
                            *(Il2CppString**)((char*)lvl_obj + 48) = il2cpp_string_new_fn(s_lvl.flavor.c_str());
                        }
                        if (!s_lvl.rawDesc.empty()) {
                            *(Il2CppString**)((char*)lvl_obj + 56) = il2cpp_string_new_fn(s_lvl.rawDesc.c_str());
                        }
                        if (!s_lvl.abName.empty()) {
                            *(Il2CppString**)((char*)lvl_obj + 64) = il2cpp_string_new_fn(s_lvl.abName.c_str());
                        }

                        Il2CppObject* coinlist = *(Il2CppObject**)((char*)lvl_obj + 72);
                        if (coinlist) {
                            Il2CppObject* cl_items = *(Il2CppObject**)((char*)coinlist + 16);
                            int cl_size = *(int*)((char*)coinlist + 24);
                            if (cl_items && cl_size > 0) {
                                Il2CppObject** cl_arr = (Il2CppObject**)((char*)cl_items + sizeof(void*)*4);
                                for (int c = 0; c < cl_size && c < (int)s_lvl.coinlist.size(); ++c) {
                                    Il2CppObject* coin_obj = cl_arr[c];
                                    if (!coin_obj) continue;
                                    const auto& s_coin = s_lvl.coinlist[c];

                                    Il2CppObject* coindescs = *(Il2CppObject**)((char*)coin_obj + 16);
                                    if (coindescs) {
                                        Il2CppObject* cd_items = *(Il2CppObject**)((char*)coindescs + 16);
                                        int cd_size = *(int*)((char*)coindescs + 24);
                                        if (cd_items && cd_size > 0) {
                                            Il2CppObject** cd_arr = (Il2CppObject**)((char*)cd_items + sizeof(void*)*4);
                                            for (int d = 0; d < cd_size && d < (int)s_coin.coindescs.size(); ++d) {
                                                Il2CppObject* cd_obj = cd_arr[d];
                                                if (!cd_obj) continue;
                                                const auto& s_cd = s_coin.coindescs[d];

                                                if (!s_cd.desc.empty()) {
                                                    *(Il2CppString**)((char*)cd_obj + 16) = il2cpp_string_new_fn(s_cd.desc.c_str());
                                                }
                                                if (!s_cd.summary.empty()) {
                                                    *(Il2CppString**)((char*)cd_obj + 24) = il2cpp_string_new_fn(s_cd.summary.c_str());
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
            patched++;
        }
    }
    LOGI("[LLC-DATA] _skillList patched: %d / %d entries updated with Chinese translations!", patched, count);
    return patched;
}

int patch_voice_list(Il2CppObject* voice_container_obj, const char* name) {
    if (!voice_container_obj) return 0;
    Il2CppObject* dic = *(Il2CppObject**)((char*)voice_container_obj + 16);
    if (!dic) return 0;

    Il2CppObject* entries = *(Il2CppObject**)((char*)dic + 24);
    int count = *(int*)((char*)dic + 32);
    if (!entries || count <= 0) return 0;

    int patched = 0;
    char* items_base = (char*)entries + sizeof(void*) * 4;
    for (int e = 0; e < count; ++e) {
        char* entry_ptr = items_base + e * 24;
        Il2CppObject* v = *(Il2CppObject**)(entry_ptr + 16);
        if (!v) continue;

        Il2CppObject* dlist = *(Il2CppObject**)((char*)v + 16);
        if (!dlist) continue;

        Il2CppObject* ditems = *(Il2CppObject**)((char*)dlist + 16);
        int dsize = *(int*)((char*)dlist + 24);
        if (!ditems || dsize <= 0) continue;

        Il2CppObject** arr = (Il2CppObject**)((char*)ditems + sizeof(void*) * 4);
        for (int j = 0; j < dsize; ++j) {
            Il2CppObject* item = arr[j];
            if (!item) continue;

            Il2CppString* id_str = *(Il2CppString**)((char*)item + 16);
            if (!id_str || (uintptr_t)id_str <= 0x10000) continue;

            const uint16_t* u = il2cpp_string_chars_fn(id_str);
            char id_buf[128] = {0};
            for (int x = 0; x < 127 && u[x]; ++x) id_buf[x] = (char)u[x];

            auto it = g_voice_dlgs.find(id_buf);
            if (it != g_voice_dlgs.end()) {
                if (!it->second.desc.empty()) {
                    *(Il2CppString**)((char*)item + 24) = il2cpp_string_new_fn(it->second.desc.c_str());
                }
                if (!it->second.dlg.empty()) {
                    *(Il2CppString**)((char*)item + 32) = il2cpp_string_new_fn(it->second.dlg.c_str());
                }
                patched++;
            }
        }
    }
    LOGI("[LLC-DATA] %s patched: %d voice lines updated with Chinese translations!", name, patched);
    return patched;
}

int patch_theater_list(Il2CppObject* theater_obj) {
    if (!theater_obj) return 0;
    Il2CppObject* dic = *(Il2CppObject**)((char*)theater_obj + 16);
    if (!dic) return 0;

    Il2CppObject* entries = *(Il2CppObject**)((char*)dic + 24);
    int count = *(int*)((char*)dic + 32);
    if (!entries || count <= 0) return 0;

    int patched = 0;
    char* items_base = (char*)entries + sizeof(void*) * 4;
    for (int e = 0; e < count; ++e) {
        char* entry_ptr = items_base + e * 24;
        Il2CppObject* v = *(Il2CppObject**)(entry_ptr + 16);
        if (!v) continue;

        char id_buf[64] = {0};
        Il2CppString* id_str = *(Il2CppString**)((char*)v + 16);
        if (id_str && (uintptr_t)id_str > 0x10000) {
            const uint16_t* u = il2cpp_string_chars_fn(id_str);
            for (int x = 0; x < 63 && u[x]; ++x) id_buf[x] = (char)u[x];
        }
        if (id_buf[0] == '\0') {
            Il2CppString* k_str = *(Il2CppString**)(entry_ptr + 8);
            if (k_str && (uintptr_t)k_str > 0x10000) {
                const uint16_t* u = il2cpp_string_chars_fn(k_str);
                for (int x = 0; x < 63 && u[x]; ++x) id_buf[x] = (char)u[x];
            }
        }
        if (id_buf[0] == '\0') {
            int kid = *(int*)(entry_ptr + 8);
            snprintf(id_buf, sizeof(id_buf), "%d", kid);
        }

        auto it = g_theater_map.find(id_buf);
        if (it != g_theater_map.end()) {
            if (!it->second.title.empty()) {
                *(Il2CppString**)((char*)v + 24) = il2cpp_string_new_fn(it->second.title.c_str());
            }
            if (!it->second.desc.empty()) {
                *(Il2CppString**)((char*)v + 32) = il2cpp_string_new_fn(it->second.desc.c_str());
            }
            patched++;
        }
    }
    LOGI("[LLC-DATA] _storyTheater patched: %d / %d entries updated with Chinese translations!", patched, count);
    return patched;
}

int patch_banner_list(Il2CppObject* banner_obj) {
    if (!banner_obj) return 0;
    Il2CppObject* dic = *(Il2CppObject**)((char*)banner_obj + 16);
    if (!dic) return 0;

    Il2CppObject* entries = *(Il2CppObject**)((char*)dic + 24);
    int count = *(int*)((char*)dic + 32);
    if (!entries || count <= 0) return 0;

    int patched = 0;
    char* items_base = (char*)entries + sizeof(void*) * 4;
    for (int e = 0; e < count; ++e) {
        char* entry_ptr = items_base + e * 24;
        Il2CppObject* v = *(Il2CppObject**)(entry_ptr + 16);
        if (!v) continue;

        int id = *(int*)((char*)v + 16);
        if (id == 0) id = *(int*)(entry_ptr + 8);

        auto it = g_banner_map.find(id);
        if (it != g_banner_map.end()) {
            if (!it->second.name.empty()) {
                *(Il2CppString**)((char*)v + 24) = il2cpp_string_new_fn(it->second.name.c_str());
            }
            if (!it->second.desc.empty()) {
                *(Il2CppString**)((char*)v + 32) = il2cpp_string_new_fn(it->second.desc.c_str());
            }
            if (!it->second.flavor.empty()) {
                *(Il2CppString**)((char*)v + 40) = il2cpp_string_new_fn(it->second.flavor.c_str());
            }
            patched++;
        }
    }
    LOGI("[LLC-DATA] _userBanner patched: %d / %d entries updated with Chinese translations!", patched, count);
    return patched;
}

int patch_stage_chapter_list(Il2CppObject* chapter_obj) {
    if (!chapter_obj) return 0;
    Il2CppObject* dic = *(Il2CppObject**)((char*)chapter_obj + 16);
    if (!dic) return 0;

    Il2CppObject* entries = *(Il2CppObject**)((char*)dic + 24);
    int count = *(int*)((char*)dic + 32);
    if (!entries || count <= 0) return 0;

    int patched = 0;
    char* items_base = (char*)entries + sizeof(void*) * 4;
    for (int e = 0; e < count; ++e) {
        char* entry_ptr = items_base + e * 24;
        Il2CppObject* v = *(Il2CppObject**)(entry_ptr + 16);
        if (!v) continue;

        char id_buf[64] = {0};
        Il2CppString* id_str = *(Il2CppString**)((char*)v + 16);
        if (id_str && (uintptr_t)id_str > 0x10000) {
            const uint16_t* u = il2cpp_string_chars_fn(id_str);
            for (int x = 0; x < 63 && u[x]; ++x) id_buf[x] = (char)u[x];
        }
        if (id_buf[0] == '\0') {
            Il2CppString* k_str = *(Il2CppString**)(entry_ptr + 8);
            if (k_str && (uintptr_t)k_str > 0x10000) {
                const uint16_t* u = il2cpp_string_chars_fn(k_str);
                for (int x = 0; x < 63 && u[x]; ++x) id_buf[x] = (char)u[x];
            }
        }
        if (id_buf[0] == '\0') {
            int kid = *(int*)(entry_ptr + 8);
            snprintf(id_buf, sizeof(id_buf), "%d", kid);
        }

        auto it = g_stage_chapter_map.find(id_buf);
        if (it != g_stage_chapter_map.end()) {
            if (!it->second.company.empty()) *(Il2CppString**)((char*)v + 24) = il2cpp_string_new_fn(it->second.company.c_str());
            if (!it->second.area.empty()) *(Il2CppString**)((char*)v + 32) = il2cpp_string_new_fn(it->second.area.c_str());
            if (!it->second.chaptertitle.empty()) *(Il2CppString**)((char*)v + 40) = il2cpp_string_new_fn(it->second.chaptertitle.c_str());
            if (!it->second.chapter.empty()) *(Il2CppString**)((char*)v + 48) = il2cpp_string_new_fn(it->second.chapter.c_str());
            if (!it->second.chapterNumber.empty()) *(Il2CppString**)((char*)v + 56) = il2cpp_string_new_fn(it->second.chapterNumber.c_str());
            if (!it->second.timeline.empty()) *(Il2CppString**)((char*)v + 64) = il2cpp_string_new_fn(it->second.timeline.c_str());
            patched++;
        }
    }
    LOGI("[LLC-DATA] _stageChapter patched: %d / %d entries updated with Chinese translations!", patched, count);
    return patched;
}

int patch_stage_node_list(Il2CppObject* node_obj, const char* name) {
    if (!node_obj) return 0;
    Il2CppObject* dic = *(Il2CppObject**)((char*)node_obj + 16);
    if (!dic) return 0;

    Il2CppObject* entries = *(Il2CppObject**)((char*)dic + 24);
    int count = *(int*)((char*)dic + 32);
    if (!entries || count <= 0) return 0;

    int patched = 0;
    char* items_base = (char*)entries + sizeof(void*) * 4;
    for (int e = 0; e < count; ++e) {
        char* entry_ptr = items_base + e * 24;
        Il2CppObject* v = *(Il2CppObject**)(entry_ptr + 16);
        if (!v) continue;

        int id = *(int*)((char*)v + 16);
        if (id == 0) id = *(int*)(entry_ptr + 8);

        auto it = g_stage_node_map.find(id);
        if (it != g_stage_node_map.end()) {
            if (!it->second.title.empty()) *(Il2CppString**)((char*)v + 24) = il2cpp_string_new_fn(it->second.title.c_str());
            if (!it->second.desc.empty()) *(Il2CppString**)((char*)v + 32) = il2cpp_string_new_fn(it->second.desc.c_str());
            if (!it->second.place.empty()) *(Il2CppString**)((char*)v + 40) = il2cpp_string_new_fn(it->second.place.c_str());
            patched++;
        }
    }
    LOGI("[LLC-DATA] %s patched: %d / %d entries updated with Chinese translations!", name, patched, count);
    return patched;
}

void patch_scenario_object(Il2CppObject* scn, const char* scen_id) {
    if (!scn) return;

    std::string s_id;
    if (scen_id && *scen_id) {
        s_id = scen_id;
    } else {
        // Read from Scenario.ID (offset 16)
        Il2CppString* id_str = *(Il2CppString**)((char*)scn + 16);
        if (id_str && (uintptr_t)id_str > 0x10000 && il2cpp_string_chars_fn) {
            const uint16_t* u = il2cpp_string_chars_fn(id_str);
            if (u) {
                char buf[128] = {0};
                for (int x = 0; x < 127 && u[x]; ++x) buf[x] = (char)u[x];
                s_id = buf;
            }
        }
    }
    if (s_id.empty()) return;

    std::string path1 = std::string("/data/data/com.ProjectMoon.LimbusCompany/files/Lang/LLC_zh-CN/StoryData/") + s_id + ".json";
    std::string path2 = std::string("/data/user/0/com.ProjectMoon.LimbusCompany/files/Lang/LLC_zh-CN/StoryData/") + s_id + ".json";
    std::string path3 = std::string("/sdcard/Android/data/com.ProjectMoon.LimbusCompany/files/Lang/LLC_zh-CN/StoryData/") + s_id + ".json";
    std::string chosen_path;
    if (access(path1.c_str(), R_OK) == 0) {
        chosen_path = path1;
    } else if (access(path2.c_str(), R_OK) == 0) {
        chosen_path = path2;
    } else if (access(path3.c_str(), R_OK) == 0) {
        chosen_path = path3;
    } else {
        // Try uppercase or lowercase fallback
        std::string upper_id = s_id;
        for (auto& c : upper_id) c = toupper((unsigned char)c);
        std::string p_up = std::string("/data/data/com.ProjectMoon.LimbusCompany/files/Lang/LLC_zh-CN/StoryData/") + upper_id + ".json";
        if (access(p_up.c_str(), R_OK) == 0) {
            chosen_path = p_up;
        } else {
            LOGW("[LLC-DATA] Scenario file for '%s' not found on disk!", s_id.c_str());
            return;
        }
    }

    std::unordered_map<int, ScenarioDialogue> dialogues;
    parse_scenario_json_file(chosen_path, dialogues);
    if (dialogues.empty()) {
        LOGW("[LLC-DATA] Scenario file '%s' had no parseable dialogues", chosen_path.c_str());
        return;
    }

    Il2CppClass* scn_class = il2cpp_object_get_class_fn(scn);
    if (!scn_class) return;

    void* dl_field = il2cpp_class_get_field_from_name_fn(scn_class, "Scenarios");
    if (!dl_field) dl_field = il2cpp_class_get_field_from_name_fn(scn_class, "_scenarios");
    if (!dl_field) dl_field = il2cpp_class_get_field_from_name_fn(scn_class, "dataList");
    if (!dl_field) {
        void* iter = nullptr;
        void* f = nullptr;
        while ((f = il2cpp_class_get_fields_fn(scn_class, &iter))) {
            const char* fn = il2cpp_field_get_name_fn(f);
            if (fn && (strcasecmp(fn, "Scenarios") == 0 || strcasecmp(fn, "dataList") == 0 || strstr(fn, "scenario") || strstr(fn, "Scenario"))) {
                dl_field = f;
                break;
            }
        }
    }
    if (!dl_field) {
        LOGE("[LLC-DATA] Could not find Scenarios field on Scenario object!");
        return;
    }

    uint32_t dl_off = il2cpp_field_get_offset_fn(dl_field);
    Il2CppObject* dlist = *(Il2CppObject**)((char*)scn + dl_off);
    if (!dlist) {
        LOGW("[LLC-DATA] Scenario.Scenarios list pointer is null!");
        return;
    }

    Il2CppObject* ditems = *(Il2CppObject**)((char*)dlist + 16);
    int dsize = *(int*)((char*)dlist + 24);
    if (!ditems || dsize <= 0) return;

    Il2CppObject** arr = (Il2CppObject**)((char*)ditems + sizeof(void*)*4);

    static uint32_t off_id = 20;
    static uint32_t off_content = 56;
    static uint32_t off_place = 24;
    static uint32_t off_teller = 40;
    static uint32_t off_title = 48;
    static bool offsets_resolved = false;

    if (!offsets_resolved && dsize > 0 && arr[0]) {
        Il2CppClass* item_class = il2cpp_object_get_class_fn(arr[0]);
        void* iter = nullptr;
        void* f = nullptr;
        while ((f = il2cpp_class_get_fields_fn(item_class, &iter))) {
            const char* fn = il2cpp_field_get_name_fn(f);
            uint32_t off = il2cpp_field_get_offset_fn(f);
            if (strcasecmp(fn, "id") == 0) off_id = off;
            else if (strcasecmp(fn, "content") == 0) off_content = off;
            else if (strcasecmp(fn, "place") == 0) off_place = off;
            else if (strcasecmp(fn, "teller") == 0) off_teller = off;
            else if (strcasecmp(fn, "title") == 0) off_title = off;
        }
        offsets_resolved = true;
        LOGI("[LLC-DATA] Scenario Dialog item class: %s, offsets: id=%u, content=%u, place=%u, teller=%u, title=%u",
             il2cpp_class_get_name_fn(item_class), off_id, off_content, off_place, off_teller, off_title);
    }

    int patched = 0;
    for (int i = 0; i < dsize; ++i) {
        Il2CppObject* item = arr[i];
        if (!item) continue;
        int id = *(int*)((char*)item + off_id);
        auto it = dialogues.find(id);
        if (it != dialogues.end()) {
            if (off_content && !it->second.content.empty()) {
                *(Il2CppString**)((char*)item + off_content) = il2cpp_string_new_fn(it->second.content.c_str());
            }
            if (off_place && !it->second.place.empty()) {
                *(Il2CppString**)((char*)item + off_place) = il2cpp_string_new_fn(it->second.place.c_str());
            }
            if (off_teller && !it->second.teller.empty()) {
                *(Il2CppString**)((char*)item + off_teller) = il2cpp_string_new_fn(it->second.teller.c_str());
            }
            if (off_title && !it->second.title.empty()) {
                *(Il2CppString**)((char*)item + off_title) = il2cpp_string_new_fn(it->second.title.c_str());
            }
            patched++;
        }
    }
    LOGI("[LLC-DATA] Scenario '%s' patched: %d / %d lines updated with Chinese dialogues!", s_id.c_str(), patched, dsize);
}
