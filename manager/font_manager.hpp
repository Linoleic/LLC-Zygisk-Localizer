#pragma once
#include "../jni/core/font_config.hpp"
#include "seven_zip.hpp"
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

inline std::vector<uint8_t> base64_decode(const std::string& in) {
    std::vector<uint8_t> out;
    std::vector<int> T(256, -1);
    for (int i = 0; i < 64; i++) {
        T["ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"[i]] = i;
    }
    int val = 0, valb = -8;
    for (uint8_t c : in) {
        if (T[c] == -1) {
            if (c == '=') break;
            continue;
        }
        val = (val << 6) + T[c];
        valb += 6;
        if (valb >= 0) {
            out.push_back(static_cast<uint8_t>((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}

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
    const bool ready = is_valid_unity_assetbundle(sel.font_path);
    result["ready"] = ready;
    result["readiness_scope"] = "readable_assetbundle_header_not_runtime_load";
    if (!ready) result["active_font"] = nullptr;

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
        {"is_active", !sel.is_custom && ready}
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

inline Json append_upload_chunk(const std::string& token, bool is_first, const std::string& base64_chunk) {
    if (token.empty() || token.find('/') != std::string::npos || token.find("..") != std::string::npos) {
        return {{"schema", 1}, {"kind", "module_manager_error"}, {"error", "invalid_upload_token"}};
    }
    std::string tmp_path = "/data/local/tmp/llc_font_" + token + ".tmp";
    int flags = O_WRONLY | O_CREAT | O_CLOEXEC | (is_first ? O_TRUNC : O_APPEND);
    int fd = open(tmp_path.c_str(), flags, 0666);
    if (fd < 0) {
        return {{"schema", 1}, {"kind", "module_manager_error"}, {"error", "cannot_open_temp_file"}};
    }
    auto bytes = base64_decode(base64_chunk);
    if (!bytes.empty()) {
        write(fd, bytes.data(), bytes.size());
    }
    close(fd);
    return {{"schema", 1}, {"kind", "font_upload_chunk_summary"}, {"token", token}, {"bytes_written", bytes.size()}};
}

inline Json import_font(const std::string& source_path, const std::string& target_filename = "") {
    ensure_fonts_directories();
    std::error_code ec;

    std::string resolved = source_path;
    if (resolved.find('/') == std::string::npos) {
        const std::vector<std::string> search_dirs = {
            "/sdcard/Download",
            "/sdcard/Download/fonts",
            "/sdcard/Download/LLC",
            "/sdcard/Documents",
            "/sdcard",
            "/storage/emulated/0/Download",
            "/storage/emulated/0/Download/fonts",
            "/storage/emulated/0/Documents"
        };
        for (const auto& sdir : search_dirs) {
            auto candidate = (std::filesystem::path(sdir) / resolved).string();
            if (std::filesystem::is_regular_file(candidate, ec)) {
                resolved = candidate;
                break;
            }
        }
    }

    if (!std::filesystem::is_regular_file(resolved, ec)) {
        return {{"schema", 1}, {"kind", "module_manager_error"}, {"error", "source_font_file_not_found"}};
    }
    if (!is_valid_unity_assetbundle(resolved)) {
        return {{"schema", 1}, {"kind", "module_manager_error"}, {"error", "invalid_unity_assetbundle_format"}};
    }

    std::filesystem::path src(resolved);
    std::string filename = target_filename.empty() ? src.filename().string() : target_filename;
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

    // Clean up temporary upload file if applicable
    if (src.string().rfind("/data/local/tmp/llc_font_", 0) == 0) {
        std::filesystem::remove(src, ec);
    }

    // Adapt to Android platform if imported bundle was built for PC/Windows
    font_config::adapt_assetbundle_to_android(dest.string());

    struct stat st{};
    if (stat("/data/user/0/com.ProjectMoon.LimbusCompany/files", &st) == 0) {
        chmod(dest.c_str(), 0664);
        chown(dest.c_str(), st.st_uid, st.st_gid);
    }

    std::filesystem::path backup_dir = "/data/local/tmp/llc";
    std::filesystem::create_directories(backup_dir, ec);
    if (!ec) {
        chmod(backup_dir.c_str(), 0755);
    }
    std::filesystem::copy_file(dest, backup_dir / "tmpchinesefont", std::filesystem::copy_options::overwrite_existing, ec);
    chmod((backup_dir / "tmpchinesefont").c_str(), 0644);

    std::filesystem::path mod_backup_dir = "/data/adb/modules/llc_zygisk/backup";
    if (std::filesystem::exists("/data/adb/modules/llc_zygisk", ec)) {
        std::filesystem::create_directories(mod_backup_dir, ec);
        if (!ec) {
            chmod(mod_backup_dir.c_str(), 0755);
            std::filesystem::copy_file(dest, mod_backup_dir / "tmpchinesefont", std::filesystem::copy_options::overwrite_existing, ec);
            chmod((mod_backup_dir / "tmpchinesefont").c_str(), 0644);
        }
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

    // Ensure selected font is adapted to Android
    font_config::adapt_assetbundle_to_android(found_path);

    struct stat st{};
    if (stat("/data/user/0/com.ProjectMoon.LimbusCompany/files", &st) == 0) {
        chmod(found_path.c_str(), 0644);
        chown(found_path.c_str(), st.st_uid, st.st_gid);
    }

    save_config(found_name, found_path);
    return summary();
}

inline constexpr const char* default_font_url = "https://github.com/LocalizeLimbusCompany/LLC_ChineseFontAsset/releases/download/250216/tmpchinesefont_BIE_250216.7z";

inline Json download_font(manager::Store& store, const std::string& input_url = "") {
    ensure_fonts_directories();
    manager::apply_network(store);
    std::string url = input_url.empty() ? default_font_url : input_url;
    std::string download_url = manager::mirror_download_url(url, manager::read_network(store));

    std::string tmp_dir = "/data/local/tmp";
    std::string tmp_download = tmp_dir + "/llc_font_dl_" + std::to_string(getpid()) + ".tmp";
    std::string tmp_extracted = tmp_dir + "/llc_font_out_" + std::to_string(getpid()) + ".tmp";

    unlink(tmp_download.c_str());
    unlink(tmp_extracted.c_str());

    uint64_t received = 0;
    try {
        int fd = open(tmp_download.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0666);
        if (fd < 0) {
            return {{"schema", 1}, {"kind", "module_manager_error"}, {"error", "cannot_open_temp_file"}};
        }
        https_client::stream(download_url, 200 * 1024 * 1024, 300, true,
            [&](const char* data, size_t amount) {
                write(fd, data, amount);
                received += amount;
            });
        close(fd);
    } catch (const std::exception& e) {
        unlink(tmp_download.c_str());
        return {{"schema", 1}, {"kind", "module_manager_error"}, {"error", e.what()}};
    } catch (...) {
        unlink(tmp_download.c_str());
        return {{"schema", 1}, {"kind", "module_manager_error"}, {"error", "font_download_failed"}};
    }

    std::string final_bundle_path = tmp_download;

    if (seven_zip::is_7z_archive(tmp_download)) {
        bool ok = seven_zip::extract_file(tmp_download, "tmpchinesefont", tmp_extracted);
        if (!ok) {
            ok = seven_zip::extract_file(tmp_download, "", tmp_extracted);
        }
        unlink(tmp_download.c_str());
        if (!ok) {
            unlink(tmp_extracted.c_str());
            return {{"schema", 1}, {"kind", "module_manager_error"}, {"error", "7z_extract_failed"}};
        }
        final_bundle_path = tmp_extracted;
    }

    if (!is_valid_unity_assetbundle(final_bundle_path)) {
        unlink(final_bundle_path.c_str());
        return {{"schema", 1}, {"kind", "module_manager_error"}, {"error", "invalid_unity_assetbundle_format"}};
    }

    // Adapt to Android platform if downloaded bundle was built for PC/Windows
    font_config::adapt_assetbundle_to_android(final_bundle_path);

    std::string target_name = "SourceHanSans.bundle";
    std::filesystem::path dest = std::filesystem::path(font_config::fonts_dir_sdcard) / target_name;
    std::error_code ec;
    std::filesystem::copy_file(final_bundle_path, dest, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        dest = std::filesystem::path(font_config::fonts_dir_user) / target_name;
        ec.clear();
        std::filesystem::copy_file(final_bundle_path, dest, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            unlink(final_bundle_path.c_str());
            return {{"schema", 1}, {"kind", "module_manager_error"}, {"error", "copy_font_failed: " + ec.message()}};
        }
    }
    unlink(final_bundle_path.c_str());

    font_config::adapt_assetbundle_to_android(dest.string());

    struct stat st{};
    if (stat("/data/user/0/com.ProjectMoon.LimbusCompany/files", &st) == 0) {
        chmod(dest.c_str(), 0664);
        chown(dest.c_str(), st.st_uid, st.st_gid);
    }

    std::filesystem::path backup_dir = "/data/local/tmp/llc";
    std::filesystem::create_directories(backup_dir, ec);
    if (!ec) {
        chmod(backup_dir.c_str(), 0755);
    }
    std::filesystem::copy_file(dest, backup_dir / "tmpchinesefont", std::filesystem::copy_options::overwrite_existing, ec);
    chmod((backup_dir / "tmpchinesefont").c_str(), 0644);

    std::filesystem::path mod_backup_dir = "/data/adb/modules/llc_zygisk/backup";
    if (std::filesystem::exists("/data/adb/modules/llc_zygisk", ec)) {
        std::filesystem::create_directories(mod_backup_dir, ec);
        if (!ec) {
            chmod(mod_backup_dir.c_str(), 0755);
            std::filesystem::copy_file(dest, mod_backup_dir / "tmpchinesefont", std::filesystem::copy_options::overwrite_existing, ec);
            chmod((mod_backup_dir / "tmpchinesefont").c_str(), 0644);
        }
    }
    font_config::adapt_assetbundle_to_android((backup_dir / "tmpchinesefont").string());

    // Also populate default fallback in app files if possible
    std::filesystem::path default_dest(font_config::default_font_path);
    std::filesystem::copy_file(dest, default_dest, std::filesystem::copy_options::overwrite_existing, ec);
    if (!ec && st.st_uid != 0) {
        chmod(default_dest.c_str(), 0644);
        chown(default_dest.c_str(), st.st_uid, st.st_gid);
    }

    save_config(target_name, dest.string());

    auto res = summary();
    res["phase"] = "succeeded";
    res["downloaded_font"] = target_name;
    res["bytes_received"] = received;
    res["source_url"] = url;
    return res;
}

} // namespace font_manager
