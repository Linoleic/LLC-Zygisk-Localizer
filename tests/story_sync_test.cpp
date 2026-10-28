#include "story_sync.hpp"
#include "scoped_content.hpp"
#include <cassert>
#include <cstdio>

int main() {
    StorySyncState state;
    ContentStamp stamp{1, 2, 3, 92, 1};
    assert(!state.observe(9, "S001B", stamp, 0));
    assert(state.observe(9, "S001B", stamp, 1));
    state.record({92, 0, 0, 0}, 1); // File unavailable; same instance must retry.
    assert(!state.table.applied);
    assert(!state.observe(9, "S001B", stamp, 2));
    assert(state.observe(9, "S001B", stamp, 5));
    state.record({92, 91, 0, 100}, 5); // Partial coverage is not completion.
    assert(!state.table.applied);
    assert(state.observe(9, "S001B", stamp, 13));
    state.record({92, 92, 1, 100}, 13); // A failed field is retryable too.
    assert(!state.table.applied);
    assert(state.observe(9, "S001B", stamp, 29));
    state.record({92, 92, 0, 0}, 29);
    assert(!state.table.applied);
    assert(state.observe(9, "S001B", stamp, 61));
    state.record({92, 92, 0, 150}, 61);
    assert(state.table.applied);
    assert(!state.observe(9, "S001B", stamp, 62));
    ++stamp.version;
    assert(!state.observe(9, "S001B", stamp, 63));
    assert(state.observe(9, "S001B", stamp, 64));
    state.record({92, 92, 0, 150}, 64);
    assert(!state.observe(9, "S002B", stamp, 65)); // ID changes on same object.
    assert(state.observe(9, "S002B", stamp, 66));
    assert(!state.observe(10, "S002B", stamp, 67)); // Manager replacement.
    assert(state.observe(10, "S002B", stamp, 68));
    assert(!state.observe(0, "", {}, 69));
    assert(state.manager == 0 && state.id.empty());
    assert(!state.observe(10, "S002B", stamp, 70));
    assert(state.observe(10, "S002B", stamp, 71));

    std::unordered_map<int, ScenarioDialogue> lines;
    assert(parse_story_document(R"({"dataList":[{"content":"\u4e2d\u6587\n\ud83d\ude00","id":0,"teller":"speaker"},{"id":"7","place":"here"},{"id":"7x","content":"invalid id"}]})", lines));
    assert(lines.size() == 2 && lines.at(0).content == "\xe4\xb8\xad\xe6\x96\x87\n\xf0\x9f\x98\x80");
    assert(lines.at(0).teller == "speaker" && lines.at(7).place == "here");
    assert(!parse_story_document("{bad", lines));
    assert(lines.size() == 2);
    std::puts("story_sync: missing resource, partial/zero/failed writes, same-instance retry, lifecycle and story JSON passed");
}
