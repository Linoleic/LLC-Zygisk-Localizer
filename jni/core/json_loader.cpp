#include "json_loader.hpp"
#include "storage.hpp"
#include "il2cpp_api.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include "translation_file.hpp"
#include "ui_content.hpp"
#include "translation_evidence.hpp"
#include "skill_content.hpp"
#include "catalog_content.hpp"
#include "flat_content.hpp"

static bool read_translation_source(const std::string& path, std::string& source, size_t limit = 15 * 1024 * 1024) {
    auto error = translation_file::read(path, source, limit);
    translation_evidence::registry.source(path, source, error == translation_file::Error::None ? "" : translation_file::reason(error));
    if (error != translation_file::Error::None) {
        LOGW("[LLC-RESOURCE] Rejected %s: %s", path.c_str(), translation_file::reason(error));
        return false;
    }
    return true;
}

static void load_scoped_file(const std::string& path, const std::string& scope, bool lyrics) {
    std::string source;
    if (!read_translation_source(path, source)) return;
    if (!parse_scoped_content(source, scope, lyrics, g_bgm_lyrics, g_dungeon_nodes))
        LOGW("[LLC-RESOURCE] Invalid scoped content: %s", path.c_str());
}

static bool parse_ui_json_file(const std::string& filepath) {
    std::string source;
    return read_translation_source(filepath, source) && parse_ui_document(source, g_ui_map);
}

static void load_string_table(const std::string& path, const char* field, std::unordered_map<std::string, std::string>& output) {
    std::string source;
    if (read_translation_source(path, source)) parse_string_table_document(source, output, field);
}

static void load_catalog(const std::string& path, bool gift) {
    std::string source;
    if (!read_translation_source(path, source)) return;
    if (!(gift ? catalog_content::gifts(source, g_ego_gifts) : catalog_content::notes(source, g_dante_notes))) {
        translation_evidence::registry.source(path, source, "invalid_catalog_model");
        LOGW("[LLC-RESOURCE] Rejected catalog model: %s", path.c_str());
    }
}

void load_all_ui_jsons(const char* dir_path) {
    DIR* d = opendir(dir_path);
    if (!d) {
        LOGE("[LLC-CORE] Failed to open lang dir: %s", dir_path);
        return;
    }
    struct dirent* entry;
    int files_scanned = 0, valid_files = 0;
    while ((entry = readdir(d)) != nullptr) {
        const char* name = entry->d_name;
        // The managed pack selector verifies this metadata before loading text.
        // It is not a dataList document and must never enter the UI dictionary.
        if (strcmp(name, "_llc_manifest.json") == 0) continue;
        size_t len = strlen(name);
        if (len > 5 && strcmp(name + len - 5, ".json") == 0) {
            std::string full_path = std::string(dir_path) + "/" + name;
            ++files_scanned;
            if (parse_ui_json_file(full_path)) ++valid_files;
        }
    }
    closedir(d);
    LOGI("[LLC-RESOURCE] Validated %d/%d JSON documents; %zu unique nonempty UI keys", valid_files, files_scanned, g_ui_map.size());
}

static void parse_personality_json(const std::string& filepath) {
    std::string source;
    if (!read_translation_source(filepath, source)) return;
    const char* buf = source.c_str();

    const char* cur = buf;
    while ((cur = strstr(cur, "\"id\""))) {
        const char* obj_start = cur;
        while (obj_start > buf && *obj_start != '{') obj_start--;
        const char* obj_end = find_json_object_end(obj_start);
        if (!obj_end) break;

        std::string id_str;
        int id_int = 0;
        if (extract_json_id(cur, obj_end, id_str, id_int) && id_int != 0) {
            PersonalityEntry entry;
            entry.id = id_int;
            extract_json_field(cur, obj_end, "\"title\"", entry.title);
            extract_json_field(cur, obj_end, "\"name\"", entry.name);
            extract_json_field(cur, obj_end, "\"desc\"", entry.desc);
            g_personalities[id_int] = entry;
        }
        cur = obj_end + 1;
    }
}

static void parse_passive_json(const std::string& filepath) {
    std::string source;
    if (!read_translation_source(filepath, source)) return;
    const char* buf = source.c_str();

    const char* cur = buf;
    while ((cur = strstr(cur, "\"id\""))) {
        const char* obj_start = cur;
        while (obj_start > buf && *obj_start != '{') obj_start--;
        const char* obj_end = find_json_object_end(obj_start);
        if (!obj_end) break;

        std::string id_str;
        int id_int = 0;
        if (extract_json_id(cur, obj_end, id_str, id_int) && id_int != 0) {
            PassiveEntry entry;
            entry.id = id_int;
            extract_json_field(cur, obj_end, "\"name\"", entry.name);
            extract_json_field(cur, obj_end, "\"desc\"", entry.desc);
            extract_json_field(cur, obj_end, "\"summary\"", entry.summary);
            extract_json_field(cur, obj_end, "\"flavor\"", entry.flavor);
            g_passives[id_int] = entry;
        }
        cur = obj_end + 1;
    }
}

static void parse_flat_json(const std::string& filepath, std::unordered_map<std::string, BufEntry>& output) {
    std::string source;
    if (!read_translation_source(filepath, source)) return;
    if (!parse_flat_content_document(source, output)) {
        translation_evidence::registry.source(filepath, source, "invalid_flat_content_model");
        LOGW("[LLC-RESOURCE] Rejected flat content model: %s", filepath.c_str());
    }
}

static void parse_ego_json(const std::string& filepath) {
    std::string source;
    if (!read_translation_source(filepath, source)) return;
    const char* buf = source.c_str();

    const char* cur = buf;
    while ((cur = strstr(cur, "\"id\""))) {
        const char* obj_start = cur;
        while (obj_start > buf && *obj_start != '{') obj_start--;
        const char* obj_end = find_json_object_end(obj_start);
        if (!obj_end) break;

        std::string id_str;
        int id_int = 0;
        if (extract_json_id(cur, obj_end, id_str, id_int) && id_int != 0) {
            EgoEntry entry;
            entry.id = id_int;
            extract_json_field(cur, obj_end, "\"name\"", entry.name);
            extract_json_field(cur, obj_end, "\"desc\"", entry.desc);
            g_egos[id_int] = entry;
        }
        cur = obj_end + 1;
    }
}

static void parse_character_json(const std::string& filepath) {
    std::string source;
    if (!read_translation_source(filepath, source)) return;
    const char* buf = source.c_str();

    const char* cur = buf;
    while ((cur = strstr(cur, "\"id\""))) {
        const char* obj_start = cur;
        while (obj_start > buf && *obj_start != '{') obj_start--;
        const char* obj_end = find_json_object_end(obj_start);
        if (!obj_end) break;

        std::string id_str;
        int id_int = 0;
        if (extract_json_id(cur, obj_end, id_str, id_int) && id_int != 0) {
            CharacterEntry entry;
            entry.id = id_int;
            extract_json_field(cur, obj_end, "\"name\"", entry.name);
            g_characters[id_int] = entry;
        }
        cur = obj_end + 1;
    }
}

static void parse_skill_json(const std::string& filepath) {
    std::string source;
    if (!read_translation_source(filepath, source)) return;
    if (!parse_skill_document(source, g_skills)) {
        translation_evidence::registry.source(filepath, source, "invalid_skill_model");
        LOGW("[LLC-RESOURCE] Rejected skill model: %s", filepath.c_str());
    }
}

static void parse_voice_json_file(const std::string& filepath) {
    std::string source;
    if (!read_translation_source(filepath, source)) return;
    const char* buf = source.c_str();

    const char* cur = buf;
    while ((cur = strstr(cur, "\"id\""))) {
        const char* obj_start = cur;
        while (obj_start > buf && *obj_start != '{') obj_start--;
        const char* obj_end = find_json_object_end(obj_start);
        if (!obj_end) break;

        std::string id_str;
        int dummy_int = 0;
        if (extract_json_id(cur, obj_end, id_str, dummy_int) && !id_str.empty()) {
            VoiceDlgEntry entry;
            entry.id = id_str;
            extract_json_field(cur, obj_end, "\"desc\"", entry.desc);
            extract_json_field(cur, obj_end, "\"dlg\"", entry.dlg);
            extract_json_field(cur, obj_end, "\"lyrics\"", entry.dlg);
            g_voice_dlgs[id_str] = entry;
        }
        cur = obj_end + 1;
    }
}

static void load_voice_dir(const char* dir_path) {
    DIR* d = opendir(dir_path);
    if (!d) return;
    struct dirent* entry;
    while ((entry = readdir(d)) != nullptr) {
        const char* name = entry->d_name;
        size_t len = strlen(name);
        if (len > 5 && strcmp(name + len - 5, ".json") == 0) {
            std::string full_path = std::string(dir_path) + "/" + name;
            parse_voice_json_file(full_path);
        }
    }
    closedir(d);
}

static void parse_theater_json_file(const std::string& filepath) {
    std::string source;
    if (!read_translation_source(filepath, source)) return;
    const char* buf = source.c_str();

    const char* cur = buf;
    while ((cur = strstr(cur, "\"id\""))) {
        const char* obj_start = cur;
        while (obj_start > buf && *obj_start != '{') obj_start--;
        const char* obj_end = find_json_object_end(obj_start);
        if (!obj_end) break;

        std::string id_str;
        int dummy_int = 0;
        if (extract_json_id(cur, obj_end, id_str, dummy_int) && !id_str.empty()) {
            TheaterEntry entry;
            entry.id = id_str;
            extract_json_field(cur, obj_end, "\"title\"", entry.title);
            extract_json_field(cur, obj_end, "\"desc\"", entry.desc);
            g_theater_map[id_str] = entry;
        }
        cur = obj_end + 1;
    }
}

static void parse_banner_json_file(const std::string& filepath) {
    std::string source;
    if (!read_translation_source(filepath, source)) return;
    const char* buf = source.c_str();

    const char* cur = buf;
    while ((cur = strstr(cur, "\"id\""))) {
        const char* obj_start = cur;
        while (obj_start > buf && *obj_start != '{') obj_start--;
        const char* obj_end = find_json_object_end(obj_start);
        if (!obj_end) break;

        std::string id_str;
        int id_int = 0;
        if (extract_json_id(cur, obj_end, id_str, id_int) && id_int != 0) {
            BannerEntry entry;
            entry.id = id_int;
            extract_json_field(cur, obj_end, "\"name\"", entry.name);
            extract_json_field(cur, obj_end, "\"desc\"", entry.desc);
            extract_json_field(cur, obj_end, "\"flavor\"", entry.flavor);
            g_banner_map[id_int] = entry;
        }
        cur = obj_end + 1;
    }
}

static void parse_stage_chapter_json_file(const std::string& filepath) {
    std::string source;
    if (!read_translation_source(filepath, source)) return;
    const char* buf = source.c_str();

    const char* cur = buf;
    while ((cur = strstr(cur, "\"id\""))) {
        const char* obj_start = cur;
        while (obj_start > buf && *obj_start != '{') obj_start--;
        const char* obj_end = find_json_object_end(obj_start);
        if (!obj_end) break;

        std::string id_str;
        int dummy_int = 0;
        if (extract_json_id(cur, obj_end, id_str, dummy_int) && !id_str.empty()) {
            StageChapterEntry entry;
            entry.id = id_str;
            extract_json_field(cur, obj_end, "\"company\"", entry.company);
            extract_json_field(cur, obj_end, "\"area\"", entry.area);
            extract_json_field(cur, obj_end, "\"chaptertitle\"", entry.chaptertitle);
            extract_json_field(cur, obj_end, "\"chapter\"", entry.chapter);
            extract_json_field(cur, obj_end, "\"chapterNumber\"", entry.chapterNumber);
            extract_json_field(cur, obj_end, "\"timeline\"", entry.timeline);
            g_stage_chapter_map[id_str] = entry;
        }
        cur = obj_end + 1;
    }
}

static void parse_stage_node_json_file(const std::string& filepath) {
    std::string source;
    if (!read_translation_source(filepath, source)) return;
    const char* buf = source.c_str();

    const char* cur = buf;
    while ((cur = strstr(cur, "\"id\""))) {
        const char* obj_start = cur;
        while (obj_start > buf && *obj_start != '{') obj_start--;
        const char* obj_end = find_json_object_end(obj_start);
        if (!obj_end) break;

        std::string id_str;
        int id_int = 0;
        if (extract_json_id(cur, obj_end, id_str, id_int) && id_int != 0) {
            StageNodeEntry entry;
            entry.id = id_int;
            extract_json_field(cur, obj_end, "\"title\"", entry.title);
            extract_json_field(cur, obj_end, "\"desc\"", entry.desc);
            extract_json_field(cur, obj_end, "\"place\"", entry.place);
            g_stage_node_map[id_int] = entry;
        }
        cur = obj_end + 1;
    }
}

void load_all_content_jsons(const char* dir_path) {
    DIR* d = opendir(dir_path);
    if (!d) return;

    struct dirent* entry;
    while ((entry = readdir(d)) != nullptr) {
        const char* name = entry->d_name;
        size_t len = strlen(name);
        if (len <= 5 || strcmp(name + len - 5, ".json") != 0) continue;

        std::string full_path = std::string(dir_path) + "/" + name;
        if (strcmp(name, "SkillTag.json") == 0) {
            load_string_table(full_path, "name", g_skill_tags);
        } else if (strcmp(name, "BattleHint.json") == 0) {
            load_string_table(full_path, "content", g_battle_hints);
        } else if (strncmp(name, "Personalities", 13) == 0) {
            parse_personality_json(full_path);
        } else if (strncmp(name, "Passive", 7) == 0) {
            parse_passive_json(full_path);
        } else if (strncmp(name, "Bufs", 4) == 0) {
            parse_flat_json(full_path, g_bufs);
        } else if (strncmp(name, "BattleKeywords", 14) == 0) {
            parse_flat_json(full_path, g_battle_keywords);
        } else if (strncmp(name, "Egos", 4) == 0) {
            parse_ego_json(full_path);
        } else if (strncmp(name, "Skills", 6) == 0) {
            parse_skill_json(full_path);
        } else if (strcmp(name, "Characters.json") == 0) {
            parse_character_json(full_path);
        } else if (strcmp(name, "UserBanner.json") == 0) {
            parse_banner_json_file(full_path);
        } else if (strncmp(name, "EGOgift_", 8) == 0) {
            load_catalog(full_path, true);
        } else if (strcmp(name, "EgoGiftCategory.json") == 0) {
            load_string_table(full_path, "name", g_ego_gift_categories);
        } else if (strcmp(name, "MirrorDungeonEgoGiftLockedDesc.json") == 0) {
            std::string source;
            if (read_translation_source(full_path, source) && !catalog_content::strings(source, "content", g_ego_gift_locked))
                translation_evidence::registry.source(full_path, source, "invalid_catalog_string_model");
        } else if (strncmp(name, "StoryTheaterDanteNoteDetail", 27) == 0) {
            load_catalog(full_path, false);
        } else if (strncmp(name, "StoryTheaterDanteNote", 21) == 0) {
            load_string_table(full_path, "content", g_dante_note_categories);
        } else if (strncmp(name, "StoryTheater", 12) == 0) {
            parse_theater_json_file(full_path);
        } else if (strncmp(name, "StageChapterText", 16) == 0) {
            parse_stage_chapter_json_file(full_path);
        } else if (strncmp(name, "DungeonNode", 11) == 0) {
            load_scoped_file(full_path, "", false);
        } else if (strncmp(name, "StageNode", 9) == 0 || strncmp(name, "RailwayDungeon", 14) == 0) {
            parse_stage_node_json_file(full_path);
        }
    }
    closedir(d);

    // Load subdirectories for voice lines
    LOGI("[LLC-RESOURCE] Scoped strings: %zu skill tags, %zu battle hints", g_skill_tags.size(), g_battle_hints.size());
    LOGI("[LLC-RESOURCE] Catalogs: %zu EGO gifts, %zu Dante notes, %zu note categories, %zu gift lock hints", g_ego_gifts.size(), g_dante_notes.size(), g_dante_note_categories.size(), g_ego_gift_locked.size());
    std::string base_dir(dir_path);
    load_voice_dir((base_dir + "/PersonalityVoiceDlg").c_str());
    load_voice_dir((base_dir + "/BattleAnnouncerDlg").c_str());
    load_voice_dir((base_dir + "/EGOVoiceDig").c_str());
    load_voice_dir((base_dir + "/EGOVoiceDlg").c_str());
    std::string lyrics_dir = base_dir + "/BgmLyrics";
    if (DIR* lyrics = opendir(lyrics_dir.c_str())) {
        while (auto* file = readdir(lyrics)) {
            std::string name(file->d_name);
            if (name.size() > 5 && name.substr(name.size() - 5) == ".json")
                load_scoped_file(lyrics_dir + "/" + name, name.substr(0, name.size() - 5), true);
        }
        closedir(lyrics);
    }
    LOGI("[LLC-RESOURCE] Scoped content: %zu songs, %zu dungeon groups", g_bgm_lyrics.size(), g_dungeon_nodes.size());

    LOGI("[LLC-CORE] Content JSONs loaded: %zu pers, %zu pass, %zu bufs, %zu kws, %zu egos, %zu skills, %zu chars, %zu voices, %zu banners, %zu theaters, %zu chapters, %zu nodes",
         g_personalities.size(), g_passives.size(), g_bufs.size(), g_battle_keywords.size(),
         g_egos.size(), g_skills.size(), g_characters.size(), g_voice_dlgs.size(),
         g_banner_map.size(), g_theater_map.size(), g_stage_chapter_map.size(), g_stage_node_map.size());
    // The source index is independent of the legacy extraction path. Comparing
    // fingerprints detects lost fields/escaping, not just missing map keys.
    auto& evidence = translation_evidence::registry;
    evidence.loaded.clear();
    for (const auto& row : g_skills) evidence.loaded_row("_skillList", std::to_string(row.first), translation_evidence::loaded_fields(row.second));
    for (const auto& row : g_passives) evidence.loaded_row("_passiveList", std::to_string(row.first), translation_evidence::loaded_fields(row.second));
    for (const auto& row : g_skill_tags) evidence.loaded_row("_skillTagList", row.first, {{"name", row.second}});
    for (const auto& row : g_battle_hints) evidence.loaded_row("_battleHint", row.first, {{"content", row.second}});
    for (const auto& row : g_bufs) evidence.loaded_row("_bufList", row.first, translation_evidence::loaded_fields(row.second));
    for (const auto& row : g_battle_keywords) evidence.loaded_row("_keywordList", row.first, translation_evidence::loaded_fields(row.second));
    for (const auto& row : g_ego_gifts) evidence.loaded_row("_egoGiftData", row.first, translation_evidence::loaded_fields(row.second));
    for (const auto& row : g_dante_notes) evidence.loaded_row("_danteNoteDesc", row.first, translation_evidence::source_fields({{"title", row.second.title}, {"desc", row.second.desc}}, "_danteNoteDesc"));
    for (const auto& row : g_dante_note_categories) evidence.loaded_row("_danteNoteCategoryKeyword", row.first, {{"content", row.second}});
    for (const auto& row : g_ego_gift_locked) evidence.loaded_row("_mirrorDungeonEgoGiftLockedDescList", row.first, {{"content", row.second}});
    for (const auto& row : g_ego_gift_categories) evidence.loaded_row("_egoGiftCategory", row.first, {{"name", row.second}});
}

void parse_scenario_json_file(const std::string& path, std::unordered_map<int, ScenarioDialogue>& out) {
    std::string source;
    if (!read_translation_source(path, source, 5 * 1024 * 1024)) return;
    if (!parse_story_document(source, out)) LOGW("[LLC-STORY] Invalid scenario JSON: %s", path.c_str());
}
