#pragma once

// Initializes the Story system watcher
void init_story_hook();

// Checks if the Story system is initialized
bool is_story_hook_installed();

// Inspects Story and Scenario system classes, fields, and methods
void inspect_story_system(const char* output_file);

// Checks if a story scenario is currently being read and dynamically patches it
void check_and_patch_active_story();

// Read managed fields only; does not call TMP/Unity methods or refresh UI.
void observe_story_state(const char* output_file, const char* request_id);
