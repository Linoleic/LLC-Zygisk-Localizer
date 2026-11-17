#include "../jni/core/scoped_content.hpp"
#include "../jni/core/ui_content.hpp"
#include "../jni/core/string_table_rule.hpp"
#include "../jni/core/story_place_labels.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>

namespace fs = std::filesystem;
using Json = nlohmann::json;
using Fields = std::map<std::pair<std::string, std::string>, std::string>;

constexpr StringTableRule tables[] = {
#define LLC_STRING_TABLE(file, prefix, excluded, field, numeric, storage) {file, prefix, excluded, field, numeric},
#include "../jni/core/string_tables.inc"
#undef LLC_STRING_TABLE
};

constexpr std::pair<const char*, std::string ScenarioDialogue::*> story_fields[] = {
    {"content", &ScenarioDialogue::content}, {"place", &ScenarioDialogue::place},
    {"teller", &ScenarioDialogue::teller}, {"title", &ScenarioDialogue::title},
};

struct Document {
    std::string bytes;
    Json rows;
    std::string state = "missing";
};

Document load(const fs::path& path, bool inventory = false) {
    if (!fs::exists(path)) return {};
    constexpr auto max_bytes = 16 * 1024 * 1024;
    if (!fs::is_regular_file(path) || fs::file_size(path) > max_bytes)
        return {{}, {}, "unreadable"};
    std::ifstream file(path, std::ios::binary);
    if (!file) return {{}, {}, "unreadable"};
    std::string bytes(std::istreambuf_iterator<char>{file}, {});
    auto root = Json::parse(bytes, nullptr, false);
    if (bytes.find('\0') != std::string::npos || root.is_discarded())
        return {{}, {}, "invalid"};
    if (inventory) return {{}, std::move(root), "ok"};
    if (!root.is_object() || !root.contains("dataList") || !root["dataList"].is_array())
        return {{}, {}, "unsupported_shape"};
    return {std::move(bytes), std::move(root["dataList"]), "ok"};
}

bool ordered(const Document& doc) {
    return !doc.rows.empty() && std::all_of(doc.rows.begin(), doc.rows.end(),
        [](const auto& row) { return row.is_object() && !row.contains("id"); });
}

void collect_shape(const Json& value, const std::string& path,
                   std::map<std::string, std::set<std::string>>& shape) {
    shape[path].insert(value.type_name());
    if (value.is_object())
        for (auto item = value.begin(); item != value.end(); ++item)
            collect_shape(item.value(), path + "[key:" + Json(item.key()).dump() + "]", shape);
    else if (value.is_array())
        for (const auto& child : value) collect_shape(child, path + "[]", shape);
}

Json structure(const Document& doc) {
    std::map<std::string, std::set<std::string>> shape;
    if (doc.state == "ok") collect_shape(doc.rows, "", shape);
    return shape;
}

Json duplicate_ids(const Document& doc) {
    std::map<std::string, std::vector<size_t>> rows;
    for (size_t i = 0; i < doc.rows.size(); ++i) {
        const auto& row = doc.rows[i];
        if (!row.is_object()) continue;
        const auto id = content_id(row);
        if (!id.empty()) rows[id].push_back(i + 1);
    }
    Json result = Json::object();
    for (const auto& [id, positions] : rows)
        if (positions.size() > 1) result[id] = positions;
    return result;
}

struct PlaceEvidence {
    std::map<std::string, std::map<std::string, std::set<std::string>>> translations;
    story_place::Stories stories;
    struct Gap { std::string file, original; size_t issue; };
    std::vector<Gap> gaps;
};

// Raw fields are kept apart from parser output so a rejected translation is
// distinguishable from an absent translation.
Fields raw_fields(const Document& doc, const StringTableRule* table, bool positions) {
    Fields fields;
    if (doc.state != "ok") return fields;
    for (size_t i = 0; i < doc.rows.size(); ++i) {
        const auto& row = doc.rows[i];
        if (!row.is_object()) continue;
        const auto id = positions ? std::to_string(i) : content_id(row);
        if (id.empty()) continue;
        auto add = [&](const char* key) {
            auto value = content_text(row, key);
            if (!value.empty()) fields[{id, key}] = std::move(value);
        };
        if (table) add(table->field);
        else for (const auto& [key, member] : story_fields) add(key);
    }
    return fields;
}

Json audit(const Document& original, const Document& translation,
           const Document& previous, const StringTableRule* table,
           const std::string& filename, PlaceEvidence& place_evidence) {
    const bool idless = !table && ordered(original);
    Json result = {{"source", original.state}, {"translation", translation.state},
                   {"previous_source", previous.state}, {"source_fields", 0},
                   {"issues", Json::array()}, {"matched_fields", 0}};
    if (original.state != "ok" || (translation.state != "ok" && translation.state != "missing"))
        return result;

    std::unordered_map<std::string, std::string> strings;
    StoryRows story;
    if (translation.state == "ok") {
        const bool ok = table
            ? parse_string_table_document(translation.bytes, strings, table->field, table->numeric)
            : parse_story_document(translation.bytes, story);
        if (!ok) { result["translation"] = "parser_rejected"; return result; }
    }
    const auto source_duplicates = table ? Json::object() : duplicate_ids(original);
    const auto translation_duplicates = table ? Json::object() : duplicate_ids(translation);
    auto same_ids = [&](const Document& doc) {
        if (doc.state != "ok" || doc.rows.size() != story.ordered.size()) return false;
        for (size_t i = 0; i < doc.rows.size(); ++i) {
            const auto& row = doc.rows[i];
            if (!row.is_object() || content_id(row) != std::to_string(story.ordered[i].id)) return false;
        }
        return true;
    };
    const bool renumbered = story.repeated_ids && same_ids(original);
    const bool by_position = idless || renumbered;
    const bool layout = !table && (!story.repeated_ids || renumbered) &&
        ordered_story_aligned(story, original.rows.size(), [&](size_t i) {
        const auto& row = original.rows[i];
        if (by_position) return std::to_string(i); // Assumed from the exported row order.
        return row.is_object() ? content_id(row) : std::string{};
    });
    const auto source = raw_fields(original, table, by_position);
    const auto supplied = raw_fields(translation, table, renumbered || (!table && ordered(translation)));
    const auto baseline = raw_fields(previous, table, renumbered || (!table && ordered(previous)));
    result["source_fields"] = source.size();
    if (!table) {
        result["matching"] = renumbered ? "renumbered_candidate" :
            (by_position || !story.ordered.empty() ? "ordered_candidate" : "explicit_id");
        result["translation_skipped_rows"] = story.skipped;
        if (!story.ordered.empty() && !layout) result["layout_mismatch"] = true;
        result["source_unkeyed_rows"] = 0;
        if (!by_position) for (const auto& row : original.rows)
            if (!row.is_object() || content_id(row).empty())
                result["source_unkeyed_rows"] = result["source_unkeyed_rows"].get<size_t>() + 1;
        if (by_position && story.ordered.empty() && translation.state == "ok")
            result["layout_mismatch"] = true;
    }
    const bool comparable = previous.state == "ok" &&
        (table || (renumbered ? same_ids(previous) : (ordered(previous) == by_position &&
                   (!by_position || previous.rows.size() == original.rows.size()))));
    result["previous_comparable"] = comparable;
    size_t matched = 0;
    for (const auto& [key, original_text] : source) {
        const auto& [id, field] = key;
        auto record = [&](const char* kind) {
            result["issues"].push_back({{"kind", kind}, {"id", id}, {"field", field}});
        };
        const auto before = baseline.find(key);
        if (comparable && before != baseline.end() && before->second != original_text)
            record("source_changed");
        if (result.contains("layout_mismatch")) continue;
        bool usable = false;
        const ScenarioDialogue* row = nullptr;
        if (table) usable = strings.count(id) != 0;
        else {
            const size_t position = by_position || layout ? static_cast<size_t>(std::stoul(id)) : 0;
            row = story_row_at(story, id, position, layout);
            for (const auto& [name, member] : story_fields)
                if (field == name && row) usable = !(row->*member).empty();
        }
        const bool place = !table && field == "place" && row && original_text != "-1"
            && !source_duplicates.contains(id) && !translation_duplicates.contains(id);
        if (place && !by_position && story.ordered.empty() && usable && row->place != "-1")
            place_evidence.translations[original_text][row->place].insert(filename);
        if (usable) { ++matched; continue; }
        if (place && !supplied.count(key))
            place_evidence.gaps.push_back({filename, original_text, result["issues"].size()});
        if (supplied.count(key)) record("translation_not_matched");
        else if (!table && (field == "teller" || field == "title")) record("story_fallback_unverified");
        else if (comparable && before == baseline.end()) record("new_missing_translation");
        else record("missing_translation");
    }
    if (comparable) for (const auto& [key, original_text] : baseline)
        if (!source.count(key))
            result["issues"].push_back({{"kind", "source_removed"}, {"id", key.first}, {"field", key.second}});
    result["matched_fields"] = matched;
    return result;
}

int main(int argc, char** argv) {
    if (argc < 3 || argc > 4) {
        std::cerr << "usage: localization-audit ORIGINAL_DIR TRANSLATION_DIR [PREVIOUS_ORIGINAL_DIR]\n";
        return 2;
    }
    try {
        for (int i = 1; i < argc; ++i)
            if (!fs::is_directory(argv[i])) throw std::runtime_error("Not a directory: " + std::string(argv[i]));
        Json report = {{"schema", 2}, {"files", Json::object()}};
        std::set<fs::path> files;
        for (int i = 1; i < argc; ++i) {
            const fs::path root(argv[i]);
            for (const auto& entry : fs::recursive_directory_iterator(root))
                if (entry.is_regular_file() && entry.path().extension() == ".json")
                    files.insert(entry.path().lexically_relative(root));
        }
        size_t stories = 0;
        PlaceEvidence place_evidence;
        for (const auto& relative : files) {
            const bool story = relative.parent_path() == "StoryData";
            const auto filename = relative.filename().string();
            const auto table = std::find_if(std::begin(tables), std::end(tables),
                [&](const auto& rule) { return relative.parent_path().empty() && rule.matches(filename); });
            const bool inventory = !story && table == std::end(tables);
            const auto original = load(fs::path(argv[1]) / relative, inventory);
            const auto translation = load(fs::path(argv[2]) / relative, inventory);
            const auto previous = argc == 4 ? load(fs::path(argv[3]) / relative, inventory) : Document{};
            if (story && translation.state == "ok") {
                const auto id = relative.stem().string();
                if (story_place::has_anchor(id)) {
                    StoryRows rows;
                    if (parse_story_document(translation.bytes, rows))
                        place_evidence.stories.emplace(id, std::move(rows));
                }
            }
            auto& file = report["files"][relative.generic_string()];
            if (story || table != std::end(tables)) {
                file = audit(original, translation, previous, story ? nullptr : table,
                    relative.generic_string(), place_evidence);
                file["coverage"] = story ? "story" : "string_table";
                stories += story;
            } else {
                file = {{"source", original.state}, {"translation", translation.state},
                        {"previous_source", previous.state}, {"coverage", "inventory_only"},
                        {"issues", Json::array()}};
            }
            if (original.state == "ok" && previous.state == "ok" &&
                structure(original) != structure(previous)) file["source_structure_changed"] = true;
            if (story || table != std::end(tables)) {
                const auto source_duplicates = duplicate_ids(original);
                const auto translation_duplicates = duplicate_ids(translation);
                if (!source_duplicates.empty() || !translation_duplicates.empty())
                    file["duplicate_ids"] = {{"source", source_duplicates}, {"translation", translation_duplicates}};
            }
        }
        const auto known_places = story_place::from_story_rows(place_evidence.stories);
        for (const auto& gap : place_evidence.gaps) {
            auto& issue = report["files"][gap.file]["issues"][gap.issue];
            const auto known = known_places.find(gap.original);
            if (known != known_places.end()) {
                issue["kind"] = "place_fallback_available";
                issue["expected"] = known->second;
            } else {
                const auto candidates = place_evidence.translations.find(gap.original);
                if (candidates == place_evidence.translations.end() || candidates->second.size() != 1) continue;
                const auto& [expected, files] = *candidates->second.begin();
                if (files.size() < 2 || expected == gap.original) continue;
                issue["kind"] = "place_fallback_candidate";
                issue["expected"] = expected;
                issue["evidence_files"] = files;
            }
            issue["original"] = gap.original;
        }
        report["story_files_discovered"] = stories;
        std::map<std::string, size_t> summary;
        for (const auto& file : report["files"]) {
            ++summary[file["coverage"].get<std::string>()];
            ++summary["source_" + file["source"].get<std::string>()];
            ++summary["translation_" + file["translation"].get<std::string>()];
            if (file.contains("layout_mismatch")) ++summary["layout_mismatch"];
            for (const auto& issue : file["issues"]) ++summary[issue["kind"].get<std::string>()];
        }
        report["summary"] = summary;
        std::cout << report.dump(2, ' ', true) << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
