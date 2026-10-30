#pragma once
#include "translation_evidence.hpp"
#include <cstdio>
#include <ctime>
#include <unistd.h>

namespace localization_status {
using Json = nlohmann::json;
inline std::string stage = "initializing", reason, root, mode, pack_key, request_id;
inline Json compatibility = Json::object(), version = nullptr;
inline std::string version_state = "not_read";
inline bool root_available = false, dirty = true;
inline std::map<std::string, translation_evidence::Attempt> attempts;
inline constexpr const char* report_path = "/sdcard/Android/data/com.ProjectMoon.LimbusCompany/files/llc_status.json";

inline Json coverage(const Json& tables) {
    size_t missing = 0, empty = 0, mismatch = 0, failures = 0, uncertain = 0, no_write = 0, waiting = 0, assessed = 0, invalid = 0, unsupported = 0, unidentified = 0, structures = 0;
    for (const auto& table : tables) {
        if (table["sync_state"] != "applied_to_snapshot" && table["sync_state"] != "settled_untranslated") ++waiting;
        auto evidence = table.find("evidence"); if (evidence == table.end() || !evidence->is_object()) continue;
        missing += evidence->value("missing_local_rows", 0u); mismatch += evidence->value("source_map_mismatches", 0u);
        empty += evidence->value("empty_source_rows", 0u);
        failures += evidence->value("failed_writes", 0u); uncertain += evidence->value("source_uncertain_rows", 0u);
        no_write += evidence->value("available_rows_without_write", 0u);
        invalid += evidence->value("invalid_runtime_rows", 0u);
        unidentified += evidence->value("unidentified_runtime_rows", 0u);
        auto fields = evidence->find("unsupported_source_fields");
        if (fields != evidence->end() && fields->is_object())
            for (const auto& field : fields->items()) unsupported += field.value().get<size_t>();
        assessed += evidence->value("traversal_complete", false);
        auto issues = evidence->find("structure_issues");
        if (issues != evidence->end() && issues->is_object()) for (const auto& issue : issues->items()) structures += issue.value().get<size_t>();
    }
    std::string state = stage != "running" ? stage : (mismatch || failures || invalid || structures) ? "module_issue" : unsupported ? "module_coverage_limited" :
        (missing || empty) ? "partial_translation" : (uncertain || unidentified) ? "source_evidence_incomplete" :
        (waiting || no_write) ? "waiting_or_retrying" : "observed_applied";
    return {{"state", state}, {"missing_local_rows", missing}, {"empty_source_rows", empty}, {"source_map_mismatches", mismatch}, {"failed_writes", failures},
        {"source_uncertain_rows", uncertain}, {"available_rows_without_write", no_write}, {"waiting_or_retrying_tables", waiting},
        {"invalid_runtime_rows", invalid},
        {"structure_mismatches", structures},
        {"unidentified_runtime_rows", unidentified},
        {"unsupported_source_fields", unsupported},
        {"id_assessed_tables", assessed}, {"total_sync_tables", tables.size()}, {"whole_game_coverage_known", false},
        {"absence_scope", "validated_local_files_not_upstream_release_completeness"}, {"tables", tables}};
}
inline Json build(int pid, const std::string& ticks, const Json& tables) {
    auto files = translation_evidence::registry.file_report();
    std::string resource_state = stage == "resource_unavailable" ? "selection_failed" : root.empty() ? "not_assessed" : !root_available ? "root_unavailable" : !files["accepted_files"].get<size_t>() ? "no_usable_files" : files["rejected_files"].get<size_t>() ? "loaded_with_file_errors" :
        files["index_model_issue_files"].get<size_t>() ? "loaded_with_unassessed_models" :
        mode == "legacy" ? "loaded_unmanaged" : mode == "previous" ? "verified_previous_at_startup" : "verified_release_at_startup";
    return {{"schema", 1}, {"kind", "localization_status"}, {"pid", pid}, {"process_start_ticks", ticks},
        {"request_id", request_id.empty() ? Json(nullptr) : Json(request_id)}, {"generated_at_unix", std::time(nullptr)},
        {"runtime", {{"stage", stage}, {"reason", reason}, {"remote_gate_scope", "most_tables_wait_for_remote_but_skill_tags_and_loading_hints_do_not"}}},
        {"compatibility", compatibility}, {"resources", {{"state", resource_state}, {"root", root}, {"mode", mode}, {"pack_key", pack_key},
            {"local_version", version}, {"version_state", version_state}, {"file_checks", files},
            {"whole_pack_manifest_checked_at_startup", mode == "active" || mode == "previous"}, {"network_used_by_core", false}}},
        {"coverage", coverage(tables)}, {"updates", {{"state", "not_checked"}, {"checker_implemented", false}, {"last_successful_check_unix", nullptr}}}};
}
inline bool publish(const Json& report) {
    auto temporary = std::string(report_path) + ".next";
    auto bytes = report.dump(2, ' ', false, Json::error_handler_t::replace);
    FILE* file = fopen(temporary.c_str(), "wb"); if (!file) return false;
    bool ok = fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size();
    if (fclose(file)) ok = false;
    if (!ok || rename(temporary.c_str(), report_path)) { unlink(temporary.c_str()); return false; }
    return true;
}
} // namespace localization_status
