#pragma once
#include "https_client.hpp"
#include <sstream>

namespace game_info {
using resource_update::Json;
inline Json parse(const std::string& text) {
    Json result = {{"state", "unavailable"}, {"version", nullptr}, {"version_code", nullptr}};
    std::istringstream lines(text); std::string line, name, code;
    while (std::getline(lines, line)) {
        auto first = line.find_first_not_of(" \t\r");
        if (first == line.npos) continue;
        line.erase(0, first);
        if (line.rfind("versionName=", 0) == 0 && name.empty()) name = line.substr(12);
        if (line.rfind("versionCode=", 0) == 0 && code.empty()) code = line.substr(12, line.find(' ', 12) - 12);
    }
    if (!name.empty() && name.back() == '\r') name.pop_back();
    if (name.empty() || name == "null" || name.size() > 128 || name.find_first_of("\n\r\t") != name.npos ||
        code.empty() || code.size() > 18 || code.find_first_not_of("0123456789") != code.npos) return result;
    try { result = {{"state", "read"}, {"version", name}, {"version_code", std::stoll(code)}}; } catch (...) {}
    return result;
}
// Package-manager query is bounded and runs only in the standalone manager.
// Never infer an installed version from a possibly obsolete core fingerprint label.
inline Json installed() {
    auto unavailable = parse("");
#ifdef __ANDROID__
    int pipefd[2]; if (pipe2(pipefd, O_CLOEXEC)) return unavailable;
    https_client::Fd input(pipefd[0]), output(pipefd[1]);
    if (fcntl(input.value, F_SETFL, O_NONBLOCK) < 0) return unavailable;
    auto child = fork(); if (child < 0) return unavailable;
    if (!child) {
        int nullfd = open("/dev/null", O_RDWR);
        if (nullfd < 0 || dup2(nullfd, STDIN_FILENO) < 0 || dup2(nullfd, STDERR_FILENO) < 0 || dup2(output.value, STDOUT_FILENO) < 0) _exit(127);
        execl("/system/bin/dumpsys", "dumpsys", "-t", "1", "package", "com.ProjectMoon.LimbusCompany", static_cast<char*>(nullptr));
        _exit(127);
    }
    close(output.value); output.value = -1;
    std::string text; int status = 0; bool reaped = false, ended = false, valid = true;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1500);
    while (!reaped || !ended) {
        char buffer[8192]; ssize_t n;
        while ((n = read(input.value, buffer, sizeof(buffer))) > 0) {
            if (text.size() + n > 512 * 1024) { valid = false; break; }
            text.append(buffer, n);
        }
        if (!valid) break;
        if (!n) ended = true;
        else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) { valid = false; break; }
        auto waited = reaped ? child : waitpid(child, &status, WNOHANG);
        if (waited == child) reaped = true;
        else if (waited < 0 && errno != EINTR) { valid = false; break; }
        if (reaped && ended) break;
        if (std::chrono::steady_clock::now() >= deadline) { valid = false; break; }
        pollfd poller{input.value, POLLIN, 0}; poll(&poller, 1, 25);
    }
    if (!reaped) { kill(child, SIGKILL); while (waitpid(child, &status, 0) < 0 && errno == EINTR) {} }
    if (valid && WIFEXITED(status) && WEXITSTATUS(status) == 0) return parse(text);
#endif
    return unavailable;
}
}
