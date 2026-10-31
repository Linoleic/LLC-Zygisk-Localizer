#pragma once
#include "il2cpp_api.hpp"
#include "story_sync.hpp"

int patch_ui_list(Il2CppObject* ui_list_obj, const char* list_name);
int patch_personality_list(Il2CppObject* pers_list_obj);
int patch_passive_list(Il2CppObject* passive_list_obj);
int patch_buf_list(Il2CppObject* buf_list_obj, const char* name_prefix);
int patch_ego_list(Il2CppObject* ego_list_obj);
int patch_character_list(Il2CppObject* char_list_obj);
int patch_skill_list(Il2CppObject* skill_list_obj);
int patch_ego_gift_list(Il2CppObject* container);
int patch_dante_note_list(Il2CppObject* container);
int patch_dante_note_category_list(Il2CppObject* container);
int patch_ego_gift_locked_list(Il2CppObject* container);
int patch_ego_gift_category_list(Il2CppObject* container);
int patch_mirror_dungeon_theme_list(Il2CppObject* container);
int patch_announcer_list(Il2CppObject* container);
int patch_announcer_voice_type_list(Il2CppObject* container);
int patch_skill_tag_list(Il2CppObject* container);
int patch_battle_hint_list(Il2CppObject* container);
int patch_voice_list(Il2CppObject* voice_container_obj, const char* name);
int patch_theater_list(Il2CppObject* theater_obj);
int patch_banner_list(Il2CppObject* banner_obj);
int patch_user_ticket_l(Il2CppObject* container);
int patch_user_ticket_r(Il2CppObject* container);
int patch_user_ticket_egobg(Il2CppObject* container);
int patch_thread_dungeon_list(Il2CppObject* container);
int patch_dungeon_name_list(Il2CppObject* container);
int patch_stage_part_list(Il2CppObject* container);
int patch_attribute_list(Il2CppObject* container);
int patch_dungeon_start_buffs(Il2CppObject* container);
int patch_panic_info_list(Il2CppObject* container);
int patch_stage_chapter_list(Il2CppObject* chapter_obj);
int patch_stage_node_list(Il2CppObject* node_obj, const char* name);
int patch_scoped_list(Il2CppObject* container, bool lyrics);
ContentStamp scenario_stamp(Il2CppObject* scn);
StoryPatchResult patch_scenario_object(Il2CppObject* scn, const char* scen_id);
