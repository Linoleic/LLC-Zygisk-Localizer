#pragma once

#include "../third_party/nlohmann/json.hpp"
#include "json_models.hpp"

#include <charconv>
#include <cstdint>
#include <string>
#include <system_error>
#include <unordered_map>
#include <unordered_set>

namespace skill_content_detail {

inline bool read_text(const nlohmann::json& object, const char* key, std::string& output) {
    const auto it = object.find(key);
    if (it == object.end() || it->is_null()) {
        output.clear();
        return true;
    }
    if (!it->is_string()) return false;
    output = it->get<std::string>();
    return output.find('\0') == output.npos;
}

inline bool read_id(const nlohmann::json& value, int& output) {
    if (value.is_number_integer()) {
        if (value.is_number_unsigned()) {
            const auto number = value.get<std::uint64_t>();
            if (number > static_cast<std::uint64_t>(INT32_MAX)) return false;
            output = static_cast<int>(number);
            return true;
        }
        const auto number = value.get<std::int64_t>();
        if (number < INT32_MIN || number > INT32_MAX) return false;
        output = static_cast<int>(number);
        return true;
    }
    if (!value.is_string()) return false;
    const std::string raw = value.get<std::string>();
    if (raw.empty()) return false;
    int parsed = 0;
    const auto result = std::from_chars(raw.data(), raw.data() + raw.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != raw.data() + raw.size()) return false;
    output = parsed;
    return true;
}

inline bool read_level(const nlohmann::json& value, int& output) {
    if (!value.is_number_integer()) return false;
    if (value.is_number_unsigned()) {
        const auto number = value.get<std::uint64_t>();
        if (number == 0 || number > static_cast<std::uint64_t>(INT32_MAX)) return false;
        output = static_cast<int>(number);
        return true;
    }
    const auto number = value.get<std::int64_t>();
    if (number <= 0 || number > INT32_MAX) return false;
    output = static_cast<int>(number);
    return true;
}

inline bool read_coinlist(const nlohmann::json& value, std::vector<SkillCoin>& output) {
    if (value.is_null()) return true;
    if (!value.is_array()) return false;
    output.reserve(value.size());
    for (const auto& item : value) {
        if (!item.is_object()) return false;
        SkillCoin coin;
        const auto descriptions = item.find("coindescs");
        if (descriptions != item.end() && !descriptions->is_null()) {
            if (!descriptions->is_array()) return false;
            coin.coindescs.reserve(descriptions->size());
            for (const auto& description : *descriptions) {
                if (!description.is_object()) return false;
                SkillCoinDesc parsed;
                if (!read_text(description, "desc", parsed.desc) ||
                    !read_text(description, "summary", parsed.summary)) return false;
                coin.coindescs.push_back(std::move(parsed));
            }
        }
        output.push_back(std::move(coin));
    }
    return true;
}

}  // namespace skill_content_detail

inline bool parse_skill_document(const std::string& source,
                                 std::unordered_map<int, SkillEntry>& output) {
    if (source.find('\0') != std::string::npos) return false;
    auto document = nlohmann::json::parse(source, nullptr, false);
    if (!document.is_object()) return false;
    const auto rows = document.find("dataList");
    if (rows == document.end() || !rows->is_array()) return false;

    std::unordered_map<int, SkillEntry> parsed;
    for (const auto& row : *rows) {
        if (!row.is_object()) return false;
        const auto id_field = row.find("id");
        int id = 0;
        if (id_field == row.end() || !skill_content_detail::read_id(*id_field, id)) return false;
        if (id == 0) continue;
        if (parsed.find(id) != parsed.end()) return false;

        const auto levels = row.find("levelList");
        if (levels == row.end() || !levels->is_array()) return false;
        SkillEntry entry;
        entry.id = id;
        std::unordered_set<int> seen_levels;
        entry.levelList.reserve(levels->size());
        for (const auto& level_row : *levels) {
            if (!level_row.is_object()) return false;
            const auto level_field = level_row.find("level");
            SkillLevel level;
            if (level_field == level_row.end() || !skill_content_detail::read_level(*level_field, level.level)) return false;
            if (!seen_levels.insert(level.level).second) return false;
            if (!skill_content_detail::read_text(level_row, "name", level.name) ||
                !skill_content_detail::read_text(level_row, "desc", level.desc) ||
                !skill_content_detail::read_text(level_row, "flavor", level.flavor) ||
                !skill_content_detail::read_text(level_row, "rawDesc", level.rawDesc) ||
                !skill_content_detail::read_text(level_row, "abName", level.abName)) return false;
            const auto coins = level_row.find("coinlist");
            if (coins != level_row.end() && !skill_content_detail::read_coinlist(*coins, level.coinlist)) return false;
            entry.levelList.push_back(std::move(level));
        }
        parsed.emplace(id, std::move(entry));
    }

    for (auto& pair : parsed) output.insert_or_assign(pair.first, std::move(pair.second));
    return true;
}
