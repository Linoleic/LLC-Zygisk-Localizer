#pragma once
#include <string>
#include <cctype>
#include "../../third_party/nlohmann/json.hpp"

inline constexpr const char* kVerifiedIl2CppSha256 =
    "fbfc653607f48c2abfe4330eab784867104b80d2ff60a91684bb18bfa09ce097";
inline constexpr const char* kFullTrial1180Sha256 =
    "25e9a8a0d08c8d83366c993e16a9d9ef3f7ce5dd444aa540f689317a31f99593";

inline const char* approved_native_label(const std::string& digest) {
    if (digest == kVerifiedIl2CppSha256) return "1.117.1 / versionCode 485";
    if (digest == kFullTrial1180Sha256) return "1.118.0 / versionCode 487";
    return nullptr;
}

inline const char* native_validation_scope(const std::string& digest) {
    if (digest == kVerifiedIl2CppSha256) return "baseline";
    if (digest == kFullTrial1180Sha256) return "full_runtime_trial";
    return nullptr;
}

inline bool approved_native_build(const std::string& digest) {
    return approved_native_label(digest) != nullptr;
}

struct CompatibilityVerdict {
    bool approved = false;
    std::string status = "unknown_native_build";
    std::string label;
    std::string scope;
    std::string matched_sha256;
};

inline CompatibilityVerdict evaluate_compatibility(
    const std::string& digest,
    const nlohmann::json& custom_config = nlohmann::json::object())
{
    CompatibilityVerdict verdict;
    if (digest.empty() || digest.length() != 64) {
        verdict.approved = false;
        verdict.status = "fingerprint_unavailable";
        return verdict;
    }
    for (char c : digest) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
            verdict.approved = false;
            verdict.status = "fingerprint_unavailable";
            return verdict;
        }
    }

    // Tier 1: Built-in verified builds
    if (const char* built_in_label = approved_native_label(digest)) {
        verdict.approved = true;
        verdict.status = "approved";
        verdict.label = built_in_label;
        const char* scope = native_validation_scope(digest);
        verdict.scope = scope ? scope : "baseline";
        verdict.matched_sha256 = digest;
        return verdict;
    }

    // Tier 2: Check custom config allowed builds
    if (custom_config.is_object()) {
        if (custom_config.contains("custom_approved_builds") && custom_config["custom_approved_builds"].is_object()) {
            auto it = custom_config["custom_approved_builds"].find(digest);
            if (it != custom_config["custom_approved_builds"].end() && it->is_string()) {
                verdict.approved = true;
                verdict.status = "approved_by_config";
                verdict.label = it->get<std::string>();
                verdict.scope = "custom_config_approval";
                verdict.matched_sha256 = digest;
                return verdict;
            }
        }
        if (custom_config.contains("allowed_hashes") && custom_config["allowed_hashes"].is_array()) {
            for (const auto& h : custom_config["allowed_hashes"]) {
                if (h.is_string() && h.get<std::string>() == digest) {
                    verdict.approved = true;
                    verdict.status = "approved_by_config";
                    verdict.label = "Configured Build (" + digest.substr(0, 8) + ")";
                    verdict.scope = "custom_config_approval";
                    verdict.matched_sha256 = digest;
                    return verdict;
                }
            }
        }

        // Tier 3: Adaptive mode check (default true if unspecified, or explicitly enabled)
        bool allow_adaptive = true;
        if (custom_config.contains("allow_adaptive_compatibility") && custom_config["allow_adaptive_compatibility"].is_boolean()) {
            allow_adaptive = custom_config["allow_adaptive_compatibility"].get<bool>();
        }
        if (allow_adaptive) {
            verdict.approved = true;
            verdict.status = "adaptive_approved";
            verdict.label = "Adaptive Dynamic Build (" + digest.substr(0, 8) + ")";
            verdict.scope = "adaptive_runtime_reflection";
            verdict.matched_sha256 = digest;
            return verdict;
        }
    } else {
        verdict.approved = true;
        verdict.status = "adaptive_approved";
        verdict.label = "Adaptive Dynamic Build (" + digest.substr(0, 8) + ")";
        verdict.scope = "adaptive_runtime_reflection";
        verdict.matched_sha256 = digest;
        return verdict;
    }

    verdict.approved = false;
    verdict.status = "unknown_native_build";
    return verdict;
}
