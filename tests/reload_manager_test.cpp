#include "../tools/reload_manager.hpp"
#include <cassert>
#include <cstdlib>
#include <cstdio>

int main() {
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
