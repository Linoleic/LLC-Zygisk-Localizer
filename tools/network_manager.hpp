#pragma once
#include "../jni/third_party/nlohmann/json.hpp"
#include <string>
#include <fstream>
#include <sstream>
#include <cctype>
#include <map>
#include <vector>
#include <algorithm>

#ifndef _WIN32
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
#endif

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
    config["https_transport"] = https_client::transport();
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

struct DetectedCandidate {
    int port = 0;
    std::string app_name;
    std::string package_name;
    int score = 0;
    bool reachable = false;
};

inline std::pair<std::string, int> classify_package(const std::string& pkg) {
    std::string lower;
    for (char c : pkg) lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    if (lower.find("clash") != std::string::npos || lower.find("mihomo") != std::string::npos) {
        return {"Clash", 100};
    }
    if (lower.find("v2ray") != std::string::npos || lower.find("xray") != std::string::npos) {
        return {"v2rayNG", 100};
    }
    if (lower.find("sing") != std::string::npos) {
        return {"sing-box", 100};
    }
    if (lower.find("neko") != std::string::npos || lower.find("sagernet") != std::string::npos) {
        return {"NekoBox", 100};
    }
    if (lower.find("shadowsocks") != std::string::npos) {
        return {"Shadowsocks", 90};
    }
    if (lower.find("surfboard") != std::string::npos) {
        return {"Surfboard", 90};
    }
    if (lower.find("vpn") != std::string::npos || lower.find("proxy") != std::string::npos) {
        return {"VPN代理客户端", 70};
    }
    return {"", 0};
}

inline bool probe_port(int port) {
#ifndef _WIN32
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return false;
    int flags = fcntl(sock, F_GETFL, 0);
    if (flags >= 0) fcntl(sock, F_SETFL, flags | O_NONBLOCK);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    int res = connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    bool reachable = false;
    if (res == 0) {
        reachable = true;
    } else if (errno == EINPROGRESS) {
        pollfd pfd{sock, POLLOUT, 0};
        if (poll(&pfd, 1, 120) > 0) {
            int err = 0;
            socklen_t len = sizeof(err);
            if (getsockopt(sock, SOL_SOCKET, SO_ERROR, &err, &len) == 0 && err == 0) {
                reachable = true;
            }
        }
    }
    close(sock);
    return reachable;
#else
    (void)port;
    return false;
#endif
}

inline Json detect_proxy(const std::string& tcp_path = "/proc/net/tcp", const std::string& pkg_path = "/data/system/packages.list") {
    Json result = {
        {"schema", 1},
        {"kind", "proxy_detection_result"},
        {"detected", false},
        {"recommended_port", nullptr},
        {"app_name", nullptr},
        {"package_name", nullptr},
        {"candidates", Json::array()}
    };

    std::map<int, std::string> uid_map;
    std::ifstream pkg_file(pkg_path);
    if (pkg_file.is_open()) {
        std::string line;
        while (std::getline(pkg_file, line)) {
            if (line.empty()) continue;
            std::istringstream iss(line);
            std::string pkg; int uid = 0;
            if (iss >> pkg >> uid) uid_map[uid] = pkg;
        }
    }

    std::map<int, DetectedCandidate> candidate_map;
    std::vector<std::string> paths = {tcp_path};
    if (tcp_path == "/proc/net/tcp") paths.push_back("/proc/net/tcp6");

    for (const auto& path : paths) {
        std::ifstream file(path);
        if (!file.is_open()) continue;
        std::string line;
        if (std::getline(file, line)) {
            while (std::getline(file, line)) {
                std::istringstream iss(line);
                std::string sl, local_addr, rem_addr, st;
                if (!(iss >> sl >> local_addr >> rem_addr >> st)) continue;
                if (st != "0A") continue;

                auto colon = local_addr.find(':');
                if (colon == std::string::npos) continue;
                std::string ip_hex = local_addr.substr(0, colon);
                std::string port_hex = local_addr.substr(colon + 1);

                bool is_local = false;
                if (ip_hex.size() == 8) {
                    if (ip_hex == "0100007F" || ip_hex == "00000000") is_local = true;
                } else if (ip_hex.size() == 32) {
                    if (ip_hex.find("0100007F") != std::string::npos ||
                        ip_hex == "00000000000000000000000000000001" ||
                        ip_hex == "00000000000000000000000000000000") is_local = true;
                }
                if (!is_local) continue;

                int port = 0;
                try { port = static_cast<int>(std::stoul(port_hex, nullptr, 16)); } catch (...) { continue; }
                if (port <= 0 || port > 65535) continue;

                std::string tx_rx, tr_tm, retrnsmt;
                int uid = 0;
                iss >> tx_rx >> tr_tm >> retrnsmt >> uid;

                std::string pkg;
                auto it = uid_map.find(uid);
                if (it != uid_map.end()) pkg = it->second;

                auto [app_name, pkg_score] = classify_package(pkg);
                int score = pkg_score;

                if (port == 7890) score += 90;
                else if (port == 10809) score += 90;
                else if (port == 2080 || port == 20808) score += 80;
                else if (port == 7891 || port == 10808) score += 60;
                else if (port == 1080 || port == 8080 || port == 9090) score += 40;
                else if (port > 1024) score += 10;

                if (candidate_map.find(port) == candidate_map.end() || score > candidate_map[port].score) {
                    DetectedCandidate cand;
                    cand.port = port;
                    cand.app_name = app_name.empty() ? pkg : app_name;
                    cand.package_name = pkg;
                    cand.score = score;
                    candidate_map[port] = cand;
                }
            }
        }
    }

    if (candidate_map.empty()) {
        for (int p : {7890, 10809, 2080, 1080}) {
            if (probe_port(p)) {
                DetectedCandidate cand;
                cand.port = p;
                cand.app_name = "本地代理客户端";
                cand.score = 70;
                cand.reachable = true;
                candidate_map[p] = cand;
            }
        }
    }

    std::vector<DetectedCandidate> ranked;
    for (auto& [p, cand] : candidate_map) {
        if (!cand.reachable && probe_port(cand.port)) {
            cand.reachable = true;
            cand.score += 50;
        }
        ranked.push_back(cand);
    }

    std::sort(ranked.begin(), ranked.end(), [](const DetectedCandidate& a, const DetectedCandidate& b) {
        if (a.reachable != b.reachable) return a.reachable > b.reachable;
        return a.score > b.score;
    });

    for (const auto& c : ranked) {
        result["candidates"].push_back({
            {"port", c.port},
            {"app_name", c.app_name},
            {"package_name", c.package_name},
            {"reachable", c.reachable},
            {"score", c.score}
        });
    }

    if (!ranked.empty() && (ranked.front().reachable || ranked.front().score >= 70)) {
        result["detected"] = true;
        result["recommended_port"] = ranked.front().port;
        result["app_name"] = ranked.front().app_name;
        result["package_name"] = ranked.front().package_name;
    }

    return result;
}

} // namespace manager
