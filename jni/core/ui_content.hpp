#pragma once
#include "../../third_party/nlohmann/json.hpp"
#include <string>
#include <unordered_map>

inline bool parse_string_table_document(const std::string& source, std::unordered_map<std::string, std::string>& output, const char* text_field, bool allow_numeric_id = false) {
    if (source.find('\0') != std::string::npos) return false;
    auto document = nlohmann::json::parse(source, nullptr, false);
    if (!document.is_object() || !document.contains("dataList") || !document["dataList"].is_array()) return false;
    std::unordered_map<std::string, std::string> parsed;
    for (const auto& row : document["dataList"]) {
        if (!row.is_object()) continue;
        auto id = row.find("id"), content = row.find(text_field);
        if (id == row.end() || content == row.end() || !content->is_string()) continue;
        std::string key;
        if (id->is_string()) key = id->get<std::string>();
        else if (allow_numeric_id && id->is_number_integer()) key = std::to_string(id->get<int64_t>());
        else continue;
        auto text = content->get<std::string>();
        if (key.empty() || text.empty() || key.find('\0') != std::string::npos || text.find('\0') != std::string::npos) continue;
        parsed[std::move(key)] = std::move(text);
    }
    for (auto& pair : parsed) output.insert_or_assign(pair.first, std::move(pair.second));
    return true;
}

inline bool parse_ui_document(const std::string& source, std::unordered_map<std::string, std::string>& output) {
    return parse_string_table_document(source, output, "content", false);
}
