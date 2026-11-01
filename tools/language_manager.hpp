#pragma once

#include <string>
#include <vector>
#include <ctime>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include "../jni/core/language_config.hpp"

namespace language_manager {

using Json = nlohmann::json;

inline Json summary() {
    auto sel = language_config::resolve_language_selection();

    Json result = {
        {"schema", 1},
        {"kind", "language_manager_summary"},
        {"mode", sel.mode},
        {"target_language", sel.target_language},
        {"target_language_name", language_config::int_to_lang_name(language_config::lang_code_to_int(sel.target_language))},
        {"available_languages", Json::array({
            {{"code", "kr"}, {"name", "韩语 (KR - 游戏默认)"}},
            {{"code", "en"}, {"name", "英语 (EN)"}},
            {{"code", "jp"}, {"name", "日语 (JP)"}}
        })}
    };
    return result;
}

inline bool save_config(const std::string& mode, const std::string& target_lang) {
    int parent_fd = open("/data/user/0/com.ProjectMoon.LimbusCompany/files", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (parent_fd < 0) return false;

    struct stat st{};
    if (fstat(parent_fd, &st) != 0) {
        close(parent_fd);
        return false;
    }

    std::string tmp_name = "language_config.json.next-" + std::to_string(getpid()) + "-" + std::to_string(std::time(nullptr));
    int fd = openat(parent_fd, tmp_name.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0644);
    if (fd < 0) {
        close(parent_fd);
        return false;
    }

    Json j = {
        {"schema", 1},
        {"mode", mode},
        {"target_language", target_lang},
        {"updated_at_unix", static_cast<int64_t>(std::time(nullptr))}
    };

    std::string content = j.dump(2);
    ssize_t written = write(fd, content.data(), content.size());
    bool ok = (written == static_cast<ssize_t>(content.size()));
    if (fsync(fd) != 0) ok = false;

    // Chown to game app UID/GID so game process can read it
    if (fchown(fd, st.st_uid, st.st_gid) != 0) {
        // Non-fatal if running in test environment
    }
    if (fchmod(fd, 0644) != 0) {
        // Non-fatal
    }

    close(fd);

    if (ok) {
        if (renameat(parent_fd, tmp_name.c_str(), parent_fd, "language_config.json") != 0) {
            unlinkat(parent_fd, tmp_name.c_str(), 0);
            ok = false;
        }
    } else {
        unlinkat(parent_fd, tmp_name.c_str(), 0);
    }

    close(parent_fd);
    return ok;
}

inline Json set_language(const std::string& mode, const std::string& target_lang = "kr") {
    std::string safe_mode = (mode == "selective") ? "selective" : "all";
    std::string safe_lang = target_lang;
    if (safe_lang != "kr" && safe_lang != "en" && safe_lang != "jp") {
        safe_lang = "kr";
    }

    save_config(safe_mode, safe_lang);
    return summary();
}

} // namespace language_manager
