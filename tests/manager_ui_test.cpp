#define LLC_MANAGER_TEST
#include "../tools/resource_manager.cpp"
#include <cassert>

int main() {
    {
        manager::Json core = {{"pid", 42}, {"process_start_ticks", "123"}, {"runtime", {{"stage", "running"}}},
            {"coverage", {{"waiting_or_retrying_tables", 2}}}, {"compatibility", {{"il2cpp_sha256", "native"}}},
            {"capabilities", {{"text_schema_audit", {{"protocol", 1}, {"catalog_signature", "catalog"}}}}}};
        manager::Json run = {{"fresh_request", true}, {"report", core}};
        assert(!manager::schema_automatic_ready(run));
        assert(!manager::schema_automatic_ready({{"fresh_request", true}, {"report", nullptr}}));
        run["report"]["coverage"]["waiting_or_retrying_tables"] = 0; assert(manager::schema_automatic_ready(run));
        assert(manager::schema_capability(run)["protocol"] == 1);
        assert(manager::schema_capability({{"report", nullptr}}).is_null());
        assert(manager::schema_capability(nullptr).is_null());
        manager::Json doc = {{"schema", 1}, {"kind", "text_schema_snapshot"}, {"read_only", true}, {"request_id", "nonce"},
            {"pid", 42}, {"process_start_ticks", "123"}, {"native_sha256", "native"}, {"catalog_signature", "catalog"}};
        assert(manager::schema_identity(doc, run, "nonce"));
        for (auto key : {"pid", "process_start_ticks", "native_sha256", "catalog_signature"}) {
            auto changed = doc; changed[key] = "wrong"; assert(!manager::schema_identity(changed, run, "nonce"));
        }
        assert(!manager::schema_identity(doc, run, "old-nonce"));
        run["fresh_request"] = false; assert(!manager::schema_identity(doc, run, "nonce"));
        run["fresh_request"] = true;
        manager::Json cache = {{"current", {{"native_sha256", "native"}, {"catalog_signature", "catalog"}}}};
        assert(!manager::schema_needs_refresh(cache, run));
        cache["current"]["native_sha256"] = "old"; assert(manager::schema_needs_refresh(cache, run));
        assert(!manager::schema_needs_refresh(nullptr, run));
    }
    assert(game_info::parse("    versionCode=487 minSdk=26\n    versionName=1.118.0\n")["version"] == "1.118.0");
    assert(game_info::parse("    versionCode=487 minSdk=26\n    versionName=1.118.0\n")["version_code"] == 487);
    for (auto invalid : {"", "Unable to find package", "versionName=1.118.0\n", "versionCode=-1\nversionName=1.0\n", "versionCode=1\nversionName=null\n"})
        assert(game_info::parse(invalid)["state"] == "unavailable");
    char pattern[] = "/data/local/tmp/llc-ui-tests-XXXXXX";
    auto created = mkdtemp(pattern); assert(created);
    manager::fs::path root(created);
    auto prop = root / "module.prop";
    { std::ofstream file(prop); file << "version=v0.4.1-gabc-local\r\nversionCode=491\r\n"; }
    assert(manager::installed_module(prop)["version"] == "v0.4.1-gabc-local");
    assert(manager::installed_module(prop)["version_code"] == 491);
    for (const auto* malformed : {"version=bad version\nversionCode=491\n", "version=dev\nversionCode=-1\n",
                                  "version=dev\nversionCode=2147483648\n", "version=dev\n"}) {
        { std::ofstream file(prop); file << malformed; }
        assert(manager::installed_module(prop)["state"] == "unavailable");
    }
    manager::fs::remove(prop);
    assert(manager::installed_module(prop)["state"] == "unavailable");
    {
        manager::Store store(root);
        store.write_document("large-state.json", {{"payload", std::string(70000, 'x')}});
        bool too_large = false;
        try { store.read_document("large-state.json", nullptr); } catch (...) { too_large = true; }
        assert(too_large);
        assert(store.read_document("large-state.json", nullptr, 2 * 1024 * 1024)["payload"].get<std::string>().size() == 70000);
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
        auto mock_tcp = root / "mock_tcp";
        auto mock_pkgs = root / "mock_packages";
        std::ofstream(mock_pkgs.string()) << "com.follow.clash 10307 0 /data/user/0/com.follow.clash\n";
        std::ofstream(mock_tcp.string()) << "  sl  local_address rem_address   st tx_queue rx_queue tr tm->when retrnsmt   uid\n"
                                         << "   0: 0100007F:1ED2 00000000:0000 0A 00000000:00000000 00:00000000 00000000 10307\n";
        auto detected = manager::detect_proxy(mock_tcp.string(), mock_pkgs.string());
        assert(detected["kind"] == "proxy_detection_result");
        assert(detected["detected"] == true);
        assert(detected["recommended_port"] == 7890);
        assert(detected["app_name"] == "Clash");
        assert(detected["package_name"] == "com.follow.clash");
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
