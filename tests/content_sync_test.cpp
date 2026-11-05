#include "content_sync.hpp"
#include <cassert>
#include <cstdio>

int main() {
    ContentSyncState personality;
    ContentSyncState skill;
    ContentStamp ready{1, 2, 3, 204, 1};
    ContentStamp late{4, 5, 0, 0, 0};
    assert(!personality.observe(ready, 0));
    assert(!skill.observe(late, 0));
    assert(personality.observe(ready, 1));
    personality.record(204, 612, 1);
    assert(personality.applied);
    assert(!skill.observe(late, 1));

    // The original failure: another table becomes ready after personality succeeded.
    late.entries = 6;
    late.count = 4476;
    late.version = 7;
    assert(!skill.observe(late, 2));
    assert(skill.observe(late, 3));
    skill.record(4474, 20000, 3);
    assert(skill.applied);
    assert(!skill.observe(late, 4));

    // Equal counts do not mean equal data: dictionary generations must be tracked.
    ++late.version;
    assert(!skill.observe(late, 5));
    assert(!skill.applied);
    assert(skill.observe(late, 6));
    skill.record(4474, 20000, 6);
    late.dictionary = 8;
    assert(!skill.observe(late, 7));
    assert(skill.observe(late, 8));

    // A matching ID with no valid text fields is not success and must retry.
    skill.record(4474, 0, 8);
    assert(!skill.applied);
    assert(!skill.observe(late, 9));
    assert(skill.observe(late, 12));
    skill.record(0, 0, 12);
    assert(!skill.observe(late, 13));
    assert(!skill.observe(late, 16));
    assert(skill.observe(late, 20));

    // Growing tables and cleared tables invalidate the previous applied snapshot.
    skill.record(4474, 20000, 20);
    ++late.count;
    assert(!skill.observe(late, 21));
    assert(!skill.applied);
    assert(skill.observe(late, 22));
    assert(!skill.observe({}, 23));
    assert(!skill.applied);
    assert(skill.attempts == 0);
    assert(!skill.observe(late, 24));
    assert(skill.observe(late, 25));
    ContentSyncState untranslated;
    assert(!untranslated.observe(late, 1));
    assert(untranslated.observe(late, 2));
    untranslated.record(0, 0, 2, true);
    assert(!untranslated.applied && untranslated.settled_untranslated);
    assert(!untranslated.observe(late, 1000));
    ++late.version;
    assert(!untranslated.observe(late, 1001));
    assert(untranslated.observe(late, 1002));
    untranslated.record(1, 1, 1002, false, true);
    assert(!untranslated.applied && !untranslated.settled_untranslated);
    assert(untranslated.observe(late, 1006));
    // An explicit validated empty dictionary is a stable observation, never an
    // applied translation. An absent or unverified zero-count table still waits.
    ContentSyncState optional;
    ContentStamp empty{100, 200, 0, 0, 1, true};
    assert(!optional.observe({100, 0, 0, 0, 1}, 0) && !optional.observed_empty);
    assert(!optional.observe({100, 200, 0, 0, 1}, 1) && !optional.observed_empty);
    assert(!optional.observe(empty, 2) && !optional.observed_empty);
    assert(!optional.observe(empty, 3) && optional.observed_empty && !optional.applied && optional.attempts==0);
    assert(!optional.observe(empty, 999) && optional.observed_empty);
    ++empty.version;
    assert(!optional.observe(empty, 1000) && !optional.observed_empty);
    assert(!optional.observe(empty, 1001) && optional.observed_empty);
    auto populated=empty; populated.count=2; populated.entries=300; populated.verified_empty=false;
    assert(!optional.observe(populated, 1002) && !optional.observed_empty);
    assert(optional.observe(populated, 1003));
    optional.record(0,0,1003,true); // New rows without local translations retain originals.
    assert(optional.settled_untranslated && !optional.applied);
    assert(!optional.observe(populated, 1004));
    ++populated.version; assert(!optional.observe(populated,1005));
    assert(optional.observe(populated,1006)); optional.record(2,2,1006);
    assert(optional.applied && !optional.observed_empty);
    assert(!optional.observe(empty,1007) && !optional.applied);
    assert(!optional.observe(empty,1008) && optional.observed_empty);
    assert(!optional.observe({},1009) && !optional.observed_empty);
    std::puts("content_sync: delayed readiness, replacement, mutation, retry and reset passed");
}
