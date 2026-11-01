#pragma once
#include "../jni/core/font_config.hpp"
#include <string>
#include <vector>
#include <unordered_set>
#include <filesystem>
#include <ctime>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

namespace font_manager {

using Json = nlohmann::json;

inline bool is_valid_unity_assetbundle(const std::string& filepath) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(filepath, ec)) return false;
    uintmax_t sz = std::filesystem::file_size(filepath, ec);
    if (sz < 16) return false;

    FILE* fp = fopen(filepath.c_str(), "rb");
    if (!fp) return false;
    char magic[8] = {0};
    size_t read_bytes = fread(magic, 1, 7, fp);
    fclose(fp);
    if (read_bytes < 7) return false;
    return (memcmp(magic, "UnityFS", 7) == 0);
}

inline void ensure_fonts_directories() {
    std::error_code ec;
    std::filesystem::create_directories(font_config::fonts_dir_user, ec);
    std::filesystem::create_directories(font_config::fonts_dir_sdcard, ec);

    struct stat st{};
    if (stat("/data/user/0/com.ProjectMoon.LimbusCompany/files", &st) == 0) {
        chmod(font_config::fonts_dir_user, 0755);
        chown(font_config::fonts_dir_user, st.st_uid, st.st_gid);

        chmod(font_config::fonts_dir_sdcard, 0775);
        chown(font_config::fonts_dir_sdcard, st.st_uid, st.st_gid);
    }
}

inline bool save_config(const std::string& font_name, const std::string& font_path);

inline Json summary() {
    ensure_fonts_directories();

    Json result = {
        {"schema", 1},
        {"kind", "font_manager_summary"},
        {"active_font", "默认思源黑体"},
        {"is_custom", false},
        {"available", Json::array()}
    };

    auto sel = font_config::resolve_font_selection();
    result["active_font"] = sel.is_custom ? sel.selected_name : "默认思源黑体";
    result["is_custom"] = sel.is_custom;

    // 1. Always list Default font
    uintmax_t default_size = 0;
    std::error_code ec;
    if (std::filesystem::is_regular_file(font_config::default_font_path, ec)) {
        default_size = std::filesystem::file_size(font_config::default_font_path, ec);
    }
    result["available"].push_back({
        {"name", "默认思源黑体"},
        {"path", "default"},
        {"size_bytes", default_size},
        {"is_default", true},
        {"is_active", !sel.is_custom}
    });

    // 2. Scan fonts directories
    const std::vector<std::string> search_dirs = {
        font_config::fonts_dir_sdcard,
        font_config::fonts_dir_user
    };

    std::unordered_set<std::string> seen_names;
    for (const auto& dir : search_dirs) {
        if (!std::filesystem::is_directory(dir, ec)) continue;
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
            if (!entry.is_regular_file(ec)) continue;
            auto path = entry.path();
            auto ext = path.extension().string();
            // Match .bundle, .ab, .unity3d, or general bundle files (skip .json, .txt, etc.)
            if (ext == ".json" || ext == ".txt" || ext == ".flag" || ext == ".tmp") continue;

            std::string filename = path.filename().string();
            if (seen_names.count(filename)) continue;
            seen_names.insert(filename);

            uintmax_t sz = entry.file_size(ec);
            bool is_active = sel.is_custom && (sel.font_path == path.string() || sel.selected_name == filename);

            result["available"].push_back({
                {"name", filename},
                {"path", path.string()},
                {"size_bytes", sz},
                {"is_default", false},
                {"is_active", is_active}
            });
        }
    }

    // 3. Scan candidate download directories for importable font bundles
    result["importable"] = Json::array();
    const std::vector<std::string> download_dirs = {
        "/sdcard/Download",
        "/sdcard/Download/fonts",
        "/sdcard/Download/LLC",
        "/storage/emulated/0/Download",
        "/storage/emulated/0/Download/fonts"
    };

    std::unordered_set<std::string> seen_download_names;
    for (const auto& ddir : download_dirs) {
        if (!std::filesystem::is_directory(ddir, ec)) continue;
        for (const auto& entry : std::filesystem::directory_iterator(ddir, ec)) {
            if (!entry.is_regular_file(ec)) continue;
            auto p = entry.path();
            auto ext = p.extension().string();
            if (ext != ".bundle" && ext != ".ab" && ext != ".unity3d") continue;

            std::string fname = p.filename().string();
            if (seen_names.count(fname)) continue;
            if (seen_download_names.count(fname)) continue;

            std::string canon = p.lexically_normal().string();
            if (!is_valid_unity_assetbundle(canon)) continue;

            seen_download_names.insert(fname);
            uintmax_t sz = entry.file_size(ec);
            result["importable"].push_back({
                {"name", fname},
                {"path", canon},
                {"size_bytes", sz}
            });
        }
    }

    return result;
}

inline Json import_font(const std::string& source_path) {
    ensure_fonts_directories();
    std::error_code ec;

    std::string resolved = source_path;
    if (resolved.find('/') == std::string::npos) {
        std::string candidate = "/sdcard/Download/" + resolved;
        if (std::filesystem::is_regular_file(candidate, ec)) {
            resolved = candidate;
        }
    }

    if (!std::filesystem::is_regular_file(resolved, ec)) {
        return {{"schema", 1}, {"kind", "module_manager_error"}, {"error", "source_font_file_not_found"}};
    }
    if (!is_valid_unity_assetbundle(resolved)) {
        return {{"schema", 1}, {"kind", "module_manager_error"}, {"error", "invalid_unity_assetbundle_format"}};
    }

    std::filesystem::path src(resolved);
    std::string filename = src.filename().string();
    std::filesystem::path dest = std::filesystem::path(font_config::fonts_dir_sdcard) / filename;

    std::filesystem::copy_file(src, dest, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        dest = std::filesystem::path(font_config::fonts_dir_user) / filename;
        ec.clear();
        std::filesystem::copy_file(src, dest, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            return {{"schema", 1}, {"kind", "module_manager_error"}, {"error", "copy_font_failed: " + ec.message()}};
        }
    }

    struct stat st{};
    if (stat("/data/user/0/com.ProjectMoon.LimbusCompany/files", &st) == 0) {
        chmod(dest.c_str(), 0664);
        chown(dest.c_str(), st.st_uid, st.st_gid);
    }

    // Automatically set as active font upon successful import
    save_config(filename, dest.string());

    auto res = summary();
    res["imported_font"] = filename;
    return res;
}

inline bool save_config(const std::string& font_name, const std::string& font_path) {
    int parent_fd = open("/data/user/0/com.ProjectMoon.LimbusCompany/files", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (parent_fd < 0) return false;

    struct stat st{};
    if (fstat(parent_fd, &st) != 0) {
        close(parent_fd);
        return false;
    }

    std::string tmp_name = "font_config.json.next-" + std::to_string(getpid()) + "-" + std::to_string(std::time(nullptr));
    int fd = openat(parent_fd, tmp_name.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0644);
    if (fd < 0) {
        close(parent_fd);
        return false;
    }

    Json j = {
        {"schema", 1},
        {"selected_font", font_name},
        {"font_path", font_path},
        {"updated_at_unix", static_cast<int64_t>(std::time(nullptr))}
    };
    std::string bytes = j.dump(2) + "\n";

    size_t written = 0;
    while (written < bytes.size()) {
        auto n = write(fd, bytes.data() + written, bytes.size() - written);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        written += n;
    }

    bool ok = (written == bytes.size());
    if (ok && fchown(fd, st.st_uid, st.st_gid) != 0) ok = false;
    if (ok && fchmod(fd, 0644) != 0) ok = false;
    if (ok && fsync(fd) != 0) ok = false;
    close(fd);

    if (ok && renameat(parent_fd, tmp_name.c_str(), parent_fd, "font_config.json") != 0) ok = false;
    if (!ok) unlinkat(parent_fd, tmp_name.c_str(), 0);
    close(parent_fd);
    return ok;
}

inline Json set_font(const std::string& target) {
    if (target.empty() || target == "default" || target == "默认思源黑体") {
        save_config("default", "default");
        return summary();
    }

    const std::vector<std::string> search_dirs = {
        font_config::fonts_dir_sdcard,
        font_config::fonts_dir_user
    };

    std::string found_path;
    std::string found_name;

    std::error_code ec;
    if (std::filesystem::is_regular_file(target, ec)) {
        found_path = target;
        found_name = std::filesystem::path(target).filename().string();
    } else {
        for (const auto& dir : search_dirs) {
            auto candidate = std::filesystem::path(dir) / target;
            if (std::filesystem::is_regular_file(candidate, ec)) {
                found_path = candidate.string();
                found_name = target;
                break;
            }
        }
    }

    if (found_path.empty()) {
        return {{"schema", 1}, {"kind", "module_manager_error"}, {"error", "font_file_not_found"}};
    }

    struct stat st{};
    if (stat("/data/user/0/com.ProjectMoon.LimbusCompany/files", &st) == 0) {
        chmod(found_path.c_str(), 0644);
        chown(found_path.c_str(), st.st_uid, st.st_gid);
    }

    save_config(found_name, found_path);
    return summary();
}

} // namespace font_manager
