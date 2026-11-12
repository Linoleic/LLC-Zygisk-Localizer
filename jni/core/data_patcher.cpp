#include "data_patcher.hpp"
#include "managed_fields.hpp"
#include "storage.hpp"
#include "json_loader.hpp"
#include "resource_paths.hpp"
#include "string_table_patcher.hpp"
#include "auto_registry.hpp"
#include "event_dialogue.hpp"
#include "diagnostic_log.hpp"
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
    Il2CppObject* dic = string_table::reference(ui_list_obj, "_dic");
    if (!dic) dic = *(Il2CppObject**)((char*)ui_list_obj + 16);
    if (!dic) return 0;
    diagnostic_log::Scope warnings(list_name);

    // In-place text update: safely update existing entries in-place.
    // NEVER inject new keys into _dic via set_Item or Add to prevent duplicate key
    // collisions (ArgumentException) when Unity game scripts dynamically call Dictionary.Add().
    Il2CppObject* entries = string_table::reference(dic, "_entries");
    if (!entries) entries = *(Il2CppObject**)((char*)dic + 24);
    int count = 0;
    if (!string_table::integer(dic, "_count", count)) count = *(int*)((char*)dic + 32);
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
                if (!write_managed_text_named(v, "content", it->second)) {
                    write_managed_text(v, 24, it->second);
                }
                updated_in_place++;
            } else {
                warnings.add("unmapped_ui_key", kbuf);
            }
        }
    }

    LOGI("[LLC-DATA] %s in-place synchronized: %d / %d entries updated from translation map.",
         list_name, updated_in_place, count);
    return updated_in_place;
}

int patch_personality_list(Il2CppObject* pers_list_obj) {
    if (!pers_list_obj) return 0;
    Il2CppObject* dic = string_table::reference(pers_list_obj, "_dic");
    if (!dic) dic = *(Il2CppObject**)((char*)pers_list_obj + 16);
    if (!dic) return 0;

    Il2CppObject* entries = string_table::reference(dic, "_entries");
    if (!entries) entries = *(Il2CppObject**)((char*)dic + 24);
    int count = 0;
    if (!string_table::integer(dic, "_count", count)) count = *(int*)((char*)dic + 32);
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
                if (!write_managed_text_named(v, "name", it->second.name)) write_managed_text(v, 24, it->second.name);
            }
            if (!it->second.desc.empty()) {
                if (!write_managed_text_named(v, "desc", it->second.desc)) write_managed_text(v, 32, it->second.desc);
            }
            if (!it->second.title.empty()) {
                if (!write_managed_text_named(v, "title", it->second.title)) write_managed_text(v, 40, it->second.title);
            }
            patched++;
        }
    }
    LOGI("[LLC-DATA] _personalityList patched: %d / %d rows matched local translations", patched, count);
    return patched;
}

int patch_passive_list(Il2CppObject* passive_list_obj) {
    if (!passive_list_obj) return 0;
    Il2CppObject* dic = string_table::reference(passive_list_obj, "_dic");
    if (!dic) dic = *(Il2CppObject**)((char*)passive_list_obj + 16);
    if (!dic) return 0;

    Il2CppObject* entries = string_table::reference(dic, "_entries");
    if (!entries) entries = *(Il2CppObject**)((char*)dic + 24);
    int count = 0;
    if (!string_table::integer(dic, "_count", count)) count = *(int*)((char*)dic + 32);
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
                if (!write_managed_text_named(v, "name", it->second.name)) write_managed_text(v, 24, it->second.name);
            }
            if (!it->second.desc.empty()) {
                if (!write_managed_text_named(v, "desc", it->second.desc)) write_managed_text(v, 32, it->second.desc);
            }
            if (!it->second.summary.empty()) {
                if (!write_managed_text_named(v, "summary", it->second.summary)) write_managed_text(v, 40, it->second.summary);
            }
            if (!it->second.flavor.empty()) {
                if (!write_managed_text_named(v, "flavor", it->second.flavor)) write_managed_text(v, 48, it->second.flavor);
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
    Il2CppObject* dic = string_table::reference(ego_list_obj, "_dic");
    if (!dic) dic = *(Il2CppObject**)((char*)ego_list_obj + 16);
    if (!dic) return 0;

    Il2CppObject* entries = string_table::reference(dic, "_entries");
    if (!entries) entries = *(Il2CppObject**)((char*)dic + 24);
    int count = 0;
    if (!string_table::integer(dic, "_count", count)) count = *(int*)((char*)dic + 32);
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
                if (!write_managed_text_named(v, "name", it->second.name)) write_managed_text(v, 24, it->second.name);
            }
            if (!it->second.desc.empty()) {
                if (!write_managed_text_named(v, "desc", it->second.desc)) write_managed_text(v, 32, it->second.desc);
            }
            patched++;
        }
    }
    LOGI("[LLC-DATA] _egoList patched: %d / %d rows matched local translations", patched, count);
    return patched;
}

int patch_character_list(Il2CppObject* char_list_obj) {
    if (!char_list_obj) return 0;
    Il2CppObject* dic = string_table::reference(char_list_obj, "_dic");
    if (!dic) dic = *(Il2CppObject**)((char*)char_list_obj + 16);
    if (!dic) return 0;

    Il2CppObject* entries = string_table::reference(dic, "_entries");
    if (!entries) entries = *(Il2CppObject**)((char*)dic + 24);
    int count = 0;
    if (!string_table::integer(dic, "_count", count)) count = *(int*)((char*)dic + 32);
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
                if (!write_managed_text_named(v, "name", it->second.name)) write_managed_text(v, 24, it->second.name);
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
    auto* dictionary = string_table::reference(container, "_dic");
    if (!dictionary) dictionary = *(Il2CppObject**)((char*)container + 16);
    if (!dictionary) return 0;
    auto* entries = string_table::reference(dictionary, "_entries");
    if (!entries) entries = *(Il2CppObject**)((char*)dictionary + 24);
    int count = 0;
    if (!string_table::integer(dictionary, "_count", count)) count = *(int*)((char*)dictionary + 32);
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
            translation_evidence::warning(lyrics ? "missing_song_scope" : "missing_dungeon_scope", scope);
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
        translation_evidence::structure_issue("voice_dictionary_unavailable", std::string(owner_name ? owner_name : "null") + "/" + (dictionary_name ? dictionary_name : "null"));
        return 0;
    }
    int32_t count = 0;
    auto* entries = string_table::reference(dictionary, "_entries");
    const bool count_read = string_table::integer(dictionary, "_count", count);
    if (!entries || !count_read || count <= 0 || count > 500000) {
        translation_evidence::structure_issue("voice_dictionary_fields_unavailable", "count=" + std::to_string(count) + ";count_read=" + std::to_string(count_read));
        return 0;
    }
    auto* entry_array_class = il2cpp_object_get_class_fn(entries);
    const char* array_class_name = il2cpp_class_get_name_fn(entry_array_class);
    size_t array_name_length = array_class_name ? strlen(array_class_name) : 0;
    size_t capacity = il2cpp_array_length_fn(entries);
    if (array_name_length < 2 || strcmp(array_class_name + array_name_length - 2, "[]") || capacity < static_cast<size_t>(count) || capacity > 500000 || !string_table::array_get_value_method(entry_array_class)) {
        translation_evidence::structure_issue("voice_entries_array_unsupported", std::string(array_class_name ? array_class_name : "null") + ";capacity=" + std::to_string(capacity));
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
            translation_evidence::structure_issue("voice_list_unavailable", key + ";class=" + (value_class_name ? value_class_name : "null"));
            traversal_complete = false; break;
        }
        auto* items = string_table::reference(list, "_items");
        int32_t size = 0;
        if (!list || !items || !string_table::integer(list, "_size", size) || size < 0 || size > 500000 ||
            static_cast<uintptr_t>(size) > il2cpp_array_length_fn(items) || !string_table::array_get_value_method(il2cpp_object_get_class_fn(items))) {
            translation_evidence::structure_issue("voice_list_fields_unsupported", key + ";size=" + std::to_string(size));
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
    Il2CppObject* dic = string_table::reference(theater_obj, "_dic");
    if (!dic) dic = *(Il2CppObject**)((char*)theater_obj + 16);
    if (!dic) return 0;

    Il2CppObject* entries = string_table::reference(dic, "_entries");
    if (!entries) entries = *(Il2CppObject**)((char*)dic + 24);
    int count = 0;
    if (!string_table::integer(dic, "_count", count)) count = *(int*)((char*)dic + 32);
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
                if (!write_managed_text_named(v, "title", it->second.title)) write_managed_text(v, 24, it->second.title);
            }
            if (!it->second.desc.empty()) {
                if (!write_managed_text_named(v, "desc", it->second.desc)) write_managed_text(v, 32, it->second.desc);
            }
            patched++;
        }
    }
    LOGI("[LLC-DATA] _storyTheater patched: %d / %d rows matched local translations", patched, count);
    return patched;
}

int patch_banner_list(Il2CppObject* banner_obj) {
    if (!banner_obj) return 0;
    Il2CppObject* dic = string_table::reference(banner_obj, "_dic");
    if (!dic) dic = *(Il2CppObject**)((char*)banner_obj + 16);
    if (!dic) return 0;

    Il2CppObject* entries = string_table::reference(dic, "_entries");
    if (!entries) entries = *(Il2CppObject**)((char*)dic + 24);
    int count = 0;
    if (!string_table::integer(dic, "_count", count)) count = *(int*)((char*)dic + 32);
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
                if (!write_managed_text_named(v, "name", it->second.name)) write_managed_text(v, 24, it->second.name);
            }
            if (!it->second.desc.empty()) {
                if (!write_managed_text_named(v, "desc", it->second.desc)) write_managed_text(v, 32, it->second.desc);
            }
            if (!it->second.flavor.empty()) {
                if (!write_managed_text_named(v, "flavor", it->second.flavor)) write_managed_text(v, 40, it->second.flavor);
            }
            patched++;
        }
    }
    LOGI("[LLC-DATA] _userBanner patched: %d / %d rows matched local translations", patched, count);
    return patched;
}

static int patch_user_ticket_map(Il2CppObject* container, const std::unordered_map<std::string, BannerEntry>& translations, const char* table_name) {
    int patched = string_table::visit(container, "TextData_Item", [&](Il2CppObject* row, const std::string& key) {
        auto it = translations.find(key);
        if (it == translations.end()) return 0;
        int writes = 0;
        if (!it->second.name.empty()) {
            if (!write_managed_text_named(row, "name", it->second.name)) return -1;
            ++writes;
        }
        if (!it->second.desc.empty()) {
            if (!write_managed_text_named(row, "desc", it->second.desc)) return -1;
            ++writes;
        }
        if (!it->second.flavor.empty()) {
            if (!write_managed_text_named(row, "flavor", it->second.flavor)) return -1;
            ++writes;
        }
        return writes ? 1 : 0;
    }, true, true);
    LOGI("[LLC-DATA] %s patched: %d rows matched local translations", table_name, patched);
    return patched;
}

int patch_user_ticket_l(Il2CppObject* container) {
    return patch_user_ticket_map(container, g_user_ticket_l_map, "_userTicket_L");
}

int patch_user_ticket_r(Il2CppObject* container) {
    return patch_user_ticket_map(container, g_user_ticket_r_map, "_userTicket_R");
}

int patch_user_ticket_egobg(Il2CppObject* container) {
    return patch_user_ticket_map(container, g_user_ticket_egobg_map, "_userTicket_EGOBg");
}

int patch_thread_dungeon_list(Il2CppObject* container) {
    return string_table::patch(container, g_thread_dungeons, "TextData_ThreadDungeon", "name");
}

int patch_dungeon_name_list(Il2CppObject* container) {
    return string_table::patch(container, g_dungeon_names, "TextData_DungeonName", "name");
}

int patch_stage_part_list(Il2CppObject* container) {
    return string_table::patch(container, g_stage_parts, "TextData_StagePart", "parttitle");
}

int patch_attribute_list(Il2CppObject* container) {
    return string_table::patch(container, g_attribute_list, "TextData_AttributeText", "name");
}

int patch_dungeon_start_buffs(Il2CppObject* container) {
    return string_table::patch(container, g_dungeon_start_buffs, "TextData_DungeonStartBuffs", "description");
}

int patch_panic_info_list(Il2CppObject* container) {
    return string_table::visit(container, "TextData_PanicInfo", [&](Il2CppObject* row, const std::string& key) {
        auto it = g_panic_infos.find(key);
        if (it == g_panic_infos.end()) return 0;
        int count = 0;
        if (!it->second.panic_name.empty()) {
            if (!write_managed_text_named(row, "panicName", it->second.panic_name)) return -1;
            count++;
        }
        if (!it->second.low_morale_desc.empty()) {
            if (!write_managed_text_named(row, "lowMoraleDescription", it->second.low_morale_desc)) return -1;
            count++;
        }
        if (!it->second.panic_desc.empty()) {
            if (!write_managed_text_named(row, "panicDescription", it->second.panic_desc)) return -1;
            count++;
        }
        if (!it->second.flavor.empty()) {
            if (!write_managed_text_named(row, "flavor", it->second.flavor)) return -1;
            count++;
        }
        return count;
    }, true /* numeric_ids */);
}

int patch_mental_condition_list(Il2CppObject* container) {
    return string_table::visit(container, "TextData_MentalCondition", [&](Il2CppObject* row, const std::string& key) {
        auto it = g_mental_conditions.find(key);
        if (it == g_mental_conditions.end()) return 0;
        int count = 0;
        if (!it->second.add.empty()) {
            if (!write_managed_text_named(row, "add", it->second.add)) return -1;
            count++;
        }
        if (!it->second.min.empty()) {
            if (!write_managed_text_named(row, "min", it->second.min)) return -1;
            count++;
        }
        return count;
    }, false /* numeric_ids */);
}

int patch_dante_ability_list(Il2CppObject* container) {
    return string_table::visit(container, "TextData_DanteAbility", [&](Il2CppObject* row, const std::string& key) {
        auto it = g_dante_abilities.find(key);
        if (it == g_dante_abilities.end()) return 0;
        int count = 0;
        if (!it->second.desc.empty()) {
            if (!write_managed_text_named(row, "desc", it->second.desc)) return -1;
            count++;
        }
        if (!it->second.raw_desc.empty()) {
            if (!write_managed_text_named(row, "rawDesc", it->second.raw_desc)) return -1;
            count++;
        }
        return count;
    }, true /* numeric_ids */);
}

int patch_unlock_code_list(Il2CppObject* container) {
    return string_table::visit(container, "TextData_UnlockCode", [&](Il2CppObject* row, const std::string& key) {
        auto it = g_unlock_codes.find(key);
        if (it == g_unlock_codes.end()) return 0;
        int count = 0;
        if (!it->second.open_condition.empty()) {
            if (!write_managed_text_named(row, "openCondition", it->second.open_condition)) return -1;
            count++;
        }
        if (!it->second.open_condition_number.empty()) {
            if (!write_managed_text_named(row, "openConditionNumber", it->second.open_condition_number)) return -1;
            count++;
        }
        if (!it->second.related_chapter_text.empty()) {
            if (!write_managed_text_named(row, "relatedChapterText", it->second.related_chapter_text)) return -1;
            count++;
        }
        if (!it->second.ask_level_up.empty()) {
            if (!write_managed_text_named(row, "askLevelUp", it->second.ask_level_up)) return -1;
            count++;
        }
        return count;
    }, true /* numeric_ids */);
}

int patch_project_gs_lesson_list(Il2CppObject* container) {
    return string_table::visit(container, "TextData_ProjectGSLesson", [&](Il2CppObject* row, const std::string& key) {
        auto it = g_project_gs_lessons.find(key);
        if (it == g_project_gs_lessons.end()) return 0;
        int count = 0;
        if (!it->second.content.empty()) {
            if (!write_managed_text_named(row, "content", it->second.content)) return -1;
            count++;
        }
        if (!it->second.teacher.empty()) {
            if (!write_managed_text_named(row, "teacher", it->second.teacher)) return -1;
            count++;
        }
        return count;
    }, true /* numeric_ids */);
}

int patch_project_gs_combo_list(Il2CppObject* container) {
    return string_table::patch(container, g_project_gs_combos, "TextData_UI", "content");
}

int patch_introduce_character_list(Il2CppObject* container) {
    return string_table::visit(container, "TextData_IntroduceCharacter", [&](Il2CppObject* row, const std::string& key) {
        auto it = g_introduce_characters.find(key);
        if (it == g_introduce_characters.end()) return 0;
        int count = 0;
        if (!it->second.name.empty()) {
            if (!write_managed_text_named(row, "name", it->second.name)) return -1;
            count++;
        }
        if (!it->second.subName.empty()) {
            if (!write_managed_text_named(row, "subName", it->second.subName)) return -1;
            count++;
        }
        if (!it->second.description.empty()) {
            if (!write_managed_text_named(row, "description", it->second.description)) return -1;
            count++;
        }
        if (!it->second.sentence.empty()) {
            if (!write_managed_text_named(row, "sentence", it->second.sentence)) return -1;
            count++;
        }
        return count ? 1 : 0;
    }, false /* numeric_ids */);
}

int patch_battle_pass_mission_list(Il2CppObject* container) {
    return string_table::patch(container, g_battle_pass_missions, "TextData_BattlePassMission", "content");
}

int patch_railway_dungeon_station_name_list(Il2CppObject* container) {
    return string_table::visit(container, "TextData_RailwayDungeonStationName", [&](Il2CppObject* row, const std::string& key) {
        auto it = g_railway_dungeon_station_names.find(key);
        if (it == g_railway_dungeon_station_names.end()) return 0;
        auto* list = read_named_managed_list(row, "nameList");
        if (!list) list = read_managed_list(row, 24);
        if (!list) return 0;
        auto* array = *(Il2CppObject**)((char*)list + 16);
        int size = *(int*)((char*)list + 24);
        if (!array || size < 0 || static_cast<uintptr_t>(size) > il2cpp_array_length_fn(array)) return 0;
        auto** items = (Il2CppObject**)((char*)array + 32);
        int writes = 0;
        for (int j = 0; j < size; ++j) {
            auto* station = items[j];
            if (!station) continue;
            std::string station_id = read_named_managed_id(station, "id");
            if (station_id.empty()) station_id = read_managed_id(station);
            const RailwayStationData* matched = nullptr;
            for (const auto& s : it->second.nameList) {
                if (s.id == station_id) {
                    matched = &s;
                    break;
                }
            }
            if (!matched) continue;
            if (!matched->content.empty()) {
                if (!write_managed_text_named(station, "content", matched->content)) return -1;
                ++writes;
            }
            if (!matched->shortName.empty()) {
                if (!write_managed_text_named(station, "shortName", matched->shortName)) return -1;
                ++writes;
            }
        }
        return writes ? 1 : 0;
    }, false /* numeric_ids */);
}

int patch_quest_list(Il2CppObject* container) {
    return string_table::patch(container, g_quests, "TextData_Quest", "content");
}

int patch_railway_dungeon_list(Il2CppObject* container) {
    return string_table::visit(container, "TextData_RailwayDungeon", [&](Il2CppObject* row, const std::string& key) {
        auto it = g_railway_dungeons.find(key);
        if (it == g_railway_dungeons.end()) return 0;
        int count = 0;
        if (!it->second.name.empty()) {
            if (!write_managed_text_named(row, "name", it->second.name)) return -1;
            count++;
        }
        if (!it->second.longName.empty()) {
            if (!write_managed_text_named(row, "longName", it->second.longName)) return -1;
            count++;
        }
        return count ? 1 : 0;
    }, true /* numeric_ids */);
}

DEFINE_UI_TABLE_PATCH(patch_railway_dungeon_buff_list, g_railway_dungeon_buffs)
DEFINE_UI_TABLE_PATCH(patch_normal_battle_result_hint_list, g_normal_battle_result_hints)
DEFINE_UI_TABLE_PATCH(patch_ab_battle_result_hint_list, g_ab_battle_result_hints)

int patch_lobby_bgm_list(Il2CppObject* container) {
    int patched = string_table::visit(container, "TextData_LobbyBGM", [&](Il2CppObject* row, const std::string& key) {
        auto it = g_lobby_bgm.find(key);
        if (it == g_lobby_bgm.end()) return 0;
        int writes = 0;
        if (!it->second.name.empty()) {
            if (!write_managed_text_named(row, "name", it->second.name)) return -1;
            ++writes;
        }
        if (!it->second.songWriter.empty()) {
            if (!write_managed_text_named(row, "songWriter", it->second.songWriter)) return -1;
            ++writes;
        }
        return writes ? 1 : 0;
    }, true /* numeric_ids */, true /* allow_unidentified_rows */);
    LOGI("[LLC-DATA] <LobbyBGM>k__BackingField patched: %d rows matched local translations", patched);
    return patched;
}

DEFINE_UI_TABLE_PATCH(patch_battle_result_hint_list, g_battle_result_hints)
DEFINE_UI_TABLE_PATCH(patch_tutorial_desc_list, g_tutorial_descs)

int patch_item_list(Il2CppObject* container) {
    return patch_user_ticket_map(container, g_items, "_itemList");
}

DEFINE_UI_TABLE_PATCH(patch_gacha_title_list, g_gacha_titles)
DEFINE_UI_TABLE_PATCH(patch_mirror_dungeon_rental_name_list, g_mirror_dungeon_rental_names)
DEFINE_UI_TABLE_PATCH(patch_error_code_msg_list, g_error_code_msgs)
DEFINE_UI_TABLE_PATCH(patch_dungeon_area_list, g_dungeon_areas)

int patch_iap_sticker_list(Il2CppObject* container) {
    return string_table::patch(container, g_iap_stickers, "TextData_IAPSticker", "content", true /* numeric_ids */, true /* allow_unidentified_rows */);
}

int patch_keyword_dictionary_list(Il2CppObject* container) {
    return string_table::visit(container, "TextData_KeywordDictionary", [&](Il2CppObject* row, const std::string& key) {
        auto it = g_keyword_dictionary.find(key);
        if (it == g_keyword_dictionary.end()) return 0;
        int count = 0;
        if (!it->second.name.empty()) {
            if (!write_managed_text_named(row, "name", it->second.name)) return -1;
            count++;
        }
        if (!it->second.desc.empty()) {
            if (!write_managed_text_named(row, "desc", it->second.desc)) return -1;
            count++;
        }
        return count ? 1 : 0;
    }, false /* numeric_ids */, true /* allow_unidentified_rows */);
}

int patch_scenario_model_code_list(Il2CppObject* container) {
    return string_table::visit(container, "TextData_ScenarioModelCode", [&](Il2CppObject* row, const std::string& key) {
        auto it = g_scenario_model_codes.find(key);
        if (it == g_scenario_model_codes.end()) return 0;
        int count = 0;
        if (!it->second.name.empty()) {
            if (!write_managed_text_named(row, "name", it->second.name)) return -1;
            count++;
        }
        if (!it->second.nickName.empty()) {
            if (!write_managed_text_named(row, "nickName", it->second.nickName)) return -1;
            count++;
        }
        return count ? 1 : 0;
    }, false /* numeric_ids */, true /* allow_unidentified_rows */);
}

int patch_battle_speech_bubble_list(Il2CppObject* container) {
    return string_table::visit(container, "TextData_BattleSpeechBubble", [&](Il2CppObject* row, const std::string& key) {
        auto it = g_battle_speech_bubbles.find(key);
        if (it == g_battle_speech_bubbles.end()) return 0;
        int count = 0;
        if (!it->second.desc.empty()) {
            if (!write_managed_text_named(row, "desc", it->second.desc)) return -1;
            count++;
        }
        if (!it->second.dlg.empty()) {
            if (!write_managed_text_named(row, "dlg", it->second.dlg)) return -1;
            count++;
        }
        return count ? 1 : 0;
    }, false /* numeric_ids */, true /* allow_unidentified_rows */);
}

int patch_iap_product_list(Il2CppObject* container) {
    return string_table::visit(container, "TextData_IAPProduct", [&](Il2CppObject* row, const std::string& key) {
        auto it = g_iap_products.find(key);
        if (it == g_iap_products.end()) return 0;
        int count = 0;
        if (!it->second.name.empty()) {
            if (!write_managed_text_named(row, "name", it->second.name)) return -1;
            count++;
        }
        if (!it->second.desc.empty()) {
            if (!write_managed_text_named(row, "desc", it->second.desc)) return -1;
            count++;
        }
        if (!it->second.subDesc.empty()) {
            if (!write_managed_text_named(row, "subDesc", it->second.subDesc)) return -1;
            count++;
        }
        if (!it->second.additionalItemDesc.empty()) {
            if (!write_managed_text_named(row, "additionalItemDesc", it->second.additionalItemDesc)) return -1;
            count++;
        }
        if (!it->second.otherItemDesc.empty()) {
            if (!write_managed_text_named(row, "otherItemDesc", it->second.otherItemDesc)) return -1;
            count++;
        }
        return count ? 1 : 0;
    }, true /* numeric_ids */, true /* allow_unidentified_rows */);
}

int patch_dungeon_node_text_list(Il2CppObject* container) {
    return string_table::visit(container, "TextData_StageText", [&](Il2CppObject* row, const std::string& key) {
        auto it = g_dungeon_node_texts.find(key);
        if (it == g_dungeon_node_texts.end()) return 0;
        int count = 0;
        if (!it->second.title.empty()) {
            if (!write_managed_text_named(row, "title", it->second.title)) return -1;
            count++;
        }
        if (!it->second.desc.empty()) {
            if (!write_managed_text_named(row, "desc", it->second.desc)) return -1;
            count++;
        }
        if (!it->second.place.empty()) {
            if (!write_managed_text_named(row, "place", it->second.place)) return -1;
            count++;
        }
        return count ? 1 : 0;
    }, true /* numeric_ids */, true /* allow_unidentified_rows */);
}

int patch_file_download_desc_list(Il2CppObject* container) {
    return string_table::visit(container, "TextData_StoryText", [&](Il2CppObject* row, const std::string& key) {
        auto it = g_file_download_descs.find(key);
        if (it == g_file_download_descs.end()) return 0;
        int count = 0;
        if (!it->second.title.empty()) {
            if (!write_managed_text_named(row, "title", it->second.title)) return -1;
            count++;
        }
        if (!it->second.desc.empty()) {
            if (!write_managed_text_named(row, "desc", it->second.desc)) return -1;
            count++;
        }
        return count ? 1 : 0;
    }, false /* numeric_ids */, true /* allow_unidentified_rows */);
}

int patch_buff_ability_list(Il2CppObject* container) {
    return string_table::visit(container, "TextData_BuffAbility", [&](Il2CppObject* row, const std::string& key) {
        auto it = g_buff_abilities.find(key);
        if (it == g_buff_abilities.end()) return 0;
        int count = 0;
        if (!it->second.desc.empty()) {
            if (!write_managed_text_named(row, "desc", it->second.desc)) return -1;
            count++;
        }
        if (!it->second.variation.empty()) {
            if (!write_managed_text_named(row, "variation", it->second.variation)) return -1;
            count++;
        }
        if (!it->second.variation2.empty()) {
            if (!write_managed_text_named(row, "variation2", it->second.variation2)) return -1;
            count++;
        }
        return count ? 1 : 0;
    }, false /* numeric_ids */, true /* allow_unidentified_rows */);
}

int patch_gacha_notice_list(Il2CppObject* container) {
    return string_table::patch(container, g_gacha_notices, "TextData_UI", "content", false, true);
}

int patch_lobby_bgm_category_list(Il2CppObject* container) {
    return string_table::patch(container, g_lobby_bgm_categories, "TextData_UI", "content", false, true);
}

int patch_enemy_list(Il2CppObject* container) {
    return string_table::visit(container, "TextData_Enemy", [&](Il2CppObject* row, const std::string& key) {
        auto it = g_enemies.find(key);
        if (it == g_enemies.end()) return 0;
        int count = 0;
        if (!it->second.name.empty()) {
            if (!write_managed_text_named(row, "name", it->second.name)) return -1;
            count++;
        }
        if (!it->second.desc.empty()) {
            if (!write_managed_text_named(row, "desc", it->second.desc)) return -1;
            count++;
        }
        return count ? 1 : 0;
    }, true /* numeric_ids */, true /* allow_unidentified_rows */);
}

static bool write_managed_string_array(Il2CppObject* owner, const char* field_name, const std::vector<std::string>& texts, int& writes) {
    if (texts.empty()) return true;
    auto* array_obj = string_table::reference(owner, field_name);
    if (!array_obj) return false;
    uintptr_t len = il2cpp_array_length_fn(array_obj);
    Il2CppObject** elements = reinterpret_cast<Il2CppObject**>(reinterpret_cast<char*>(array_obj) + 4 * sizeof(void*));
    for (size_t i = 0; i < texts.size() && i < len; ++i) {
        if (!texts[i].empty()) {
            Il2CppString* str = g_managed_string_pool.intern(texts[i]);
            if (!str) { translation_evidence::write(false, "string_allocation_failed"); return false; }
            il2cpp_gc_wbarrier_set_field_fn(array_obj, reinterpret_cast<void**>(&elements[i]), str);
            writes++;
            ++g_managed_text_writes;
            translation_evidence::write(true);
        }
    }
    return true;
}

static bool write_managed_string_list(Il2CppObject* owner, const char* field_name, const std::vector<std::string>& texts, int& writes) {
    if (texts.empty()) return true;
    auto* list_obj = read_named_managed_list(owner, field_name);
    if (!list_obj) return false;
    structured_content::List list(list_obj);
    if (!list.valid) return false;
    auto* array_obj = list.array.get();
    Il2CppObject** elements = reinterpret_cast<Il2CppObject**>(reinterpret_cast<char*>(array_obj) + 4 * sizeof(void*));
    for (size_t j = 0; j < texts.size() && static_cast<int32_t>(j) < list.size; ++j) {
        if (!texts[j].empty()) {
            Il2CppString* str = g_managed_string_pool.intern(texts[j]);
            if (!str) { translation_evidence::write(false, "string_allocation_failed"); return false; }
            il2cpp_gc_wbarrier_set_field_fn(array_obj, reinterpret_cast<void**>(&elements[j]), str);
            writes++;
            ++g_managed_text_writes;
            translation_evidence::write(true);
        }
    }
    return true;
}

DEFINE_UI_TABLE_PATCH(patch_choice_event_result_list, g_choice_event_results)

int patch_event_dialogue_list(Il2CppObject* container) {
    int matched = 0;
    auto stamp = event_dialogue::inspect(container, [&](Il2CppObject* row) {
        auto id = read_named_managed_id(row, "id");
        if (id.empty()) { translation_evidence::unidentified_row(); return false; }
        int32_t personality = 0;
        if (!string_table::integer(row, "personalityid", personality) || personality <= 0) {
            translation_evidence::invalid_row("event_dialogue_personality_missing"); return false;
        }
        id = std::to_string(personality) + "/" + id;
        translation_evidence::observe(id);
        auto found = g_event_dialogues.find(id);
        if (found == g_event_dialogues.end()) return true;
        if (personality != found->second.personality_id) {
            translation_evidence::invalid_row("event_dialogue_personality_mismatch"); return false;
        }
        if (!found->second.dialog.empty() && !write_managed_text_named(row, "dialog", found->second.dialog)) return false;
        ++matched;
        return true;
    });
    if (stamp.ready()) translation_evidence::complete();
    else translation_evidence::invalid_row("event_dialogue_graph_incomplete");
    return matched;
}

int patch_abnormality_event_list(Il2CppObject* container) {
    return string_table::visit(container, "TextData_AbnormalityEvents", [&](Il2CppObject* row, const std::string& key) {
        auto it = g_abnormality_events.find(key);
        if (it == g_abnormality_events.end()) return 0;
        int count = 0;
        if (!it->second.eventDesc.empty()) {
            if (!write_managed_text_named(row, "eventDesc", it->second.eventDesc)) return -1;
            count++;
        }
        if (!it->second.prevDesc.empty()) {
            if (!write_managed_text_named(row, "prevDesc", it->second.prevDesc)) return -1;
            count++;
        }
        if (!it->second.behaveDesc.empty()) {
            if (!write_managed_text_named(row, "behaveDesc", it->second.behaveDesc)) return -1;
            count++;
        }
        if (!it->second.cancelDesc.empty()) {
            if (!write_managed_text_named(row, "cancelDesc", it->second.cancelDesc)) return -1;
            count++;
        }
        if (!write_managed_string_array(row, "successDesc", it->second.successDesc, count)) return -1;
        if (!write_managed_string_array(row, "failureDesc", it->second.failureDesc, count)) return -1;
        return count ? 1 : 0;
    }, true /* numeric_ids */, true /* allow_unidentified_rows */);
}

int patch_action_event_list(Il2CppObject* container) {
    return string_table::visit(container, "TextData_ActionEvents", [&](Il2CppObject* row, const std::string& key) {
        auto it = g_action_events.find(key);
        if (it == g_action_events.end()) return 0;
        int count = 0;
        if (!it->second.name.empty()) {
            if (!write_managed_text_named(row, "name", it->second.name)) return -1;
            count++;
        }
        if (!it->second.desc.empty()) {
            if (!write_managed_text_named(row, "desc", it->second.desc)) return -1;
            count++;
        }
        if (!it->second.subDesc.empty()) {
            if (!write_managed_text_named(row, "subDesc", it->second.subDesc)) return -1;
            count++;
        }
        auto* options_obj = read_named_managed_list(row, "options");
        if (options_obj && !it->second.options.empty()) {
            structured_content::List opt_list(options_obj);
            if (opt_list.valid) {
                for (size_t i = 0; i < it->second.options.size() && static_cast<int32_t>(i) < opt_list.size; ++i) {
                    auto* opt_item = opt_list.at(static_cast<int32_t>(i));
                    if (!opt_item) continue;
                    const auto& opt_data = it->second.options[i];
                    if (!opt_data.message.empty()) {
                        if (!write_managed_text_named(opt_item, "message", opt_data.message)) return -1;
                        count++;
                    }
                    if (!opt_data.messageDesc.empty()) {
                        if (!write_managed_text_named(opt_item, "messageDesc", opt_data.messageDesc)) return -1;
                        count++;
                    }
                    if (!write_managed_string_list(opt_item, "result", opt_data.result, count)) return -1;
                }
            }
        }
        return count ? 1 : 0;
    }, true /* numeric_ids */, true /* allow_unidentified_rows */);
}

int patch_abnormality_guide_list(Il2CppObject* container) {
    return string_table::visit(container, "TextData_AbnormalityContents", [&](Il2CppObject* row, const std::string& key) {
        auto it = g_abnormality_guides.find(key);
        if (it == g_abnormality_guides.end()) return 0;
        int count = 0;
        if (!it->second.codeName.empty()) {
            if (!write_managed_text_named(row, "codeName", it->second.codeName)) return -1;
            count++;
        }
        if (!it->second.name.empty()) {
            if (!write_managed_text_named(row, "name", it->second.name)) return -1;
            count++;
        }
        if (!it->second.clue.empty()) {
            if (!write_managed_text_named(row, "clue", it->second.clue)) return -1;
            count++;
        }
        auto* story_list_obj = read_named_managed_list(row, "storyList");
        if (story_list_obj && !it->second.storyList.empty()) {
            structured_content::List stories(story_list_obj);
            if (stories.valid) {
                for (size_t i = 0; i < it->second.storyList.size() && static_cast<int32_t>(i) < stories.size; ++i) {
                    auto* story_item = stories.at(static_cast<int32_t>(i));
                    if (!story_item) continue;
                    const auto& story_data = it->second.storyList[i];
                    if (!story_data.story.empty()) {
                        if (!write_managed_text_named(story_item, "story", story_data.story)) return -1;
                        count++;
                    }
                }
            }
        }
        return count ? 1 : 0;
    }, true /* numeric_ids */, true /* allow_unidentified_rows */);
}

int patch_stage_chapter_list(Il2CppObject* chapter_obj) {
    if (!chapter_obj) return 0;
    Il2CppObject* dic = string_table::reference(chapter_obj, "_dic");
    if (!dic) dic = *(Il2CppObject**)((char*)chapter_obj + 16);
    if (!dic) return 0;

    Il2CppObject* entries = string_table::reference(dic, "_entries");
    if (!entries) entries = *(Il2CppObject**)((char*)dic + 24);
    int count = 0;
    if (!string_table::integer(dic, "_count", count)) count = *(int*)((char*)dic + 32);
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
            if (!it->second.company.empty()) {
                if (!write_managed_text_named(v, "company", it->second.company)) write_managed_text(v, 24, it->second.company);
            }
            if (!it->second.area.empty()) {
                if (!write_managed_text_named(v, "area", it->second.area)) write_managed_text(v, 32, it->second.area);
            }
            if (!it->second.chaptertitle.empty()) {
                if (!write_managed_text_named(v, "chaptertitle", it->second.chaptertitle)) write_managed_text(v, 40, it->second.chaptertitle);
            }
            if (!it->second.chapter.empty()) {
                if (!write_managed_text_named(v, "chapter", it->second.chapter)) write_managed_text(v, 48, it->second.chapter);
            }
            if (!it->second.chapterNumber.empty()) {
                if (!write_managed_text_named(v, "chapterNumber", it->second.chapterNumber)) write_managed_text(v, 56, it->second.chapterNumber);
            }
            if (!it->second.timeline.empty()) {
                if (!write_managed_text_named(v, "timeline", it->second.timeline)) write_managed_text(v, 64, it->second.timeline);
            }
            patched++;
        }
    }
    LOGI("[LLC-DATA] _stageChapter patched: %d / %d rows matched local translations", patched, count);
    return patched;
}

int patch_stage_node_list(Il2CppObject* node_obj, const char* name) {
    if (!node_obj) return 0;
    Il2CppObject* dic = string_table::reference(node_obj, "_dic");
    if (!dic) dic = *(Il2CppObject**)((char*)node_obj + 16);
    if (!dic) return 0;

    Il2CppObject* entries = string_table::reference(dic, "_entries");
    if (!entries) entries = *(Il2CppObject**)((char*)dic + 24);
    int count = 0;
    if (!string_table::integer(dic, "_count", count)) count = *(int*)((char*)dic + 32);
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
            if (!it->second.title.empty()) {
                if (!write_managed_text_named(v, "title", it->second.title)) write_managed_text(v, 24, it->second.title);
            }
            if (!it->second.desc.empty()) {
                if (!write_managed_text_named(v, "desc", it->second.desc)) write_managed_text(v, 32, it->second.desc);
            }
            if (!it->second.place.empty()) {
                if (!write_managed_text_named(v, "place", it->second.place)) write_managed_text(v, 40, it->second.place);
            }
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

bool scenario_ordered_aligned(const ContentStamp& stamp, const StoryRows& rows) {
    if (!stamp.ready()) return false;
    auto* const* entries = reinterpret_cast<Il2CppObject* const*>(stamp.entries + 32);
    return ordered_story_aligned(rows, static_cast<size_t>(stamp.count),
        [&](size_t i) { return read_named_managed_id(entries[i], "Id"); });
}

const ScenarioDialogue* scenario_dialogue_translation(Il2CppObject* scenario, Il2CppObject* dialog,
                                                     const StoryRows& rows, const std::string& id) {
    if (rows.ordered.empty()) return story_row_at(rows, id, 0, false);
    if (!dialog) return nullptr;
    const auto stamp = scenario_stamp(scenario);
    if (!scenario_ordered_aligned(stamp, rows)) return nullptr;
    auto* const* entries = reinterpret_cast<Il2CppObject* const*>(stamp.entries + 32);
    for (int i = 0; i < stamp.count; ++i)
        if (entries[i] == dialog) return story_row_at(rows, id, i, true);
    return nullptr;
}

StoryPatchResult patch_scenario_object(Il2CppObject* scn, const char* scen_id) {
    StoryPatchResult result;
    auto stamp = scenario_stamp(scn);
    result.rows = stamp.count;
    if (!stamp.ready() || !scen_id || !*scen_id) return result;
    std::string id(scen_id);
    const auto& resource = story_translation(id);
    if (!resource.found) {
        warning_summary::Batch warnings; warnings["resource_unavailable"].add(id);
        diagnostic_log::submit("story_resource", warnings);
        return result;
    }
    diagnostic_log::submit("story_resource", {});
    const auto& dialogues = resource.rows;
    if (dialogues.empty()) return result;
    const bool ordered_aligned = scenario_ordered_aligned(stamp, dialogues);
    if (!dialogues.ordered.empty() && !ordered_aligned) {
        warning_summary::Batch warnings; warnings["ordered_layout_mismatch"].add(id);
        diagnostic_log::submit("story_schema", warnings);
        return result;
    }
    auto** rows = reinterpret_cast<Il2CppObject**>(stamp.entries + 32);
    auto before = g_managed_text_writes;
    diagnostic_log::Scope schema_warnings("story_schema");
    bool schema_sampled = false;
    for (int i = 0; i < stamp.count; ++i) {
        auto* row = rows[i];
        if (!row) continue;
        std::string key = read_named_managed_id(row, "id");
        if (key.empty()) key = read_named_managed_id(row, "ID");
        if (key.empty()) {
            if (!schema_sampled) {
                schema_sampled = true;
                for (auto* k = il2cpp_object_get_class_fn(row); k; k = il2cpp_class_get_parent_fn(k)) {
                    void* iter = nullptr;
                    while (auto* field = il2cpp_class_get_fields_fn(k, &iter)) {
                        auto* type = il2cpp_class_from_type_fn(il2cpp_field_get_type_fn(field));
                        schema_warnings.add("row_id_schema", std::string(il2cpp_class_get_name_fn(k)) + "." +
                            il2cpp_field_get_name_fn(field) + ";offset=" + std::to_string(il2cpp_field_get_offset_fn(field)) +
                            ";type=" + (type ? il2cpp_class_get_name_fn(type) : "unknown"));
                    }
                }
            }
        }
        const auto* text = story_row_at(dialogues, key, i, ordered_aligned);
        if (!text) continue;
        ++result.matched;
        auto write = [&](const char* field, const std::string& value) {
            if (!value.empty() && !write_managed_text_named(row, field, value)) ++result.failed_writes;
        };
        const auto fields = resolve_story_fields(*text,
            read_named_managed_id(row, "Model"), read_named_managed_id(row, "Teller"),
            read_named_managed_id(row, "Title"), g_scenario_model_codes, g_ui_map);
        write("content", fields.content);
        write("place", fields.place);
        write("teller", fields.speaker);
        write("title", fields.title);
    }
    result.strings_written = g_managed_text_writes - before;
    return result;
}
