#pragma once
#include "update_policy.hpp"
#include <sys/stat.h>
#include <sys/wait.h>
#include <poll.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <chrono>
#include <functional>
#include <vector>
#include <cerrno>
#include <string>

namespace https_client {
using resource_update::require;
struct Fd {
    int value;
    explicit Fd(int fd) : value(fd) { require(fd >= 0, "https_pipe_failed"); }
    ~Fd() { if (value >= 0) close(value); }
    Fd(const Fd&) = delete;
};
using Sink = std::function<void(const char*, size_t)>;
inline bool trusted_file(const std::string& path, bool executable) {
    struct stat st{};
    return !lstat(path.c_str(), &st) && S_ISREG(st.st_mode) && st.st_uid == 0 && !(st.st_mode & 0022) &&
        !access(path.c_str(), executable ? X_OK : R_OK);
}
inline std::string helper_path() {
#ifdef __ANDROID__
    char path[4096];
    auto count = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (count > 0) {
        path[count] = 0;
        std::string executable(path);
        return executable.substr(0, executable.find_last_of('/')) + "/llc-https.jar";
    }
#endif
    return {};
}
inline std::string transport() {
    if (trusted_file("/system/bin/curl", true)) return "system_curl";
    auto helper = helper_path();
    if (!helper.empty() && trusted_file(helper, false) && !access("/system/bin/app_process", X_OK)) return "android_https";
    return "unavailable";
}
inline const char* exit_reason(int code) {
    switch (code) {
        case 3: return "https_invalid_url_or_proxy";
        case 6: return "https_dns_failed";
        case 7: return "https_connection_failed";
        case 22: return "https_http_error";
        case 28: return "https_timeout";
        case 35: return "https_tls_failed";
        case 47: return "https_redirect_refused";
        case 60: return "https_tls_verification_failed";
        case 63: return "https_response_too_large";
        default: return "https_request_failed";
    }
}
// Both transports end stdout with this fixed-width status trailer. Never
// interpret an HTTP error body as a successful metadata/archive response.
inline const char* http_reason(const std::string& trailer, const std::string& url) {
    if (trailer == "\nLLC_HTTP_STATUS:403")
        return url.rfind("https://api.github.com/", 0) == 0 ? "github_api_access_denied" : "https_access_denied";
    if (trailer == "\nLLC_HTTP_STATUS:429") return "https_rate_limited";
    if (trailer == "\nLLC_HTTP_STATUS:404") return "https_not_found";
    if (trailer.size() == 20 && trailer.rfind("\nLLC_HTTP_STATUS:5", 0) == 0) return "https_server_error";
    return "https_http_error";
}
inline uint64_t stream(const std::string& url, uint64_t limit, int seconds, bool redirects, const Sink& sink) {
    require(url.rfind("https://", 0) == 0 && limit > 0 && limit <= 64 * 1024 * 1024 && seconds > 0 && seconds <= 120, "invalid_https_request");
    auto selected = transport();
    require(selected != "unavailable", "https_client_unavailable");
    std::vector<std::string> args = {"/system/bin/curl", "-q", "--silent", "--show-error", "--fail", "--proto", "=https", "--connect-timeout", "5",
        "--max-time", std::to_string(seconds), "--max-filesize", std::to_string(limit), "--write-out", "\nLLC_HTTP_STATUS:%{http_code}"};
    if (redirects) args.insert(args.end(), {"--location", "--max-redirs", "5", "--proto-redir", "=https"});
    args.insert(args.end(), {"--", url});
    const auto helper = selected == "android_https" ? helper_path() : std::string{};
    if (!helper.empty()) args = {"/system/bin/app_process", "/system/bin", "llc.HttpsFetch", url, std::to_string(limit), std::to_string(seconds), redirects ? "1" : "0"};
    std::vector<char*> argv; for (auto& argument : args) argv.push_back(argument.data()); argv.push_back(nullptr);
    int pipefd[2]; require(!pipe2(pipefd, O_CLOEXEC), "https_pipe_failed");
    Fd input(pipefd[0]), output(pipefd[1]);
    require(fcntl(input.value, F_SETFL, O_NONBLOCK) != -1, "https_pipe_flags_failed");
    pid_t child = fork(); require(child >= 0, "https_spawn_failed");
    if (!child) {
        int nullfd = open("/dev/null", O_RDWR);
        if (nullfd < 0 || dup2(nullfd, STDIN_FILENO) < 0 || dup2(nullfd, STDERR_FILENO) < 0 || dup2(output.value, STDOUT_FILENO) < 0) _exit(127);
        if (!helper.empty() && setenv("CLASSPATH", helper.c_str(), 1)) _exit(127);
        execv(args[0].c_str(), argv.data()); _exit(127);
    }
    close(output.value); output.value = -1;
    const std::string suffix = "\nLLC_HTTP_STATUS:200";
    std::string tail; uint64_t emitted = 0; int status = 0; bool reaped = false, ended = false;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds + 4);
    try {
        while (!reaped || !ended) {
            char buffer[8192]; ssize_t n;
            while ((n = read(input.value, buffer, sizeof(buffer))) > 0) {
                tail.append(buffer, n);
                if (tail.size() > suffix.size()) {
                    size_t amount = tail.size() - suffix.size();
                    require(amount <= limit - emitted, "https_response_too_large");
                    sink(tail.data(), amount); emitted += amount; tail.erase(0, amount);
                }
            }
            if (!n) ended = true;
            else require(errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR, "https_read_failed");
            if (!reaped) {
                auto result = waitpid(child, &status, WNOHANG);
                require(result >= 0 || errno == EINTR, "https_wait_failed"); reaped = result == child;
            }
            if (reaped && ended) break;
            require(std::chrono::steady_clock::now() < deadline, "https_timeout");
            pollfd poller{input.value, POLLIN, 0}; poll(&poller, 1, 100);
        }
        if (WIFEXITED(status) && WEXITSTATUS(status) == 22)
            throw std::runtime_error(http_reason(tail, url));
        require(WIFEXITED(status) && WEXITSTATUS(status) == 0, WIFEXITED(status) ? exit_reason(WEXITSTATUS(status)) : "https_request_failed");
        require(tail == suffix, "https_status_not_200");
    } catch (...) {
        if (!reaped) { kill(child, SIGKILL); while (waitpid(child, &status, 0) < 0 && errno == EINTR) {} }
        throw;
    }
    return emitted;
}
inline std::string text(const std::string& url, size_t limit = 65536) {
    std::string result;
    stream(url, limit, 12, false, [&](const char* data, size_t size) { result.append(data, size); });
    return result;
}
} // namespace https_client
