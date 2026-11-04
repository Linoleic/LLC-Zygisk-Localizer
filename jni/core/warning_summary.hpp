#pragma once
#include <cstdint>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace warning_summary {
inline std::string sample(const std::string& value) {
    size_t end = value.size() < 160 ? value.size() : 160;
    while (end < value.size() && end && (static_cast<unsigned char>(value[end]) & 0xc0) == 0x80) --end;
    auto text = value.substr(0, end);
    for (auto& c : text) if (static_cast<unsigned char>(c) < 32 || c == 127) c = ' ';
    return text;
}
struct Group {
    uint64_t count = 0, fingerprint = 0;
    std::set<std::string> examples;
    void add(const std::string& id) {
        uint64_t hash = 1469598103934665603ULL;
        for (unsigned char c : id) hash = (hash ^ c) * 1099511628211ULL;
        ++count; fingerprint += hash; // Order-independent; count preserves repeated observations.
        examples.insert(sample(id));
        if (examples.size() > 3) examples.erase(std::prev(examples.end()));
    }
    bool operator==(const Group& other) const { return count == other.count && fingerprint == other.fingerprint; }
};
using Batch = std::map<std::string, Group>;
struct Line {
    std::string table, reason;
    Group group;
    uint64_t suppressed = 0;
    bool recovered = false;
};
class State {
    struct Bucket {
        Group current, emitted;
        uint64_t last_ms = 0, suppressed = 0;
        bool sent = false, pending = false;
    };
    std::map<std::pair<std::string, std::string>, Bucket> buckets_;
    bool overflow_reported_ = false;
public:
    static constexpr size_t capacity = 512;
    static constexpr uint64_t interval_ms = 30000;
    uint64_t dropped_groups = 0;
    size_t size() const { return buckets_.size(); }
    std::vector<Line> flush(uint64_t now) {
        std::vector<Line> lines;
        for (auto& item : buckets_) {
            auto& b = item.second;
            if (!b.pending || (b.sent && now - b.last_ms < interval_ms)) continue;
            lines.push_back({item.first.first, item.first.second, b.current, b.suppressed, false});
            b.emitted = b.current; b.sent = true; b.pending = false; b.last_ms = now; b.suppressed = 0;
        }
        return lines;
    }
    std::vector<Line> submit(const std::string& table, const Batch& batch, uint64_t now, bool complete = true) {
        std::vector<Line> lines;
        for (auto it = buckets_.begin(); it != buckets_.end();) {
            if (complete && it->first.first == table && !batch.count(it->first.second)) {
                if (it->second.sent) lines.push_back({table, it->first.second, {}, it->second.suppressed, true});
                it = buckets_.erase(it);
            } else ++it;
        }
        for (const auto& item : batch) {
            auto key = std::make_pair(table, item.first);
            auto it = buckets_.find(key);
            if (it == buckets_.end()) {
                if (buckets_.size() >= capacity) {
                    ++dropped_groups;
                    if (!overflow_reported_) {
                        Group group; group.add("diagnostic group limit reached; full audit remains available");
                        lines.push_back({"diagnostics", "group_limit", group, 0, false}); overflow_reported_ = true;
                    }
                    continue;
                }
                it = buckets_.emplace(key, Bucket{}).first;
            }
            auto& b = it->second;
            b.current = item.second;
            b.pending = !b.sent || !(b.current == b.emitted);
            if (b.sent && (!b.pending || now - b.last_ms < interval_ms)) ++b.suppressed;
        }
        auto due = flush(now); lines.insert(lines.end(), due.begin(), due.end());
        return lines;
    }
    void reset() { buckets_.clear(); dropped_groups = 0; overflow_reported_ = false; }
};
}
