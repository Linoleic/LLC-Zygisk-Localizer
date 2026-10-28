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
    std::puts("story_refresh_guard: stale scene, manager and dialog tickets rejected");
}
