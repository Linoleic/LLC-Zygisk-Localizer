#pragma once
#include "structured_content_patcher.hpp"
#include "story_observation.hpp"
#include "event_dialogue.hpp"

// Explicit, read-only diagnostic. No text is allocated/written and no resource
// registry is touched. Call on UnityMain, outside any translation write scope.
namespace content_readback {
using Json = nlohmann::json;
inline bool candidate_table(const std::string& table) {
    return table == "_gachaNotice" || table == "<LobbyBGMCategory>k__BackingField";
}
inline bool supported(const std::string& table) {
    if (table.rfind("audit:", 0) == 0) return candidate_table(table.substr(6));
    return candidate_table(table) || table == "_story" || table == "_illustGetConditionText" ||
        table == "_bufList" || table == "_keywordList" || table == "_egoGiftData" || table == "_danteNoteDesc" ||
        table == "_abnormalityEventCharDlg" || table == "_choiceEventResultDesc";
}
struct Check {
    size_t rows = 0, missing = 0, compared = 0, equal = 0, mismatches = 0, unsupported = 0, structures = 0;
    size_t empty = 0, mapping_gaps = 0, uncertain = 0, missing_subrows = 0, retained = 0;
    void source(const std::string& table, const std::string& id) {
        using translation_evidence::RowState;
        switch (translation_evidence::registry.classify_snapshot(table, id)) {
            case RowState::MissingLocal: ++missing; break;
            case RowState::EmptySource: ++empty; break;
            case RowState::MappingGap: ++mapping_gaps; break;
            case RowState::SourceUncertain: ++uncertain; break;
            case RowState::Available: break;
        }
    }
    Json examples = Json::array();
    void issue(const std::string& id, const char* field, const char* reason) {
        if (examples.size() < 12) examples.push_back({{"id", id}, {"field", field}, {"reason", reason}});
    }
    void field(Il2CppObject* row, const std::string& id, const char* name, const std::string& expected) {
        if (expected.empty()) return; // Empty translations never clear runtime text.
        auto* info = managed_field_named(row, name);
        if (!info) { ++unsupported; issue(id, name, "runtime_field_absent"); return; }
        auto* type = il2cpp_class_from_type_fn(il2cpp_field_get_type_fn(info));
        if (!type || strcmp(il2cpp_class_get_name_fn(type), "String")) {
            ++mismatches; issue(id, name, "runtime_field_not_string"); return;
        }
        ++compared;
        Il2CppString* value = nullptr; il2cpp_field_get_value_fn(row, info, &value);
        int32_t length = value ? il2cpp_string_length_fn(value) : 0;
        if (!value || length < 0 || length > 65536) { ++mismatches; issue(id, name, "runtime_text_unavailable"); return; }
        if (observed_utf8(il2cpp_string_chars_fn(value), static_cast<size_t>(length)) == expected) ++equal;
        else { ++mismatches; issue(id, name, "text_differs"); }
    }
    static Json ability_counts(const std::map<int, size_t>& counts) {
        Json result = Json::array();
        for (const auto& [id, count] : counts) {
            if (result.size() == 16) break;
            result.push_back({{"ability_id", id}, {"count", count}});
        }
        return result;
    }
    static Json text_sample(Il2CppObject* row, const char* name, size_t limit = 256) {
        auto* info = managed_field_named(row, name);
        if (!info) return nullptr;
        auto* type = il2cpp_class_from_type_fn(il2cpp_field_get_type_fn(info));
        if (!type || strcmp(il2cpp_class_get_name_fn(type), "String")) return nullptr;
        Il2CppString* value = nullptr; il2cpp_field_get_value_fn(row, info, &value);
        if (!value) return nullptr;
        int32_t length = il2cpp_string_length_fn(value);
        if (length < 0 || length > 65536) return nullptr;
        auto* chars = il2cpp_string_chars_fn(value); if (!chars && length) return nullptr;
        size_t count = std::min<size_t>(length, std::min<size_t>(limit, 1024));
        if (count && count < static_cast<size_t>(length) && chars[count - 1] >= 0xd800 && chars[count - 1] <= 0xdbff) --count;
        return {{"text", observed_utf8(chars, count)}, {"truncated", count < static_cast<size_t>(length)}};
    }
    static Json source_sample(const std::string& text) {
        size_t count = std::min<size_t>(text.size(), 1024);
        if (count < text.size()) while (count && (static_cast<unsigned char>(text[count]) & 0xc0) == 0x80) --count;
        return {{"text", text.substr(0, count)}, {"truncated", count < text.size()}};
    }
    void gift(Il2CppObject* row, const std::string& id, const EgoGiftEntry& source) {
        field(row, id, "name", source.name);
        field(row, id, "abnormalityName", source.abnormalityName);
        auto* list = read_named_managed_list(row, "simpleDesc");
        if (!list && source.simpleDesc.empty()) { field(row, id, "desc", source.desc); return; }
        structured_content::List abilities(list);
        if (!abilities.valid) { ++structures; if (!source.desc.empty()) ++retained; issue(id, "simpleDesc", "list_unavailable"); return; }
        std::map<int, size_t> counts, occurrences, source_counts;
        bool valid_ids = true;
        for (const auto& ability : source.simpleDesc) if (ability.abilityID > 0) ++source_counts[ability.abilityID];
        for (int i = 0; i < abilities.size; ++i) {
            int32_t ability = 0;
            if (!structured_content::expected(abilities.at(i), "TextData_EgoGiftAbility") ||
                !string_table::integer(abilities.at(i), "abilityID", ability)) { ++structures; valid_ids = false; issue(id, "simpleDesc", "ability_identity_invalid"); continue; }
            if (ability > 0) ++counts[ability];
        }
        if (valid_ids && counts == source_counts) field(row, id, "desc", source.desc);
        else if (!source.desc.empty()) ++retained;
        for (int i = 0; i < abilities.size; ++i) {
            int32_t ability = 0;
            if (!structured_content::expected(abilities.at(i), "TextData_EgoGiftAbility") ||
                !string_table::integer(abilities.at(i), "abilityID", ability) || ability <= 0) continue;
            const auto occurrence = occurrences[ability]++;
            auto* source_row = content_alignment::ability(source, ability, occurrence, counts[ability]);
            if (!source_row) {
                const auto found = source_counts.find(ability);
                const size_t source_count = found == source_counts.end() ? 0 : found->second;
                if (source_count == 0) ++missing_subrows; else ++structures;
                if (examples.size() < 12) examples.push_back({{"id", id}, {"field", "simpleDesc"},
                    {"reason", source_count == 0 ? "ability_missing_local" : "ability_count_mismatch"},
                    {"ability_id", ability}, {"occurrence", occurrence},
                    {"runtime_count", counts[ability]}, {"source_count", source_count},
                    {"runtime_abilities", ability_counts(counts)}, {"source_abilities", ability_counts(source_counts)},
                    {"ability_lists_truncated", counts.size() > 16 || source_counts.size() > 16},
                    {"observed_text", text_sample(abilities.at(i), "simpleDesc")},
                    {"observed_parent_desc", text_sample(row, "desc", 1024)}, {"source_parent_desc", source_sample(source.desc)}});
                continue;
            }
            field(abilities.at(i), id, "simpleDesc", source_row->simpleDesc);
        }
        for (const auto& item : source_counts) if (!counts.count(item.first)) {
            ++structures;
            if (examples.size() < 12) examples.push_back({{"id", id}, {"field", "simpleDesc"}, {"reason", "ability_missing_runtime"},
                {"ability_id", item.first}, {"runtime_count", 0}, {"source_count", item.second},
                {"runtime_abilities", ability_counts(counts)}, {"source_abilities", ability_counts(source_counts)},
                {"ability_lists_truncated", counts.size() > 16 || source_counts.size() > 16},
                {"observed_parent_desc", text_sample(row, "desc", 1024)}, {"source_parent_desc", source_sample(source.desc)}});
        }
    }
};
// Explicitly separated from translation acceptance. A complete audit may find
// no translation, including an empty table awaiting future game content.
inline Json inspect_candidate(Il2CppObject* dataset, const std::string& table) {
    Json report = {{"table", table}, {"status", "failed"}, {"read_only", true}, {"candidate_audit", true},
        {"whole_game_coverage_known", false}, {"samples", Json::array()}};
    auto* container = string_table::reference(dataset, table.c_str());
    string_table::Root root(container);
    auto* dictionary = string_table::reference(root.get(), "_dic");
    int32_t count = -1;
    if (!dictionary || strcmp(il2cpp_class_get_name_fn(il2cpp_object_get_class_fn(dictionary)), "Dictionary`2") ||
        !string_table::integer(dictionary, "_count", count) || count < 0 || count > 15000) {
        report["error"] = "candidate_table_unavailable_or_too_large"; return report;
    }
    auto before = string_table::stamp(root.get());
    const auto& mapping = table == "_gachaNotice" ? g_gacha_notices : g_lobby_bgm_categories;
    report["source_map_scope"] = "dedicated_table_map";
    size_t rows = 0, nonempty = 0, mapped = 0, equal = 0, unreadable = 0;
    std::set<std::string> ids;
    string_table::VisitStats stats;
    if (count) string_table::visit(root.get(), "TextData_UI", [&](Il2CppObject* row, const std::string& id) {
        ++rows;
        ids.insert(id);
        auto observed = Check::text_sample(row, "content", 1024);
        if (observed.is_null()) ++unreadable;
        else if (!observed.at("text").get<std::string>().empty()) ++nonempty;
        auto found = mapping.find(id);
        const bool has_translation = found != mapping.end() && !found->second.empty();
        if (has_translation) ++mapped;
        // Compare the full string, not the possibly truncated diagnostic sample.
        if (has_translation) {
            Check check; check.field(row, id, "content", found->second); equal += check.equal;
        }
        if (report["samples"].size() < 12) report["samples"].push_back({{"id", id}, {"observed", observed},
            {"mapped_hint", has_translation ? Check::source_sample(found->second) : Json(nullptr)}});
        return 0;
    }, false, true, &stats);
    else stats.complete = true;
    int32_t after_count = -1;
    const auto identity_bytes = Json(ids).dump();
    BuildSha256 identity_hash;
    identity_hash.update(reinterpret_cast<const uint8_t*>(identity_bytes.data()), identity_bytes.size());
    const bool unchanged = before == string_table::stamp(root.get()) &&
        dictionary == string_table::reference(root.get(), "_dic") && string_table::integer(dictionary, "_count", after_count) && count == after_count;
    report.update({{"runtime_slots", count}, {"observed_rows", rows}, {"observed_id_set_sha256", identity_hash.hex_digest()}, {"nonempty_runtime_rows", nonempty},
        {"mapped_hint_rows", mapped}, {"equal_hint_rows", equal}, {"unreadable_rows", unreadable},
        {"invalid_runtime_rows", stats.invalid}, {"unidentified_runtime_rows", stats.unidentified},
        {"traversal_complete", stats.complete}, {"snapshot_unchanged", unchanged}});
    if (stats.complete && unchanged && !stats.invalid && !unreadable) report["status"] = "passed";
    return report;
}
inline Json inspect(Il2CppObject* dataset, const std::string& table) {
    if (table.rfind("audit:", 0) == 0 && candidate_table(table.substr(6))) return inspect_candidate(dataset, table.substr(6));
    Check check; string_table::VisitStats stats;
    Json report = {{"table", table}, {"status", "failed"}, {"read_only", true}, {"whole_game_coverage_known", false}};
    if (!supported(table)) { report["error"] = "unsupported_table"; return report; }
    auto* container = string_table::reference(dataset, table.c_str());
    const bool event_dialogues = table == "_abnormalityEventCharDlg", event_results = table == "_choiceEventResultDesc";
    auto before = event_dialogues ? event_dialogue::inspect(container) : string_table::stamp(container);
    if (!before.ready() || before.count > 15000) { report["error"] = "table_unavailable_or_too_large"; return report; }
    string_table::Root root(container);
    const bool notes = table == "_danteNoteDesc", gifts = table == "_egoGiftData", keywords = table == "_keywordList";
    const bool story = table == "_story", illust = table == "_illustGetConditionText", optional = candidate_table(table);
    const char* klass = (event_results || story || illust || optional) ? "TextData_UI" : notes ? "TextData_DanteNote" : gifts ? "TextData_EgoGift" : keywords ? "TextData_BattleKeywords" : "TextData_Buf";
    if (event_dialogues) {
        auto observed = event_dialogue::inspect(root.get(), [&](Il2CppObject* row) {
            ++check.rows;
            auto id = read_named_managed_id(row, "id");
            if (id.empty()) { ++stats.unidentified; return false; }
            int32_t personality = 0;
            if (!string_table::integer(row, "personalityid", personality) || personality <= 0) {
                ++check.structures; return false;
            }
            id = std::to_string(personality) + "/" + id;
            check.source(table, id);
            auto found = g_event_dialogues.find(id);
            if (found == g_event_dialogues.end()) return true;
            if (personality != found->second.personality_id) {
                ++check.structures; check.issue(id, "personalityid", "identity_mismatch"); return true;
            }
            check.field(row, id, "dialog", found->second.dialog);
            return true;
        });
        stats.complete = observed.ready();
    } else string_table::visit(root.get(), klass, [&](Il2CppObject* row, const std::string& id) {
        ++check.rows;
        check.source(table, id);
        if (event_results || story || illust || optional) {
            const auto& map = story ? g_story_texts : illust ? g_illust_get_conditions :
                table == "_gachaNotice" ? g_gacha_notices : optional ? g_lobby_bgm_categories : g_choice_event_results;
            auto it = map.find(id); if (it == map.end()) { return 0; }
            check.field(row, id, "content", it->second);
        } else if (notes) {
            auto it = g_dante_notes.find(id); if (it == g_dante_notes.end()) { return 0; }
            check.field(row, id, "title", it->second.title); check.field(row, id, "desc", it->second.desc);
        } else if (gifts) {
            auto it = g_ego_gifts.find(id); if (it == g_ego_gifts.end()) { return 0; }
            check.gift(row, id, it->second);
        } else {
            const auto& map = keywords ? g_battle_keywords : g_bufs;
            auto it = map.find(id); if (it == map.end()) { return 0; }
            check.field(row, id, "name", it->second.name); check.field(row, id, "desc", it->second.desc);
            check.field(row, id, "summary", it->second.summary); check.field(row, id, "flavor", it->second.flavor);
        }
        return 1;
    }, false, !gifts, &stats);
    const bool unchanged = before == (event_dialogues ? event_dialogue::inspect(root.get()) : string_table::stamp(root.get()));
    report.update({{"runtime_slots", before.count}, {"observed_rows", check.rows}, {"missing_local_rows", check.missing},
        {"missing_local_subrows", check.missing_subrows},
        {"retained_original_fields", check.retained},
        {"empty_source_rows", check.empty}, {"source_map_mismatches", check.mapping_gaps}, {"source_uncertain_rows", check.uncertain},
        {"compared_fields", check.compared}, {"equal_fields", check.equal}, {"mismatched_fields", check.mismatches},
        {"unsupported_source_fields", check.unsupported}, {"structure_mismatches", check.structures},
        {"invalid_runtime_rows", stats.invalid}, {"unidentified_runtime_rows", stats.unidentified},
        {"traversal_complete", stats.complete}, {"snapshot_unchanged", unchanged}, {"examples", check.examples}});
    if (stats.complete && unchanged && check.compared && !check.mismatches && !stats.invalid && !check.structures) report["status"] = "passed";
    return report;
}
} // namespace content_readback
