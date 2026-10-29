#pragma once

// Included after the standalone manager primitives, never by the injected core.
namespace manager {
inline Json read_network(const Store& store) {
    auto config = store.read_document("network.json", {{"schema", 1}, {"mode", "system"}, {"port", nullptr}});
    require(config.is_object() && config.value("schema", Json()) == 1 &&
        (config.value("mode", Json()) == "system" || config.value("mode", Json()) == "loopback"), "invalid_network_config");
    if (config["mode"] == "loopback") {
        require(config.contains("port") && config["port"].is_number_integer() &&
            config["port"].get<int64_t>() > 0 && config["port"].get<int64_t>() <= 65535, "invalid_proxy_port");
    }
    return config;
}
inline bool caller_proxy() {
    for (auto key : {"HTTPS_PROXY", "https_proxy", "ALL_PROXY", "all_proxy"}) {
        auto value = getenv(key); if (value && *value) return true;
    }
    return false;
}
inline Json network_summary(const Store& store) {
    auto config = read_network(store);
    config["scope"] = "module_network_child_process_only";
    config["effective_source"] = caller_proxy() ? "caller_environment" : config["mode"] == "loopback" ? "module_loopback" : "system";
    return config;
}
inline void save_network(Store& store, const std::string& operand) {
    Json config = {{"schema", 1}, {"mode", "system"}, {"port", nullptr}};
    if (operand != "system") {
        require(!operand.empty() && operand.size() <= 5 && operand.front() != '0' &&
            operand.find_first_not_of("0123456789") == operand.npos, "invalid_proxy_port");
        auto port = std::stoul(operand); require(port > 0 && port <= 65535, "invalid_proxy_port");
        config["mode"] = "loopback"; config["port"] = port;
    }
    store.write_document("network.json", config);
}
inline void apply_network(const Store& store) {
    auto config = read_network(store);
    if (caller_proxy() || config["mode"] != "loopback") return;
    auto url = "http://127.0.0.1:" + std::to_string(config["port"].get<int>());
    require(!setenv("HTTPS_PROXY", url.c_str(), 1) && !setenv("https_proxy", url.c_str(), 1) &&
        !unsetenv("NO_PROXY") && !unsetenv("no_proxy"), "network_environment_failed");
}
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
