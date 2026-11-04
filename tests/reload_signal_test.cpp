#include "reload_signal.hpp"
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <cstdio>

int main() {
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
