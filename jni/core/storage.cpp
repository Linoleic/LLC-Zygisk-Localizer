#include "storage.hpp"
#include <cstring>

std::unordered_map<std::string, std::string> g_ui_map;
std::unordered_map<std::string, std::string> g_skill_tags;
std::unordered_map<std::string, std::string> g_battle_hints;
std::unordered_map<int, PersonalityEntry> g_personalities;
std::unordered_map<int, PassiveEntry> g_passives;
std::unordered_map<std::string, BufEntry> g_bufs;
std::unordered_map<std::string, BufEntry> g_battle_keywords;
std::unordered_map<int, EgoEntry> g_egos;
std::unordered_map<std::string, EgoGiftEntry> g_ego_gifts;
std::unordered_map<std::string, DanteNoteEntry> g_dante_notes;
std::unordered_map<std::string, std::string> g_dante_note_categories;
std::unordered_map<std::string, std::string> g_ego_gift_locked;
std::unordered_map<std::string, std::string> g_ego_gift_categories;
std::unordered_map<std::string, MirrorDungeonThemeEntry> g_mirror_dungeon_themes;
std::unordered_map<std::string, std::string> g_announcers;
std::unordered_map<std::string, std::string> g_announcer_voice_types;
std::unordered_map<std::string, BannerEntry> g_user_ticket_l_map;
std::unordered_map<std::string, BannerEntry> g_user_ticket_r_map;
std::unordered_map<std::string, BannerEntry> g_user_ticket_egobg_map;
std::unordered_map<std::string, std::string> g_thread_dungeons;
std::unordered_map<std::string, std::string> g_dungeon_names;
std::unordered_map<std::string, std::string> g_stage_parts;
std::unordered_map<std::string, std::string> g_attribute_list;
std::unordered_map<std::string, std::string> g_dungeon_start_buffs;
std::unordered_map<std::string, PanicInfoEntry> g_panic_infos;
std::unordered_map<std::string, MentalConditionEntry> g_mental_conditions;
std::unordered_map<std::string, DanteAbilityEntry> g_dante_abilities;
std::unordered_map<std::string, UnlockCodeEntry> g_unlock_codes;
std::unordered_map<std::string, ProjectGSLessonEntry> g_project_gs_lessons;
std::unordered_map<std::string, std::string> g_project_gs_combos;
std::unordered_map<std::string, RailwayDungeonStationNameEntry> g_railway_dungeon_station_names;
std::unordered_map<std::string, IntroduceCharacterEntry> g_introduce_characters;
std::unordered_map<std::string, std::string> g_battle_pass_missions;
std::unordered_map<std::string, std::string> g_quests;
std::unordered_map<std::string, RailwayDungeonEntry> g_railway_dungeons;
std::unordered_map<std::string, std::string> g_railway_dungeon_buffs;
std::unordered_map<std::string, std::string> g_normal_battle_result_hints;
std::unordered_map<std::string, std::string> g_ab_battle_result_hints;
std::unordered_map<std::string, LobbyBGMEntry> g_lobby_bgm;
std::unordered_map<std::string, std::string> g_battle_result_hints;
std::unordered_map<std::string, std::string> g_tutorial_descs;
std::unordered_map<std::string, ItemEntry> g_items;
std::unordered_map<std::string, std::string> g_gacha_titles;
std::unordered_map<std::string, std::string> g_mirror_dungeon_rental_names;
std::unordered_map<std::string, std::string> g_error_code_msgs;
std::unordered_map<std::string, std::string> g_dungeon_areas;
std::unordered_map<std::string, std::string> g_iap_stickers;
std::unordered_map<std::string, KeywordDictionaryEntry> g_keyword_dictionary;
std::unordered_map<std::string, ScenarioModelCodeEntry> g_scenario_model_codes;
std::unordered_map<std::string, BattleSpeechBubbleEntry> g_battle_speech_bubbles;
std::unordered_map<std::string, IAPProductEntry> g_iap_products;
std::unordered_map<std::string, DungeonNodeEntry> g_dungeon_node_texts;
std::unordered_map<std::string, FileDownloadDescEntry> g_file_download_descs;
std::unordered_map<std::string, BuffAbilityEntry> g_buff_abilities;
std::unordered_map<std::string, std::string> g_gacha_notices;
std::unordered_map<std::string, std::string> g_lobby_bgm_categories;
std::unordered_map<std::string, EnemyEntry> g_enemies;
std::unordered_map<std::string, AbnormalityEventEntry> g_abnormality_events;
std::unordered_map<std::string, ActionEventEntry> g_action_events;
std::unordered_map<std::string, AbnormalityGuideEntry> g_abnormality_guides;
std::unordered_map<int, CharacterEntry> g_characters;
std::unordered_map<int, SkillEntry> g_skills;
std::unordered_map<std::string, VoiceDlgEntry> g_voice_dlgs;
std::unordered_map<std::string, TheaterEntry> g_theater_map;
std::unordered_map<int, BannerEntry> g_banner_map;
std::unordered_map<std::string, StageChapterEntry> g_stage_chapter_map;
std::unordered_map<int, StageNodeEntry> g_stage_node_map;
LyricsMap g_bgm_lyrics;
DungeonMap g_dungeon_nodes;

const char* find_json_object_end(const char* start) {
    const char* p = start;
    int depth = 0;
    bool in_string = false;
    while (*p) {
        if (*p == '\\' && in_string) {
            p += 2;
            continue;
        }
        if (*p == '\"') {
            in_string = !in_string;
        } else if (!in_string) {
            if (*p == '{') {
                depth++;
            } else if (*p == '}') {
                depth--;
                if (depth == 0) return p;
            }
        }
        p++;
    }
    return nullptr;
}

bool extract_json_field(const char* start, const char* end, const char* key, std::string& out) {
    const char* p = strstr(start, key);
    if (!p || p >= end) return false;
    p += strlen(key);
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == ':')) p++;
    if (p >= end) return false;

    if (*p == '\"') {
        p++;
        out.clear();
        while (p < end && *p != '\"') {
            if (*p == '\\') {
                char next_c = *(p + 1);
                if (next_c == '\"') { out.push_back('\"'); p += 2; }
                else if (next_c == 'n') { out.push_back('\n'); p += 2; }
                else if (next_c == 'r') { out.push_back('\r'); p += 2; }
                else if (next_c == 't') { out.push_back('\t'); p += 2; }
                else if (next_c == '\\') { out.push_back('\\'); p += 2; }
                else { p++; out.push_back(*p++); }
            } else {
                out.push_back(*p++);
            }
        }
        return true;
    } else {
        out.clear();
        while (p < end && *p != ',' && *p != '}' && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') {
            out.push_back(*p++);
        }
        return !out.empty();
    }
}

bool extract_json_id(const char* start, const char* end, std::string& out_id_str, int& out_id_int) {
    const char* p = strstr(start, "\"id\"");
    if (!p || p >= end) return false;
    p += 4;
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == ':')) p++;
    if (p >= end) return false;
    out_id_str.clear();
    out_id_int = 0;
    if (*p == '\"') {
        p++;
        while (p < end && *p != '\"') out_id_str.push_back(*p++);
        out_id_int = atoi(out_id_str.c_str());
        return !out_id_str.empty();
    } else {
        while (p < end && ((*p >= '0' && *p <= '9') || *p == '-')) out_id_str.push_back(*p++);
        out_id_int = atoi(out_id_str.c_str());
        return !out_id_str.empty();
    }
}

bool extract_json_int_named(const char* start, const char* end, const char* key, int& out) {
    const char* p = strstr(start, key);
    if (!p || p >= end) return false;
    p += strlen(key);
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == ':')) p++;
    if (p >= end) return false;
    out = atoi(p);
    return true;
}
