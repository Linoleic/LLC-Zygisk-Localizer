#pragma once
#include <string>
#include <cstdint>
struct StoryRefreshIdentity {
    uintptr_t manager = 0, scenario = 0, dialog = 0;
    std::string story_id, dialog_id;
    bool valid() const { return manager && scenario && dialog && !story_id.empty() && !dialog_id.empty(); }
    bool operator==(const StoryRefreshIdentity& other) const {
        return valid() && other.valid() && manager == other.manager && scenario == other.scenario &&
            dialog == other.dialog && story_id == other.story_id && dialog_id == other.dialog_id;
    }
};

// These conditions only authorize the normal-dialog cache path. An existing
// subtitle reference is conservative evidence, not proof it is currently visible.
inline const char* story_refresh_mode_error(bool controller, bool schema_valid,
                                            bool black, bool subtitle, bool ruby) {
    if (!controller) return "normal_dialog_controller_missing";
    if (!schema_valid) return "dialog_mode_schema_mismatch";
    if (black) return "black_screen_mode_unsupported";
    if (subtitle) return "subtitle_mode_unsupported";
    if (ruby) return "ruby_mode_unsupported";
    return nullptr;
}
