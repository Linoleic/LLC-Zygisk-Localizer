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
