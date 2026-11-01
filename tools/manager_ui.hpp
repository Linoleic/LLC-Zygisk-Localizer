#pragma once
#include "network_manager.hpp"

// Included after the standalone manager primitives, never by the injected core.
namespace manager {
inline Json managed_summary(const fs::path& packs = resource_paths::store_root) {
    Json result = {{"state", "legacy"}, {"active", nullptr}, {"previous", nullptr}, {"previous_version", nullptr}};
    try {
        if (!fs::exists(fs::symlink_status(packs / "state.json"))) return result;
        auto state = resource_pack::read_state(packs);
        result["state"] = "managed"; result["active"] = state.active;
        if (!state.previous.empty()) {
            result["previous"] = state.previous;
            try {
                auto info = document(read_path(packs / "releases" / state.previous / "Info/version.json", 16384));
                result["previous_version"] = version(info.at("version"));
            } catch (const std::exception&) { /* The rollback command does the full verification. */ }
        }
    } catch (const std::exception&) { result["state"] = "unavailable"; }
    result["rollback_rechecks_integrity"] = true;
    return result;
}
inline Json progress(const Store& store) {
    return {{"schema", 1}, {"kind", "module_manager_progress"}, {"busy", !store.locked},
        {"updates", store.load()}, {"downloads", read_transfer(store)}, {"network", network_summary(store)}};
}
inline Json cleanup_history(const Store& store) {
    auto state = store.read_document("cleanup-state.json", nullptr);
    if (state.is_null()) return state;
    require(state.is_object() && state.value("schema", Json()) == 1 && state.value("kind", Json()) == "module_cleanup_result" &&
        state.contains("deleted") && state["deleted"].is_array() && state["deleted"].size() <= 128 &&
        (state.value("phase", Json()) == "working" || state.value("phase", Json()) == "failed" || state.value("phase", Json()) == "succeeded"), "invalid_cleanup_history");
    if (state["phase"] == "working" && store.locked) state["phase"] = "interrupted";
    return state;
}
} // namespace manager
