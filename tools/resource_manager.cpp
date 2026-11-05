// Standalone root manager. Never linked into the injected core.
#include "update_policy.hpp"
#include "https_client.hpp"
#include "game_info.hpp"
#include "../jni/core/resource_paths.hpp"
#include "../jni/core/build_info.hpp"
#include <sys/file.h>
#include <sys/wait.h>
#include <poll.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <chrono>
#include <fstream>
#include <sstream>
#include <iostream>
#include <functional>
#include <ctime>
#include <cerrno>

namespace manager {
using namespace resource_update;
namespace fs = std::filesystem;
inline constexpr const char* state_root = "/data/adb/llc_zygisk";
inline constexpr const char* package = "com.ProjectMoon.LimbusCompany";
inline constexpr const char* runtime_path = "/sdcard/Android/data/com.ProjectMoon.LimbusCompany/files/llc_status.json";
struct Fd {
    int value;
    explicit Fd(int fd) : value(fd) { require(fd >= 0, "file_open_failed"); }
    ~Fd() { if (value >= 0) close(value); }
    Fd(const Fd&) = delete;
};
inline std::string token() {
    return std::to_string(getpid()) + "-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
}
inline std::string read_fd(int fd, size_t limit) {
    std::string result; char bytes[8192];
    while (true) {
        auto n = read(fd, bytes, sizeof(bytes));
        if (n < 0 && errno == EINTR) continue;
        require(n >= 0, "file_read_failed");
        if (!n) return result;
        require(result.size() + static_cast<size_t>(n) <= limit, "file_too_large");
        result.append(bytes, n);
    }
}
inline void write_fd(int fd, const std::string& bytes) {
    size_t done = 0;
    while (done < bytes.size()) {
        auto n = write(fd, bytes.data() + done, bytes.size() - done);
        if (n < 0 && errno == EINTR) continue;
        require(n > 0, "file_write_failed"); done += n;
    }
}
inline std::string read_path(const std::string& path, size_t limit) {
    Fd fd(open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK));
    struct stat st{};
    require(!fstat(fd.value, &st) && S_ISREG(st.st_mode), "not_regular_file");
    return read_fd(fd.value, limit); // /proc files legitimately have st_size == 0.
}
struct Store {
    fs::path root;
    Fd directory;
    Fd lock;
    bool locked = false;
    static int open_directory(const fs::path& path) {
        require(!mkdir(path.c_str(), 0700) || errno == EEXIST, "state_directory_create_failed");
        int fd = open(path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        if (fd < 0) throw std::runtime_error("state_directory_open_failed");
        struct stat st{};
        if (fstat(fd, &st) || st.st_uid != geteuid() || ((st.st_mode & 07777) != 0700 && fchmod(fd, 0700))) { close(fd); throw std::runtime_error("unsafe_state_directory"); }
        return fd;
    }
    explicit Store(const fs::path& path) : root(path), directory(open_directory(path)),
        lock(openat(directory.value, ".update.lock", O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600)) {
        struct stat st{};
        require(!fstat(lock.value, &st) && S_ISREG(st.st_mode) && st.st_uid == geteuid(), "unsafe_state_lock");
        locked = !flock(lock.value, LOCK_EX | LOCK_NB);
        require(locked || errno == EWOULDBLOCK || errno == EAGAIN, "state_lock_failed");
    }
    Json read_document(const char* name, const Json& fallback, size_t limit = 65536) const {
        require(resource_pack::identifier(name), "invalid_state_filename");
        int raw = openat(directory.value, name, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
        if (raw < 0 && errno == ENOENT) return fallback;
        Fd fd(raw); struct stat st{};
        require(!fstat(fd.value, &st) && S_ISREG(st.st_mode) && st.st_uid == geteuid(), "unsafe_update_cache");
        auto bytes = read_fd(fd.value, limit);
        if (limit <= 65536) return document(bytes);
        require(!bytes.empty() && bytes.find('\0') == bytes.npos, "invalid_large_state_document");
        auto doc = Json::parse(bytes, nullptr, false);
        require(doc.is_object(), "invalid_large_state_document");
        return doc;
    }
    Json load() const { auto cache = read_document("update-state.json", initial()); validate_cache(cache); return cache; }
    void write_document(const char* filename, const Json& cache) const {
        require(locked && resource_pack::identifier(filename), "update_check_busy_or_invalid_filename");
        auto name = "update-state.next-" + token();
        Fd fd(openat(directory.value, name.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600));
        try {
            write_fd(fd.value, cache.dump(2) + "\n"); require(!fsync(fd.value), "state_fsync_failed");
            require(!renameat(directory.value, name.c_str(), directory.value, filename), "state_rename_failed");
            require(!fsync(directory.value), "state_directory_fsync_failed");
        } catch (...) { unlinkat(directory.value, name.c_str(), 0); throw; }
    }
    void save(const Json& cache) const { validate_cache(cache); write_document("update-state.json", cache); }
};

inline Json local(const fs::path& store = resource_paths::store_root, const fs::path& legacy = resource_paths::legacy_root) {
    Json result = {{"state", "unavailable"}, {"version", nullptr}, {"scope", "selected_on_disk_for_next_launch"}};
    try {
        auto selected = resource_pack::select(store, legacy);
        result["root"] = selected.root; result["mode"] = selected.mode; result["pack_key"] = selected.key;
        auto raw = resource_pack::read_bytes(fs::path(selected.root) / "Info/version.json", 16384);
        auto doc = document(raw); require(doc.contains("version"), "missing_local_version");
        result["version"] = version(doc["version"]); result["state"] = "read";
        result["version_metadata_sha256"] = resource_pack::digest(raw);
    } catch (const std::exception&) { result["error"] = "local_selection_or_version_unavailable"; }
    return result;
}

inline std::string fetch_metadata() {
    return https_client::text(endpoint);
}
using Fetch = std::function<std::string()>;
inline Json check(Store& store, const Json& local_version, const Fetch& fetch, const std::function<int64_t()>& now) {
    require(store.locked, "update_check_busy");
    auto checking = begin(store.load(), now()); store.save(checking);
    Json next;
    try { next = succeeded(checking, metadata(fetch()), local_version, now()); }
    catch (const std::exception& error) { next = failed(checking, error.what(), now()); }
    store.save(next); return next;
}

inline std::string start_ticks(int pid) {
    auto stat = read_path("/proc/" + std::to_string(pid) + "/stat", 8192);
    auto offset = stat.rfind(") "); require(offset != stat.npos, "invalid_proc_stat");
    std::istringstream fields(stat.substr(offset + 2)); std::string value;
    for (int index = 0; index <= 19; ++index) require(static_cast<bool>(fields >> value), "invalid_proc_start");
    return value;
}
inline bool runtime_identity(const Json& report) {
    try {
        if (!report.is_object() || report.value("schema", Json()) != 1 || report.value("kind", Json()) != "localization_status" ||
            !report.contains("pid") || !report["pid"].is_number_integer() || !report.contains("process_start_ticks") || !report["process_start_ticks"].is_string()) return false;
        int64_t number = report["pid"].get<int64_t>(); if (number <= 0 || number > INT32_MAX) return false;
        int pid = static_cast<int>(number); auto prefix = "/proc/" + std::to_string(pid);
        auto cmdline = read_path(prefix + "/cmdline", 8192); auto end = cmdline.find('\0');
        if (end != cmdline.npos) cmdline.resize(end);
        return cmdline == package && start_ticks(pid) == report["process_start_ticks"].get<std::string>() &&
            read_path(prefix + "/maps", 4 * 1024 * 1024).find("libllc_core.so") != std::string::npos;
    } catch (const std::exception&) { return false; }
}
inline void request_runtime(const std::string& nonce, const char* destination = "llc_status_check") {
    Fd parent(open("/data/user/0/com.ProjectMoon.LimbusCompany/files", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
    struct stat st{}; require(!fstat(parent.value, &st), "runtime_directory_stat_failed");
    auto name = "llc_status_check.next-" + nonce;
    Fd fd(openat(parent.value, name.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600));
    try {
        write_fd(fd.value, nonce); require(!fchown(fd.value, st.st_uid, st.st_gid) && !fchmod(fd.value, 0600), "runtime_request_access_failed");
        require(!renameat(parent.value, name.c_str(), parent.value, destination), "runtime_request_rename_failed");
    } catch (...) { unlinkat(parent.value, name.c_str(), 0); throw; }
}
inline Json runtime() {
    Json unavailable = {{"state", "game_not_running_or_report_unavailable"}, {"fresh_request", false}, {"report", nullptr}};
    try {
        auto report = Json::parse(read_path(runtime_path, 1024 * 1024));
        if (!runtime_identity(report)) return unavailable;
        auto stage = report.at("runtime").at("stage").get<std::string>();
        if (stage != "running") return {{"state", stage}, {"fresh_request", false}, {"report", report}};
        auto nonce = token(); request_runtime(nonce);
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(2500);
        while (std::chrono::steady_clock::now() < deadline) {
            auto current = Json::parse(read_path(runtime_path, 1024 * 1024), nullptr, false);
            if (current.is_object() && current.value("request_id", Json()) == nonce && current["pid"] == report["pid"] &&
                current["process_start_ticks"] == report["process_start_ticks"] && runtime_identity(current))
                return {{"state", "report_available"}, {"fresh_request", true}, {"report", current}};
            usleep(100000);
        }
        if (!runtime_identity(report)) return unavailable;
        return {{"state", "refresh_timeout"}, {"fresh_request", false}, {"identity_verified", true}, {"report", report}};
    } catch (const std::exception&) { return unavailable; }
}
inline Json installed_module(const fs::path& path = "/data/adb/modules/llc_zygisk/module.prop") {
    Json result = {{"state", "unavailable"}, {"version", nullptr}, {"version_code", nullptr}};
    try {
        std::istringstream lines(read_path(path.string(), 16384)); std::string line, release, code;
        while (std::getline(lines, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.rfind("version=", 0) == 0) release = line.substr(8);
            if (line.rfind("versionCode=", 0) == 0) code = line.substr(12);
        }
        require(!release.empty() && release.size() <= 96 && release.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._-") == std::string::npos,
            "invalid_module_version");
        require(!code.empty() && code.size() <= 10 && code.find_first_not_of("0123456789") == std::string::npos, "invalid_module_version_code");
        auto number = std::stoll(code); require(number <= INT32_MAX, "invalid_module_version_code");
        result = {{"state", "read"}, {"version", release}, {"version_code", number}};
    } catch (const std::exception&) {}
    return result;
}
inline Json snapshot(const Json& cache, const Json& local_version, int64_t now, bool busy = false) {
    auto run = runtime();
    // The injected core does not own network state. Expose a single update
    // authority here instead of retaining its older offline placeholder.
    if (run["report"].is_object()) run["report"].erase("updates");
    return {{"schema", 1}, {"kind", "module_manager_status"}, {"generated_at_unix", now}, {"local_resources", local_version},
        {"module", installed_module()}, {"game", game_info::installed()}, {"manager_build", build_info::json()}, {"updates", summary(cache, local_version, now, busy)}, {"runtime", run}};
}
inline std::string label(const std::string& state) {
    if (state == "no_update") return "按版本号暂未发现更新";
    if (state == "update_available") return "有新版本汉化资源";
    if (state == "local_ahead") return "本地版本高于当前上游版本，不建议降级";
    if (state == "check_failed") return "检查失败，保留上次成功结果";
    if (state == "check_interrupted") return "上次检查中断，请重新检查";
    if (state == "checking") return "另一次检查正在进行";
    if (state == "manager_busy") return "管理工具正忙，请稍后重试";
    if (state == "stale_result") return "上次检查结果已过期，请重新检查";
    if (state == "comparison_unavailable") return "已取得上游版本，本地版本无法比较";
    return "尚未检查";
}
inline void print(const Json& report, bool json) {
    if (json) { std::cout << report.dump(2, ' ', false, Json::error_handler_t::replace) << '\n'; return; }
    const auto& update = report["updates"];
    auto local_version = report["local_resources"].value("version", Json());
    std::cout << "LLC 汉化资源\n已安装资源版本：" << (local_version.is_null() ? "无法读取" : version(local_version)) << '\n'
        << "检查结果：" << label(update["state"]) << '\n';
    if (!update["last_success"].is_null()) {
        const auto& success = update["last_success"];
        auto timestamp = success["checked_at_unix"].get<int64_t>(); time_t seconds = timestamp; tm date{}; char when[64] = {};
        gmtime_r(&seconds, &date); strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S UTC", &date);
        std::cout << "上游版本：" << version(success["remote"]["version"]) << "\n上次成功检查：" << when << '\n';
    }
    std::cout << "版本相同仍可能存在新文本缺译。\n检查不改变资源；下载、激活需要分别执行。\n";
    auto& run = report["runtime"];
    auto runtime_state = run.value("state", std::string{});
    std::string runtime_label = "游戏未运行或报告不可用；资源检查可独立使用";
    if (run["fresh_request"] == true) runtime_label = "已取得当前进程报告";
    else if (runtime_state == "disabled") runtime_label = "核心修改已停用，请查看运行兼容状态";
    else if (runtime_state == "initializing") runtime_label = "核心正在初始化，请稍后查询";
    else if (runtime_state == "resource_unavailable") runtime_label = "核心未找到可用资源";
    else if (runtime_state == "api_unavailable") runtime_label = "核心运行接口不可用";
    else if (runtime_state == "refresh_timeout") runtime_label = "运行报告刷新超时，请稍后查询";
    std::cout << "游戏状态：" << runtime_label << '\n';
    if (run["fresh_request"] == true && run["report"].contains("coverage") && run["report"]["coverage"].is_object()) {
        const auto& coverage = run["report"]["coverage"];
        std::cout << "已观察到的本地缺译 ID：" << coverage.value("missing_local_rows", 0u) << "（不是全游戏总量）\n";
        if (coverage.value("state", std::string{}) == "module_coverage_limited") std::cout << "部分源字段在游戏类型中没有对应字段，属于适配范围限制。\n";
        else if (coverage.value("state", std::string{}) == "module_issue") std::cout << "运行报告存在模块异常，详见状态查询结果。\n";
    }
}
} // namespace manager

#include "network_manager.hpp"
#include "resource_download.hpp"
#include "manager_ui.hpp"
#include "private_cleanup.hpp"
#include "font_manager.hpp"
#include "language_manager.hpp"
#include "reload_manager.hpp"
#include "text_schema_manager.hpp"

#ifndef LLC_MANAGER_TEST
int main(int argc, char** argv) {
    bool json = false; std::string command, operand, operand2, operand3;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--json") json = true;
        else if (command.empty()) command = argv[i];
        else if (operand.empty()) operand = argv[i];
        else if (operand2.empty()) operand2 = argv[i];
        else if (operand3.empty()) operand3 = argv[i];
        else { std::cerr << "too many arguments\n"; return 2; }
    }
    if (command != "status" && command != "check" && command != "download" && command != "import-current" &&
        command != "activate" && command != "rollback" && command != "network" && command != "progress" &&
        command != "cleanup-plan" && command != "cleanup" && command != "fonts" && command != "set-font" &&
        command != "language" && command != "set-language" && command != "import-font" && command != "reload" &&
        command != "upload-chunk" && command != "detect-proxy" && command != "discover") {
        std::cerr << "usage: llc-manager status|progress|discover|check|download|import-current|activate [key]|rollback [key]|network system|port [mirror] [url]|cleanup-plan|cleanup token|fonts|set-font [name]|import-font [path] [target_name]|upload-chunk [token:first] [base64]|reload|language|set-language [mode] [lang]|detect-proxy [--json]\n"; return 2;
    }
    try {
        manager::require(geteuid() == 0, "root_required");
        manager::require(operand.empty() || command == "activate" || command == "rollback" || command == "network" || command == "cleanup" || command == "set-font" || command == "set-language" || command == "import-font" || command == "upload-chunk", "unexpected_operand");
        if (!operand.empty() && (command == "activate" || command == "rollback")) manager::require(resource_pack::identifier(operand), "invalid_target_key");
        if (command == "fonts") {
            auto res = font_manager::summary();
            std::cout << res.dump(json ? -1 : 2) << '\n';
            return 0;
        }
        if (command == "set-font") {
            manager::require(!operand.empty(), "font_name_required");
            auto res = font_manager::set_font(operand);
            std::cout << res.dump(json ? -1 : 2) << '\n';
            return res.value("kind", std::string{}) == "module_manager_error" ? 2 : 0;
        }
        if (command == "import-font") {
            manager::require(!operand.empty(), "font_path_required");
            auto res = font_manager::import_font(operand, operand2);
            std::cout << res.dump(json ? -1 : 2) << '\n';
            return res.value("kind", std::string{}) == "module_manager_error" ? 2 : 0;
        }
        if (command == "upload-chunk") {
            manager::require(!operand.empty(), "token_required");
            auto colon = operand.find(':');
            manager::require(colon != std::string::npos, "invalid_chunk_header");
            std::string token = operand.substr(0, colon);
            bool is_first = (operand.substr(colon + 1) == "1");
            auto res = font_manager::append_upload_chunk(token, is_first, operand2);
            std::cout << res.dump(json ? -1 : 2) << '\n';
            return res.value("kind", std::string{}) == "module_manager_error" ? 2 : 0;
        }
        if (command == "reload") {
            auto res = reload_manager::trigger_reload();
            std::cout << res.dump(json ? -1 : 2) << '\n';
            return 0;
        }
        if (command == "language") {
            auto res = language_manager::summary();
            std::cout << res.dump(json ? -1 : 2) << '\n';
            return 0;
        }
        if (command == "set-language") {
            manager::require(!operand.empty(), "language_mode_required");
            std::string mode = operand;
            std::string lang = operand2;
            if (mode == "kr" || mode == "en" || mode == "jp") {
                lang = mode;
                mode = "selective";
            }
            auto res = language_manager::set_language(mode, lang);
            std::cout << res.dump(json ? -1 : 2) << '\n';
            return 0;
        }
        if (command == "detect-proxy") {
            auto res = manager::detect_proxy();
            std::cout << res.dump(json ? -1 : 2) << '\n';
            return 0;
        }
        manager::Store store(manager::state_root);
        if (command == "progress") { std::cout << manager::progress(store).dump() << '\n'; return 0; }
        if (!store.locked && (command == "network" || command == "cleanup-plan" || command == "cleanup" || command == "discover")) {
            if (json) std::cout << manager::Json({{"schema", 1}, {"kind", "module_manager_error"}, {"error", "manager_busy"}, {"busy", true}}).dump() << '\n';
            else std::cerr << "另一个管理操作正在进行，本次操作未执行。\n";
            return 3;
        }
        if (command == "cleanup-plan" || command == "cleanup") {
            manager::require(store.locked, "manager_busy");
            manager::require(command != "cleanup" || resource_pack::digest_valid(operand), "invalid_cleanup_token");
            auto result = command == "cleanup-plan" ? manager::cleanup_plan(store) : manager::cleanup_apply(store, operand);
            std::cout << result.dump(2) << '\n'; return result.value("phase", std::string{}) == "failed" ? 2 : 0;
        }
        if (command == "network") {
            manager::require(!operand.empty(), "network_mode_required");
            manager::require(store.locked, "manager_busy"); manager::save_network(store, operand, operand2, operand3);
        }
        auto network = manager::network_summary(store);
        if (command == "check" || command == "download") manager::apply_network(store);
        auto cache = store.load();
        bool busy = !store.locked;
        auto now = [] { return static_cast<int64_t>(std::time(nullptr)); };
        if (command == "check" && !busy) cache = manager::check(store, manager::local(), manager::fetch_metadata, now);
        bool resource_operation = command == "download" || command == "import-current" || command == "activate" || command == "rollback";
        if (resource_operation && !busy) manager::operate(store, command, {}, operand);
        if (resource_operation) cache = store.load();
        auto report = manager::snapshot(cache, manager::local(), now(), busy);
        report["capabilities"] = {{"text_schema_audit", 1}};
        bool schema_failed = false;
        try {
            auto schema = manager::schema_cache(store);
            auto attempt = store.read_document("text-schema-attempt.json", manager::Json::object());
            const auto& run = report["runtime"];
            const auto identity = run.value("fresh_request", false) ? run["report"]["compatibility"].value("il2cpp_sha256", std::string{}) : std::string{};
            const auto capability = manager::schema_capability(run);
            const auto catalog = capability.is_object() ? capability.value("catalog_signature", std::string{}) : std::string{};
            const bool cooldown = !attempt.value("error", std::string{}).empty() && attempt.value("native_sha256", std::string{}) == identity &&
                attempt.value("catalog_signature", std::string{}) == catalog && now() - attempt.value("at_unix", int64_t(0)) < 300;
            const bool automatic = command == "status" && store.locked && !cooldown && manager::schema_automatic_ready(run) && manager::schema_needs_refresh(schema, run, report["game"]);
            std::string error;
            if (command == "discover" || automatic) {
                try { manager::discover_schema(store, run, report["game"]); schema = manager::schema_cache(store); }
                catch (const std::exception& issue) { error = issue.what(); schema_failed = command == "discover"; }
                if (store.locked) store.write_document("text-schema-attempt.json", {{"native_sha256", identity}, {"catalog_signature", catalog}, {"at_unix", now()}, {"error", error}});
            }
            report["text_schema"] = manager::schema_summary(schema, report["game"]);
            report["text_schema"]["requires_refresh"] = manager::schema_needs_refresh(schema, run, report["game"]);
            if (!error.empty()) report["text_schema"]["last_error"] = error;
            else if (command != "discover" && !automatic && cooldown && !attempt.value("error", std::string{}).empty()) report["text_schema"]["last_error"] = attempt["error"];
            if (schema_failed) report["error"] = error;
        } catch (const std::exception& issue) {
            report["text_schema"] = {{"state", "cache_unavailable"}, {"last_error", issue.what()}};
            schema_failed = command == "discover";
            if (schema_failed) report["error"] = issue.what();
        }
        report["command"] = command; report["busy"] = busy;
        report["network"] = network; report["resource_store"] = manager::managed_summary();
        report["cleanup"] = manager::cleanup_history(store);
        report["fonts"] = font_manager::summary();
        report["language"] = language_manager::summary();
        report["updates"]["download_implemented"] = true;
        report["downloads"] = manager::transfer_summary(manager::read_transfer(store), report["local_resources"], busy);
        manager::print(report, json);
        if (!json && resource_operation && busy) std::cout << "另一个管理操作正在进行，本次资源操作未执行。\n";
        if (!json && resource_operation && !busy) {
            std::cout << "资源操作：" << command << " / " << report["downloads"]["state"].get<std::string>() << '\n';
            if (report["downloads"]["state"] == "succeeded") std::cout << (command == "download" ? "候选包已校验并暂存，尚未激活。\n" : "磁盘资源状态已提交，运行中游戏不会强制重载；下次冷启动选用。\n");
            else if (report["downloads"]["last_attempt"].is_object()) std::cout << "原因：" << report["downloads"]["last_attempt"].value("error", manager::Json()).dump() << '\n';
        }
        if (resource_operation) return busy ? 3 : report["downloads"]["state"] == "succeeded" ? 0 : 2;
        if (command == "check") return busy ? 3 : report["updates"]["state"] == "check_failed" ? 2 : 0;
        if (command == "discover") return schema_failed ? 2 : 0;
        return 0;
    } catch (const std::exception& error) {
        if (json) std::cout << manager::Json({{"schema", 1}, {"kind", "module_manager_error"}, {"error", error.what()}}).dump() << '\n';
        else std::cerr << "无法读取或保存检查状态：" << error.what() << '\n';
        return 2;
    }
}
#endif
