#define LLC_MANAGER_TEST
#include "../tools/resource_manager.cpp"
#include <cassert>

int main() {
    char pattern[] = "/data/local/tmp/llc-ui-tests-XXXXXX";
    auto created = mkdtemp(pattern); assert(created);
    manager::fs::path root(created);
    {
        manager::Store store(root);
        unsetenv("HTTPS_PROXY"); unsetenv("https_proxy"); unsetenv("ALL_PROXY"); unsetenv("all_proxy");
        assert(manager::network_summary(store)["mode"] == "system");
        for (const auto& invalid : {"0", "65536", "07890", "-1", "7890;id", "http://host", ""}) {
            bool refused = false;
            try { manager::save_network(store, invalid); } catch (...) { refused = true; }
            assert(refused && manager::read_network(store)["mode"] == "system");
        }
        manager::save_network(store, "7890");
        assert(manager::network_summary(store)["effective_source"] == "module_loopback");
        setenv("HTTPS_PROXY", "http://caller.invalid:1234", 1);
        assert(manager::network_summary(store).dump().find("caller.invalid") == std::string::npos);
        manager::apply_network(store);
        assert(std::string(getenv("HTTPS_PROXY")) == "http://caller.invalid:1234");
        unsetenv("HTTPS_PROXY"); setenv("NO_PROXY", "*", 1);
        manager::apply_network(store);
        assert(std::string(getenv("HTTPS_PROXY")) == "http://127.0.0.1:7890" && !getenv("NO_PROXY"));
        unsetenv("HTTPS_PROXY"); unsetenv("https_proxy");
        manager::save_network(store, "system"); manager::apply_network(store); assert(!getenv("HTTPS_PROXY"));
        assert(manager::network_summary(store)["mirror_mode"] == "official");
        manager::save_network(store, "7890", "ghproxy");
        assert(manager::network_summary(store)["mirror_mode"] == "ghproxy");
        assert(manager::mirror_download_url("https://github.com/test/asset.zip", manager::read_network(store)) == "https://ghproxy.net/https://github.com/test/asset.zip");
        manager::save_network(store, "system", "custom", "https://cdn.example.com/");
        assert(manager::network_summary(store)["mirror_mode"] == "custom");
        assert(manager::mirror_download_url("https://github.com/test/asset.zip", manager::read_network(store)) == "https://cdn.example.com/https://github.com/test/asset.zip");
        for (const auto& bad_mirror : {"invalid", "ftp://foo", "custom"}) {
            bool mirror_refused = false;
            try { manager::save_network(store, "system", bad_mirror); } catch (...) { mirror_refused = true; }
            assert(mirror_refused);
        }
        for (const auto& bad_url : {"http://insecure.com/", "https://bad;cmd/", "https://bad$var/", ""}) {
            bool url_refused = false;
            try { manager::save_network(store, "system", "custom", bad_url); } catch (...) { url_refused = true; }
            assert(url_refused);
        }
        manager::save_network(store, "system", "official");
        auto progress = manager::progress(store);
        assert(progress["kind"] == "module_manager_progress" && progress["busy"] == false);
        assert(!progress.contains("runtime") && !progress.contains("local_resources"));
        manager::Store busy(root); assert(!busy.locked);
        assert(manager::progress(busy)["busy"] == true);
        bool refused = false;
        try { manager::save_network(busy, "7890"); } catch (...) { refused = true; }
        assert(refused);
        store.write_document("network.json", {{"schema", 1}, {"mode", "loopback"}, {"port", -1}});
        refused = false;
        try { manager::read_network(store); } catch (...) { refused = true; }
        assert(refused);
        auto packs = root / "fake-game"; manager::fs::create_directory(packs);
        assert(manager::managed_summary(packs)["state"] == "legacy");
        pack_tool::write_new(packs / "state.json", manager::Json({{"schema", 1}, {"active", "pack-active"}, {"previous", "pack-old"}}).dump());
        auto managed = manager::managed_summary(packs);
        assert(managed["state"] == "managed" && managed["previous"] == "pack-old" && managed["previous_version"].is_null());
        manager::fs::remove(packs / "state.json");
        pack_tool::write_new(packs / "state.json", "{}");
        assert(manager::managed_summary(packs)["state"] == "unavailable");
        assert(manager::cleanup_history(store).is_null());
        store.write_document("cleanup-state.json", {{"schema", 1}, {"kind", "module_cleanup_result"}, {"phase", "working"}, {"deleted", manager::Json::array()}});
        assert(manager::cleanup_history(store)["phase"] == "interrupted");
        assert(manager::cleanup_history(busy)["phase"] == "working");
    }
    manager::fs::remove_all(root);
    std::cout << "manager UI configuration and progress tests passed\n";
}
