#pragma once
#include "../third_party/nlohmann/json.hpp"
#if __has_include("llc_build_generated.hpp")
#include "llc_build_generated.hpp"
#else
// Host tests and third-party builds must not impersonate a release build.
#define LLC_RELEASE_VERSION "unknown"
#define LLC_VERSION_CODE 0
#define LLC_GIT_REVISION "unknown"
#define LLC_BUILD_DIRTY 0
#define LLC_BUILD_ID "unidentified"
#define LLC_BUILT_AT "unknown"
#endif

namespace build_info {
inline nlohmann::json json() {
    return {{"version", LLC_RELEASE_VERSION}, {"version_code", LLC_VERSION_CODE},
        {"git_revision", LLC_GIT_REVISION}, {"dirty", LLC_BUILD_DIRTY != 0},
        {"build_id", LLC_BUILD_ID}, {"built_at_utc", LLC_BUILT_AT}};
}
}
