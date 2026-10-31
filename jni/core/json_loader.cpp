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

static void load_string_table(const std::string& path, const char* field, std::unordered_map<std::string, std::string>& output, bool allow_numeric_id = false) {
    std::string source;
    if (read_translation_source(path, source)) parse_string_table_document(source, output, field, allow_numeric_id);
}

static void load_catalog(const std::string& path, bool gift) {
    std::string source;
    if (!read_translation_source(path, source)) return;
    if (!(gift ? catalog_content::gifts(source, g_ego_gifts) : catalog_content::notes(source, g_dante_notes))) {
        translation_evidence::registry.source(path, source, "invalid_catalog_model");
        LOGW("[LLC-RESOURCE] Rejected catalog model: %s", path.c_str());
    }
}

static void load_mirror_dungeon_themes(const std::string& path) {
    std::string source;
    if (!read_translation_source(path, source)) return;
    auto document = nlohmann::json::parse(source, nullptr, false);
    if (!document.is_object() || !document.contains("dataList") || !document["dataList"].is_array()) {
        translation_evidence::registry.source(path, source, "invalid_theme_model");
        return;
    }
    std::unordered_map<std::string, MirrorDungeonThemeEntry> parsed;
    for (const auto& row : document["dataList"]) {
        if (!row.is_object()) { translation_evidence::registry.source(path, source, "invalid_theme_model"); return; }
        auto key = catalog_content::id(row);
        MirrorDungeonThemeEntry theme;
        if (key.empty() || !catalog_content::text(row, "name", theme.name) ||
            !catalog_content::text(row, "specialName", theme.specialName) || theme.name.empty() ||
            !parsed.emplace(key, std::move(theme)).second) {
            translation_evidence::registry.source(path, source, "invalid_theme_model");
            return;
        }
    }
    for (auto& entry : parsed) g_mirror_dungeon_themes.insert_or_assign(entry.first, std::move(entry.second));
}

static void load_announcer_file(const std::string& path) {
    std::string source;
    if (!read_translation_source(path, source)) return;
    auto document = nlohmann::json::parse(source, nullptr, false);
    if (!document.is_object() || !document.contains("dataList") || !document["dataList"].is_array()) {
        translation_evidence::registry.source(path, source, "invalid_announcer_model");
        return;
    }
    for (const auto& row : document["dataList"]) {
        if (!row.is_object()) continue;
        auto id = row.find("id");
        auto name = row.find("name");
        if (id == row.end() || name == row.end() || !name->is_string()) continue;
        std::string key;
        if (id->is_number_integer()) key = id->dump();
        else if (id->is_string()) key = id->get<std::string>();
        if (key.empty()) continue;
        std::string text = name->get<std::string>();
        if (text.empty()) continue;
        auto it = g_announcers.find(key);
        if (it != g_announcers.end() && !it->second.empty() && it->second != "？？" && text == "？？") {
            continue;
        }
        g_announcers[key] = std::move(text);
    }
}

static void load_user_ticket_file(const std::string& path, std::unordered_map<std::string, BannerEntry>& output) {
    std::string source;
    if (!read_translation_source(path, source)) return;
    auto document = nlohmann::json::parse(source, nullptr, false);
    if (!document.is_object() || !document.contains("dataList") || !document["dataList"].is_array()) {
        translation_evidence::registry.source(path, source, "invalid_user_ticket_model");
        return;
    }
    for (const auto& row : document["dataList"]) {
        if (!row.is_object()) continue;
        auto id = row.find("id");
        if (id == row.end()) continue;
        std::string key;
        if (id->is_number_integer()) key = id->dump();
        else if (id->is_string()) key = id->get<std::string>();
        if (key.empty()) continue;

        BannerEntry entry;
        if (id->is_number_integer()) entry.id = id->get<int>();
        auto name = row.find("name");
        if (name != row.end() && name->is_string()) entry.name = name->get<std::string>();
        auto desc = row.find("desc");
        if (desc != row.end() && desc->is_string()) entry.desc = desc->get<std::string>();
        auto flavor = row.find("flavor");
        if (flavor != row.end() && flavor->is_string()) entry.flavor = flavor->get<std::string>();

        if (!entry.name.empty() || !entry.desc.empty() || !entry.flavor.empty()) {
            output[key] = std::move(entry);
        }
    }
}

static void load_panic_info_file(const std::string& path) {
    std::string source;
    if (!read_translation_source(path, source)) return;
    auto document = nlohmann::json::parse(source, nullptr, false);
    if (!document.is_object() || !document.contains("dataList") || !document["dataList"].is_array()) {
        translation_evidence::registry.source(path, source, "invalid_panic_info_model");
        return;
    }
    for (const auto& row : document["dataList"]) {
        if (!row.is_object()) continue;
        auto id = row.find("id");
        if (id == row.end()) continue;
        std::string key;
        if (id->is_number_integer()) key = id->dump();
        else if (id->is_string()) key = id->get<std::string>();
        if (key.empty()) continue;

        PanicInfoEntry entry;
        auto panic_name = row.find("panicName");
        if (panic_name != row.end() && panic_name->is_string()) entry.panic_name = panic_name->get<std::string>();
        auto low_morale = row.find("lowMoraleDescription");
        if (low_morale != row.end() && low_morale->is_string()) entry.low_morale_desc = low_morale->get<std::string>();
        auto panic_desc = row.find("panicDescription");
        if (panic_desc != row.end() && panic_desc->is_string()) entry.panic_desc = panic_desc->get<std::string>();
        auto flavor = row.find("flavor");
        if (flavor != row.end() && flavor->is_string()) entry.flavor = flavor->get<std::string>();

        if (!entry.panic_name.empty() || !entry.low_morale_desc.empty() || !entry.panic_desc.empty() || !entry.flavor.empty()) {
            g_panic_infos[key] = std::move(entry);
        }
    }
}

static void load_mental_condition_file(const std::string& path) {
    std::string source;
    if (!read_translation_source(path, source)) return;
    auto document = nlohmann::json::parse(source, nullptr, false);
    if (!document.is_object() || !document.contains("dataList") || !document["dataList"].is_array()) {
        translation_evidence::registry.source(path, source, "invalid_mental_condition_model");
        return;
    }
    for (const auto& row : document["dataList"]) {
        if (!row.is_object()) continue;
        auto id = row.find("id");
        if (id == row.end()) continue;
        std::string key;
        if (id->is_string()) key = id->get<std::string>();
        else if (id->is_number_integer()) key = id->dump();
        if (key.empty()) continue;

        MentalConditionEntry entry;
        auto add = row.find("add");
        if (add != row.end() && add->is_string()) entry.add = add->get<std::string>();
        auto min = row.find("min");
        if (min != row.end() && min->is_string()) entry.min = min->get<std::string>();

        if (!entry.add.empty() || !entry.min.empty()) {
            g_mental_conditions[key] = std::move(entry);
        }
    }
}

static void load_dante_ability_file(const std::string& path) {
    std::string source;
    if (!read_translation_source(path, source)) return;
    auto document = nlohmann::json::parse(source, nullptr, false);
    if (!document.is_object() || !document.contains("dataList") || !document["dataList"].is_array()) {
        translation_evidence::registry.source(path, source, "invalid_dante_ability_model");
        return;
    }
    for (const auto& row : document["dataList"]) {
        if (!row.is_object()) continue;
        auto id = row.find("id");
        if (id == row.end()) continue;
        std::string key;
        if (id->is_number_integer()) key = id->dump();
        else if (id->is_string()) key = id->get<std::string>();
        if (key.empty()) continue;

        DanteAbilityEntry entry;
        auto desc = row.find("desc");
        if (desc != row.end() && desc->is_string()) entry.desc = desc->get<std::string>();
        auto raw_desc = row.find("rawDesc");
        if (raw_desc != row.end() && raw_desc->is_string()) entry.raw_desc = raw_desc->get<std::string>();

        if (!entry.desc.empty() || !entry.raw_desc.empty()) {
            g_dante_abilities[key] = std::move(entry);
        }
    }
}

static void load_unlock_code_file(const std::string& path) {
    std::string source;
    if (!read_translation_source(path, source)) return;
    auto document = nlohmann::json::parse(source, nullptr, false);
    if (!document.is_object() || !document.contains("dataList") || !document["dataList"].is_array()) {
        translation_evidence::registry.source(path, source, "invalid_unlock_code_model");
        return;
    }
    for (const auto& row : document["dataList"]) {
        if (!row.is_object()) continue;
        auto id = row.find("id");
        if (id == row.end()) continue;
        std::string key;
        if (id->is_number_integer()) key = id->dump();
        else if (id->is_string()) key = id->get<std::string>();
        if (key.empty()) continue;

        UnlockCodeEntry entry;
        auto oc = row.find("openCondition");
        if (oc != row.end() && oc->is_string()) entry.open_condition = oc->get<std::string>();
        auto ocn = row.find("openConditionNumber");
        if (ocn != row.end() && ocn->is_string()) entry.open_condition_number = ocn->get<std::string>();
        auto rct = row.find("relatedChapterText");
        if (rct != row.end() && rct->is_string()) entry.related_chapter_text = rct->get<std::string>();
        auto alu = row.find("askLevelUp");
        if (alu != row.end() && alu->is_string()) entry.ask_level_up = alu->get<std::string>();

        if (!entry.open_condition.empty() || !entry.open_condition_number.empty() ||
            !entry.related_chapter_text.empty() || !entry.ask_level_up.empty()) {
            g_unlock_codes[key] = std::move(entry);
        }
    }
}

static void load_project_gs_lesson_file(const std::string& path) {
    std::string source;
    if (!read_translation_source(path, source)) return;
    auto document = nlohmann::json::parse(source, nullptr, false);
    if (!document.is_object() || !document.contains("dataList") || !document["dataList"].is_array()) {
        translation_evidence::registry.source(path, source, "invalid_project_gs_lesson_model");
        return;
    }
    for (const auto& row : document["dataList"]) {
        if (!row.is_object()) continue;
        auto id = row.find("id");
        if (id == row.end()) continue;
        std::string key;
        if (id->is_number_integer()) key = id->dump();
        else if (id->is_string()) key = id->get<std::string>();
        if (key.empty()) continue;

        ProjectGSLessonEntry entry;
        auto content = row.find("content");
        if (content != row.end() && content->is_string()) entry.content = content->get<std::string>();
        auto teacher = row.find("teacher");
        if (teacher != row.end() && teacher->is_string()) entry.teacher = teacher->get<std::string>();

        if (!entry.content.empty() || !entry.teacher.empty()) {
            g_project_gs_lessons[key] = std::move(entry);
        }
    }
}

static void load_introduce_character_file(const std::string& path) {
    std::string source;
    if (!read_translation_source(path, source)) return;
    auto document = nlohmann::json::parse(source, nullptr, false);
    if (!document.is_object() || !document.contains("dataList") || !document["dataList"].is_array()) {
        translation_evidence::registry.source(path, source, "invalid_introduce_character_model");
        return;
    }
    for (const auto& row : document["dataList"]) {
        if (!row.is_object()) continue;
        auto id = row.find("id");
        if (id == row.end() || !id->is_string()) continue;
        std::string key = id->get<std::string>();
        if (key.empty()) continue;

        IntroduceCharacterEntry entry;
        entry.id = key;
        auto name = row.find("name");
        if (name != row.end() && name->is_string()) entry.name = name->get<std::string>();
        auto sub_name = row.find("subName");
        if (sub_name != row.end() && sub_name->is_string()) entry.subName = sub_name->get<std::string>();
        auto desc = row.find("description");
        if (desc != row.end() && desc->is_string()) entry.description = desc->get<std::string>();
        auto sentence = row.find("sentence");
        if (sentence != row.end() && sentence->is_string()) entry.sentence = sentence->get<std::string>();

        if (!entry.name.empty() || !entry.description.empty() || !entry.subName.empty() || !entry.sentence.empty()) {
            g_introduce_characters[key] = std::move(entry);
        }
    }
}

static void load_railway_dungeon_station_name_file(const std::string& path) {
    std::string source;
    if (!read_translation_source(path, source)) return;
    auto document = nlohmann::json::parse(source, nullptr, false);
    if (!document.is_object() || !document.contains("dataList") || !document["dataList"].is_array()) {
        translation_evidence::registry.source(path, source, "invalid_railway_dungeon_station_name_model");
        return;
    }
    for (const auto& row : document["dataList"]) {
        if (!row.is_object()) continue;
        auto id = row.find("id");
        if (id == row.end()) continue;
        std::string key;
        if (id->is_number_integer()) key = id->dump();
        else if (id->is_string()) key = id->get<std::string>();
        if (key.empty()) continue;

        RailwayDungeonStationNameEntry entry;
        entry.id = key;
        auto name_list = row.find("nameList");
        if (name_list != row.end() && name_list->is_array()) {
            for (const auto& station : *name_list) {
                if (!station.is_object()) continue;
                auto sid = station.find("id");
                if (sid == station.end()) continue;
                std::string station_id;
                if (sid->is_number_integer()) station_id = sid->dump();
                else if (sid->is_string()) station_id = sid->get<std::string>();
                if (station_id.empty()) continue;

                RailwayStationData sdata;
                sdata.id = station_id;
                auto content = station.find("content");
                if (content != station.end() && content->is_string()) sdata.content = content->get<std::string>();
                auto short_name = station.find("shortName");
                if (short_name != station.end() && short_name->is_string()) sdata.shortName = short_name->get<std::string>();

                entry.nameList.push_back(std::move(sdata));
            }
        }
        if (!entry.nameList.empty()) {
            g_railway_dungeon_station_names[key] = std::move(entry);
        }
    }
}

static void load_railway_dungeon_file(const std::string& path) {
    std::string source;
    if (!read_translation_source(path, source)) return;
    auto document = nlohmann::json::parse(source, nullptr, false);
    if (!document.is_object() || !document.contains("dataList") || !document["dataList"].is_array()) {
        translation_evidence::registry.source(path, source, "invalid_railway_dungeon_model");
        return;
    }
    for (const auto& row : document["dataList"]) {
        if (!row.is_object()) continue;
        auto id = row.find("id");
        if (id == row.end()) continue;
        std::string key;
        if (id->is_number_integer()) key = id->dump();
        else if (id->is_string()) key = id->get<std::string>();
        if (key.empty()) continue;

        RailwayDungeonEntry entry;
        entry.id = key;
        auto name = row.find("name");
        if (name != row.end() && name->is_string()) entry.name = name->get<std::string>();
        auto long_name = row.find("longName");
        if (long_name != row.end() && long_name->is_string()) entry.longName = long_name->get<std::string>();

        if (!entry.name.empty() || !entry.longName.empty()) {
            g_railway_dungeons[key] = std::move(entry);
        }
    }
}

static void load_lobby_bgm_file(const std::string& path) {
    std::string source;
    if (!read_translation_source(path, source)) return;
    auto document = nlohmann::json::parse(source, nullptr, false);
    if (!document.is_object() || !document.contains("dataList") || !document["dataList"].is_array()) {
        translation_evidence::registry.source(path, source, "invalid_lobby_bgm_model");
        return;
    }
    for (const auto& row : document["dataList"]) {
        if (!row.is_object()) continue;
        auto id = row.find("id");
        if (id == row.end()) continue;
        std::string key;
        if (id->is_number_integer()) key = id->dump();
        else if (id->is_string()) key = id->get<std::string>();
        if (key.empty()) continue;

        LobbyBGMEntry entry;
        entry.id = key;
        auto name = row.find("name");
        if (name != row.end() && name->is_string()) entry.name = name->get<std::string>();
        auto song_writer = row.find("songWriter");
        if (song_writer != row.end() && song_writer->is_string()) entry.songWriter = song_writer->get<std::string>();

        if (!entry.name.empty() || !entry.songWriter.empty()) {
            g_lobby_bgm[key] = std::move(entry);
        }
    }
}

static void load_item_file(const std::string& path) {
    load_user_ticket_file(path, g_items);
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

    std::vector<std::string> file_names;
    struct dirent* entry;
    while ((entry = readdir(d)) != nullptr) {
        const char* name = entry->d_name;
        size_t len = strlen(name);
        if (len <= 5 || strcmp(name + len - 5, ".json") != 0) continue;
        file_names.emplace_back(name);
    }
    closedir(d);
    std::sort(file_names.begin(), file_names.end());

    for (const auto& file_name : file_names) {
        const char* name = file_name.c_str();
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
        } else if (strncmp(name, "UserTicket-EGOBg", 16) == 0) {
            load_user_ticket_file(full_path, g_user_ticket_egobg_map);
        } else if (strncmp(name, "UserTicket-L", 12) == 0) {
            load_user_ticket_file(full_path, g_user_ticket_l_map);
        } else if (strncmp(name, "UserTicket-R", 12) == 0) {
            load_user_ticket_file(full_path, g_user_ticket_r_map);
        } else if (strncmp(name, "EGOgift_", 8) == 0) {
            load_catalog(full_path, true);
        } else if (strcmp(name, "EgoGiftCategory.json") == 0) {
            load_string_table(full_path, "name", g_ego_gift_categories);
        } else if (strcmp(name, "MirrorDungeonEgoGiftLockedDesc.json") == 0) {
            std::string source;
            if (read_translation_source(full_path, source) && !catalog_content::strings(source, "content", g_ego_gift_locked))
                translation_evidence::registry.source(full_path, source, "invalid_catalog_string_model");
        } else if (strcmp(name, "MirrorDungeonTheme-1.json") == 0) {
            load_mirror_dungeon_themes(full_path);
        } else if (strcmp(name, "AnnouncerVoiceType.json") == 0) {
            load_string_table(full_path, "content", g_announcer_voice_types);
        } else if (strncmp(name, "Announcer", 9) == 0 && strcmp(name, "AnnouncerTicketUIPopup.json") != 0) {
            load_announcer_file(full_path);
        } else if (strncmp(name, "ThreadDungeon", 13) == 0) {
            load_string_table(full_path, "name", g_thread_dungeons, true);
        } else if (strncmp(name, "DungeonName", 11) == 0) {
            load_string_table(full_path, "name", g_dungeon_names, true);
        } else if (strncmp(name, "StagePartText", 13) == 0) {
            load_string_table(full_path, "parttitle", g_stage_parts, false);
        } else if (strncmp(name, "AttributeText", 13) == 0) {
            load_string_table(full_path, "name", g_attribute_list, false);
        } else if (strncmp(name, "DungeonStartBuffs", 17) == 0) {
            load_string_table(full_path, "description", g_dungeon_start_buffs, false);
        } else if (strncmp(name, "PanicInfo", 9) == 0) {
            load_panic_info_file(full_path);
        } else if (strncmp(name, "MentalCondition", 15) == 0 || strncmp(name, "reward-popup-text", 17) == 0) {
            load_mental_condition_file(full_path);
        } else if (strncmp(name, "DanteAbility", 12) == 0 && strncmp(name, "DanteAbilityUIText", 18) != 0) {
            load_dante_ability_file(full_path);
        } else if (strncmp(name, "UnlockCode", 10) == 0) {
            load_unlock_code_file(full_path);
        } else if (strncmp(name, "ProjectGSLessonName", 19) == 0) {
            load_project_gs_lesson_file(full_path);
        } else if (strncmp(name, "ProjectGSComboName", 18) == 0) {
            load_string_table(full_path, "content", g_project_gs_combos, true);
        } else if (strcmp(name, "IntroduceCharacter.json") == 0) {
            load_introduce_character_file(full_path);
        } else if (strncmp(name, "BattlePass_Mission", 18) == 0) {
            load_string_table(full_path, "content", g_battle_pass_missions, false);
        } else if (strncmp(name, "RailwayDungeonStationName", 25) == 0) {
            load_railway_dungeon_station_name_file(full_path);
        } else if (strcmp(name, "Quest.json") == 0) {
            load_string_table(full_path, "content", g_quests, true);
        } else if (strcmp(name, "RailwayDungeon.json") == 0) {
            load_railway_dungeon_file(full_path);
        } else if (strcmp(name, "RailwayDungeonBuff.json") == 0) {
            load_string_table(full_path, "content", g_railway_dungeon_buffs, false);
        } else if (strcmp(name, "BattleHint_NormalBattle.json") == 0) {
            load_string_table(full_path, "content", g_normal_battle_result_hints, true);
        } else if (strcmp(name, "BattleHint_AbnorBattle.json") == 0) {
            load_string_table(full_path, "content", g_ab_battle_result_hints, true);
        } else if (strncmp(name, "LobbyBGM", 8) == 0 && strcmp(name, "LobbyBGMCategory.json") != 0 && strcmp(name, "LobbyBGMUI.json") != 0) {
            load_lobby_bgm_file(full_path);
        } else if (strncmp(name, "BattleResultHint", 16) == 0) {
            load_string_table(full_path, "content", g_battle_result_hints, false);
        } else if (strncmp(name, "TutorialDesc", 12) == 0 || strcmp(name, "TutorialRenewalDesc.json") == 0 ||
                   strcmp(name, "TutorialRenewalMainUIDesc.json") == 0 || strcmp(name, "TutorialPopUpDesc.json") == 0 ||
                   strcmp(name, "TutorialNewUpperSkillInfoUI.json") == 0) {
            load_string_table(full_path, "content", g_tutorial_descs, false);
        } else if (strncmp(name, "Items", 5) == 0) {
            load_item_file(full_path);
        } else if (strcmp(name, "GachaTitle.json") == 0) {
            load_string_table(full_path, "content", g_gacha_titles, true);
        } else if (strcmp(name, "MirrorDungeonRentalName.json") == 0) {
            load_string_table(full_path, "content", g_mirror_dungeon_rental_names, true);
        } else if (strcmp(name, "ErrorCodeMsg.json") == 0) {
            load_string_table(full_path, "content", g_error_code_msgs, false);
        } else if (strncmp(name, "DungeonArea", 11) == 0) {
            load_string_table(full_path, "content", g_dungeon_areas, false);
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

    // Load subdirectories for voice lines
    LOGI("[LLC-RESOURCE] Scoped strings: %zu skill tags, %zu battle hints", g_skill_tags.size(), g_battle_hints.size());
    LOGI("[LLC-RESOURCE] Announcers: %zu names, %zu voice types", g_announcers.size(), g_announcer_voice_types.size());
    LOGI("[LLC-RESOURCE] UserTickets: %zu L, %zu R, %zu EGOBg", g_user_ticket_l_map.size(), g_user_ticket_r_map.size(), g_user_ticket_egobg_map.size());
    LOGI("[LLC-RESOURCE] Dungeon tables: %zu ThreadDungeon, %zu DungeonName, %zu StagePart, %zu AttributeText, %zu DungeonStartBuffs, %zu PanicInfo, %zu MentalCondition, %zu DanteAbility, %zu UnlockCode",
         g_thread_dungeons.size(), g_dungeon_names.size(), g_stage_parts.size(),
         g_attribute_list.size(), g_dungeon_start_buffs.size(), g_panic_infos.size(),
         g_mental_conditions.size(), g_dante_abilities.size(), g_unlock_codes.size());
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
    for (const auto& row : g_mirror_dungeon_themes)
        evidence.loaded_row("_mirrorDungeonThemeList", row.first, translation_evidence::source_fields(
            nlohmann::json({{"name", row.second.name}, {"specialName", row.second.specialName}}), "_mirrorDungeonThemeList"));
    for (const auto& row : g_announcers) evidence.loaded_row("_announcer", row.first, {{"name", row.second}});
    for (const auto& row : g_announcer_voice_types) evidence.loaded_row("_announcerVoiceType", row.first, {{"content", row.second}});
    for (const auto& row : g_user_ticket_l_map)
        evidence.loaded_row("_userTicket_L", row.first, translation_evidence::source_fields(
            nlohmann::json({{"name", row.second.name}, {"desc", row.second.desc}, {"flavor", row.second.flavor}}), "_userTicket_L"));
    for (const auto& row : g_user_ticket_r_map)
        evidence.loaded_row("_userTicket_R", row.first, translation_evidence::source_fields(
            nlohmann::json({{"name", row.second.name}, {"desc", row.second.desc}, {"flavor", row.second.flavor}}), "_userTicket_R"));
    for (const auto& row : g_user_ticket_egobg_map)
        evidence.loaded_row("_userTicket_EGOBg", row.first, translation_evidence::source_fields(
            nlohmann::json({{"name", row.second.name}, {"desc", row.second.desc}, {"flavor", row.second.flavor}}), "_userTicket_EGOBg"));
    for (const auto& row : g_thread_dungeons) evidence.loaded_row("_threadDungeon", row.first, {{"name", row.second}});
    for (const auto& row : g_dungeon_names) evidence.loaded_row("_dungeonName", row.first, {{"name", row.second}});
    for (const auto& row : g_stage_parts) evidence.loaded_row("_stagePart", row.first, {{"parttitle", row.second}});
    for (const auto& row : g_attribute_list) evidence.loaded_row("_attributeList", row.first, {{"name", row.second}});
    for (const auto& row : g_dungeon_start_buffs) evidence.loaded_row("_dungeonStartBuffs", row.first, {{"description", row.second}});
    for (const auto& row : g_panic_infos)
        evidence.loaded_row("_panicInfo", row.first, translation_evidence::source_fields(
            nlohmann::json({{"panicName", row.second.panic_name}, {"lowMoraleDescription", row.second.low_morale_desc}, {"panicDescription", row.second.panic_desc}, {"flavor", row.second.flavor}}), "_panicInfo"));
    for (const auto& row : g_mental_conditions)
        evidence.loaded_row("_mentalConditionList", row.first, translation_evidence::source_fields(
            nlohmann::json({{"add", row.second.add}, {"min", row.second.min}}), "_mentalConditionList"));
    for (const auto& row : g_dante_abilities)
        evidence.loaded_row("_danteAbilityDataList", row.first, translation_evidence::source_fields(
            nlohmann::json({{"desc", row.second.desc}, {"rawDesc", row.second.raw_desc}}), "_danteAbilityDataList"));
    for (const auto& row : g_unlock_codes)
        evidence.loaded_row("_unlockCodeList", row.first, translation_evidence::source_fields(
            nlohmann::json({{"openCondition", row.second.open_condition},
                            {"openConditionNumber", row.second.open_condition_number},
                            {"relatedChapterText", row.second.related_chapter_text},
                            {"askLevelUp", row.second.ask_level_up}}), "_unlockCodeList"));
    for (const auto& row : g_project_gs_lessons)
        evidence.loaded_row("_projectGSLessonName", row.first, translation_evidence::source_fields(
            nlohmann::json({{"content", row.second.content}, {"teacher", row.second.teacher}}), "_projectGSLessonName"));
    for (const auto& row : g_project_gs_combos)
        evidence.loaded_row("_projectGSComboName", row.first, {{"content", row.second}});
    for (const auto& row : g_introduce_characters)
        evidence.loaded_row("_introduceCharacter", row.first, translation_evidence::source_fields(
            nlohmann::json({{"name", row.second.name}, {"subName", row.second.subName},
                            {"description", row.second.description}, {"sentence", row.second.sentence}}), "_introduceCharacter"));
    for (const auto& row : g_battle_pass_missions)
        evidence.loaded_row("_battlePassMission", row.first, {{"content", row.second}});
    for (const auto& row : g_railway_dungeon_station_names) {
        nlohmann::json doc = {{"nameList", nlohmann::json::array()}};
        for (const auto& station : row.second.nameList) {
            nlohmann::json s = {{"id", station.id}, {"content", station.content}};
            if (!station.shortName.empty()) s["shortName"] = station.shortName;
            doc["nameList"].push_back(std::move(s));
        }
        evidence.loaded_row("_railwayDungeonStationName", row.first, translation_evidence::source_fields(doc, "_railwayDungeonStationName"));
    }
    for (const auto& row : g_quests)
        evidence.loaded_row("_quest", row.first, {{"content", row.second}});
    for (const auto& row : g_railway_dungeons)
        evidence.loaded_row("_railwayDungeonText", row.first, translation_evidence::source_fields(
            nlohmann::json({{"name", row.second.name}, {"longName", row.second.longName}}), "_railwayDungeonText"));
    for (const auto& row : g_railway_dungeon_buffs)
        evidence.loaded_row("_railwayDungeonBuffText", row.first, {{"content", row.second}});
    for (const auto& row : g_normal_battle_result_hints)
        evidence.loaded_row("_normalBattleResultHint", row.first, {{"content", row.second}});
    for (const auto& row : g_ab_battle_result_hints)
        evidence.loaded_row("_abBattleResultHint", row.first, {{"content", row.second}});
    for (const auto& row : g_lobby_bgm)
        evidence.loaded_row("<LobbyBGM>k__BackingField", row.first, translation_evidence::source_fields(
            nlohmann::json({{"name", row.second.name}, {"songWriter", row.second.songWriter}}), "<LobbyBGM>k__BackingField"));
    for (const auto& row : g_battle_result_hints)
        evidence.loaded_row("_battleResultHint", row.first, {{"content", row.second}});
    for (const auto& row : g_tutorial_descs)
        evidence.loaded_row("_tutorialDesc", row.first, {{"content", row.second}});
    for (const auto& row : g_items)
        evidence.loaded_row("_itemList", row.first, translation_evidence::source_fields(
            nlohmann::json({{"name", row.second.name}, {"desc", row.second.desc}, {"flavor", row.second.flavor}}), "_itemList"));
    for (const auto& row : g_gacha_titles)
        evidence.loaded_row("_gachaTitle", row.first, {{"content", row.second}});
    for (const auto& row : g_mirror_dungeon_rental_names)
        evidence.loaded_row("_mirrorDungeonRentalName", row.first, {{"content", row.second}});
    for (const auto& row : g_error_code_msgs)
        evidence.loaded_row("_errorCodeMsgList", row.first, {{"content", row.second}});
    for (const auto& row : g_dungeon_areas)
        evidence.loaded_row("_dungeonArea", row.first, {{"content", row.second}});
    evidence.rebuild();
    for (const auto& row : g_voice_dlgs) {
        auto fields = translation_evidence::source_fields(
            nlohmann::json({{"desc", row.second.desc}, {"dlg", row.second.dlg}}), "_announcerVoiceText");
        for (const auto* table : {"_announcerVoiceText", "_personalityVoiceText", "_egoVoiceText"})
            if (evidence.sources[table].rows.count(row.first)) evidence.loaded_row(table, row.first, fields);
    }
}

void parse_scenario_json_file(const std::string& path, std::unordered_map<int, ScenarioDialogue>& out) {
    std::string source;
    if (!read_translation_source(path, source, 5 * 1024 * 1024)) return;
    if (!parse_story_document(source, out)) LOGW("[LLC-STORY] Invalid scenario JSON: %s", path.c_str());
}
