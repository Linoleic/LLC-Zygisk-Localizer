#pragma once
#include "json_models.hpp"
#include "../third_party/nlohmann/json.hpp"
#include <charconv>
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

inline bool parse_story_document(const std::string& source, std::unordered_map<int, ScenarioDialogue>& out) {
    auto root = nlohmann::json::parse(source, nullptr, false);
    if (!root.is_object() || !root.contains("dataList") || !root["dataList"].is_array()) return false;
    std::unordered_map<int, ScenarioDialogue> parsed;
    for (const auto& row : root["dataList"]) {
        if (!row.is_object()) continue;
        std::string key = content_id(row);
        int id = 0;
        auto result = std::from_chars(key.data(), key.data() + key.size(), id);
        if (key.empty() || result.ec != std::errc{} || result.ptr != key.data() + key.size()) continue;
        ScenarioDialogue entry;
        entry.id = id;
        entry.content = content_text(row, "content");
        entry.place = content_text(row, "place");
        entry.teller = content_text(row, "teller");
        entry.title = content_text(row, "title");
        parsed[id] = std::move(entry);
    }
    out = std::move(parsed);
    return true;
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
