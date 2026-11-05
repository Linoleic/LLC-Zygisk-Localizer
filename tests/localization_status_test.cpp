#include "localization_status.hpp"

#include <cassert>
#include <string>

using localization_status::Json;

static Json table(const std::string& sync_state, const Json& evidence = Json::object()) {
    return {{"sync_state", sync_state}, {"evidence", evidence}};
}

static std::string coverage_state(const std::string& stage, const Json& tables) {
    localization_status::stage = stage;
    return localization_status::build(101, "2026", tables)["coverage"]["state"].get<std::string>();
}

int main() {
    using namespace localization_status;
    translation_evidence::registry = translation_evidence::Registry{};
    root_available = true;
    root = "/data/language";
    mode = "legacy";
    stage = "running";
    auto only_empty=coverage(Json::array({table("observed_empty", {{"traversal_complete", true}, {"successful_writes", 99}})}));
    assert(only_empty["state"]=="observed_empty_only" && only_empty["waiting_or_retrying_tables"]==0);
    assert(only_empty["observed_empty_tables"]==1 && only_empty["id_assessed_tables"]==0 && only_empty["applied_to_snapshot_tables"]==0);
    auto mixed_empty=coverage(Json::array({table("observed_empty"),table("applied_to_snapshot",{{"traversal_complete",true}})}));
    assert(mixed_empty["state"]=="observed_applied" && mixed_empty["id_assessed_tables"]==1 && mixed_empty["observed_empty_tables"]==1);
    assert(coverage(Json::array({table("checking_empty")}))["waiting_or_retrying_tables"]==1);
    auto nested = coverage(Json::array({table("applied_to_snapshot", {{"missing_local_subrows", 1}, {"retained_original_fields", 1}})}));
    assert(nested["state"] == "partial_translation" && nested["missing_local_subrows"] == 1 && nested["retained_original_fields"] == 1);
    request_id = "status-request";
    assert(build(101, "2026", Json::array())["resources"]["state"] == "no_usable_files");
    translation_evidence::registry.source("/data/UI.json", R"({"dataList":[]})");

    Json partial_evidence = {{"traversal_complete", true}, {"missing_local_rows", 2},
                             {"source_map_mismatches", 0}, {"failed_writes", 0},
                             {"source_uncertain_rows", 0}, {"available_rows_without_write", 0},
                             {"invalid_runtime_rows", 0}};
    Json partial_tables = Json::array({table("applied_to_snapshot", partial_evidence)});
    Json partial = build(101, "2026", partial_tables);
    assert(partial["coverage"]["state"] == "partial_translation");
    assert(partial["coverage"]["missing_local_rows"] == 2);
    assert(partial["coverage"]["whole_game_coverage_known"] == false);
    auto empty_evidence = partial_evidence;
    empty_evidence["missing_local_rows"] = 0;
    empty_evidence["empty_source_rows"] = 1;
    auto empty_report = build(101, "2026", Json::array({table("settled_untranslated", empty_evidence)}));
    assert(empty_report["coverage"]["state"] == "partial_translation");
    assert(empty_report["coverage"]["empty_source_rows"] == 1);
    auto limited_evidence = partial_evidence;
    limited_evidence["unsupported_source_fields"] = {{"summary", 251}};
    auto limited = build(101, "2026", Json::array({table("applied_to_snapshot", limited_evidence)}));
    assert(limited["coverage"]["state"] == "module_coverage_limited");
    assert(limited["coverage"]["unsupported_source_fields"] == 251);
    assert(limited["coverage"]["missing_local_rows"] == 2);

    Json issue_evidence = partial_evidence;
    issue_evidence["source_map_mismatches"] = 1;
    assert(coverage_state("running", Json::array({table("applied_to_snapshot", issue_evidence)})) == "module_issue");
    issue_evidence["source_map_mismatches"] = 0;
    issue_evidence["structure_issues"] = {{"skill_coin_count_mismatch", 1}};
    auto structure_report = coverage(Json::array({table("applied_to_snapshot", issue_evidence)}));
    assert(structure_report["state"] == "module_issue" && structure_report["structure_mismatches"] == 1);
    assert(structure_report["failed_writes"] == 0);
    assert(coverage_state("running", Json::array({table("queued")})) == "waiting_or_retrying");
    assert(coverage_state("disabled", Json::array()) == "disabled");

    // Assessment follows the supplied evidence; it does not invent totals from slot deltas.
    Json no_assessable_evidence = Json::array({table("queued")});
    stage = "running";
    Json waiting = build(101, "2026", no_assessable_evidence);
    assert(waiting["coverage"]["state"] == "waiting_or_retrying");
    assert(waiting["coverage"]["id_assessed_tables"] == 0);
    assert(waiting["coverage"]["whole_game_coverage_known"] == false);

    Json report = build(101, "2026", partial_tables);
    assert(report["build"] == build_info::json());
    assert(report["build"]["git_revision"].is_string());
    assert(report["updates"]["state"] == "not_checked");
    assert(report["updates"]["last_successful_check_unix"].is_null());
    assert(report["coverage"]["whole_game_coverage_known"] == false);
    assert(report["runtime"]["stage"] == "running");
    assert(report["request_id"] == "status-request");

    // Resource state differentiates unmanaged loaded files from read/parse errors.
    Json unmanaged = build(101, "2026", Json::array());
    assert(unmanaged["resources"]["state"] == "loaded_unmanaged");
    translation_evidence::registry.source("/data/Skills-broken.json", "not-json");
    Json with_errors = build(101, "2026", Json::array());
    assert(with_errors["resources"]["state"] == "loaded_with_file_errors");
    assert(with_errors["resources"]["file_checks"]["rejected_files"] == 1);

    return 0;
}
