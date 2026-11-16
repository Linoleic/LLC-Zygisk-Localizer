#pragma once
#include "json_models.hpp"
#include <unordered_map>

namespace story_place {
using Stories = std::unordered_map<std::string, StoryRows>;
using Labels = std::unordered_map<std::string, std::string>;

// The English export e38545d and LLC pack 2026110501 agree on these
// explicit place rows. E406B places its localized transition earlier.
inline constexpr struct { const char* story; int row; } entrance[] = {
    {"E403B", 18}, {"E404B", 2}, {"E405A", 0}, {"E407A", 6}, {"E407B", 0},
};

inline bool has_anchor(const std::string& story) {
    for (const auto& anchor : entrance)
        if (story == anchor.story) return true;
    return false;
}

inline Labels from_story_rows(const Stories& stories) {
    std::string translated;
    unsigned agreeing = 0;
    for (const auto& anchor : entrance) {
        const auto story = stories.find(anchor.story);
        if (story == stories.end()) continue;
        const auto row = story->second.by_id.find(anchor.row);
        if (row == story->second.by_id.end() || row->second.place.empty()) continue;
        const auto& place = row->second.place;
        if (place == "Molar Boatworks Entrance" || place == "-1" ||
            (!translated.empty() && translated != place)) return {};
        translated = place;
        ++agreeing;
    }
    if (agreeing < 2) return {};
    return {{"Molar Boatworks Entrance", translated}};
}

inline std::string resolve(const std::string& explicit_place, const std::string& runtime_place,
                           const Labels& labels) {
    if (!explicit_place.empty()) return explicit_place;
    const auto found = labels.find(runtime_place);
    return found == labels.end() ? std::string{} : found->second;
}
}
