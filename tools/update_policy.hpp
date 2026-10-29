#pragma once
#include "../jni/third_party/nlohmann/json.hpp"
#include "../jni/core/sha256.hpp"
#include <charconv>
#include <string>
#include <stdexcept>
#include <limits>

// Pure metadata/comparison policy. No networking and no game process access.
namespace resource_update {
using Json = nlohmann::json;
inline constexpr const char* endpoint = "https://api.zeroasso.top/v2/resource/get_version";
inline constexpr int64_t fresh_seconds = 24 * 60 * 60;
inline void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
inline std::string version(const Json& value) {
    std::string raw;
    if (value.is_string()) raw = value.get<std::string>();
    else if (value.is_number_integer()) raw = value.dump();
    require(!raw.empty() && raw.size() <= 18 && raw[0] != '0', "invalid_version");
    uint64_t parsed = 0;
    auto end = std::from_chars(raw.data(), raw.data() + raw.size(), parsed);
    require(end.ec == std::errc{} && end.ptr == raw.data() + raw.size() && parsed > 0, "invalid_version");
    return raw;
}
inline Json document(const std::string& raw) {
    require(!raw.empty() && raw.size() <= 65536 && raw.find('\0') == raw.npos, "invalid_metadata_size_or_nul");
    auto value = Json::parse(raw, nullptr, false);
    require(value.is_object(), "invalid_metadata_json");
    return value;
}
inline Json metadata(const std::string& raw) {
    auto value = document(raw);
    require(value.contains("version"), "missing_remote_version");
    auto number = version(value["version"]);
    std::string notice;
    if (value.contains("notice")) {
        require(value["notice"].is_string(), "invalid_notice");
        notice = value["notice"].get<std::string>();
        require(notice.size() <= 8192 && notice.find('\0') == notice.npos, "invalid_notice");
    }
    BuildSha256 hash; hash.update(raw.data(), raw.size());
    return {{"version", number}, {"notice", notice}, {"metadata_sha256", hash.hex_digest()},
        {"endpoint", endpoint}, {"download_ready", false}, {"archive_identity_verified", false}};
}
inline Json initial() {
    return {{"schema", 1}, {"kind", "resource_update_cache"}, {"last_attempt", nullptr}, {"last_success", nullptr}};
}
inline bool timestamp(const Json& value) {
    if (!value.is_number_integer()) return false;
    return value.is_number_unsigned() ? value.get<uint64_t>() <= static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) : value.get<int64_t>() >= 0;
}
inline void validate_cache(const Json& cache) {
    require(cache.is_object() && cache.value("schema", Json()) == 1 && cache.value("kind", Json()) == "resource_update_cache" &&
        cache.contains("last_attempt") && cache.contains("last_success"), "invalid_update_cache");
    auto& attempt = cache["last_attempt"];
    if (!attempt.is_null()) {
        require(attempt.is_object() && attempt.contains("status") && attempt["status"].is_string() &&
            attempt.contains("started_at_unix") && timestamp(attempt["started_at_unix"]), "invalid_cached_attempt");
        auto status = attempt["status"].get<std::string>();
        require(status == "checking" || status == "succeeded" || status == "failed", "invalid_cached_attempt_status");
        if (status != "checking") require(attempt.contains("finished_at_unix") && timestamp(attempt["finished_at_unix"]), "invalid_cached_finish");
        if (status == "failed") require(attempt.contains("error") && attempt["error"].is_string(), "invalid_cached_error");
    }
    auto& success = cache["last_success"];
    if (!success.is_null()) {
        require(success.is_object() && success.contains("checked_at_unix") && timestamp(success["checked_at_unix"]) &&
            success.contains("remote") && success["remote"].is_object(), "invalid_cached_success");
        require(success["remote"].value("endpoint", std::string{}) == endpoint && success["remote"].contains("version"), "invalid_cached_source");
        version(success["remote"]["version"]);
        auto hash = success["remote"].value("metadata_sha256", std::string{});
        require(hash.size() == 64 && hash.find_first_not_of("0123456789abcdef") == hash.npos, "invalid_cached_digest");
    }
}
inline Json begin(const Json& previous, int64_t now) {
    validate_cache(previous); auto next = previous;
    next["last_attempt"] = {{"status", "checking"}, {"started_at_unix", now}, {"finished_at_unix", nullptr}, {"error", nullptr}};
    return next;
}
inline Json succeeded(const Json& checking, const Json& remote, const Json& local, int64_t now) {
    auto next = checking;
    next["last_attempt"]["status"] = "succeeded";
    next["last_attempt"]["finished_at_unix"] = now;
    next["last_success"] = {{"checked_at_unix", now}, {"remote", remote}, {"local_at_check", local}};
    return next;
}
inline Json failed(const Json& checking, const std::string& error, int64_t now) {
    auto next = checking;
    next["last_attempt"]["status"] = "failed";
    next["last_attempt"]["finished_at_unix"] = now;
    next["last_attempt"]["error"] = error;
    return next; // The successful observation, including its timestamp, is retained.
}
inline Json summary(const Json& cache, const Json& local, int64_t now, bool busy = false) {
    validate_cache(cache);
    Json comparison = {{"state", "not_checked"}, {"cached_remote", true}, {"age_seconds", nullptr}};
    const auto& success = cache["last_success"];
    if (!success.is_null()) {
        auto checked = success["checked_at_unix"].get<int64_t>();
        comparison["age_seconds"] = now >= checked ? Json(now - checked) : Json(nullptr);
        if (now < checked || now - checked > fresh_seconds) comparison["state"] = "stale_result";
        else if (!local.contains("version") || local["version"].is_null()) comparison["state"] = "comparison_unavailable";
        else {
            auto left = version(local["version"]), right = version(success["remote"]["version"]);
            // Canonical positive decimal strings: length then lexical comparison.
            bool newer = right.size() != left.size() ? right.size() > left.size() : right > left;
            comparison["state"] = left == right ? "no_update" : newer ? "update_available" : "local_ahead";
        }
    }
    std::string state = comparison["state"];
    if (busy) state = !cache["last_attempt"].is_null() && cache["last_attempt"]["status"] == "checking" ? "checking" : "manager_busy";
    else if (!cache["last_attempt"].is_null()) {
        auto status = cache["last_attempt"]["status"].get<std::string>();
        if (status == "failed") state = "check_failed";
        else if (status == "checking") state = "check_interrupted";
    }
    return {{"state", state}, {"checker_implemented", true}, {"comparison", comparison},
        {"last_attempt", cache["last_attempt"]}, {"last_success", success}, {"download_implemented", false},
        {"game_compatibility_inferred", false}, {"whole_game_coverage_inferred", false}};
}
} // namespace resource_update
