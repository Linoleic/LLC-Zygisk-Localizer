#pragma once
#include "../jni/third_party/nlohmann/json.hpp"
#include <string>

namespace manager {
using Json = nlohmann::json;

inline Json read_network(const Store& store) {
    auto config = store.read_document("network.json", {{"schema", 1}, {"mode", "system"}, {"port", nullptr}, {"mirror_mode", "official"}, {"custom_mirror", ""}});
    require(config.is_object() && config.value("schema", Json()) == 1 &&
        (config.value("mode", Json()) == "system" || config.value("mode", Json()) == "loopback"), "invalid_network_config");
    if (config["mode"] == "loopback") {
        require(config.contains("port") && config["port"].is_number_integer() &&
            config["port"].get<int64_t>() > 0 && config["port"].get<int64_t>() <= 65535, "invalid_proxy_port");
    }
    std::string mirror = config.value("mirror_mode", "official");
    require(mirror == "official" || mirror == "ghproxy" || mirror == "ghfast" || mirror == "custom", "invalid_mirror_mode");
    if (mirror == "custom") {
        std::string custom = config.value("custom_mirror", "");
        require(custom.rfind("https://", 0) == 0 && custom.size() <= 200 &&
            custom.find_first_of(" ;&|`$\n\r\t'\"\\") == custom.npos, "invalid_custom_mirror");
    }
    if (!config.contains("mirror_mode")) config["mirror_mode"] = "official";
    if (!config.contains("custom_mirror")) config["custom_mirror"] = "";
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

inline void save_network(Store& store, const std::string& operand, const std::string& mirror_mode = "", const std::string& custom_mirror = "") {
    Json existing = {{"schema", 1}, {"mode", "system"}, {"port", nullptr}, {"mirror_mode", "official"}, {"custom_mirror", ""}};
    try { existing = read_network(store); } catch (...) {}
    Json config = existing;
    if (operand != "system") {
        require(!operand.empty() && operand.size() <= 5 && operand.front() != '0' &&
            operand.find_first_not_of("0123456789") == operand.npos, "invalid_proxy_port");
        auto port = std::stoul(operand); require(port > 0 && port <= 65535, "invalid_proxy_port");
        config["mode"] = "loopback"; config["port"] = port;
    } else {
        config["mode"] = "system"; config["port"] = nullptr;
    }
    if (!mirror_mode.empty()) {
        require(mirror_mode == "official" || mirror_mode == "ghproxy" || mirror_mode == "ghfast" || mirror_mode == "custom", "invalid_mirror_mode");
        config["mirror_mode"] = mirror_mode;
        if (mirror_mode == "custom") {
            require(custom_mirror.rfind("https://", 0) == 0 && custom_mirror.size() <= 200 &&
                custom_mirror.find_first_of(" ;&|`$\n\r\t'\"\\") == custom_mirror.npos, "invalid_custom_mirror");
            config["custom_mirror"] = custom_mirror;
        } else {
            config["custom_mirror"] = "";
        }
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

inline std::string mirror_download_url(const std::string& original, const Json& network) {
    auto mode = network.value("mirror_mode", "official");
    if (mode == "ghproxy") {
        return "https://ghproxy.net/" + original;
    } else if (mode == "ghfast") {
        return "https://ghfast.top/" + original;
    } else if (mode == "custom") {
        auto custom = network.value("custom_mirror", "");
        if (!custom.empty()) {
            if (custom.back() == '/') return custom + original;
            return custom + "/" + original;
        }
    }
    return original;
}

} // namespace manager
