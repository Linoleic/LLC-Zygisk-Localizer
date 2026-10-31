#pragma once
#include "json_models.hpp"
#include "scoped_content.hpp"
#include <string>
#include <unordered_map>

// Global storage containers
extern std::unordered_map<std::string, std::string> g_ui_map;
extern std::unordered_map<std::string, std::string> g_skill_tags;
extern std::unordered_map<std::string, std::string> g_battle_hints;
extern std::unordered_map<int, PersonalityEntry> g_personalities;
extern std::unordered_map<int, PassiveEntry> g_passives;
extern std::unordered_map<std::string, BufEntry> g_bufs;
extern std::unordered_map<std::string, BufEntry> g_battle_keywords;
extern std::unordered_map<int, EgoEntry> g_egos;
extern std::unordered_map<std::string, EgoGiftEntry> g_ego_gifts;
extern std::unordered_map<std::string, DanteNoteEntry> g_dante_notes;
extern std::unordered_map<std::string, std::string> g_dante_note_categories;
extern std::unordered_map<std::string, std::string> g_ego_gift_locked;
extern std::unordered_map<std::string, std::string> g_ego_gift_categories;
extern std::unordered_map<std::string, MirrorDungeonThemeEntry> g_mirror_dungeon_themes;
extern std::unordered_map<std::string, std::string> g_announcers;
extern std::unordered_map<std::string, std::string> g_announcer_voice_types;
extern std::unordered_map<std::string, BannerEntry> g_user_ticket_l_map;
extern std::unordered_map<std::string, BannerEntry> g_user_ticket_r_map;
extern std::unordered_map<std::string, BannerEntry> g_user_ticket_egobg_map;
extern std::unordered_map<std::string, std::string> g_thread_dungeons;
extern std::unordered_map<std::string, std::string> g_dungeon_names;
extern std::unordered_map<std::string, std::string> g_stage_parts;
extern std::unordered_map<std::string, std::string> g_attribute_list;
extern std::unordered_map<std::string, std::string> g_dungeon_start_buffs;
extern std::unordered_map<std::string, PanicInfoEntry> g_panic_infos;
extern std::unordered_map<int, CharacterEntry> g_characters;
extern std::unordered_map<int, SkillEntry> g_skills;
extern std::unordered_map<std::string, VoiceDlgEntry> g_voice_dlgs;
extern std::unordered_map<std::string, TheaterEntry> g_theater_map;
extern std::unordered_map<int, BannerEntry> g_banner_map;
extern std::unordered_map<std::string, StageChapterEntry> g_stage_chapter_map;
extern std::unordered_map<int, StageNodeEntry> g_stage_node_map;
extern LyricsMap g_bgm_lyrics;
extern DungeonMap g_dungeon_nodes;

// JSON parsing helper functions
const char* find_json_object_end(const char* start);
bool extract_json_field(const char* start, const char* end, const char* key, std::string& out);
bool extract_json_id(const char* start, const char* end, std::string& out_id_str, int& out_id_int);
bool extract_json_int_named(const char* start, const char* end, const char* key, int& out);
