#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <tuple>
#include "../../third_party/nlohmann/json.hpp"

// Callers on UnityMain compare in memory. The engine worker owns session
// commands and exports the detached snapshot.
class StoryAudit {
public:
    enum class Phase { playback, screen, history };
    enum class Outcome { unavailable, not_applicable, no_expected, equal, different };
    using Sample = std::array<Outcome, 4>;

    bool enabled() const { return enabled_.load(std::memory_order_relaxed); }

    void start() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (enabled()) return;
        rows_.clear();
        first_.reset();
        dropped_ = 0;
        enabled_.store(true, std::memory_order_relaxed);
    }

    // History UI does not expose a dialog ID. Only a single observed first
    // dialog and a single visible history entry can be paired by this probe.
    void remember_first(const std::string& story, const std::string& line, uintptr_t dialog,
                        const std::string& content, const std::string& speaker) {
        if (content.empty() || content.size() > 8192 || speaker.size() > 1024) return;
        std::lock_guard<std::mutex> lock(mutex_);
        if (enabled() && rows_.size() == 1 && !first_) {
            const Key key{story, line, dialog};
            if (rows_.begin()->first == key) first_ = First{key, content, speaker};
        }
    }

    bool record_history(const std::string& story, const std::string& line, uintptr_t dialog,
                        const std::string& content, const std::string* speaker) {
        std::lock_guard<std::mutex> lock(mutex_);
        const Key key{story, line, dialog};
        if (!enabled() || rows_.size() != 1 || !first_ || first_->key != key) return false;
        auto& history = rows_.begin()->second[static_cast<size_t>(Phase::history)];
        history[0] |= bit(compare(first_->content, &content));
        history[1] |= bit(compare(first_->speaker, speaker));
        return true;
    }

    void record(const std::string& story, const std::string& line, uintptr_t dialog,
                Phase phase, const Sample& sample) {
        if (!dialog || story.empty() || story.size() > 80 || line.empty() || line.size() > 24) return;
        std::lock_guard<std::mutex> lock(mutex_);
        if (!enabled()) return;
        const auto key = std::make_tuple(story, line, dialog);
        auto it = rows_.find(key);
        if (it == rows_.end()) {
            if (rows_.size() >= 512) { ++dropped_; return; }
            it = rows_.try_emplace(key).first;
        }
        auto& observations = it->second[static_cast<size_t>(phase)];
        for (size_t i = 0; i < sample.size(); ++i)
            observations[i] |= 1u << static_cast<unsigned>(sample[i]);
    }

    nlohmann::json stop() {
        decltype(rows_) rows;
        size_t dropped;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            enabled_.store(false, std::memory_order_relaxed);
            rows.swap(rows_);
            first_.reset();
            dropped = dropped_;
            dropped_ = 0;
        }
        const char* fields[] = {"content", "speaker", "title", "place"};
        nlohmann::json report = {{"schema", 1}, {"kind", "story_runtime_audit"},
            {"capacity_dropped_samples", dropped},
            {"rows", nlohmann::json::array()}, {"summary", nlohmann::json::object()}};
        std::map<std::string, size_t> playback_counts, screen_counts, history_counts;
        size_t changed = 0;
        for (const auto& [key, row] : rows) {
            nlohmann::json entry = {{"story_id", std::get<0>(key)}, {"dialog_id", std::get<1>(key)}};
            for (size_t i = 0; i < row[0].size(); ++i) {
                const auto play = row[0][i], screen = row[1][i], history = row[2][i];
                auto& field = entry["fields"][fields[i]];
                field = {{"playback", label(play)}, {"screen", label(screen)}, {"history", label(history)}};
                ++playback_counts[label(play)];
                ++screen_counts[label(screen)];
                ++history_counts[label(history)];
                if (play == bit(Outcome::different) && screen == bit(Outcome::equal)) {
                    field["changed_after_playback"] = true;
                    ++changed;
                }
            }
            report["rows"].push_back(std::move(entry));
        }
        report["summary"] = {{"playback", playback_counts}, {"screen", screen_counts}, {"history", history_counts},
                             {"changed_after_playback", changed}};
        return report;
    }

    static Outcome compare(const std::string& expected, const std::string* observed) {
        if (!observed) return Outcome::unavailable;
        if (expected.empty()) return observed->empty() ? Outcome::not_applicable : Outcome::no_expected;
        return *observed == expected ? Outcome::equal : Outcome::different;
    }

private:
    static unsigned bit(Outcome value) { return 1u << static_cast<unsigned>(value); }
    static const char* label(unsigned bits) {
        if (!bits) return "not_observed";
        if (bits == bit(Outcome::no_expected)) return "no_expected_translation";
        if (bits == bit(Outcome::not_applicable)) return "not_applicable";
        if (bits == bit(Outcome::unavailable)) return "unavailable";
        if (bits == bit(Outcome::equal)) return "equal";
        if (bits == bit(Outcome::different)) return "different";
        return "mixed";
    }
    using Key = std::tuple<std::string, std::string, uintptr_t>;
    struct First { Key key; std::string content, speaker; };
    std::optional<First> first_;
    std::map<Key, std::array<std::array<unsigned, 4>, 3>> rows_;
    std::atomic<bool> enabled_{false};
    std::mutex mutex_;
    size_t dropped_ = 0;
};
