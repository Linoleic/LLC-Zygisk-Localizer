#pragma once
#include "../third_party/nlohmann/json.hpp"
#include <string>
#include <unordered_map>

inline bool parse_ui_document(const std::string& source, std::unordered_map<std::string, std::string>& output) {
    if (source.find('\0') != std::string::npos) return false;
    auto document = nlohmann::json::parse(source, nullptr, false);
    if (!document.is_object() || !document.contains("dataList") || !document["dataList"].is_array()) return false;
    std::unordered_map<std::string, std::string> parsed;
    for (const auto& row : document["dataList"]) {
        if (!row.is_object()) continue;
        auto id = row.find("id"), content = row.find("content");
        if (id == row.end() || content == row.end() || !id->is_string() || !content->is_string()) continue;
        auto key = id->get<std::string>(), text = content->get<std::string>();
        if (key.empty() || text.empty() || key.find('\0') != std::string::npos || text.find('\0') != std::string::npos) continue;
        parsed[std::move(key)] = std::move(text);
    }
    for (auto& pair : parsed) output.insert_or_assign(pair.first, std::move(pair.second));
    return true;
}
