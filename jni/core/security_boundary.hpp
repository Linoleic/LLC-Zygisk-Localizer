#pragma once

#include <climits>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>

namespace llc::security {

inline bool is_safe_file_or_candidate(const char* path, const char* tmp_prefix = "/data/local/tmp/") {
#ifdef _WIN32
    (void)path;
    (void)tmp_prefix;
    return true;
#else
    if (!path || !*path) return false;

    struct stat st{};
    if (lstat(path, &st) != 0) return false;
    if (!S_ISREG(st.st_mode)) return false;

    uid_t euid = geteuid();
    if (st.st_uid != 0 && st.st_uid != euid) return false;
    if (st.st_mode & 0002) return false;

    if (tmp_prefix && *tmp_prefix) {
        size_t prefix_len = strlen(tmp_prefix);
        if (strncmp(path, tmp_prefix, prefix_len) == 0) {
            std::string_view p(path);
            size_t last_slash = p.find_last_of('/');
            if (last_slash != std::string_view::npos && last_slash > 0) {
                std::string parent(p.substr(0, last_slash));
                struct stat pst{};
                if (lstat(parent.c_str(), &pst) != 0 || !S_ISDIR(pst.st_mode)) return false;
                if (pst.st_uid != 0 && pst.st_uid != euid) return false;
                if (pst.st_mode & 0002) return false;

                char resolved_parent[PATH_MAX];
                if (!realpath(parent.c_str(), resolved_parent)) return false;
                struct stat rpst{};
                if (lstat(resolved_parent, &rpst) != 0 || !S_ISDIR(rpst.st_mode)) return false;
                if (rpst.st_uid != 0 && rpst.st_uid != euid) return false;
                if (rpst.st_mode & 0002) return false;
            }
        }
    }
    return true;
#endif
}

} // namespace llc::security
