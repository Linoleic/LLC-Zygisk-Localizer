#include "data_patcher.hpp"
#include "managed_fields.hpp"
#include "storage.hpp"
#include "json_loader.hpp"
#include "resource_paths.hpp"
#include "string_table_patcher.hpp"
#include "structured_content_patcher.hpp"
#include <unistd.h>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <string>
#include <unordered_map>

int patch_skill_tag_list(Il2CppObject* container) {
    return string_table::patch(container, g_skill_tags, "TextData_SkillTag", "name");
}
int patch_battle_hint_list(Il2CppObject* container) {
    return string_table::patch(container, g_battle_hints, "TextData_UI", "content");
}

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
        if (!read_managed_numeric_id(v, id)) {
            if (managed_id_absent(v)) translation_evidence::unidentified_row();
            else translation_evidence::invalid_row(read_managed_id(v));
            continue;
        }
        translation_evidence::observe(id ? std::to_string(id) : std::string{});

        auto it = g_passives.find(id);
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
    translation_evidence::complete();
    LOGI("[LLC-DATA] _passiveList patched: %d / %d rows matched local translations", patched, count);
    return patched;
}

int patch_buf_list(Il2CppObject* container, const char* name_prefix) {
    return structured_content::flat(container, strcmp(name_prefix, "_keywordList") == 0);
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

int patch_skill_list(Il2CppObject* container) { return structured_content::skills(container); }
int patch_ego_gift_list(Il2CppObject* container) { return structured_content::gifts(container); }
int patch_dante_note_list(Il2CppObject* container) { return structured_content::notes(container); }
int patch_dante_note_category_list(Il2CppObject* container) {
    return string_table::patch(container, g_dante_note_categories, "TextData_UI", "content");
}
int patch_ego_gift_locked_list(Il2CppObject* container) {
    return string_table::patch(container, g_ego_gift_locked, "TextData_UI", "content");
}
int patch_ego_gift_category_list(Il2CppObject* container) {
    return string_table::patch(container, g_ego_gift_categories, "TextData_EgoGiftCategory", "name");
}
int patch_mirror_dungeon_theme_list(Il2CppObject* container) {
    return string_table::visit(container, "TextData_MirrorDungeonTheme", [](Il2CppObject* row, const std::string& key) {
        auto it = g_mirror_dungeon_themes.find(key);
        if (it == g_mirror_dungeon_themes.end()) return 0;
        int writes = 0;
        if (!it->second.name.empty()) {
            if (!write_managed_text_named(row, "name", it->second.name)) return -1;
            ++writes;
        }
        if (!it->second.specialName.empty()) {
            if (!write_managed_text_named(row, "specialName", it->second.specialName)) return -1;
            ++writes;
        }
        return writes ? 1 : 0;
    }, true);
}
int patch_announcer_list(Il2CppObject* container) {
    return string_table::patch(container, g_announcers, "TextData_Announcer", "name");
}
int patch_announcer_voice_type_list(Il2CppObject* container) {
    return string_table::patch(container, g_announcer_voice_types, "TextData_UI", "content");
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
    auto* dictionary = string_table::reference(voice_container_obj, "_dic");
    if (!dictionary) {
        // The three specialized voice containers expose the same dictionary
        // at the verified JsonDataList base-field offset, but omit its name.
        auto* candidate = *reinterpret_cast<Il2CppObject**>(reinterpret_cast<char*>(voice_container_obj) + 16);
        auto* candidate_class = candidate ? il2cpp_object_get_class_fn(candidate) : nullptr;
        const char* candidate_name = candidate_class ? il2cpp_class_get_name_fn(candidate_class) : nullptr;
        if (candidate_name && !strcmp(candidate_name, "Dictionary`2")) {
            dictionary = candidate;
            LOGI("[LLC-SCHEMA] %s container has no reflected _dic; using validated Dictionary`2 at base offset 16", name);
        }
    }
    auto* owner_class = il2cpp_object_get_class_fn(voice_container_obj);
    const char* owner_name = owner_class ? il2cpp_class_get_name_fn(owner_class) : nullptr;
    auto* dictionary_class = dictionary ? il2cpp_object_get_class_fn(dictionary) : nullptr;
    const char* dictionary_name = dictionary_class ? il2cpp_class_get_name_fn(dictionary_class) : nullptr;
    if (!dictionary || !dictionary_name || strcmp(dictionary_name, "Dictionary`2")) {
        LOGW("[LLC-SCHEMA] %s dictionary unavailable: container=%s reflected=%d candidate=%s", name,
             owner_name ? owner_name : "null", string_table::reference(voice_container_obj, "_dic") != nullptr,
             dictionary_name ? dictionary_name : "null");
        return 0;
    }
    int32_t count = 0;
    auto* entries = string_table::reference(dictionary, "_entries");
    const bool count_read = string_table::integer(dictionary, "_count", count);
    if (!entries || !count_read || count <= 0 || count > 500000) {
        LOGW("[LLC-SCHEMA] %s dictionary fields unavailable: entries=%p count_read=%d count=%d", name, entries,
             count_read, count);
        return 0;
    }
    auto* entry_array_class = il2cpp_object_get_class_fn(entries);
    const char* array_class_name = il2cpp_class_get_name_fn(entry_array_class);
    size_t array_name_length = array_class_name ? strlen(array_class_name) : 0;
    size_t capacity = il2cpp_array_length_fn(entries);
    if (array_name_length < 2 || strcmp(array_class_name + array_name_length - 2, "[]") || capacity < static_cast<size_t>(count) || capacity > 500000 || !string_table::array_get_value_method(entry_array_class)) {
        LOGW("[LLC-SCHEMA] %s entries array unsupported: type=%s capacity=%zu count=%d", name,
             array_class_name ? array_class_name : "null", capacity, count);
        return 0;
    }
    string_table::Root rooted_entries(entries);
    if (!rooted_entries.get()) return 0;
    int patched = 0;
    int active_entries = 0;
    bool traversal_complete = true;
    const char* expected_row_class = strcmp(name, "_announcerVoiceText") == 0 ? "TextData_AnnouncerVoice" :
        strcmp(name, "_personalityVoiceText") == 0 ? "TextData_PersonalityVoice" : "TextData_EGOVoice";
    for (size_t e = 0; e < capacity; ++e) {
        auto* boxed = string_table::array_value(rooted_entries.get(), static_cast<int32_t>(e));
        if (!boxed || !il2cpp_class_is_valuetype_fn(il2cpp_object_get_class_fn(boxed))) { traversal_complete = false; break; }
        string_table::Root entry(boxed);
        int32_t hash = -1;
        if (!string_table::integer(entry.get(), "hashCode", hash)) { traversal_complete = false; break; }
        if (hash < 0) continue;
        auto key = read_named_managed_id(entry.get(), "key");
        auto* value = string_table::reference(entry.get(), "value");
        if (key.empty() && !value) continue;
        ++active_entries;
        // The specialized voice Dictionary stores a wrapper as its value; the
        // actual List<T> is a validated managed field at offset 16 on that
        // wrapper. Some game builds may store List<T> directly, so accept that
        // shape only after checking its runtime class.
        auto* list = read_managed_list(value, 16);
        if (!list && value) {
            auto* value_class = il2cpp_object_get_class_fn(value);
            const char* value_class_name = value_class ? il2cpp_class_get_name_fn(value_class) : nullptr;
            if (value_class_name && !strcmp(value_class_name, "List`1")) list = value;
        }
        if (!list) {
            auto* value_class = value ? il2cpp_object_get_class_fn(value) : nullptr;
            const char* value_class_name = value_class ? il2cpp_class_get_name_fn(value_class) : nullptr;
            LOGW("[LLC-SCHEMA] %s dictionary value has no validated List<T>: key=%s value_class=%s", name,
                 key.c_str(), value_class_name ? value_class_name : "null");
            traversal_complete = false; break;
        }
        auto* items = string_table::reference(list, "_items");
        int32_t size = 0;
        if (!list || !items || !string_table::integer(list, "_size", size) || size < 0 || size > 500000 ||
            static_cast<uintptr_t>(size) > il2cpp_array_length_fn(items) || !string_table::array_get_value_method(il2cpp_object_get_class_fn(items))) {
            LOGW("[LLC-SCHEMA] %s List<T> fields unsupported: items=%p size=%d", name, items, size);
            traversal_complete = false; break;
        }
        string_table::Root rooted_items(items), rooted_list(list);
        if (!rooted_items.get() || !rooted_list.get()) { traversal_complete = false; break; }
        for (int32_t j = 0; j < size; ++j) {
            auto* item = string_table::array_value(rooted_items.get(), j);
            if (!item) { translation_evidence::invalid_row("null_voice_row"); traversal_complete = false; continue; }
            string_table::Root rooted_item(item);
            const char* row_class = il2cpp_class_get_name_fn(il2cpp_object_get_class_fn(rooted_item.get()));
            if (!row_class || strcmp(row_class, expected_row_class)) {
                translation_evidence::invalid_row(std::string("unexpected_voice_class:") + (row_class ? row_class : "null"));
                traversal_complete = false; continue;
            }
            std::string id = read_named_managed_id(rooted_item.get(), "id");
            if (id.empty()) { translation_evidence::unidentified_row(); traversal_complete = false; continue; }
            translation_evidence::observe(id);
            auto it = g_voice_dlgs.find(id);
            if (it == g_voice_dlgs.end()) continue;
            if (!it->second.desc.empty() && !write_managed_text_named(rooted_item.get(), "desc", it->second.desc)) traversal_complete = false;
            if (!it->second.dlg.empty() && !write_managed_text_named(rooted_item.get(), "dlg", it->second.dlg)) traversal_complete = false;
            ++patched;
        }
    }
    if (active_entries != count) traversal_complete = false;
    if (traversal_complete) translation_evidence::complete();
    LOGI("[LLC-DATA] %s patched: %d voice IDs matched local translations; active=%d/%d complete=%d", name, patched, active_entries, count, traversal_complete);
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
