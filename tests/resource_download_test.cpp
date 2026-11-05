#define LLC_MANAGER_TEST
#include "../tools/resource_manager.cpp"

#include <cassert>
#include <cstring>
#include <fstream>
#include <iostream>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {
namespace fs = std::filesystem;

void check(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

template <typename Function>
void expect_failure(Function function) {
    bool failed = false;
    try { function(); } catch (const std::exception&) { failed = true; }
    check(failed, "operation unexpectedly succeeded");
}

template <typename Function>
void expect_error(Function function, const std::string& expected) {
    try { function(); }
    catch (const std::exception& error) {
        check(error.what() == expected, "unexpected error: " + std::string(error.what()) + "; expected: " + expected);
        return;
    }
    throw std::runtime_error("operation unexpectedly succeeded; expected: " + expected);
}

std::string read_file(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    check(input.good(), "cannot open fixture file: " + path.string());
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

void write_file(const fs::path& path, const std::string& content) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    check(output.good(), "cannot write fixture file: " + path.string());
    output.write(content.data(), static_cast<std::streamsize>(content.size()));
    output.close();
    check(output.good(), "fixture write failed: " + path.string());
}

struct Fixture {
    fs::path root;
    Fixture() {
        char pattern[] = "/data/local/tmp/llc-download-tests-XXXXXX";
        char* created = mkdtemp(pattern);
        check(created != nullptr, "mkdtemp failed under /data/local/tmp");
        root = created;
        struct stat st{};
        check(root.string().rfind("/data/local/tmp/llc-download-tests-", 0) == 0 &&
            lstat(root.c_str(), &st) == 0 && S_ISDIR(st.st_mode) && !S_ISLNK(st.st_mode),
            "fixture root is not a real directory under the required prefix");
    }
    ~Fixture() {
        struct stat st{};
        const auto value = root.string();
        if (value.rfind("/data/local/tmp/llc-download-tests-", 0) == 0 &&
            lstat(root.c_str(), &st) == 0 && S_ISDIR(st.st_mode) && !S_ISLNK(st.st_mode)) {
            std::error_code ignored;
            fs::remove_all(root, ignored);
        }
    }
};

manager::Json load_plan_metadata(const fs::path& path) {
    return manager::Json::parse(read_file(path));
}

void child_kill_at(const manager::DownloadContext& context, const fs::path& state_root,
                   const char* checkpoint) {
    {
        manager::Store store(state_root);
        auto state = manager::read_transfer(store);
        state["last_attempt"] = {{"operation", "activate"}, {"phase", "working"},
            {"started_at_unix", std::time(nullptr)}, {"finished_at_unix", nullptr}, {"error", nullptr}};
        manager::save_transfer(store, state);
    }
    const pid_t child = fork();
    check(child >= 0, "fork failed");
    if (child == 0) {
        try {
            manager::Store store(state_root);
            const auto state = manager::read_transfer(store);
            manager::activate(store, state, context, [checkpoint](const char* reached) {
                if (std::strcmp(reached, checkpoint) == 0) kill(getpid(), SIGKILL);
            });
            _exit(90); // The requested transaction checkpoint was not reached.
        } catch (...) { _exit(91); }
    }
    int status = 0;
    check(waitpid(child, &status, 0) == child, "waitpid failed");
    check(WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL, "child did not die at the requested checkpoint");
}

} // namespace

int main(int argc, char** argv) {
    try {
        check(argc == 3, "usage: resource_download_test <archive.zip> <release-metadata.json>");
        const fs::path archive_path(argv[1]);
        const auto release_doc = load_plan_metadata(argv[2]);
        auto plan = release_plan::parse(release_doc.dump(), release_doc.at("tag_name").get<std::string>());
        Fixture fixture;

        manager::DownloadContext context;
        context.private_root = fixture.root / "private";
        fs::create_directory(context.private_root);
        const auto state_root = context.private_root;
        const auto app_files = fixture.root / "fake-app-files";
        fs::create_directories(app_files / "Lang");
        context.app_files = app_files;
        context.packs = app_files / "Lang" / ".llc-packs";

        {
            manager::Store store(state_root);
            auto state = manager::transfer_initial();
            state["last_attempt"] = {{"operation", "download"}, {"phase", "working"},
                {"started_at_unix", std::time(nullptr)}, {"finished_at_unix", nullptr}, {"error", nullptr}};
            manager::save_transfer(store, state);
            state["prepared"] = manager::prepare_verified(store, state, context, plan, archive_path);
            manager::save_transfer(store, state);
            check(state["prepared"]["plan"] == plan, "prepared candidate did not retain its verified plan");
            check(state["prepared"]["files"].get<size_t>() > 0, "candidate manifest is empty");
            context.legacy = context.private_root / "candidates" / state["prepared"]["id"].get<std::string>() / "pack";
            check(fs::is_directory(context.legacy), "prepared legacy fixture is missing");
        }

        manager::Json imported;
        {
            manager::Store store(state_root);
            imported = manager::operate(store, "import-current", context);
        }
        check(imported["last_attempt"]["phase"] == "succeeded", "import-current failed: " + imported.dump());
        std::string baseline_key;
        {
            manager::Store store(state_root);
            baseline_key = resource_pack::read_state(context.packs).active;
            check(!baseline_key.empty(), "import-current did not create the rollback baseline");
        }

        auto activated = [&]() {
            manager::Store store(state_root);
            return manager::operate(store, "activate", context);
        };
        auto result = activated();
        check(result["last_attempt"]["phase"] == "succeeded", "first activation failed: " + result.dump());
        const auto candidate_key = result["prepared"]["key"].get<std::string>();
        check(resource_pack::read_state(context.packs).active == candidate_key, "candidate did not become active");
        check(resource_pack::read_state(context.packs).previous == baseline_key, "baseline was not retained for rollback");

        {
            manager::Store store(state_root);
            auto before = read_file(context.packs / "state.json");
            auto wrong_activation = manager::operate(store, "activate", context, "different-reviewed-key");
            check(wrong_activation["last_attempt"]["error"] == "prepared_candidate_changed", "activation ignored the reviewed candidate");
            auto wrong_rollback = manager::operate(store, "rollback", context, candidate_key);
            check(wrong_rollback["last_attempt"]["error"] == "rollback_target_changed", "rollback ignored the reviewed target");
            check(read_file(context.packs / "state.json") == before, "rejected UI target changed active resources");
        }

        {
            manager::Store store(state_root);
            auto rollback = manager::operate(store, "rollback", context, baseline_key);
            check(rollback["last_attempt"]["phase"] == "succeeded", "rollback failed: " + rollback.dump());
        }
        check(resource_pack::read_state(context.packs).active == baseline_key, "rollback did not restore baseline");
        result = activated();
        check(result["last_attempt"]["phase"] == "succeeded", "second activation failed: " + result.dump());
        check(resource_pack::read_state(context.packs).active == candidate_key, "second activation did not restore candidate");
        check(resource_pack::read_state(context.packs).previous == baseline_key, "second activation lost rollback target");
        result = activated();
        check(result["last_attempt"]["phase"] == "succeeded", "idempotent activation failed: " + result.dump());
        check(resource_pack::read_state(context.packs).active == candidate_key &&
            resource_pack::read_state(context.packs).previous == baseline_key,
            "idempotent activation changed active or previous key");

        const auto pack_state_path = context.packs / "state.json";
        const auto state_before_guard = read_file(pack_state_path);
        const auto current_pack_state = resource_pack::read_state(context.packs);
        resource_pack::State wrong_expected_state = current_pack_state;
        wrong_expected_state.previous = "guard-state-mismatch";
        const auto prepared_state = [&]() {
            manager::Store store(state_root);
            return manager::read_transfer(store);
        }();
        const auto candidate_directory = state_root / "candidates" / prepared_state["prepared"]["id"].get<std::string>();
        pack_tool::Access app_access(context.app_files);
        expect_error([&] {
            pack_tool::install(context.legacy, (candidate_directory / "manifest.json").string(), context.packs,
                {}, &app_access, &wrong_expected_state);
        }, "resource activation state changed");
        check(read_file(pack_state_path) == state_before_guard, "failed expected_state guard changed pack state.json");

        const auto active_state_before_rejection = read_file(pack_state_path);
        fs::path mutable_translation;
        std::string valid_translation;
        const auto candidate_pack = context.legacy;
        for (const auto& file : fs::recursive_directory_iterator(candidate_pack)) {
            if (!file.is_regular_file()) continue;
            const auto relative = file.path().lexically_relative(candidate_pack).generic_string();
            if (resource_pack::translation(relative) && relative != "Info/version.json") {
                mutable_translation = file.path(); valid_translation = read_file(mutable_translation); break;
            }
        }
        check(!mutable_translation.empty(), "no non-Info translation file found to corrupt");
        write_file(mutable_translation, "corrupt candidate contents");
        result = activated();
        check(result["last_attempt"]["phase"] == "failed", "activation accepted a modified candidate");
        check(read_file(pack_state_path) == active_state_before_rejection, "modified candidate changed active state");
        write_file(mutable_translation, valid_translation);

        const auto unchanged_active_state = read_file(pack_state_path);
        auto bad_digest_plan = plan; bad_digest_plan["sha256"] = std::string(64, '0');
        {
            manager::Store store(state_root); auto state = manager::read_transfer(store);
            expect_failure([&] { manager::prepare_verified(store, state, context, bad_digest_plan, archive_path); });
        }
        auto bad_size_plan = plan; bad_size_plan["size"] = plan["size"].get<uint64_t>() + 1;
        {
            manager::Store store(state_root); auto state = manager::read_transfer(store);
            expect_failure([&] { manager::prepare_verified(store, state, context, bad_size_plan, archive_path); });
        }
        manager::Json official_zip_asset;
        for (const auto& asset : release_doc.at("assets")) {
            if (asset.value("name", std::string{}) == plan.at("name").get<std::string>()) {
                official_zip_asset = asset;
                break;
            }
        }
        check(official_zip_asset.is_object(), "official ZIP asset not found in release metadata");
        auto newer_release = release_doc;
        const auto next_version = std::to_string(std::stoull(plan.at("version").get<std::string>()) + 1);
        newer_release["tag_name"] = next_version;
        official_zip_asset["name"] = "LimbusLocalize_" + next_version + ".zip";
        official_zip_asset["browser_download_url"] = std::string("https://github.com/") + release_plan::repository +
            "/releases/download/" + next_version + "/LimbusLocalize_" + next_version + ".zip";
        newer_release["assets"] = manager::Json::array({official_zip_asset});
        check(official_zip_asset["size"] == plan["size"] &&
            official_zip_asset["digest"].get<std::string>() == "sha256:" + plan["sha256"].get<std::string>(),
            "version mismatch fixture did not preserve the ZIP asset size and digest");
        const auto mismatched_version_plan = release_plan::parse(newer_release.dump(), next_version);
        {
            manager::Store store(state_root); auto state = manager::read_transfer(store);
            expect_error([&] { manager::prepare_verified(store, state, context, mismatched_version_plan, archive_path); },
                "archive_resource_version_mismatch");
        }
        check(read_file(pack_state_path) == unchanged_active_state, "invalid archive plans changed active state");

        {
            manager::Store store(state_root); auto state = manager::read_transfer(store);
            state["prepared"]["id"] = "../outside";
            expect_failure([&] { manager::save_transfer(store, state); });
            state = manager::read_transfer(store); state["prepared"]["id"] = "candidate-../../escape";
            expect_failure([&] { manager::save_transfer(store, state); });
        }
        check(read_file(pack_state_path) == unchanged_active_state, "malformed prepared identity changed active state");

        // Leave the legacy baseline active before the two crash-point checks.
        {
            manager::Store store(state_root);
            const auto rollback = manager::operate(store, "rollback", context);
            check(rollback["last_attempt"]["phase"] == "succeeded", "pre-crash rollback failed");
        }
        check(resource_pack::read_state(context.packs).active == baseline_key, "pre-crash baseline was not restored");

        child_kill_at(context, state_root, "before_state");
        check(resource_pack::read_state(context.packs).active == baseline_key, "before_state crash changed active release");
        {
            manager::Store store(state_root);
            const auto transfer = manager::read_transfer(store);
            const auto summary = manager::transfer_summary(transfer, manager::local(context.packs, context.legacy), false);
            check(summary["state"] == "operation_interrupted", "before_state crash was not reported interrupted");
            check(summary["prepared"]["key"] == candidate_key, "before_state crash lost prepared candidate");
        }

        child_kill_at(context, state_root, "committed");
        check(resource_pack::read_state(context.packs).active == candidate_key, "committed crash did not leave candidate active");
        {
            manager::Store store(state_root);
            const auto transfer = manager::read_transfer(store);
            const auto summary = manager::transfer_summary(transfer, manager::local(context.packs, context.legacy), false);
            check(summary["state"] == "activation_committed", "committed crash was not reported committed");
            check(summary["prepared"]["key"] == candidate_key, "committed crash lost prepared candidate");
        }

        std::cout << "resource_download: prepared official ZIP, imported baseline, activated/rolled back/retried, rejected corrupt inputs, and recovered both SIGKILL checkpoints\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "resource_download_test: " << error.what() << '\n';
        return 1;
    }
}
