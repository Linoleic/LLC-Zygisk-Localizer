#include "security_boundary.hpp"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace fs = std::filesystem;

static void test_security_boundary_rejection() {
    auto test_root = fs::temp_directory_path() / ("llc_sec_test_" + std::to_string(getpid()));
    fs::create_directories(test_root);
    chmod(test_root.c_str(), 0755);

    // 1. Valid secure file
    auto secure_file = test_root / "libvalid.so";
    {
        std::ofstream f(secure_file);
        f << "ELF_DATA";
    }
    chmod(secure_file.c_str(), 0755);
    assert(llc::security::is_safe_file_or_candidate(secure_file.c_str(), test_root.c_str()));

    // 2. Reject world-writable candidate file (e.g. 0777 or 0666)
    auto world_writable = test_root / "libinsecure.so";
    {
        std::ofstream f(world_writable);
        f << "ELF_DATA";
    }
    chmod(world_writable.c_str(), 0777);
    assert(!llc::security::is_safe_file_or_candidate(world_writable.c_str(), test_root.c_str()));

    // 3. Reject symlink pointing to a legitimate file (symlink hijacking defense)
    auto symlink_path = test_root / "libsymlink.so";
    fs::create_symlink(secure_file, symlink_path);
    assert(!llc::security::is_safe_file_or_candidate(symlink_path.c_str(), test_root.c_str()));

    // 4. Reject directories
    auto sub_dir = test_root / "not_a_file.so";
    fs::create_directories(sub_dir);
    assert(!llc::security::is_safe_file_or_candidate(sub_dir.c_str(), test_root.c_str()));

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
    assert(!llc::security::is_safe_file_or_candidate(file_in_0777_dir.c_str(), test_root.c_str()));

    // 6. Parent directory properly secured with 0755 or 0700 -> ACCEPT
    chmod(world_writable_dir.c_str(), 0755);
    assert(llc::security::is_safe_file_or_candidate(file_in_0777_dir.c_str(), test_root.c_str()));

    // 7. Reject symlinked parent directory traversal
    auto symlink_dir = test_root / "staging_symlink_dir";
    fs::create_directory_symlink(world_writable_dir, symlink_dir);
    auto file_via_symlink_dir = symlink_dir / "libtarget.so";
    assert(!llc::security::is_safe_file_or_candidate(file_via_symlink_dir.c_str(), test_root.c_str()));

    fs::remove_all(test_root);
}

int main() {
    test_security_boundary_rejection();
    std::cout << "security_boundary: world-writable, symlink, directory, and parent permissions rejection passed" << std::endl;
    return 0;
}
