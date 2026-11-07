#pragma once
#include "json_models.hpp"
#include "../../third_party/nlohmann/json.hpp"
#include <unordered_map>
#include <limits>

inline bool parse_event_dialogue_document(const std::string& source, std::unordered_map<std::string, EventDialogueEntry>& output) {
    if (source.find('\0') != std::string::npos) return false;
    auto doc = nlohmann::json::parse(source, nullptr, false);
    if (!doc.is_object() || !doc.contains("dataList") || !doc["dataList"].is_array()) return false;
    std::unordered_map<std::string, EventDialogueEntry> parsed;
    for (const auto& row : doc["dataList"]) {
        if (!row.is_object()) return false;
        for (const char* field : {"id", "personalityid"}) {
            if (!row.contains(field) || !row[field].is_number_integer() ||
                row[field].get<int64_t>() <= 0 || row[field].get<int64_t>() > std::numeric_limits<int>::max()) return false;
        }
        EventDialogueEntry entry;
        entry.personality_id = row["personalityid"].get<int>();
        auto dialog = row.find("dialog");
        if (dialog != row.end() && !dialog->is_null()) {
            if (!dialog->is_string()) return false;
            entry.dialog = dialog->get<std::string>();
            if (entry.dialog.find('\0') != std::string::npos) return false;
        }
        auto identity = std::to_string(entry.personality_id) + "/" + std::to_string(row["id"].get<int>());
        if (!parsed.emplace(identity, std::move(entry)).second) return false;
    }
    for (auto& row : parsed) output.insert_or_assign(row.first, std::move(row.second));
    return true;
}
