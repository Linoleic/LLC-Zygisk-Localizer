#pragma once
#include "json_models.hpp"
#include "scoped_content.hpp"
#include <string>
#include <unordered_map>

// Global storage containers
extern std::unordered_map<std::string, std::string> g_ui_map;
extern std::unordered_map<int, PersonalityEntry> g_personalities;
extern std::unordered_map<int, PassiveEntry> g_passives;
extern std::unordered_map<std::string, BufEntry> g_bufs;
extern std::unordered_map<std::string, BufEntry> g_battle_keywords;
extern std::unordered_map<int, EgoEntry> g_egos;
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
