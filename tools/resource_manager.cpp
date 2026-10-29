// Standalone root manager. Never linked into the injected core.
#include "update_policy.hpp"
#include "../jni/core/resource_paths.hpp"
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
    Fd directory;
    Fd lock;
    bool locked = false;
    static int open_directory(const fs::path& path) {
        require(!mkdir(path.c_str(), 0700) || errno == EEXIST, "state_directory_create_failed");
        int fd = open(path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        if (fd < 0) throw std::runtime_error("state_directory_open_failed");
        struct stat st{};
        if (fstat(fd, &st) || st.st_uid != geteuid() || fchmod(fd, 0700)) { close(fd); throw std::runtime_error("unsafe_state_directory"); }
        return fd;
    }
    explicit Store(const fs::path& path) : directory(open_directory(path)),
        lock(openat(directory.value, ".update.lock", O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600)) {
        struct stat st{};
        require(!fstat(lock.value, &st) && S_ISREG(st.st_mode) && st.st_uid == geteuid(), "unsafe_state_lock");
        locked = !flock(lock.value, LOCK_EX | LOCK_NB);
        require(locked || errno == EWOULDBLOCK || errno == EAGAIN, "state_lock_failed");
    }
    Json load() const {
        int raw = openat(directory.value, "update-state.json", O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
        if (raw < 0 && errno == ENOENT) return initial();
        Fd fd(raw); struct stat st{};
        require(!fstat(fd.value, &st) && S_ISREG(st.st_mode) && st.st_uid == geteuid(), "unsafe_update_cache");
        auto cache = document(read_fd(fd.value, 65536)); validate_cache(cache); return cache;
    }
    void save(const Json& cache) const {
        require(locked, "update_check_busy"); validate_cache(cache);
        auto name = "update-state.next-" + token();
        Fd fd(openat(directory.value, name.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600));
        try {
            write_fd(fd.value, cache.dump(2) + "\n"); require(!fsync(fd.value), "state_fsync_failed");
            require(!renameat(directory.value, name.c_str(), directory.value, "update-state.json"), "state_rename_failed");
            require(!fsync(directory.value), "state_directory_fsync_failed");
        } catch (...) { unlinkat(directory.value, name.c_str(), 0); throw; }
    }
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
    const char* curl = "/system/bin/curl";
    struct stat st{};
    require(!stat(curl, &st) && S_ISREG(st.st_mode) && st.st_uid == 0 && !(st.st_mode & 0022) && !access(curl, X_OK), "https_client_unavailable");
    int pipefd[2]; require(!pipe2(pipefd, O_CLOEXEC), "https_pipe_failed");
    Fd input(pipefd[0]); Fd output(pipefd[1]);
    require(fcntl(input.value, F_SETFL, O_NONBLOCK) != -1, "https_pipe_flags_failed");
    pid_t child = fork(); require(child >= 0, "https_spawn_failed");
    if (!child) {
        int nullfd = open("/dev/null", O_RDWR);
        if (nullfd < 0 || dup2(nullfd, STDIN_FILENO) < 0 || dup2(nullfd, STDERR_FILENO) < 0 || dup2(output.value, STDOUT_FILENO) < 0) _exit(127);
        // -q disables curlrc. No insecure TLS flags, redirects, or shell parsing.
        execl(curl, curl, "-q", "--silent", "--show-error", "--fail", "--proto", "=https", "--connect-timeout", "5",
            "--max-time", "12", "--max-filesize", "65536", "--write-out", "\nLLC_HTTP_STATUS:%{http_code}", endpoint, static_cast<char*>(nullptr));
        _exit(127);
    }
    // Parent must close the pipe's write end or EOF would never be observable.
    close(output.value); output.value = -1;
    std::string bytes; int status = 0; bool reaped = false, ended = false;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(16);
    try {
        while (!reaped || !ended) {
            char buffer[4096]; ssize_t n;
            while ((n = read(input.value, buffer, sizeof(buffer))) > 0) {
                require(bytes.size() + static_cast<size_t>(n) <= 66000, "metadata_response_too_large"); bytes.append(buffer, n);
            }
            if (!n) ended = true;
            else require(errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR, "https_read_failed");
            if (!reaped) {
                auto result = waitpid(child, &status, WNOHANG);
                require(result >= 0 || errno == EINTR, "https_wait_failed"); reaped = result == child;
            }
            if (reaped && ended) break;
            require(std::chrono::steady_clock::now() < deadline, "https_timeout");
            pollfd p{input.value, POLLIN, 0}; poll(&p, 1, 100);
        }
    } catch (...) { if (!reaped) { kill(child, SIGKILL); while (waitpid(child, &status, 0) < 0 && errno == EINTR) {} } throw; }
    require(WIFEXITED(status) && WEXITSTATUS(status) == 0, WIFEXITED(status) && WEXITSTATUS(status) == 28 ? "https_timeout" : "https_request_failed");
    const std::string suffix = "\nLLC_HTTP_STATUS:200";
    require(bytes.size() >= suffix.size() && bytes.compare(bytes.size() - suffix.size(), suffix.size(), suffix) == 0, "https_status_not_200");
    bytes.resize(bytes.size() - suffix.size()); return bytes;
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
inline void request_runtime(const std::string& nonce) {
    Fd parent(open("/data/user/0/com.ProjectMoon.LimbusCompany/files", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
    struct stat st{}; require(!fstat(parent.value, &st), "runtime_directory_stat_failed");
    auto name = "llc_status_check.next-" + nonce;
    Fd fd(openat(parent.value, name.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600));
    try {
        write_fd(fd.value, nonce); require(!fchown(fd.value, st.st_uid, st.st_gid) && !fchmod(fd.value, 0600), "runtime_request_access_failed");
        require(!renameat(parent.value, name.c_str(), parent.value, "llc_status_check"), "runtime_request_rename_failed");
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
        return {{"state", "refresh_timeout"}, {"fresh_request", false}, {"report", report}};
    } catch (const std::exception&) { return unavailable; }
}
inline Json snapshot(const Json& cache, const Json& local_version, int64_t now, bool busy = false) {
    auto run = runtime();
    // The injected core does not own network state. Expose a single update
    // authority here instead of retaining its older offline placeholder.
    if (run["report"].is_object()) run["report"].erase("updates");
    return {{"schema", 1}, {"kind", "module_manager_status"}, {"generated_at_unix", now}, {"local_resources", local_version},
        {"updates", summary(cache, local_version, now, busy)}, {"runtime", run}};
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
    std::cout << "版本相同仍可能存在新文本缺译。\n此入口仅检查版本，不下载或替换资源。\n";
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

#ifndef LLC_MANAGER_TEST
int main(int argc, char** argv) {
    bool json = false; std::string command;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--json") json = true;
        else if (command.empty()) command = argv[i];
        else { std::cerr << "usage: llc-manager status|check [--json]\n"; return 2; }
    }
    if (command != "status" && command != "check") { std::cerr << "usage: llc-manager status|check [--json]\n"; return 2; }
    try {
        manager::require(geteuid() == 0, "root_required");
        manager::Store store(manager::state_root);
        auto local = manager::local(); auto cache = store.load();
        bool busy = !store.locked;
        auto now = [] { return static_cast<int64_t>(std::time(nullptr)); };
        if (command == "check" && !busy) cache = manager::check(store, local, manager::fetch_metadata, now);
        auto report = manager::snapshot(cache, manager::local(), now(), busy);
        manager::print(report, json);
        if (command == "check") return busy ? 3 : report["updates"]["state"] == "check_failed" ? 2 : 0;
        return 0;
    } catch (const std::exception& error) {
        if (json) std::cout << manager::Json({{"schema", 1}, {"kind", "module_manager_error"}, {"error", error.what()}}).dump() << '\n';
        else std::cerr << "无法读取或保存检查状态：" << error.what() << '\n';
        return 2;
    }
}
#endif
