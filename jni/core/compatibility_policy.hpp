#pragma once
#include <string>

inline constexpr const char* kVerifiedIl2CppSha256 =
    "fbfc653607f48c2abfe4330eab784867104b80d2ff60a91684bb18bfa09ce097";

inline bool approved_native_build(const std::string& digest) {
    return digest == kVerifiedIl2CppSha256;
}
