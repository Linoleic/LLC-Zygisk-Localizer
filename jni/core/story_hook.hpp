#pragma once
#include "il2cpp_api.hpp"
#include <string>

// Initializes the Story system watcher
void init_story_hook();

// Checks if the Story system is initialized
bool is_story_hook_installed();

// Inspects Story and Scenario system classes, fields, and methods
void inspect_story_system(const char* output_file);

// Checks if a story scenario is currently being read and dynamically patches it.
// Returns true if a story is actively loaded/reading; false otherwise.
bool check_and_patch_active_story();

// Safe observational readers (metadata and static fields only; never invoke methods)
Il2CppObject* observed_game();
Il2CppObject* observed_reference(Il2CppObject* owner, const char* name, const char* expected);

// Read managed fields only; does not call TMP/Unity methods or refresh UI.
void observe_story_state(const char* output_file, const char* request_id);
void request_story_refresh(const char* output_file, const char* request_id);

// Retry current normal dialog cache refresh on UnityMain after data is ready.
void check_and_refresh_active_story();

// Called on the engine worker after loading or reloading translation data.
void reload_story_playback_index(const std::string& game_digest);
void update_story_playback_subscription(bool localization_active);
