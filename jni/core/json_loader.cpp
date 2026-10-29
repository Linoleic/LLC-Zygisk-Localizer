#include "json_loader.hpp"
#include "storage.hpp"
#include "il2cpp_api.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include "translation_file.hpp"
#include "ui_content.hpp"

static bool read_translation_source(const std::string& path, std::string& source, size_t limit = 15 * 1024 * 1024) {
    auto error = translation_file::read(path, source, limit);
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

static void parse_buf_json(const std::string& filepath) {
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
        if (extract_json_id(cur, obj_end, id_str, id_int) && !id_str.empty()) {
            BufEntry entry;
            entry.id = id_str;
            extract_json_field(cur, obj_end, "\"name\"", entry.name);
            extract_json_field(cur, obj_end, "\"desc\"", entry.desc);
            extract_json_field(cur, obj_end, "\"summary\"", entry.summary);
            extract_json_field(cur, obj_end, "\"flavor\"", entry.flavor);
            g_bufs[id_str] = entry;
        }
        cur = obj_end + 1;
    }
}

static void parse_keyword_json(const std::string& filepath) {
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
        if (extract_json_id(cur, obj_end, id_str, id_int) && !id_str.empty()) {
            BufEntry entry;
            entry.id = id_str;
            extract_json_field(cur, obj_end, "\"name\"", entry.name);
            extract_json_field(cur, obj_end, "\"desc\"", entry.desc);
            extract_json_field(cur, obj_end, "\"summary\"", entry.summary);
            extract_json_field(cur, obj_end, "\"flavor\"", entry.flavor);
            g_battle_keywords[id_str] = entry;
        }
        cur = obj_end + 1;
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
            SkillEntry entry;
            entry.id = id_int;

            const char* ll_ptr = strstr(cur, "\"levelList\"");
            if (ll_ptr && ll_ptr < obj_end) {
                const char* ll_bracket = strchr(ll_ptr, '[');
                if (ll_bracket && ll_bracket < obj_end) {
                    const char* l_cur = ll_bracket + 1;
                    while (l_cur < obj_end) {
                        while (l_cur < obj_end && *l_cur != '{' && *l_cur != ']') l_cur++;
                        if (l_cur >= obj_end || *l_cur == ']') break;

                        const char* lvl_start = l_cur;
                        const char* lvl_end = find_json_object_end(lvl_start);
                        if (!lvl_end || lvl_end > obj_end) break;

                        SkillLevel lvl;
                        extract_json_int_named(lvl_start, lvl_end, "\"level\"", lvl.level);

                        const char* cl_ptr = strstr(lvl_start, "\"coinlist\"");
                        const char* lvl_header_end = (cl_ptr && cl_ptr < lvl_end) ? cl_ptr : lvl_end;

                        extract_json_field(lvl_start, lvl_header_end, "\"name\"", lvl.name);
                        extract_json_field(lvl_start, lvl_header_end, "\"desc\"", lvl.desc);
                        extract_json_field(lvl_start, lvl_header_end, "\"flavor\"", lvl.flavor);
                        extract_json_field(lvl_start, lvl_header_end, "\"rawDesc\"", lvl.rawDesc);
                        extract_json_field(lvl_start, lvl_header_end, "\"abName\"", lvl.abName);

                        if (cl_ptr && cl_ptr < lvl_end) {
                            const char* cl_bracket = strchr(cl_ptr, '[');
                            if (cl_bracket && cl_bracket < lvl_end) {
                                const char* c_cur = cl_bracket + 1;
                                while (c_cur < lvl_end) {
                                    while (c_cur < lvl_end && *c_cur != '{' && *c_cur != ']') c_cur++;
                                    if (c_cur >= lvl_end || *c_cur == ']') break;

                                    const char* c_start = c_cur;
                                    const char* c_end = find_json_object_end(c_start);
                                    if (!c_end || c_end > lvl_end) break;

                                    SkillCoin coin;
                                    const char* cd_ptr = strstr(c_start, "\"coindescs\"");
                                    if (cd_ptr && cd_ptr < c_end) {
                                        const char* cd_bracket = strchr(cd_ptr, '[');
                                        if (cd_bracket && cd_bracket < c_end) {
                                            const char* cd_cur = cd_bracket + 1;
                                            while (cd_cur < c_end) {
                                                while (cd_cur < c_end && *cd_cur != '{' && *cd_cur != ']') cd_cur++;
                                                if (cd_cur >= c_end || *cd_cur == ']') break;

                                                const char* cd_start = cd_cur;
                                                const char* cd_end = find_json_object_end(cd_start);
                                                if (!cd_end || cd_end > c_end) break;

                                                SkillCoinDesc cd;
                                                extract_json_field(cd_start, cd_end, "\"desc\"", cd.desc);
                                                extract_json_field(cd_start, cd_end, "\"summary\"", cd.summary);
                                                coin.coindescs.push_back(cd);

                                                cd_cur = cd_end + 1;
                                            }
                                        }
                                    }
                                    lvl.coinlist.push_back(coin);
                                    c_cur = c_end + 1;
                                }
                            }
                        }
                        entry.levelList.push_back(lvl);
                        l_cur = lvl_end + 1;
                    }
                }
            }
            g_skills[id_int] = entry;
        }
        cur = obj_end + 1;
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
        if (strncmp(name, "Personalities", 13) == 0) {
            parse_personality_json(full_path);
        } else if (strncmp(name, "Passive", 7) == 0) {
            parse_passive_json(full_path);
        } else if (strncmp(name, "Bufs", 4) == 0) {
            parse_buf_json(full_path);
        } else if (strncmp(name, "BattleKeywords", 14) == 0) {
            parse_keyword_json(full_path);
        } else if (strncmp(name, "Egos", 4) == 0) {
            parse_ego_json(full_path);
        } else if (strncmp(name, "Skills", 6) == 0) {
            parse_skill_json(full_path);
        } else if (strcmp(name, "Characters.json") == 0) {
            parse_character_json(full_path);
        } else if (strcmp(name, "UserBanner.json") == 0) {
            parse_banner_json_file(full_path);
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
}

void parse_scenario_json_file(const std::string& path, std::unordered_map<int, ScenarioDialogue>& out) {
    std::string source;
    if (!read_translation_source(path, source, 5 * 1024 * 1024)) return;
    if (!parse_story_document(source, out)) LOGW("[LLC-STORY] Invalid scenario JSON: %s", path.c_str());
}
