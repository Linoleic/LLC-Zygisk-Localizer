// Android/POSIX transaction fault tests, isolated from actual game resources.
#define LLC_PACK_NO_MAIN
#include "../manager/resource_pack.cpp"
#include <cassert>
#include <fstream>
#include <sys/wait.h>
#include <signal.h>

namespace rp = resource_pack;
namespace fs = std::filesystem;
template<class F> void fails(F fn) {
    bool failed = false; try { fn(); } catch (const std::exception&) { failed = true; } assert(failed);
}
void put(const fs::path& path, const std::string& bytes) {
    fs::create_directories(path.parent_path()); std::ofstream file(path); file << bytes; assert(file.good());
}
int main() {
    auto root = fs::temp_directory_path() / ("llc-transaction-" + pack_tool::token());
    auto source = root / "source", store = root / "store", manifest_file = root / "manifest.json";
    auto old_text = std::string(R"({"dataList":[{"id":"key","content":"old"}]})");
    put(source / "UI.json", old_text);
    const std::string overrides = R"({"missing_strings":{"Support":"Help"}})";
    put(source / "static_overrides.json", overrides);
    auto a = rp::make_manifest(source, "old"); put(manifest_file, a.bytes);
    assert(pack_tool::install(source, manifest_file, store) == a.key);
    assert(rp::read_bytes(store / "releases" / a.key / "static_overrides.json") == overrides);
    assert(rp::read_state(store).previous.empty());
    put(source / "UI.json", R"({"dataList":[{"id":"key","content":"new"}]})");
    auto b = rp::make_manifest(source, "new"); put(manifest_file, b.bytes);
    auto killed_at = [&](const char* point) {
        auto child = fork(); assert(child >= 0);
        if (child == 0) {
            pack_tool::install(source, manifest_file, store, [=](const char* seen) {
                if (strcmp(seen, point) == 0) kill(getpid(), SIGKILL);
            });
            _exit(9); // checkpoint was not reached
        }
        int status = 0; assert(waitpid(child, &status, 0) == child);
        assert(WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL);
    };
    for (const char* point : {"staged", "published", "before_state"}) {
        killed_at(point);
        assert(rp::select(store, source).key == a.key);
    }
    { pack_tool::StoreLock held(store); fails([&] { pack_tool::install(source, manifest_file, store); }); }
    killed_at("committed"); assert(rp::select(store, source).key == b.key);
    assert(rp::read_state(store).previous == a.key);
    assert(pack_tool::install(source, manifest_file, store) == b.key);
    assert(rp::read_state(store).previous == a.key); // repeat install preserves rollback
    assert(pack_tool::rollback(store) == a.key);
    assert(rp::read_state(store).previous == b.key);
    assert(pack_tool::rollback(store) == b.key);
    // Failure before any commit never mutates the active release.
    put(source / "UI.json", "partial"); fails([&] { pack_tool::install(source, manifest_file, store); });
    assert(rp::select(store, source).key == b.key);
    put(store / "releases" / b.key / "UI.json", "damaged");
    assert(rp::select(store, source).key == a.key);
    assert(pack_tool::rollback(store) == a.key);
    // Preserve interrupted stages for diagnosis; test fixtures are deleted explicitly.
    std::vector<fs::path> created;
    for (const auto& entry : fs::recursive_directory_iterator(root)) created.push_back(entry.path());
    for (auto it = created.rbegin(); it != created.rend(); ++it) fs::remove(*it);
    fs::remove(root);
    std::puts("pack_transaction: SIGKILL before/after commit, locking, retry, activation, rollback and corrupt-active recovery passed");
}
