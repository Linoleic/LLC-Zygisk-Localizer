#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>

namespace fs = std::filesystem;

// The security policy under test
static bool is_safe_library_candidate(const char* path, const char* tmp_prefix) {
    struct stat st{};
    if (lstat(path, &st) != 0) return false;
    if (!S_ISREG(st.st_mode)) return false;
    uid_t euid = geteuid();
    if (st.st_uid != 0 && st.st_uid != euid) return false;
    if (st.st_mode & 0002) return false;

    size_t prefix_len = strlen(tmp_prefix);
    if (strncmp(path, tmp_prefix, prefix_len) == 0) {
        std::string_view p(path);
        size_t last_slash = p.find_last_of('/');
        if (last_slash != std::string_view::npos && last_slash > 0) {
            std::string parent(p.substr(0, last_slash));
            struct stat pst{};
            if (lstat(parent.c_str(), &pst) != 0 || !S_ISDIR(pst.st_mode)) return false;
            if (pst.st_uid != 0 && pst.st_uid != euid) return false;
            if (pst.st_mode & 0002) return false; // Parent must NOT be world-writable
        }
    }
    return true;
}

static void test_security_boundary_rejection() {
    auto test_root = fs::temp_directory_path() / ("llc_sec_test_" + std::to_string(getpid()));
    fs::create_directories(test_root);

    // 1. Valid secure file
    auto secure_file = test_root / "libvalid.so";
    {
        std::ofstream f(secure_file);
        f << "ELF_DATA";
    }
    chmod(secure_file.c_str(), 0755);
    assert(is_safe_library_candidate(secure_file.c_str(), test_root.c_str()));

    // 2. Reject world-writable candidate file (e.g. 0777 or 0666)
    auto world_writable = test_root / "libinsecure.so";
    {
        std::ofstream f(world_writable);
        f << "ELF_DATA";
    }
    chmod(world_writable.c_str(), 0777);
    assert(!is_safe_library_candidate(world_writable.c_str(), test_root.c_str()));

    // 3. Reject symlink pointing to a legitimate file (symlink hijacking defense)
    auto symlink_path = test_root / "libsymlink.so";
    fs::create_symlink(secure_file, symlink_path);
    assert(!is_safe_library_candidate(symlink_path.c_str(), test_root.c_str()));

    // 4. Reject directories
    auto sub_dir = test_root / "not_a_file.so";
    fs::create_directories(sub_dir);
    assert(!is_safe_library_candidate(sub_dir.c_str(), test_root.c_str()));

    // 5. Parent directory world-writable rejection:
    // If staging directory is 0777 (as previously in /data/local/tmp/llc), it MUST be rejected!
    auto world_writable_dir = test_root / "staging_0777";
    fs::create_directories(world_writable_dir);
    chmod(world_writable_dir.c_str(), 0777);

    auto file_in_0777_dir = world_writable_dir / "libtarget.so";
    {
        std::ofstream f(file_in_0777_dir);
        f << "ELF_DATA";
    }
    chmod(file_in_0777_dir.c_str(), 0755);
    // Even though file is 0755, parent directory is 0777 -> MUST REJECT!
    assert(!is_safe_library_candidate(file_in_0777_dir.c_str(), test_root.c_str()));

    // 6. Parent directory properly secured with 0755 or 0700 -> ACCEPT
    chmod(world_writable_dir.c_str(), 0755);
    assert(is_safe_library_candidate(file_in_0777_dir.c_str(), test_root.c_str()));

    fs::remove_all(test_root);
}

int main() {
    test_security_boundary_rejection();
    std::cout << "security_boundary: world-writable, symlink, directory, and parent permissions rejection passed" << std::endl;
    return 0;
}
