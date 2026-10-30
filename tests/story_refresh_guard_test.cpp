#include "story_refresh_guard.hpp"
#include <cassert>
#include <cstdio>
int main() {
    StoryRefreshIdentity expected{1, 2, 3, "S001B", "0"};
    assert(expected == expected);
    assert(!(StoryRefreshIdentity{} == StoryRefreshIdentity{}));
    auto changed = expected;
    changed.dialog = 4;
    assert(!(expected == changed));
    changed = expected; changed.dialog_id = "1";
    assert(!(expected == changed));
    changed = expected; changed.story_id = "S002B";
    assert(!(expected == changed));
    changed = expected; changed.manager = 8;
    assert(!(expected == changed));
    changed = expected; changed.scenario = 9;
    assert(!(expected == changed));
    assert(!story_refresh_mode_error(true, true, false, false, false));
    assert(std::string(story_refresh_mode_error(false, true, false, false, false)) == "normal_dialog_controller_missing");
    assert(std::string(story_refresh_mode_error(true, false, false, false, false)) == "dialog_mode_schema_mismatch");
    assert(std::string(story_refresh_mode_error(true, true, true, false, false)) == "black_screen_mode_unsupported");
    assert(std::string(story_refresh_mode_error(true, true, false, true, false)) == "subtitle_mode_unsupported");
    assert(std::string(story_refresh_mode_error(true, true, false, false, true)) == "ruby_mode_unsupported");
    std::puts("story_refresh_guard: stale scene, manager and dialog tickets rejected");
}
