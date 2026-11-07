#define LLC_MANAGER_TEST
#include "../manager/resource_manager.cpp"
#include "../manager/private_cleanup.hpp"

#include <cassert>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace {
namespace fs = std::filesystem;
using manager::Json;

template <typename Function>
static bool throws(Function function) {
    try {
        function();
    } catch (const std::exception&) {
        return true;
    }
    return false;
}

static void write_file(const fs::path& path, const std::string& contents) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    assert(output.good());
    output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    output.close();
    assert(output.good());
}

static std::string read_file(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    assert(input.good());
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

struct TempDirectory {
    std::string path;
    TempDirectory() {
        char pattern[] = "/data/local/tmp/llc-manager-tests-XXXXXX";
        char* created = mkdtemp(pattern);
        assert(created != nullptr);
        path = created;
        assert(path.rfind("/data/local/tmp/llc-manager-tests-", 0) == 0);
        struct stat info{};
        assert(lstat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode) && !S_ISLNK(info.st_mode));
    }
    ~TempDirectory() {
        if (path.rfind("/data/local/tmp/llc-manager-tests-", 0) != 0) return;
        struct stat info{};
        if (lstat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode) && !S_ISLNK(info.st_mode)) {
            fs::remove_all(path);
        }
    }
};

static Json local_version(const std::string& value = "9") {
    return {{"state", "read"}, {"version", value}};
}

static void reap_child(pid_t child, int& status) {
    const int kill_result = kill(child, SIGKILL);
    const int kill_error = errno;
    pid_t result;
    do {
        result = waitpid(child, &status, 0);
    } while (result < 0 && errno == EINTR);
    assert(kill_result == 0 || kill_error == ESRCH);
    assert(result == child);
}
} // namespace

int main() {
    using namespace manager;
    TempDirectory temporary;
    const fs::path root = fs::path(temporary.path) / "state";
    const int64_t now = 1'800'000'000;
    Json initial;

    // A new store starts empty; a second opener observes the same state but cannot check.
    {
        Store first(root);
        assert(first.locked);
        initial = first.load();
        assert(initial == resource_update::initial());
        Store second(root);
        assert(!second.locked);
        assert(second.load() == initial);
        bool fetched = false;
        assert(throws([&] {
            check(second, local_version(), [&] { fetched = true; return R"({"version":9})"; }, [=] { return now; });
        }));
        assert(!fetched);

        auto success9 = check(first, local_version(), [] { return R"({"version":9})"; }, [=] { return now; });
        assert(success9["last_attempt"]["status"] == "succeeded");
        assert(success9["last_success"]["remote"]["version"] == "9");
        auto success10 = check(first, local_version(), [] { return R"({"version":"10"})"; }, [=] { return now + 1; });
        assert(success10["last_success"]["remote"]["version"] == "10");

        // Fetch exceptions and malformed metadata mark failure without losing success.
        const Json successful_record = success10["last_success"];
        auto thrown_fetch = check(first, local_version(), []() -> std::string { throw std::runtime_error("fake_fetch_error"); }, [=] { return now + 2; });
        assert(thrown_fetch["last_attempt"]["status"] == "failed");
        assert(thrown_fetch["last_success"] == successful_record);
        auto malformed = check(first, local_version(), [] { return R"({"version":)"; }, [=] { return now + 3; });
        assert(malformed["last_attempt"]["status"] == "failed");
        assert(malformed["last_success"] == successful_record);
        assert(resource_update::summary(malformed, local_version(), now + 3)["state"] == "check_failed");
    }

    // A persisted in-progress attempt is reported as interrupted after reopening.
    {
        Store store(root);
        auto cache = store.load();
        auto checking = resource_update::begin(cache, now + 4);
        store.save(checking);
    }
    Json recovered;
    {
        Store reopened(root);
        assert(reopened.locked);
        recovered = reopened.load();
        assert(recovered["last_success"]["remote"]["version"] == "10");
        assert(resource_update::summary(recovered, local_version(), now + 5)["state"] == "check_interrupted");
    }

    // Kill a real checker after it has durably saved "checking" but before fetch returns.
    const fs::path killed_root = fs::path(temporary.path) / "killed-check-state";
    Json before_kill;
    {
        Store baseline(killed_root);
        before_kill = check(baseline, local_version(), [] { return R"({"version":10})"; }, [=] { return now + 6; });
        assert(before_kill["last_attempt"]["status"] == "succeeded");
    }
    int ready_pipe[2] = {-1, -1};
    bool pipe_ready = pipe(ready_pipe) == 0;
    pid_t checker = pipe_ready ? fork() : -1;
    if (!pipe_ready || checker < 0) {
        if (ready_pipe[0] >= 0) close(ready_pipe[0]);
        if (ready_pipe[1] >= 0) close(ready_pipe[1]);
        assert(false && "failed to create interrupted-check child");
    }
    if (checker == 0) {
        close(ready_pipe[0]);
        try {
            Store child_store(killed_root);
            check(child_store, local_version(), [&]() -> std::string {
                const char ready = 'R';
                ssize_t written;
                do {
                    written = write(ready_pipe[1], &ready, 1);
                } while (written < 0 && errno == EINTR);
                if (written != 1) throw std::runtime_error("ready_pipe_write_failed");
                for (;;) pause();
            }, [=] { return now + 7; });
        } catch (...) {
            _exit(3);
        }
        _exit(4);
    }
    close(ready_pipe[1]);
    struct pollfd ready_event {ready_pipe[0], POLLIN | POLLHUP | POLLERR, 0};
    const auto poll_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    int poll_result = -1;
    do {
        auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(poll_deadline - std::chrono::steady_clock::now()).count();
        if (remaining < 0) remaining = 0;
        poll_result = poll(&ready_event, 1, static_cast<int>(remaining));
    } while (poll_result < 0 && errno == EINTR && std::chrono::steady_clock::now() < poll_deadline);
    char ready_byte = 0;
    ssize_t received = -1;
    if (poll_result > 0 && (ready_event.revents & POLLIN)) {
        do {
            received = read(ready_pipe[0], &ready_byte, 1);
        } while (received < 0 && errno == EINTR);
    }
    close(ready_pipe[0]);
    int child_status = 0;
    reap_child(checker, child_status);
    assert(poll_result > 0 && received == 1 && ready_byte == 'R');
    assert(WIFSIGNALED(child_status) && WTERMSIG(child_status) == SIGKILL);
    {
        Store after_kill(killed_root);
        assert(after_kill.locked);
        auto interrupted = after_kill.load();
        assert(interrupted["last_attempt"]["status"] == "checking");
        assert(resource_update::summary(interrupted, local_version(), now + 8)["state"] == "check_interrupted");
        assert(interrupted["last_success"] == before_kill["last_success"]);
    }

    // Corrupt JSON is rejected and remains byte-for-byte untouched on disk.
    const fs::path cache_path = root / "update-state.json";
    const std::string corrupt = R"({"schema":1,"kind":)";
    write_file(cache_path, corrupt);
    {
        Store store(root);
        assert(throws([&] { (void)store.load(); }));
    }
    assert(read_file(cache_path) == corrupt);

    // Refuse a symlink where the cache belongs, and refuse a symlink state root.
    const fs::path cache_link_root = fs::path(temporary.path) / "cache-link-root";
    fs::create_directory(cache_link_root);
    const fs::path cache_target = fs::path(temporary.path) / "cache-target.json";
    write_file(cache_target, resource_update::initial().dump());
    assert(::symlink(cache_target.c_str(), (cache_link_root / "update-state.json").c_str()) == 0);
    {
        Store store(cache_link_root);
        assert(throws([&] { (void)store.load(); }));
    }
    const fs::path real_state_root = fs::path(temporary.path) / "real-state-root";
    fs::create_directory(real_state_root);
    const fs::path state_root_link = fs::path(temporary.path) / "state-root-link";
    assert(::symlink(real_state_root.c_str(), state_root_link.c_str()) == 0);
    assert(throws([&] { Store store(state_root_link); }));

    // Missing local version metadata stays unavailable, never a no-update claim.
    const fs::path local_store = fs::path(temporary.path) / "local-store";
    const fs::path missing_legacy = fs::path(temporary.path) / "missing-legacy";
    auto missing_local = local(local_store, missing_legacy);
    assert(missing_local["version"].is_null());
    assert(resource_update::summary(recovered, missing_local, now + 5)["comparison"]["state"] == "comparison_unavailable");

    // A regular legacy fixture provides a readable local version.
    const fs::path fixture_legacy = fs::path(temporary.path) / "fixture-legacy";
    fs::create_directories(fixture_legacy / "Info");
    write_file(fixture_legacy / "Info/version.json", R"({"version":9})");
    auto fixture_local = local(local_store, fixture_legacy);
    assert(fixture_local["state"] == "read");
    assert(fixture_local["version"] == "9");

    std::puts("resource_manager: store locking, persistence, recovery, symlink safety, and local version passed");

    // Private cleanup tests
    {
        auto base = fs::temp_directory_path() / ("llc-private-cleanup-" + manager::token());
        auto root = base / "store", game = base / "game";
        fs::create_directories(root);
        auto put = [](const fs::path& p, const std::string& value = "x") {
            fs::create_directories(p.parent_path());
            std::ofstream(p, std::ios::binary) << value;
        };
        put(game / "releases/sentinel", "keep");
        {
            manager::Store store(root);
            auto transfer = manager::transfer_initial();
            transfer["prepared"] = {{"id", "candidate-9-9"}, {"key", "pack-key"}, {"plan", {{"version", "123"}, {"url", "https://github.com/LocalizeLimbusCompany/LocalizeLimbusCompany/releases/download/123/LimbusLocalize_123.zip"}, {"metadata_endpoint", "https://api.github.com/repos/LocalizeLimbusCompany/LocalizeLimbusCompany/releases/tags/123"}, {"source", "official_github_release_asset"}, {"name", "LimbusLocalize_123.zip"}, {"sha256", std::string(64, 'b')}, {"size", 1}, {"asset_id", 13}, {"release_id", 1}, {"updated_at", "2026-10-29T00:00:00Z"}}}};
            manager::save_transfer(store, transfer);
            put(root / "candidates/candidate-1-2/pack/a");
            put(root / "candidates/candidate-9-9/keep");
            put(root / "downloads/asset-12-aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.zip");
            put(root / "downloads/asset-13-bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb.zip");
            put(root / "downloads/asset-13-bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb.zip.part-3-4");
            put(root / "import-manifest-5-6.json");
            put(root / "unknown.tmp");
            fs::create_directories(root / "candidates/candidate-7-8");
            fs::create_symlink(game, root / "candidates/candidate-7-8/link");
            fs::create_directory(root / "releases");
            put(root / "releases/untouched");
            auto p = manager::cleanup_plan(store);
            assert(p["kind"] == "module_cleanup_plan" && p["token"].get<std::string>().size() == 64);
            {
                manager::Store another(root);
                assert(!another.locked);
            }
            assert(manager::cleanup_plan(store)["token"] == p["token"]);
            bool has_candidate = false, has_archive = false, has_prepared_archive = false, has_part = false, has_import = false;
            for (const auto& x : p["items"]) {
                auto id = x["id"].get<std::string>();
                has_candidate |= id == "candidates/candidate-1-2";
                has_archive |= id == "downloads/asset-12-aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.zip";
                has_prepared_archive |= id == "downloads/asset-13-bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb.zip";
                has_part |= id == "downloads/asset-13-bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb.zip.part-3-4";
                has_import |= id == "import-manifest-5-6.json";
            }
            assert(has_candidate && has_archive && !has_prepared_archive && has_part && has_import);
            auto stale = p["token"].get<std::string>();
            put(root / "unknown.tmp", "changed");
            bool expired = false;
            try {
                manager::cleanup_apply(store, stale);
            } catch (const std::exception& e) {
                expired = std::string(e.what()) == "cleanup_preview_changed";
            }
            assert(expired && fs::exists(root / "candidates/candidate-1-2/pack/a"));
            auto refreshed = manager::cleanup_plan(store);
            auto result = manager::cleanup_apply(store, refreshed["token"]);
            assert(result["phase"] == "succeeded");
            assert(!fs::exists(root / "candidates/candidate-1-2"));
            assert(fs::exists(root / "candidates/candidate-9-9/keep"));
            assert(fs::exists(root / "downloads/asset-13-bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb.zip"));
            assert(fs::exists(root / "candidates/candidate-7-8/link"));
            assert(fs::exists(root / "unknown.tmp") && fs::exists(root / "releases/untouched"));
            assert(fs::exists(game / "releases/sentinel"));
        }
        fs::remove_all(base);
    }

    std::puts("resource_manager: private_cleanup preview, token invalidation and apply passed");
    return 0;
}
