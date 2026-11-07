#pragma once
#include "../../third_party/nlohmann/json.hpp"
#include <map>
#include <set>
#include <string>
#include <stdexcept>

namespace text_schema_policy {
using Json = nlohmann::json;
inline bool candidate_type(const std::string& type) { return type.find("TextData") != std::string::npos; }
inline std::string folded(std::string name) {
    for (auto& c : name) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return name;
}
inline bool registered_field(const std::set<std::string>& catalog, const std::string& field) {
    auto actual = folded(field);
    for (const auto& name : catalog) {
        if (folded(name) == actual) return true;
        if (name.size() > 4 && (name.compare(name.size() - 4, 4, "List") == 0 || name.compare(name.size() - 4, 4, "Text") == 0) &&
            folded(name.substr(0, name.size() - 4)) == actual) return true;
    }
    return false;
}
inline bool candidate_field(const std::set<std::string>& catalog, const std::string& field, const std::string& type) {
    return registered_field(catalog, field) || candidate_type(type);
}
inline bool game_changed(const Json& before, const Json& after) {
    return before.is_object() && after.is_object() && !before.value("version", Json()).is_null() && !after.value("version", Json()).is_null() &&
        (before.value("version", Json()) != after.value("version", Json()) || before.value("version_code", Json()) != after.value("version_code", Json()));
}
inline Json difference(const Json& before, const Json& after) {
    Json result = {{"added_containers", Json::array()}, {"removed_containers", Json::array()},
        {"changed_containers", Json::array()}, {"added_classes", Json::array()}, {"changed_classes", Json::array()}, {"removed_classes", Json::array()}};
    for (const auto& [section, suffix] : {std::pair{"containers", "containers"}, std::pair{"classes", "classes"}}) {
        std::map<std::string, Json> old, current;
        for (const auto& row : before.at(section)) old.emplace(row.at("name").get<std::string>(), row);
        for (const auto& row : after.at(section)) current.emplace(row.at("name").get<std::string>(), row);
        for (const auto& [name, row] : current) {
            if (!old.count(name)) result[std::string("added_") + suffix].push_back(name);
            else {
                auto a = old.at(name), b = row;
                a.erase("registered"); b.erase("registered");
                if (a != b) result[std::string("changed_") + suffix].push_back({{"name", name}, {"before", a}, {"after", b}});
            }
        }
        for (const auto& [name, row] : old) if (!current.count(name)) result[std::string("removed_") + suffix].push_back(name);
    }
    return result;
}
inline Json advance(const Json& previous, const Json& snapshot, const Json& game) {
    if (snapshot.value("status", std::string{}) != "complete") throw std::runtime_error("text_schema_incomplete");
    Json baseline = nullptr;
    if (previous.is_object() && previous.value("schema", 0) == 1 && previous.contains("current")) {
        const auto& old = previous.at("current");
        if (old.value("native_sha256", std::string{}) != snapshot.value("native_sha256", std::string{}) ||
            old.value("catalog_signature", std::string{}) != snapshot.value("catalog_signature", std::string{}) ||
            game_changed(previous.value("game", Json()), game)) baseline = old;
        else if (previous.contains("previous") && previous["previous"].is_object()) baseline = previous["previous"];
        else if (old.at("containers") != snapshot.at("containers") || old.at("classes") != snapshot.at("classes")) baseline = old;
    }
    return {{"schema", 1}, {"current", snapshot}, {"game", game}, {"previous", baseline},
        {"first_baseline", baseline.is_null()}, {"delta", baseline.is_null() ? Json(nullptr) : difference(baseline, snapshot)}};
}
}
