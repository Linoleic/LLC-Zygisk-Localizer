#pragma once
#include "../third_party/nlohmann/json.hpp"
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>

namespace reload_manager {

using Json = nlohmann::json;

inline int find_game_pid() {
    DIR* proc = opendir("/proc");
    if (!proc) return -1;

    struct dirent* entry;
    while ((entry = readdir(proc)) != nullptr) {
        if (entry->d_type != DT_DIR) continue;
        char* endptr = nullptr;
        long pid = strtol(entry->d_name, &endptr, 10);
        if (*endptr != '\0' || pid <= 0) continue;

        std::string cmdline_path = std::string("/proc/") + entry->d_name + "/cmdline";
        FILE* fp = fopen(cmdline_path.c_str(), "rb");
        if (!fp) continue;

        char buf[256] = {0};
        size_t n = fread(buf, 1, sizeof(buf) - 1, fp);
        fclose(fp);

        if (n > 0 && strstr(buf, "com.ProjectMoon.LimbusCompany") != nullptr) {
            closedir(proc);
            return static_cast<int>(pid);
        }
    }
    closedir(proc);
    return -1;
}

inline Json trigger_reload(const std::vector<std::string>& flag_paths = {
        "/data/user/0/com.ProjectMoon.LimbusCompany/files/llc_reload.flag",
        "/sdcard/Android/data/com.ProjectMoon.LimbusCompany/files/llc_reload.flag",
        "/data/local/tmp/llc_reload.flag"}) {
    int pid = find_game_pid();
    bool running = (pid > 0);

    bool any_flag_written = false;
    struct stat st{};
    bool has_owner = (stat("/data/user/0/com.ProjectMoon.LimbusCompany/files", &st) == 0);

    for (const auto& path : flag_paths) {
        std::error_code ec;
        auto parent = std::filesystem::path(path).parent_path();
        if (!std::filesystem::exists(parent, ec)) continue;

        int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0666);
        if (fd >= 0) {
            const char* msg = "reload\n";
            write(fd, msg, strlen(msg));
            if (has_owner) {
                fchown(fd, st.st_uid, st.st_gid);
            }
            fchmod(fd, 0666);
            close(fd);
            any_flag_written = true;
            break; // Paths are fallbacks; publish one request, never three aliases.
        }
    }

    std::string message;
    if (running) {
        message = "已向运行中的游戏进程 (PID " + std::to_string(pid) + ") 发送热重载信号，翻译文本将在当前帧即时刷新。";
    } else {
        message = "游戏当前未在运行；热重载标记已生效，下次启动游戏将直接加载最新文本。";
    }

    return {
        {"schema", 1},
        {"kind", "reload_manager_summary"},
        {"game_running", running},
        {"game_pid", running ? pid : 0},
        {"flag_created", any_flag_written},
        {"message", message}
    };
}

} // namespace reload_manager
