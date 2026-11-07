#include "../tools/reload_manager.hpp"
#include "reload_signal.hpp"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

static void test_reload_manager() {
char pattern[] = "/tmp/llc-reload-manager-XXXXXX";
    auto directory = mkdtemp(pattern); assert(directory);
    std::filesystem::path root(directory);
    std::vector<std::string> paths = {(root / "private").string(), (root / "public").string(), (root / "temporary").string()};
    auto first = reload_manager::trigger_reload(paths);
    assert(first["flag_created"] == true);
    assert(std::filesystem::exists(paths[0]) && !std::filesystem::exists(paths[1]) && !std::filesystem::exists(paths[2]));
    std::filesystem::remove(paths[0]);
    paths[0] = (root / "absent" / "private").string();
    assert(reload_manager::trigger_reload(paths)["flag_created"] == true);
    assert(std::filesystem::exists(paths[1]) && !std::filesystem::exists(paths[2]));
    assert(reload_manager::trigger_reload({paths[0]})["flag_created"] == false);
    std::filesystem::remove_all(root);
    std::puts("reload_manager: publishes one marker and uses alternate paths only as fallbacks");
}

static void test_reload_signal() {
char pattern[] = "/tmp/llc-reload-XXXXXX";
    auto directory = mkdtemp(pattern); assert(directory);
    std::filesystem::path root(directory);
    std::vector<std::string> paths = {(root / "private").string(), (root / "public").string(), (root / "temporary").string()};
    auto write = [](const std::string& path, const char* text) { std::ofstream file(path); file << text; };
    ReloadSignals signals;
    assert(!signals.consume(paths));
    for (const auto& path : paths) write(path, "reload");
    assert(signals.consume(paths));
    for (const auto& path : paths) assert(!std::filesystem::exists(path));
    for (int i = 0; i < 10; ++i) assert(!signals.consume(paths));
    write(paths[1], "reload again");
    assert(signals.consume(paths) && !signals.consume(paths));
    ReloadSignals denied([](const char*) { return -1; });
    for (const auto& path : paths) write(path, "reload");
    assert(denied.consume(paths));
    assert(!denied.consume(paths)); // Same unremovable markers must not reload forever.
    write(paths[2], "a new request with a different stamp");
    assert(denied.consume(paths) && !denied.consume(paths));
    for (const auto& path : paths) std::filesystem::remove(path);
    assert(!denied.consume(paths));
    write(paths[0], "new");
    assert(denied.consume(paths));
    std::filesystem::remove(paths[0]);
    std::filesystem::create_directory(paths[0]);
    std::filesystem::create_symlink(paths[0], paths[1]);
    assert(!signals.consume(paths));
    std::filesystem::remove_all(root);
    std::puts("reload_signal: aliases coalesced, repeated/new requests, unlink failure and non-file markers passed");
}

int main() {
    test_reload_manager();
    test_reload_signal();
    std::puts("All reload tests passed successfully.");
    return 0;
}
