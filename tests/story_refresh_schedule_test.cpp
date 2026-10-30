#include "../jni/core/story_refresh_schedule.hpp"
#include <cassert>

static StoryRefreshIdentity identity(uintptr_t manager = 1, uintptr_t scenario = 2,
                                     uintptr_t dialog = 3, const char* story = "S001B",
                                     const char* line = "17") {
    return {manager, scenario, dialog, story, line};
}

int main() {
    StoryRefreshSchedule schedule;
    const auto first = identity();

    schedule.observe(first);
    assert(schedule.stable == 1 && !schedule.ready(0, false));
    schedule.observe(first);
    assert(schedule.stable == 2 && schedule.ready(0, false));
    assert(!schedule.ready(0, true));

    schedule.queued(100);
    assert(schedule.in_flight && schedule.attempts == 1);
    assert(!schedule.ready(5100, false));
    assert(!schedule.expired(5100));
    assert(schedule.expired(5101));
    schedule.record("typing_active_retry_when_idle", 200);
    assert(!schedule.in_flight && schedule.failures == 1);
    assert(schedule.next_attempt_ms == 450);
    assert(!schedule.ready(449, false) && schedule.ready(450, false));

    // Exponential retry delay is capped at 4 seconds.
    const uint64_t delays[] = {500, 1000, 2000, 4000, 4000};
    for (uint64_t delay : delays) {
        schedule.queued(1000);
        schedule.record("automatic_refresh_timeout", 1000);
        assert(schedule.next_attempt_ms == 1000 + delay);
    }

    schedule.queued(6000);
    schedule.record(nullptr, 6000);
    assert(schedule.settled && !schedule.ready(100000, false));

    StoryRefreshSchedule permanent;
    permanent.observe(first);
    permanent.observe(first);
    permanent.queued(10);
    permanent.record("bridge_method_schema_mismatch", 11);
    assert(permanent.blocked && !permanent.ready(100000, false));

    StoryRefreshSchedule changed;
    changed.observe(first);
    changed.observe(first);
    changed.queued(20);
    changed.record(nullptr, 21);
    changed.observe(identity(1, 2, 3, "S001B", "18"));
    assert(changed.stable == 1 && changed.attempts == 0 && !changed.settled && !changed.blocked);
    changed.observe(identity(2, 2, 3, "S001B", "18"));
    assert(changed.stable == 1 && changed.attempts == 0);

    StoryRefreshSchedule invalid;
    invalid.observe(first);
    invalid.observe(first);
    invalid.queued(30);
    invalid.observe(StoryRefreshIdentity{});
    assert(!invalid.identity.valid() && invalid.stable == 0 && invalid.attempts == 0 && !invalid.in_flight);

    return 0;
}
