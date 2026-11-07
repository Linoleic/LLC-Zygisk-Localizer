#pragma once

#include "../../third_party/nlohmann/json.hpp"
#include "json_models.hpp"

#include <string>
#include <unordered_map>

inline bool parse_flat_content_document(
    const std::string& source,
    std::unordered_map<std::string, BufEntry>& output) {
    if (source.find('\0') != std::string::npos) return false;
    const auto document = nlohmann::json::parse(source, nullptr, false);
    if (document.is_discarded() || !document.is_object()) return false;

    const auto data_list = document.find("dataList");
    if (data_list == document.end() || !data_list->is_array()) return false;

    std::unordered_map<std::string, BufEntry> parsed;
    for (const auto& row : *data_list) {
        if (!row.is_object()) return false;

        const auto id_value = row.find("id");
        if (id_value == row.end() || !id_value->is_string()) return false;
        const std::string id = id_value->get<std::string>();
        if (id.empty() || id.find('\0') != std::string::npos) return false;

        BufEntry entry;
        entry.id = id;
        const auto read_text = [&row](const char* key, std::string& value) {
            const auto field = row.find(key);
            if (field == row.end() || field->is_null()) {
                value.clear();
                return true;
            }
            if (!field->is_string()) return false;
            value = field->get<std::string>();
            return value.find('\0') == std::string::npos;
        };
        if (!read_text("name", entry.name) ||
            !read_text("desc", entry.desc) ||
            !read_text("summary", entry.summary) ||
            !read_text("flavor", entry.flavor)) {
            return false;
        }
        if (!parsed.emplace(id, std::move(entry)).second) return false;
    }

    for (auto& item : parsed) {
        output.insert_or_assign(item.first, std::move(item.second));
    }
    return true;
}
