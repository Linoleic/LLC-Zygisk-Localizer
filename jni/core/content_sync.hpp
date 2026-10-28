#pragma once
#include <cstdint>

// A table is independent of every other table. A populated table is not proof
// that another asynchronous loader has finished.
struct ContentStamp {
    uintptr_t owner = 0;
    uintptr_t dictionary = 0;
    uintptr_t entries = 0;
    int count = 0;
    int version = 0;

    bool ready() const { return owner && dictionary && entries && count > 0; }
    bool operator==(const ContentStamp& other) const {
        return owner == other.owner && dictionary == other.dictionary &&
               entries == other.entries && count == other.count && version == other.version;
    }
};

struct ContentSyncState {
    ContentStamp observed;
    unsigned stable_observations = 0;
    unsigned attempts = 0;
    uint64_t next_attempt = 0;
    int matched = 0;
    uint64_t strings_written = 0;
    bool applied = false;

    bool observe(const ContentStamp& stamp, uint64_t tick) {
        if (!(stamp == observed)) {
            *this = ContentSyncState{};
            observed = stamp;
        }
        if (!stamp.ready()) return false;
        if (stable_observations < 2) ++stable_observations;
        return stable_observations >= 2 && !applied && tick >= next_attempt;
    }

    void record(int matching_entries, uint64_t written, uint64_t tick) {
        ++attempts;
        matched = matching_entries;
        strings_written = written;
        // No matching rows or no valid fields must remain retryable.
        applied = matched > 0 && strings_written > 0;
        unsigned shift = attempts > 6 ? 6 : attempts - 1;
        uint64_t delay = 4ULL << shift;
        next_attempt = tick + (delay > 240 ? 240 : delay);
    }
};
