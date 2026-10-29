#pragma once
#include "resource_pack.hpp"

namespace resource_paths {
inline constexpr const char* legacy_root = "/data/user/0/com.ProjectMoon.LimbusCompany/files/Lang/LLC_zh-CN";
inline constexpr const char* store_root = "/data/user/0/com.ProjectMoon.LimbusCompany/files/Lang/.llc-packs";

// Initialized once per process; never resolve state.json again for later stories.
inline const resource_pack::Selection& pinned() {
    static const auto selected = resource_pack::select(store_root, legacy_root);
    return selected;
}
inline std::vector<std::string> story_roots(const resource_pack::Selection& selected) {
    std::vector<std::string> roots{selected.root + "/StoryData/"};
    if (selected.mode == "legacy") roots.push_back("/sdcard/Android/data/com.ProjectMoon.LimbusCompany/files/Lang/LLC_zh-CN/StoryData/");
    return roots;
}
inline std::vector<std::string> story_roots() { return story_roots(pinned()); }
} // namespace resource_paths
