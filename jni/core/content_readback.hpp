#pragma once
#include "structured_content_patcher.hpp"
#include "story_observation.hpp"

// Explicit, read-only diagnostic. No text is allocated/written and no resource
// registry is touched. Call on UnityMain, outside any translation write scope.
namespace content_readback {
using Json = nlohmann::json;
inline bool supported(const std::string& table) {
    return table == "_bufList" || table == "_keywordList" || table == "_egoGiftData" || table == "_danteNoteDesc";
}
struct Check {
    size_t rows = 0, missing = 0, compared = 0, equal = 0, mismatches = 0, unsupported = 0, structures = 0;
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
    void gift(Il2CppObject* row, const std::string& id, const EgoGiftEntry& source) {
        field(row, id, "name", source.name); field(row, id, "desc", source.desc);
        field(row, id, "abnormalityName", source.abnormalityName);
        auto* list = read_named_managed_list(row, "simpleDesc");
        if (!list && source.simpleDesc.empty()) return;
        structured_content::List abilities(list);
        if (!abilities.valid) { ++structures; issue(id, "simpleDesc", "list_unavailable"); return; }
        std::map<int, size_t> counts, occurrences;
        for (int i = 0; i < abilities.size; ++i) {
            int32_t ability = 0;
            if (!structured_content::expected(abilities.at(i), "TextData_EgoGiftAbility") ||
                !string_table::integer(abilities.at(i), "abilityID", ability)) { ++structures; continue; }
            if (ability > 0) ++counts[ability];
        }
        for (int i = 0; i < abilities.size; ++i) {
            int32_t ability = 0;
            if (!structured_content::expected(abilities.at(i), "TextData_EgoGiftAbility") ||
                !string_table::integer(abilities.at(i), "abilityID", ability) || ability <= 0) continue;
            auto* source_row = content_alignment::ability(source, ability, occurrences[ability]++, counts[ability]);
            if (!source_row) { ++structures; issue(id, "simpleDesc", "ability_identity_mismatch"); continue; }
            field(abilities.at(i), id, "simpleDesc", source_row->simpleDesc);
        }
    }
};
inline Json inspect(Il2CppObject* dataset, const std::string& table) {
    Check check; string_table::VisitStats stats;
    Json report = {{"table", table}, {"status", "failed"}, {"read_only", true}, {"whole_game_coverage_known", false}};
    if (!supported(table)) { report["error"] = "unsupported_table"; return report; }
    auto* container = string_table::reference(dataset, table.c_str());
    auto before = string_table::stamp(container);
    if (!before.ready() || before.count > 15000) { report["error"] = "table_unavailable_or_too_large"; return report; }
    string_table::Root root(container);
    const bool notes = table == "_danteNoteDesc", gifts = table == "_egoGiftData", keywords = table == "_keywordList";
    const char* klass = notes ? "TextData_DanteNote" : gifts ? "TextData_EgoGift" : keywords ? "TextData_BattleKeywords" : "TextData_Buf";
    string_table::visit(root.get(), klass, [&](Il2CppObject* row, const std::string& id) {
        ++check.rows;
        if (notes) {
            auto it = g_dante_notes.find(id); if (it == g_dante_notes.end()) { ++check.missing; return 0; }
            check.field(row, id, "title", it->second.title); check.field(row, id, "desc", it->second.desc);
        } else if (gifts) {
            auto it = g_ego_gifts.find(id); if (it == g_ego_gifts.end()) { ++check.missing; return 0; }
            check.gift(row, id, it->second);
        } else {
            const auto& map = keywords ? g_battle_keywords : g_bufs;
            auto it = map.find(id); if (it == map.end()) { ++check.missing; return 0; }
            check.field(row, id, "name", it->second.name); check.field(row, id, "desc", it->second.desc);
            check.field(row, id, "summary", it->second.summary); check.field(row, id, "flavor", it->second.flavor);
        }
        return 1;
    }, false, !gifts, &stats);
    const bool unchanged = before == string_table::stamp(root.get());
    report.update({{"runtime_slots", before.count}, {"observed_rows", check.rows}, {"missing_local_rows", check.missing},
        {"compared_fields", check.compared}, {"equal_fields", check.equal}, {"mismatched_fields", check.mismatches},
        {"unsupported_source_fields", check.unsupported}, {"structure_mismatches", check.structures},
        {"invalid_runtime_rows", stats.invalid}, {"unidentified_runtime_rows", stats.unidentified},
        {"traversal_complete", stats.complete}, {"snapshot_unchanged", unchanged}, {"examples", check.examples}});
    if (stats.complete && unchanged && check.compared && !check.mismatches && !stats.invalid && !check.structures) report["status"] = "passed";
    return report;
}
} // namespace content_readback
