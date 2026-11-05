#include "resource_pack.hpp"
#include "resource_paths.hpp"
#include <cassert>
#include <chrono>
#include <fstream>

namespace rp = resource_pack;
namespace fs = std::filesystem;
template<class F> void fails(F fn) {
    bool failed = false; try { fn(); } catch (const std::exception&) { failed = true; } assert(failed);
}
void put(const fs::path& path, const std::string& bytes) {
    fs::create_directories(path.parent_path()); std::ofstream f(path, std::ios::binary); f << bytes; assert(f.good());
}
int main() {
    auto root = fs::temp_directory_path() / ("llc-pack-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    auto source = root / "source", store = root / "store";
    put(source / "UI.json", R"({"dataList":[{"id":"key","content":"old"}]})");
    put(source / "StoryData/S001B.json", R"({"dataList":[{"id":1,"content":"story"}]})");
    put(source / "Info/version.json", R"({"version":2026101602})");
    put(source / "Info/LICENSE", "test license");
    put(source / "static_overrides.json", R"({"missing_strings":{"Support":"Help"}})");
    put(source / "title_overrides.json", R"({"paths":{"Title":"Start"}})");
    put(source / "custom_overrides.json", R"({"force_font_unify":true})");
    assert(!rp::translation("static_overrides.json"));
    assert(rp::translation("nested/static_overrides.json"));
    fails([&] { rp::validate_content("static_overrides.json", "[]"); });
    fails([&] { rp::validate_content("static_overrides.json", "{"); });
    fails([&] { rp::validate_content("unrecognized.json", "{}"); });
    fails([&] { rp::validate_content("nested/static_overrides.json", "{}"); });
    put(root / "override-only/static_overrides.json", "{}");
    fails([&] { rp::make_manifest(root / "override-only", "no-translations"); });
    assert(rp::select(store, source).mode == "legacy");
    auto a = rp::make_manifest(source, "baseline-1");
    assert(a.files.size() == 7); rp::verify(source, a, false);
    put(source / "static_overrides.json", "{}"); fails([&] { rp::verify(source, a, false); });
    put(source / "static_overrides.json", R"({"missing_strings":{"Support":"Help"}})");
    for (const auto& path : {"../x", "/x", "C:/x", "a\\b", "a//b", "a/./b", "a/../b", "a/", "_llc_manifest.json"}) assert(!rp::relative_path(path));
    auto bad = rp::Json::parse(a.bytes);
    bad["files"][0]["path"] = "../outside"; fails([&] { rp::parse_manifest(bad.dump()); });
    bad = rp::Json::parse(a.bytes); bad["files"].push_back(bad["files"][0]); fails([&] { rp::parse_manifest(bad.dump()); });
    bad = rp::Json::parse(a.bytes); bad["files"][0]["size"] = -1; fails([&] { rp::parse_manifest(bad.dump()); });
    bad = rp::Json::parse(a.bytes); bad["files"][0]["sha256"] = "no"; fails([&] { rp::parse_manifest(bad.dump()); });
    fails([&] { rp::parse_manifest(R"({"schema":1,"version":"v","files":[]})"); });
    put(source / "extra.txt", "extra"); fails([&] { rp::verify(source, a, false); }); fs::remove(source / "extra.txt");
    fs::rename(source / "UI.json", source / "missing"); fails([&] { rp::verify(source, a, false); }); fs::rename(source / "missing", source / "UI.json");
    auto bytes = rp::read_bytes(source / "UI.json");
    put(source / "UI.json", R"({"dataList":[{"id":"key","content":"new"}]})");
    fails([&] { rp::verify(source, a, false); });
    auto b = rp::make_manifest(source, "baseline-2");
    auto release_b = store / "releases" / b.key;
    for (const auto& entry : b.files) put(release_b / entry.path, rp::read_bytes(source / entry.path));
    put(release_b / rp::manifest_name, b.bytes);
    put(source / "UI.json", bytes);
    auto release_a = store / "releases" / a.key;
    for (const auto& entry : a.files) put(release_a / entry.path, rp::read_bytes(source / entry.path));
    put(release_a / rp::manifest_name, a.bytes);
    fails([&] { rp::verify_release(release_a, b.key); });
    auto state = [&](const std::string& active, const std::string& previous) {
        put(store / "state.json", rp::Json({{"schema", 1}, {"active", active}, {"previous", previous}}).dump());
    };
    state(a.key, ""); auto pinned = rp::select(store, source);
    state(b.key, a.key); assert(rp::select(store, source).key == b.key);
    assert(pinned.key == a.key && resource_paths::story_roots(pinned) == std::vector<std::string>{release_a.string() + "/StoryData/"});
    put(release_b / "UI.json", "broken"); assert(rp::select(store, source).mode == "previous");
    put(release_a / "UI.json", "broken"); fails([&] { rp::select(store, source); });
    put(store / "state.json", "{"); fails([&] { rp::select(store, source); });
    state("../outside", ""); fails([&] { rp::select(store, source); });
#ifndef _WIN32
    fs::create_symlink(source / "UI.json", source / "alias.json");
    fails([&] { rp::make_manifest(source, "bad-link"); }); fs::remove(source / "alias.json");
#endif
    std::vector<fs::path> created;
    for (const auto& entry : fs::recursive_directory_iterator(root)) created.push_back(entry.path());
    for (auto it = created.rbegin(); it != created.rend(); ++it) fs::remove(*it);
    fs::remove(root);
    std::puts("resource_pack: exact manifests, missing/extra/tampered files, safe paths, pinned roots and previous-release recovery passed");
}
