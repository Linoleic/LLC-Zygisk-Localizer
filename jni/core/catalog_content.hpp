#pragma once
#include "json_models.hpp"
#include "../../third_party/nlohmann/json.hpp"
#include <charconv>
#include <limits>

namespace catalog_content {
using Json = nlohmann::json;
inline bool text(const Json& row, const char* key, std::string& value) {
    auto item = row.find(key);
    if (item == row.end() || item->is_null()) return true;
    if (!item->is_string()) return false;
    value = item->get<std::string>(); return value.find('\0') == value.npos;
}
inline bool integer(const Json& value, int& result) {
    if (value.is_number_integer()) {
        if (value.is_number_unsigned() && value.get<uint64_t>() > INT32_MAX) return false;
        auto number = value.get<int64_t>(); if (number < 0 || number > INT32_MAX) return false;
        result = static_cast<int>(number); return true;
    }
    if (!value.is_string()) return false;
    auto raw = value.get<std::string>();
    auto parsed = std::from_chars(raw.data(), raw.data() + raw.size(), result);
    return !raw.empty() && parsed.ec == std::errc{} && parsed.ptr == raw.data() + raw.size() && result >= 0;
}
inline std::string id(const Json& row) {
    auto item = row.find("id"); if (item == row.end()) return {};
    if (item->is_string()) { auto raw = item->get<std::string>(); return raw.find('\0') == raw.npos ? raw : ""; }
    int number = 0; return integer(*item, number) ? std::to_string(number) : "";
}
inline bool gifts(const std::string& source, std::unordered_map<std::string, EgoGiftEntry>& output) {
    if (source.find('\0') != source.npos) return false;
    auto doc = Json::parse(source, nullptr, false);
    if (!doc.is_object() || !doc.contains("dataList") || !doc["dataList"].is_array()) return false;
    std::unordered_map<std::string, EgoGiftEntry> parsed;
    for (const auto& row : doc["dataList"]) {
        if (!row.is_object()) return false;
        auto key = id(row); if (key.empty() || parsed.count(key)) return false;
        EgoGiftEntry gift;
        if (!text(row, "name", gift.name) || !text(row, "desc", gift.desc) || !text(row, "abnormalityName", gift.abnormalityName)) return false;
        auto list = row.find("simpleDesc");
        if (list != row.end() && !list->is_null()) {
            if (!list->is_array()) return false;
            for (const auto& item : *list) {
                int ability = 0; std::string value;
                if (!item.is_object() || !text(item, "simpleDesc", value)) return false;
                auto identity = item.find("abilityID");
                if (identity != item.end() && !identity->is_null() && !integer(*identity, ability)) return false;
                if (!ability && !value.empty()) return false;
                // Real packs contain repeated ability IDs and empty placeholders.
                // Preserve each occurrence; runtime pairing checks group cardinality.
                gift.simpleDesc.push_back({ability, std::move(value)});
            }
        }
        parsed.emplace(std::move(key), std::move(gift));
    }
    for (auto& entry : parsed) output.insert_or_assign(entry.first, std::move(entry.second));
    return true;
}
inline bool notes(const std::string& source, std::unordered_map<std::string, DanteNoteEntry>& output) {
    if (source.find('\0') != source.npos) return false;
    auto doc = Json::parse(source, nullptr, false);
    if (!doc.is_object() || !doc.contains("dataList") || !doc["dataList"].is_array()) return false;
    std::unordered_map<std::string, DanteNoteEntry> parsed;
    for (const auto& row : doc["dataList"]) {
        if (!row.is_object()) return false;
        auto key = id(row); if (key.empty() || parsed.count(key)) return false;
        DanteNoteEntry note;
        if (!text(row, "title", note.title) || !text(row, "desc", note.desc)) return false;
        parsed.emplace(std::move(key), std::move(note));
    }
    for (auto& entry : parsed) output.insert_or_assign(entry.first, std::move(entry.second));
    return true;
}
inline bool strings(const std::string& source, const char* field, std::unordered_map<std::string, std::string>& output) {
    if (source.find('\0') != source.npos) return false;
    auto doc = Json::parse(source, nullptr, false);
    if (!doc.is_object() || !doc.contains("dataList") || !doc["dataList"].is_array()) return false;
    std::unordered_map<std::string, std::string> parsed;
    for (const auto& row : doc["dataList"]) {
        if (!row.is_object()) return false;
        auto key = id(row); std::string value;
        if (key.empty() || parsed.count(key) || !text(row, field, value)) return false;
        parsed.emplace(std::move(key), std::move(value));
    }
    for (auto& entry : parsed) if (!entry.second.empty()) output.insert_or_assign(entry.first, std::move(entry.second));
    return true;
}
} // namespace catalog_content
