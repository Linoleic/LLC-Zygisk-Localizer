#pragma once
#include "content_sync.hpp"
#include <string>

struct StoryPatchResult {
    int rows = 0;
    int matched = 0;
    int failed_writes = 0;
    uint64_t strings_written = 0;
    bool complete() const {
        return rows > 0 && matched == rows && strings_written > 0 && failed_writes == 0;
    }
};

struct StorySyncState {
    uintptr_t manager = 0;
    std::string id;
    ContentSyncState table;

    void reset() { *this = StorySyncState{}; }
    bool observe(uintptr_t current_manager, const std::string& current_id,
                 const ContentStamp& stamp, uint64_t tick) {
        if (!current_manager || !stamp.owner) { reset(); return false; }
        if (manager != current_manager || id != current_id) {
            reset(); manager = current_manager; id = current_id;
        }
        if (id.empty()) return false;
        return table.observe(stamp, tick);
    }
    void record(const StoryPatchResult& result, uint64_t tick) {
        table.record(result.matched, result.strings_written, tick);
        table.applied = result.complete();
    }
};
