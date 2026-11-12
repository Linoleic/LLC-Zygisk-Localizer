#pragma once

#include "story_refresh_guard.hpp"
#include <cstdint>
#include <cstring>

struct StoryRefreshSchedule {
    StoryRefreshIdentity identity;
    unsigned stable = 0;
    unsigned attempts = 0;
    unsigned failures = 0;
    bool in_flight = false;
    bool settled = false;
    bool blocked = false;
    uint64_t next_attempt_ms = 0;
    uint64_t deadline_ms = 0;

    void reset() { *this = StoryRefreshSchedule{}; }

    void observe(const StoryRefreshIdentity& current) {
        if (!current.valid()) {
            reset();
            return;
        }
        if (!(identity == current)) {
            reset();
            identity = current;
        }
        if (stable < 2) ++stable;
    }

    bool ready(uint64_t now, bool bridge_busy) const {
        return identity.valid() && stable >= 2 && !in_flight && !settled && !blocked &&
            !bridge_busy && now >= next_attempt_ms;
    }

    void queued(uint64_t now) {
        in_flight = true;
        ++attempts;
        deadline_ms = now + 5000;
    }

    bool expired(uint64_t now) const {
        return in_flight && now > deadline_ms;
    }

    static bool transient_error(const char* error) {
        if (!error) return false;
        static const char* const transient[] = {
            "current_dialog_resource_missing",
            "dialog_data_not_localized",
            "typing_active_retry_when_idle",
            "text_cache_unavailable",
            "normal_dialog_controller_missing",
            "text_target_changed",
            "text_cache_changed_since_request",
            "story_changed_since_request",
            "dialog_text_changed_since_request",
            "refresh_ticket_expired",
            "automatic_refresh_timeout",
            "black_screen_mode_unsupported",
            "subtitle_mode_unsupported",
            "ruby_mode_unsupported"
        };
        for (const char* candidate : transient)
            if (std::strcmp(error, candidate) == 0) return true;
        return false;
    }

    void record(const char* error, uint64_t now) {
        in_flight = false;
        deadline_ms = 0;
        if (!error) {
            settled = true;
            return;
        }
        if (!transient_error(error)) {
            blocked = true;
            return;
        }

        // Typing is normal and short-lived; check promptly when it ends.
        if (std::strcmp(error, "typing_active_retry_when_idle") == 0) {
            next_attempt_ms = now + 250;
            return;
        }
        ++failures;
        uint64_t delay = 250;
        for (unsigned i = 1; i < failures && delay < 4000; ++i) delay *= 2;
        if (delay > 4000) delay = 4000;
        next_attempt_ms = now + delay;
    }
};
