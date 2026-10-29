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

namespace https_client {
using resource_update::require;
struct Fd {
    int value;
    explicit Fd(int fd) : value(fd) { require(fd >= 0, "https_pipe_failed"); }
    ~Fd() { if (value >= 0) close(value); }
    Fd(const Fd&) = delete;
};
using Sink = std::function<void(const char*, size_t)>;
inline uint64_t stream(const std::string& url, uint64_t limit, int seconds, bool redirects, const Sink& sink) {
    require(url.rfind("https://", 0) == 0 && limit > 0 && limit <= 64 * 1024 * 1024 && seconds > 0 && seconds <= 120, "invalid_https_request");
    const char* curl = "/system/bin/curl";
    struct stat st{};
    require(!stat(curl, &st) && S_ISREG(st.st_mode) && st.st_uid == 0 && !(st.st_mode & 0022) && !access(curl, X_OK), "https_client_unavailable");
    std::vector<std::string> args = {curl, "-q", "--silent", "--show-error", "--fail", "--proto", "=https", "--connect-timeout", "5",
        "--max-time", std::to_string(seconds), "--max-filesize", std::to_string(limit), "--write-out", "\nLLC_HTTP_STATUS:%{http_code}"};
    if (redirects) args.insert(args.end(), {"--location", "--max-redirs", "5", "--proto-redir", "=https"});
    args.insert(args.end(), {"--", url});
    std::vector<char*> argv; for (auto& argument : args) argv.push_back(argument.data()); argv.push_back(nullptr);
    int pipefd[2]; require(!pipe2(pipefd, O_CLOEXEC), "https_pipe_failed");
    Fd input(pipefd[0]), output(pipefd[1]);
    require(fcntl(input.value, F_SETFL, O_NONBLOCK) != -1, "https_pipe_flags_failed");
    pid_t child = fork(); require(child >= 0, "https_spawn_failed");
    if (!child) {
        int nullfd = open("/dev/null", O_RDWR);
        if (nullfd < 0 || dup2(nullfd, STDIN_FILENO) < 0 || dup2(nullfd, STDERR_FILENO) < 0 || dup2(output.value, STDOUT_FILENO) < 0) _exit(127);
        execv(curl, argv.data()); _exit(127);
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
        require(WIFEXITED(status) && WEXITSTATUS(status) == 0, WIFEXITED(status) && WEXITSTATUS(status) == 28 ? "https_timeout" : "https_request_failed");
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
