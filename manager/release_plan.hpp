#pragma once
#include "update_policy.hpp"
#include "../jni/core/resource_pack.hpp"

namespace release_plan {
using resource_update::Json;
using resource_update::require;
inline constexpr const char* repository = "LocalizeLimbusCompany/LocalizeLimbusCompany";
inline constexpr uint64_t archive_limit = 64 * 1024 * 1024;
inline std::string api(const std::string& version) {
    require(resource_update::version(version) == version, "invalid_release_version");
    return std::string("https://api.github.com/repos/") + repository + "/releases/tags/" + version;
}
inline uint64_t positive(const Json& value) {
    require(value.is_number_integer(), "invalid_release_number");
    auto text = resource_update::version(value);
    return std::stoull(text);
}
inline Json direct(const std::string& wanted) {
    require(resource_update::version(wanted) == wanted, "invalid_release_version");
    auto filename = "LimbusLocalize_" + wanted + ".zip";
    auto url = std::string("https://github.com/") + repository + "/releases/download/" + wanted + "/" + filename;
    uint64_t numeric_id = std::stoull(wanted);
    return {{"version", wanted}, {"release_id", numeric_id}, {"asset_id", numeric_id}, {"name", filename},
        {"url", url}, {"size", 0}, {"sha256", ""}, {"updated_at", ""}, {"metadata_endpoint", api(wanted)},
        {"source", "official_github_release_asset"}, {"direct_release", true}};
}
inline Json parse(const std::string& raw, const std::string& wanted) {
    require(raw.size() <= 1024 * 1024 && raw.find('\0') == raw.npos, "invalid_release_response_size");
    auto doc = Json::parse(raw, nullptr, false);
    require(doc.is_object() && doc.value("tag_name", Json()) == wanted && doc.value("draft", Json()) == false &&
        doc.value("prerelease", Json()) == false && doc.contains("assets") && doc["assets"].is_array() && doc["assets"].size() <= 128,
        "unrecognized_release");
    api(wanted);
    auto release_id = positive(doc.at("id"));
    auto filename = "LimbusLocalize_" + wanted + ".zip";
    auto url = std::string("https://github.com/") + repository + "/releases/download/" + wanted + "/" + filename;
    Json found = nullptr;
    for (const auto& asset : doc["assets"]) {
        require(asset.is_object(), "invalid_release_asset");
        if (asset.value("name", Json()) != filename) continue;
        require(found.is_null() && asset.value("state", Json()) == "uploaded" && asset.value("browser_download_url", Json()) == url,
            "ambiguous_or_unready_asset");
        auto size = positive(asset.at("size")), id = positive(asset.at("id"));
        require(size <= archive_limit, "archive_too_large");
        auto digest = asset.value("digest", std::string{});
        require(digest.rfind("sha256:", 0) == 0 && resource_pack::digest_valid(digest.substr(7)), "missing_asset_sha256");
        require(asset.contains("updated_at") && asset["updated_at"].is_string() && asset["updated_at"].get<std::string>().size() <= 64, "invalid_asset_timestamp");
        found = {{"version", wanted}, {"release_id", release_id}, {"asset_id", id}, {"name", filename}, {"url", url},
            {"size", size}, {"sha256", digest.substr(7)}, {"updated_at", asset["updated_at"]}, {"metadata_endpoint", api(wanted)},
            {"source", "official_github_release_asset"}};
    }
    require(!found.is_null(), "zip_asset_unavailable");
    return found;
}
inline void validate(const Json& plan) {
    require(plan.is_object() && plan.contains("version"), "invalid_download_plan");
    auto v = resource_update::version(plan["version"]);
    auto expected_url = std::string("https://github.com/") + repository + "/releases/download/" + v + "/LimbusLocalize_" + v + ".zip";
    require(plan.value("url", Json()) == expected_url && plan.value("metadata_endpoint", Json()) == api(v) &&
        plan.value("source", Json()) == "official_github_release_asset" && plan.value("name", Json()) == "LimbusLocalize_" + v + ".zip" &&
        resource_pack::digest_valid(plan.value("sha256", std::string{})), "invalid_download_identity");
    require(positive(plan.at("size")) <= archive_limit, "invalid_download_size");
    positive(plan.at("asset_id")); positive(plan.at("release_id"));
    require(plan.contains("updated_at") && plan["updated_at"].is_string() && plan["updated_at"].get<std::string>().size() <= 64, "invalid_plan_timestamp");
}
inline bool same(const Json& a, const Json& b) { validate(a); validate(b); return a == b; }
} // namespace release_plan
