#pragma once
#include <string>

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
