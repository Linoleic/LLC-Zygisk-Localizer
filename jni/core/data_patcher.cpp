#include "data_patcher.hpp"
#include "managed_fields.hpp"
#include "storage.hpp"
#include "json_loader.hpp"
#include "resource_paths.hpp"
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
                write_managed_text(v, 24, it->second);
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
            il2cpp_gc_wbarrier_set_field_fn(elem, (void**)((char*)elem + 16), k_str);
            il2cpp_gc_wbarrier_set_field_fn(elem, (void**)((char*)elem + 24), c_str);
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

        int id = 0;
        if (!read_managed_numeric_id(v, id)) continue;

        auto it = g_personalities.find(id);
        if (it != g_personalities.end()) {
            if (!it->second.name.empty()) {
                write_managed_text(v, 24, it->second.name);
            }
            if (!it->second.desc.empty()) {
                write_managed_text(v, 32, it->second.desc);
            }
            if (!it->second.title.empty()) {
                write_managed_text(v, 40, it->second.title);
            }
            patched++;
        }
    }
    LOGI("[LLC-DATA] _personalityList patched: %d / %d rows matched local translations", patched, count);
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
        if (!read_managed_numeric_id(v, id)) continue;

        auto it = g_passives.find(id);
        if (it == g_passives.end()) LOGW("[LLC-MISSING] _passiveList id=%d has no local translation", id);
        if (it != g_passives.end()) {
            if (!it->second.name.empty()) {
                write_managed_text(v, 24, it->second.name);
            }
            if (!it->second.desc.empty()) {
                write_managed_text(v, 32, it->second.desc);
            }
            if (!it->second.summary.empty()) {
                write_managed_text(v, 40, it->second.summary);
            }
            if (!it->second.flavor.empty()) {
                write_managed_text(v, 48, it->second.flavor);
            }
            patched++;
        }
    }
    LOGI("[LLC-DATA] _passiveList patched: %d / %d rows matched local translations", patched, count);
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

        std::string id_str_val = read_managed_id(v);
        if (id_str_val.empty()) continue;
        const auto& translations = strcmp(name_prefix, "_keywordList") == 0
            ? g_battle_keywords : g_bufs;
        auto it = translations.find(id_str_val);
        if (it != translations.end()) {
            if (!it->second.name.empty()) {
                write_managed_text_named(v, "name", it->second.name);
            }
            if (!it->second.desc.empty()) {
                write_managed_text_named(v, "desc", it->second.desc);
            }
            if (!it->second.summary.empty()) {
                write_managed_text_named(v, "summary", it->second.summary);
            }
            if (!it->second.flavor.empty()) {
                write_managed_text_named(v, "flavor", it->second.flavor);
            }
            patched++;
        }
    }
    LOGI("[LLC-DATA] %s patched: %d / %d rows matched local translations", name_prefix, patched, count);
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
        if (!read_managed_numeric_id(v, id)) continue;

        auto it = g_egos.find(id);
        if (it != g_egos.end()) {
            if (!it->second.name.empty()) {
                write_managed_text(v, 24, it->second.name);
            }
            if (!it->second.desc.empty()) {
                write_managed_text(v, 32, it->second.desc);
            }
            patched++;
        }
    }
    LOGI("[LLC-DATA] _egoList patched: %d / %d rows matched local translations", patched, count);
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
        if (!read_managed_numeric_id(v, id)) continue;

        auto it = g_characters.find(id);
        if (it != g_characters.end()) {
            if (!it->second.name.empty()) {
                write_managed_text(v, 24, it->second.name);
            }
            patched++;
        }
    }
    LOGI("[LLC-DATA] _characterList patched: %d / %d rows matched local translations", patched, count);
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
        if (!read_managed_numeric_id(v, id)) continue;

        auto it = g_skills.find(id);
        if (it == g_skills.end()) LOGW("[LLC-MISSING] _skillList id=%d has no local translation", id);
        if (it != g_skills.end()) {
            Il2CppObject* level_list = read_managed_list(v, 24);
            if (level_list) {
                Il2CppObject* ll_items = *(Il2CppObject**)((char*)level_list + 16);
                int ll_size = *(int*)((char*)level_list + 24);
                if (ll_items && ll_size > 0 && static_cast<uintptr_t>(ll_size) <= il2cpp_array_length_fn(ll_items)) {
                    Il2CppObject** lvl_arr = (Il2CppObject**)((char*)ll_items + sizeof(void*)*4);
                    for (int l = 0; l < ll_size && l < (int)it->second.levelList.size(); ++l) {
                        Il2CppObject* lvl_obj = lvl_arr[l];
                        if (!lvl_obj) continue;
                        const auto& s_lvl = it->second.levelList[l];

                        if (!s_lvl.name.empty()) {
                            write_managed_text(lvl_obj, 40, s_lvl.name);
                        }
                        if (!s_lvl.desc.empty()) {
                            write_managed_text(lvl_obj, 32, s_lvl.desc);
                        }
                        if (!s_lvl.flavor.empty()) {
                            write_managed_text(lvl_obj, 48, s_lvl.flavor);
                        }
                        if (!s_lvl.rawDesc.empty()) {
                            write_managed_text(lvl_obj, 56, s_lvl.rawDesc);
                        }
                        if (!s_lvl.abName.empty()) {
                            write_managed_text(lvl_obj, 64, s_lvl.abName);
                        }

                        Il2CppObject* coinlist = read_managed_list(lvl_obj, 72);
                        if (coinlist) {
                            Il2CppObject* cl_items = *(Il2CppObject**)((char*)coinlist + 16);
                            int cl_size = *(int*)((char*)coinlist + 24);
                            if (cl_items && cl_size > 0 && static_cast<uintptr_t>(cl_size) <= il2cpp_array_length_fn(cl_items)) {
                                Il2CppObject** cl_arr = (Il2CppObject**)((char*)cl_items + sizeof(void*)*4);
                                for (int c = 0; c < cl_size && c < (int)s_lvl.coinlist.size(); ++c) {
                                    Il2CppObject* coin_obj = cl_arr[c];
                                    if (!coin_obj) continue;
                                    const auto& s_coin = s_lvl.coinlist[c];

                                    Il2CppObject* coindescs = read_managed_list(coin_obj, 16);
                                    if (coindescs) {
                                        Il2CppObject* cd_items = *(Il2CppObject**)((char*)coindescs + 16);
                                        int cd_size = *(int*)((char*)coindescs + 24);
                                        if (cd_items && cd_size > 0 && static_cast<uintptr_t>(cd_size) <= il2cpp_array_length_fn(cd_items)) {
                                            Il2CppObject** cd_arr = (Il2CppObject**)((char*)cd_items + sizeof(void*)*4);
                                            for (int d = 0; d < cd_size && d < (int)s_coin.coindescs.size(); ++d) {
                                                Il2CppObject* cd_obj = cd_arr[d];
                                                if (!cd_obj) continue;
                                                const auto& s_cd = s_coin.coindescs[d];

                                                if (!s_cd.desc.empty()) {
                                                    write_managed_text(cd_obj, 16, s_cd.desc);
                                                }
                                                if (!s_cd.summary.empty()) {
                                                    write_managed_text(cd_obj, 24, s_cd.summary);
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
    LOGI("[LLC-DATA] _skillList patched: %d / %d rows matched local translations", patched, count);
    return patched;
}

int patch_scoped_list(Il2CppObject* container, bool lyrics) {
    if (!container) return 0;
    auto* dictionary = *(Il2CppObject**)((char*)container + 16);
    if (!dictionary) return 0;
    auto* entries = *(Il2CppObject**)((char*)dictionary + 24);
    int count = *(int*)((char*)dictionary + 32);
    if (!entries || count < 0 || static_cast<uintptr_t>(count) > il2cpp_array_length_fn(entries)) return 0;
    int matched = 0;
    for (int i = 0; i < count; ++i) {
        char* entry = (char*)entries + 32 + i * 24;
        auto* value = *(Il2CppObject**)(entry + 16);
        if (!value) continue;
        std::string scope;
        if (lyrics) {
            auto* key = *(Il2CppObject**)(entry + 8);
            if (!key || strcmp(il2cpp_class_get_name_fn(il2cpp_object_get_class_fn(key)), "String") != 0) continue;
            scope = managed_ascii_string(reinterpret_cast<Il2CppString*>(key));
        } else scope = read_managed_id(value);
        auto song = g_bgm_lyrics.find(scope);
        if (lyrics && song == g_bgm_lyrics.end()) song = g_bgm_lyrics.find("BgmLyrics_" + scope);
        auto dungeon = g_dungeon_nodes.find(scope);
        if ((lyrics && song == g_bgm_lyrics.end()) || (!lyrics && dungeon == g_dungeon_nodes.end())) {
            LOGW("[LLC-RESOURCE] Missing %s scope=%s", lyrics ? "song" : "dungeon", scope.c_str());
            continue;
        }
        // Lyrics containers expose their list at +16; dungeon containers name it stageList.
        auto* list = lyrics ? read_managed_list(value, 16) : read_named_managed_list(value, "stageList");
        if (!list) continue;
        auto* array = *(Il2CppObject**)((char*)list + 16);
        int size = *(int*)((char*)list + 24);
        if (!array || size < 0 || static_cast<uintptr_t>(size) > il2cpp_array_length_fn(array)) continue;
        auto** rows = (Il2CppObject**)((char*)array + 32);
        int group_matches = 0;
        auto writes_before = g_managed_text_writes;
        for (int j = 0; j < size; ++j) {
            auto* row = rows[j];
            if (!row) continue;
            std::string id = read_managed_id(row);
            if (lyrics) {
                auto text = song->second.find(id);
                if (text == song->second.end()) continue;
                write_managed_text_named(row, "content", text->second);
            } else {
                auto text = dungeon->second.find(id);
                if (text == dungeon->second.end()) continue;
                if (!text->second.title.empty()) write_managed_text_named(row, "title", text->second.title);
                if (!text->second.desc.empty()) write_managed_text_named(row, "desc", text->second.desc);
                if (!text->second.place.empty()) write_managed_text_named(row, "place", text->second.place);
            }
            ++group_matches;
        }
        matched += group_matches;
        LOGI("[LLC-SCOPED] %s scope=%s rows=%d matched=%d writes=%llu", lyrics ? "song" : "dungeon",
             scope.c_str(), size, group_matches, (unsigned long long)(g_managed_text_writes - writes_before));
    }
    return matched;
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

        Il2CppObject* dlist = read_managed_list(v, 16);
        if (!dlist) continue;

        Il2CppObject* ditems = *(Il2CppObject**)((char*)dlist + 16);
        int dsize = *(int*)((char*)dlist + 24);
        if (!ditems || dsize <= 0 || static_cast<uintptr_t>(dsize) > il2cpp_array_length_fn(ditems)) continue;

        Il2CppObject** arr = (Il2CppObject**)((char*)ditems + sizeof(void*) * 4);
        for (int j = 0; j < dsize; ++j) {
            Il2CppObject* item = arr[j];
            if (!item) continue;

            std::string id_buf = read_managed_id(item);
            if (id_buf.empty()) continue;

            auto it = g_voice_dlgs.find(id_buf);
            if (it != g_voice_dlgs.end()) {
                if (!it->second.desc.empty()) {
                    write_managed_text(item, 24, it->second.desc);
                }
                if (!it->second.dlg.empty()) {
                    write_managed_text(item, 32, it->second.dlg);
                }
                patched++;
            }
        }
    }
    LOGI("[LLC-DATA] %s patched: %d voice IDs matched local translations", name, patched);
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

        std::string id_buf = read_managed_id(v);
        if (id_buf.empty()) continue;

        auto it = g_theater_map.find(id_buf);
        if (it != g_theater_map.end()) {
            if (!it->second.title.empty()) {
                write_managed_text(v, 24, it->second.title);
            }
            if (!it->second.desc.empty()) {
                write_managed_text(v, 32, it->second.desc);
            }
            patched++;
        }
    }
    LOGI("[LLC-DATA] _storyTheater patched: %d / %d rows matched local translations", patched, count);
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

        int id = 0;
        if (!read_managed_numeric_id(v, id)) continue;

        auto it = g_banner_map.find(id);
        if (it != g_banner_map.end()) {
            if (!it->second.name.empty()) {
                write_managed_text(v, 24, it->second.name);
            }
            if (!it->second.desc.empty()) {
                write_managed_text(v, 32, it->second.desc);
            }
            if (!it->second.flavor.empty()) {
                write_managed_text(v, 40, it->second.flavor);
            }
            patched++;
        }
    }
    LOGI("[LLC-DATA] _userBanner patched: %d / %d rows matched local translations", patched, count);
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

        std::string id_buf = read_managed_id(v);
        if (id_buf.empty()) continue;

        auto it = g_stage_chapter_map.find(id_buf);
        if (it != g_stage_chapter_map.end()) {
            if (!it->second.company.empty()) write_managed_text(v, 24, it->second.company);
            if (!it->second.area.empty()) write_managed_text(v, 32, it->second.area);
            if (!it->second.chaptertitle.empty()) write_managed_text(v, 40, it->second.chaptertitle);
            if (!it->second.chapter.empty()) write_managed_text(v, 48, it->second.chapter);
            if (!it->second.chapterNumber.empty()) write_managed_text(v, 56, it->second.chapterNumber);
            if (!it->second.timeline.empty()) write_managed_text(v, 64, it->second.timeline);
            patched++;
        }
    }
    LOGI("[LLC-DATA] _stageChapter patched: %d / %d rows matched local translations", patched, count);
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

        int id = 0;
        if (!read_managed_numeric_id(v, id)) continue;

        auto it = g_stage_node_map.find(id);
        if (it != g_stage_node_map.end()) {
            if (!it->second.title.empty()) write_managed_text(v, 24, it->second.title);
            if (!it->second.desc.empty()) write_managed_text(v, 32, it->second.desc);
            if (!it->second.place.empty()) write_managed_text(v, 40, it->second.place);
            patched++;
        }
    }
    LOGI("[LLC-DATA] %s patched: %d / %d rows matched local translations", name, patched, count);
    return patched;
}

ContentStamp scenario_stamp(Il2CppObject* scn) {
    ContentStamp stamp;
    stamp.owner = reinterpret_cast<uintptr_t>(scn);
    if (!scn) return stamp;
    auto* list = read_named_managed_list(scn, "Scenarios");
    if (!list) list = read_named_managed_list(scn, "_scenarios");
    if (!list) list = read_named_managed_list(scn, "dataList");
    if (!list) return stamp;
    stamp.dictionary = reinterpret_cast<uintptr_t>(list);
    auto* array = *(Il2CppObject**)((char*)list + 16);
    int size = *(int*)((char*)list + 24);
    if (!array || size < 0 || static_cast<uintptr_t>(size) > il2cpp_array_length_fn(array)) return stamp;
    stamp.entries = reinterpret_cast<uintptr_t>(array);
    stamp.count = size;
    auto* klass = il2cpp_object_get_class_fn(list);
    auto* version = il2cpp_class_get_field_from_name_fn(klass, "_version");
    if (!version) version = il2cpp_class_get_field_from_name_fn(klass, "version");
    if (version) il2cpp_field_get_value_fn(list, version, &stamp.version);
    return stamp;
}

StoryPatchResult patch_scenario_object(Il2CppObject* scn, const char* scen_id) {
    StoryPatchResult result;
    auto stamp = scenario_stamp(scn);
    result.rows = stamp.count;
    if (!stamp.ready() || !scen_id || !*scen_id) return result;
    std::string id(scen_id);
    // IDs select a filename, never a path supplied by runtime data.
    if (id.size() > 127 || id.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-") != std::string::npos) return result;
    const auto roots = resource_paths::story_roots();
    std::string upper = id;
    for (auto& c : upper) c = toupper(static_cast<unsigned char>(c));
    std::string path;
    for (const auto& candidate : {id, upper}) {
        for (const auto& root : roots) {
            std::string attempt = std::string(root) + candidate + ".json";
            if (access(attempt.c_str(), R_OK) == 0) { path = attempt; break; }
        }
        if (!path.empty()) break;
    }
    if (path.empty()) {
        LOGW("[LLC-STORY] Resource unavailable: %s", id.c_str());
        return result;
    }
    std::unordered_map<int, ScenarioDialogue> dialogues;
    parse_scenario_json_file(path, dialogues);
    if (dialogues.empty()) return result;
    auto** rows = reinterpret_cast<Il2CppObject**>(stamp.entries + 32);
    auto before = g_managed_text_writes;
    for (int i = 0; i < stamp.count; ++i) {
        auto* row = rows[i];
        if (!row) continue;
        std::string key = read_named_managed_id(row, "id");
        if (key.empty()) key = read_named_managed_id(row, "ID");
        if (key.empty()) {
            static bool logged = false;
            if (!logged) {
                logged = true;
                for (auto* k = il2cpp_object_get_class_fn(row); k; k = il2cpp_class_get_parent_fn(k)) {
                    void* iter = nullptr;
                    while (auto* field = il2cpp_class_get_fields_fn(k, &iter)) {
                        auto* type = il2cpp_class_from_type_fn(il2cpp_field_get_type_fn(field));
                        LOGW("[LLC-STORY-SCHEMA] %s.%s offset=%u type=%s", il2cpp_class_get_name_fn(k),
                             il2cpp_field_get_name_fn(field), il2cpp_field_get_offset_fn(field),
                             type ? il2cpp_class_get_name_fn(type) : "unknown");
                    }
                }
            }
        }
        int number = 0;
        auto parsed = std::from_chars(key.data(), key.data() + key.size(), number);
        if (key.empty() || parsed.ec != std::errc{} || parsed.ptr != key.data() + key.size()) continue;
        auto text = dialogues.find(number);
        if (text == dialogues.end()) continue;
        ++result.matched;
        auto write = [&](const char* field, const std::string& value) {
            if (!value.empty() && !write_managed_text_named(row, field, value)) ++result.failed_writes;
        };
        write("content", text->second.content);
        write("place", text->second.place);
        write("teller", text->second.teller);
        write("title", text->second.title);
    }
    result.strings_written = g_managed_text_writes - before;
    return result;
}
