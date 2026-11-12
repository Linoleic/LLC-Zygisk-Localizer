#pragma once
#include "json_models.hpp"
#include "story_faction_labels.hpp"
#include "../../third_party/nlohmann/json.hpp"
#include <charconv>
#include <algorithm>
#include <unordered_map>

using LyricsMap = std::unordered_map<std::string, std::unordered_map<std::string, std::string>>;
using DungeonMap = std::unordered_map<std::string, std::unordered_map<std::string, StageNodeEntry>>;

inline std::string content_id(const nlohmann::json& row) {
    auto it = row.find("id");
    if (it == row.end()) return {};
    if (it->is_string()) return it->get<std::string>();
    if (it->is_number_integer()) return it->dump();
    return {};
}

inline std::string content_text(const nlohmann::json& row, const char* key) {
    auto it = row.find(key);
    return it != row.end() && it->is_string() ? it->get<std::string>() : std::string{};
}

inline std::string resolve_story_speaker(
        const ScenarioDialogue& text, const std::string& runtime_model,
        const std::unordered_map<std::string, ScenarioModelCodeEntry>& models) {
    if (!text.teller.empty()) return text.teller;
    const auto& key = text.model.empty() ? runtime_model : text.model;
    if (key.empty() || (!runtime_model.empty() && key != runtime_model)) return {};
    const auto found = models.find(key);
    return found == models.end() ? std::string{} : found->second.name;
}

struct ResolvedStoryFields {
    std::string content, place, speaker, title;
};

inline ResolvedStoryFields resolve_story_fields(
        const ScenarioDialogue& text, const std::string& runtime_model,
        const std::string& runtime_speaker, const std::string& runtime_title,
        const std::unordered_map<std::string, ScenarioModelCodeEntry>& models,
        const std::unordered_map<std::string, std::string>& labels = {}) {
    ResolvedStoryFields out{text.content, text.place, text.teller, text.title};
    if (out.speaker.empty() && !runtime_speaker.empty())
        out.speaker = resolve_story_speaker(text, runtime_model, models);
    if (out.title.empty() && !text.title_fallback_original.empty() &&
        !text.title_fallback_translation.empty() &&
        (runtime_title == text.title_fallback_original || runtime_title == text.title_fallback_translation))
        out.title = text.title_fallback_translation;
    if (out.title.empty()) out.title = story_faction::resolve(runtime_title, labels);
    return out;
}

inline bool parse_story_document(const std::string& source, StoryRows& out) {
    auto root = nlohmann::json::parse(source, nullptr, false);
    if (!root.is_object() || !root.contains("dataList") || !root["dataList"].is_array()) return false;
    StoryRows parsed;
    const auto& list = root["dataList"];
    const bool ordered = !list.empty() && std::all_of(list.begin(), list.end(),
        [](const auto& row) { return row.is_object() && !row.contains("id"); });
    for (const auto& row : list) {
        if (!row.is_object()) { ++parsed.skipped; continue; }
        std::string key = content_id(row);
        int id = 0;
        auto result = std::from_chars(key.data(), key.data() + key.size(), id);
        if (!ordered && (key.empty() || result.ec != std::errc{} || result.ptr != key.data() + key.size())) {
            ++parsed.skipped;
            continue;
        }
        ScenarioDialogue entry;
        entry.id = id;
        entry.content = content_text(row, "content");
        entry.place = content_text(row, "place");
        entry.teller = content_text(row, "teller");
        entry.model = content_text(row, "model");
        entry.title = content_text(row, "title");
        auto fallback = row.find("titleFallback");
        if (fallback != row.end() && fallback->is_object()) {
            entry.title_fallback_original = content_text(*fallback, "original");
            entry.title_fallback_translation = content_text(*fallback, "translation");
        }
        if (ordered) parsed.ordered.push_back(std::move(entry));
        else parsed.by_id[id] = std::move(entry);
    }
    out = std::move(parsed);
    return true;
}

inline bool parse_story_document(const std::string& source, std::unordered_map<int, ScenarioDialogue>& out) {
    StoryRows parsed;
    if (!parse_story_document(source, parsed)) return false;
    out = std::move(parsed.by_id);
    return true;
}

template<typename ReadId>
inline bool ordered_story_aligned(const StoryRows& rows, size_t count, ReadId read_id) {
    if (rows.ordered.empty() || rows.ordered.size() != count || count > 4096) return false;
    for (size_t i = 0; i < count; ++i) if (read_id(i) != std::to_string(i)) return false;
    return true;
}

inline const ScenarioDialogue* story_row_at(const StoryRows& rows, const std::string& id,
                                            size_t position, bool ordered_aligned) {
    if (!rows.ordered.empty())
        return ordered_aligned && position < rows.ordered.size() ? &rows.ordered[position] : nullptr;
    if (id.empty()) return nullptr;
    int number = 0;
    const auto parsed = std::from_chars(id.data(), id.data() + id.size(), number);
    if (parsed.ec != std::errc{} || parsed.ptr != id.data() + id.size()) return nullptr;
    const auto row = rows.by_id.find(number);
    return row == rows.by_id.end() ? nullptr : &row->second;
}

// Parse a complete document before changing storage; malformed input leaves it intact.
inline bool parse_scoped_content(const std::string& source, const std::string& scope,
                                 bool lyrics, LyricsMap& songs, DungeonMap& dungeons) {
    auto root = nlohmann::json::parse(source, nullptr, false);
    if (!root.is_object() || !root.contains("dataList") || !root["dataList"].is_array()) return false;
    if (lyrics) {
        std::unordered_map<std::string, std::string> rows;
        for (const auto& row : root["dataList"]) {
            if (!row.is_object()) continue;
            auto id = content_id(row);
            auto text = content_text(row, "content");
            if (!id.empty() && !text.empty()) rows[id] = text;
        }
        songs[scope] = std::move(rows);
    } else {
        for (const auto& group : root["dataList"]) {
            if (!group.is_object()) continue;
            auto parent = content_id(group);
            auto list = group.find("stageList");
            if (parent.empty() || list == group.end() || !list->is_array()) continue;
            for (const auto& row : *list) {
                if (!row.is_object()) continue;
                auto id = content_id(row);
                if (id.empty()) continue;
                StageNodeEntry entry;
                entry.title = content_text(row, "title");
                entry.desc = content_text(row, "desc");
                entry.place = content_text(row, "place");
                dungeons[parent][id] = std::move(entry);
            }
        }
    }
    return true;
}
