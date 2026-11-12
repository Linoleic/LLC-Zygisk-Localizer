#pragma once
#include "json_models.hpp"
#include <string>
#include <unordered_map>

void load_all_ui_jsons(const char* dir_path);
void load_all_content_jsons(const char* dir_path);
void parse_scenario_json_file(const std::string& path, std::unordered_map<int, ScenarioDialogue>& out);

// Engine worker only; callbacks receive copies of the resolved fields.
struct StoryTranslationResource {
    bool found = false;
    StoryRows rows;
};
const StoryTranslationResource& story_translation(const std::string& id);
void clear_story_translation_cache();
