#pragma once
#include "../jni/core/text_schema_policy.hpp"

namespace manager {
inline Json schema_capability(const Json& run) {
    if (!run.is_object()) return nullptr;
    auto report = run.value("report", Json());
    if (!report.is_object()) return nullptr;
    auto capabilities = report.value("capabilities", Json());
    return capabilities.is_object() ? capabilities.value("text_schema_audit", Json()) : Json();
}
inline Json schema_cache(const Store& store) { return store.read_document("text-schema.json", nullptr, 2 * 1024 * 1024); }
inline bool schema_identity(const Json& doc, const Json& run, const std::string& nonce) {
    if (!doc.is_object() || !run.value("fresh_request", false) || !run.contains("report")) return false;
    const auto& current = run.at("report");
    if (!current.is_object() || !current.contains("compatibility") || !current.contains("capabilities") ||
        !current["capabilities"].contains("text_schema_audit") || !current["capabilities"]["text_schema_audit"].is_object()) return false;
    return doc.value("schema", Json()) == 1 && doc.value("kind", Json()) == "text_schema_snapshot" &&
        doc.value("read_only", Json()) == true && doc.value("request_id", Json()) == nonce &&
        doc.value("pid", Json()) == current.at("pid") && doc.value("process_start_ticks", Json()) == current.at("process_start_ticks") &&
        doc.value("native_sha256", Json()) == current.at("compatibility").at("il2cpp_sha256") &&
        doc.value("catalog_signature", Json()) == current.at("capabilities").at("text_schema_audit").at("catalog_signature");
}
inline void discover_schema(Store& store, const Json& run, const Json& game) {
    require(store.locked, "manager_busy");
    require(run.value("fresh_request", false) && run.at("report").at("runtime").value("stage", std::string{}) == "running", "text_schema_live_core_required");
    const auto& current = run.at("report");
    require(current.contains("capabilities") && current["capabilities"].contains("text_schema_audit") &&
        current["capabilities"]["text_schema_audit"].is_object() && current["capabilities"]["text_schema_audit"].value("protocol", 0) == 1, "text_schema_core_unsupported");
    auto nonce = token(); request_runtime(nonce, "llc_schema_check");
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    Json doc;
    while (std::chrono::steady_clock::now() < deadline) {
        try { doc = Json::parse(read_path("/sdcard/Android/data/com.ProjectMoon.LimbusCompany/files/llc_text_schema.json", 512 * 1024)); }
        catch (const std::exception&) { doc = nullptr; }
        if (schema_identity(doc, run, nonce)) break;
        usleep(100000);
    }
    require(schema_identity(doc, run, nonce) && runtime_identity(current), "text_schema_timeout_or_process_changed");
    require(doc.value("status", std::string{}) == "complete" && doc.at("containers").is_array() && doc.at("classes").is_array(), "text_schema_incomplete");
    auto state = text_schema_policy::advance(schema_cache(store), doc, game);
    require(state.dump(2).size() <= 2 * 1024 * 1024, "text_schema_baseline_too_large");
    store.write_document("text-schema.json", state);
}
inline bool schema_automatic_ready(const Json& run) {
    if (!run.value("fresh_request", false)) return false;
    auto report = run.value("report", Json());
    if (!report.is_object()) return false;
    auto runtime = report.value("runtime", Json()), coverage = report.value("coverage", Json());
    return runtime.is_object() && coverage.is_object() && runtime.value("stage", std::string{}) == "running" &&
        coverage.value("waiting_or_retrying_tables", -1) == 0;
}
inline bool schema_needs_refresh(const Json& cache, const Json& run, const Json& game = nullptr) {
    if (!cache.is_object() || !run.value("fresh_request", false)) return false;
    const auto& current = run.at("report");
    if (!current.contains("capabilities") || !current["capabilities"].value("text_schema_audit", Json()).is_object()) return false;
    const auto& stored = cache.at("current");
    return stored.value("native_sha256", Json()) != current.at("compatibility").value("il2cpp_sha256", Json()) ||
        stored.value("catalog_signature", Json()) != current["capabilities"]["text_schema_audit"].value("catalog_signature", Json()) ||
        text_schema_policy::game_changed(cache.value("game", Json()), game);
}
inline Json schema_summary(const Json& cache, const Json& game) {
    if (cache.is_null()) return {{"state", "not_checked"}};
    const auto& current = cache.at("current");
    Json unregistered = Json::array(); size_t count = 0;
    for (const auto& field : current.at("containers")) if (!field.value("registered", false)) {
        ++count; if (unregistered.size() < 12) unregistered.push_back(field);
    }
    Json deltas = Json::object(), totals = Json::object();
    if (cache.at("delta").is_object()) for (const auto& entry : cache["delta"].items()) {
        totals[entry.key()] = entry.value().size(); deltas[entry.key()] = Json::array();
        for (const auto& row : entry.value()) {
            if (deltas[entry.key()].size() == 12) break;
            // Full metadata is retained privately; the UI receives bounded names.
            deltas[entry.key()].push_back(row.is_string() ? row : row.at("name"));
        }
    }
    return {{"state", "available"}, {"first_baseline", cache.at("first_baseline")},
        {"checked_at_unix", current.at("generated_at_unix")}, {"game", cache.at("game")},
        {"outdated_game", game.value("state", std::string{}) == "read" && cache.at("game").value("state", std::string{}) == "read" &&
            (game.value("version", Json()) != cache.at("game").value("version", Json()) || game.value("version_code", Json()) != cache.at("game").value("version_code", Json()))}, {"scope", current.at("scope")},
        {"container_count", current.at("containers").size()}, {"text_class_count", current.at("classes").size()},
        {"candidate_container_count", count}, {"candidate_containers", unregistered},
        {"missing_registered", current.at("missing_registered")}, {"delta_counts", totals}, {"delta_examples", deltas},
        {"elapsed_ms", current.at("elapsed_ms")}};
}
}
