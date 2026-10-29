// Android/POSIX local pack publisher. No downloads, game restarts or native gates.
#include "../jni/core/resource_pack.hpp"
#include <sys/file.h>
#include <unistd.h>
#include <cerrno>
#include <chrono>
#include <functional>
#include <iostream>
#ifdef __ANDROID__
#include <sys/xattr.h>
#endif

namespace pack_tool {
namespace rp = resource_pack;
namespace fs = std::filesystem;
using Checkpoint = std::function<void(const char*)>;
inline void checkpoint(const Checkpoint& hook, const char* name) { if (hook) hook(name); }
struct Descriptor {
    int value;
    ~Descriptor() { if (value >= 0) close(value); }
    explicit Descriptor(int fd) : value(fd) { rp::require(fd >= 0, "open failed"); }
    Descriptor(const Descriptor&) = delete;
};
// Root-created files do not reliably inherit the application's UID/MCS label.
// Preserve the source directory's access identity; do not change global policy.
struct Access {
    uid_t uid; gid_t gid;
#ifdef __ANDROID__
    std::string context;
#endif
    explicit Access(const fs::path& source) {
        Descriptor fd(open(source.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
        struct stat st{}; rp::require(fstat(fd.value, &st) == 0, "source stat failed"); uid = st.st_uid; gid = st.st_gid;
#ifdef __ANDROID__
        char label[1024]; auto size = fgetxattr(fd.value, "security.selinux", label, sizeof(label));
        rp::require(size > 0, "source SELinux label unavailable"); context.assign(label, size);
#endif
    }
    void apply_fd(int fd, mode_t mode) const {
        struct stat st{}; rp::require(fstat(fd, &st) == 0, "target stat failed");
        if (st.st_uid != uid || st.st_gid != gid) rp::require(fchown(fd, uid, gid) == 0, "resource chown failed");
        rp::require(fchmod(fd, mode) == 0, "resource chmod failed");
#ifdef __ANDROID__
        char label[1024]; auto size = fgetxattr(fd, "security.selinux", label, sizeof(label));
        if (size < 0 || context != std::string(label, size))
            rp::require(fsetxattr(fd, "security.selinux", context.data(), context.size(), 0) == 0, "resource SELinux label failed");
#endif
    }
    void apply(const fs::path& path, mode_t mode = 0755) const {
        Descriptor fd(open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
        apply_fd(fd.value, mode); rp::require(fsync(fd.value) == 0, "resource metadata fsync failed");
    }
};
inline void sync_directory(const fs::path& path) {
    Descriptor descriptor(open(path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
    rp::require(fsync(descriptor.value) == 0, "directory fsync failed");
}
inline void directory(const fs::path& path) {
    fs::create_directories(path);
    rp::require(fs::is_directory(fs::symlink_status(path)), "expected real directory: " + path.string());
}
inline void write_new(const fs::path& path, const std::string& bytes, const Access* access = nullptr) {
    Descriptor descriptor(open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0644));
    size_t offset = 0;
    while (offset < bytes.size()) {
        ssize_t written = write(descriptor.value, bytes.data() + offset, bytes.size() - offset);
        if (written < 0 && errno == EINTR) continue;
        rp::require(written > 0, "write failed: " + path.string());
        offset += written;
    }
    if (access) access->apply_fd(descriptor.value, 0644);
    rp::require(fsync(descriptor.value) == 0, "file fsync failed: " + path.string());
}
inline std::string token() {
    return std::to_string(getpid()) + "-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
}
struct StoreLock {
    Descriptor descriptor;
    explicit StoreLock(const fs::path& store) : descriptor(open((store / ".lock").c_str(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600)) {
        rp::require(flock(descriptor.value, LOCK_EX | LOCK_NB) == 0, "pack store busy");
    }
};
inline rp::State current(const fs::path& store) {
    return fs::exists(fs::symlink_status(store / "state.json")) ? rp::read_state(store) : rp::State{};
}
inline void commit(const fs::path& store, const rp::State& state, const Checkpoint& hook = {}) {
    auto next = store / ("state.next-" + token());
    Access access(store);
    write_new(next, rp::Json({{"schema", 1}, {"active", state.active}, {"previous", state.previous}}).dump(2) + "\n", &access);
    checkpoint(hook, "before_state");
    // Same-filesystem rename is the only visibility commit. Previous stays in it.
    rp::require(rename(next.c_str(), (store / "state.json").c_str()) == 0, "state rename failed");
    sync_directory(store);
    checkpoint(hook, "committed");
}
inline std::string install(const fs::path& source, const fs::path& manifest_path, const fs::path& store, const Checkpoint& hook = {}, const Access* desired_access = nullptr, const rp::State* expected_state = nullptr) {
    auto manifest = rp::parse_manifest(rp::read_bytes(manifest_path, rp::manifest_limit));
    rp::verify(source, manifest, false); // No writes before the input passes.
    Access access = desired_access ? *desired_access : Access(source);
    directory(store); sync_directory(fs::absolute(store).parent_path()); StoreLock lock(store);
    access.apply(store); access.apply(store / ".lock", 0600);
    directory(store / "releases"); directory(store / "staging");
    access.apply(store / "releases"); access.apply(store / "staging");
    auto old = current(store);
    rp::require(!expected_state || (old.active == expected_state->active && old.previous == expected_state->previous), "resource activation state changed");
    // Preserve the last actually valid release, including recovery from bad active.
    std::string previous;
    if (!old.active.empty()) previous = rp::select(store, source).key;
    auto release = store / "releases" / manifest.key;
    if (fs::exists(fs::symlink_status(release))) {
        rp::verify_release(release, manifest.key);
        if (old.active != manifest.key) commit(store, {manifest.key, previous == manifest.key ? "" : previous}, hook);
        return manifest.key;
    }
    auto stage = store / "staging" / (manifest.key + "-" + token());
    rp::require(fs::create_directory(stage), "staging already exists");
    for (const auto& entry : manifest.files) {
        auto bytes = rp::read_bytes(source / entry.path, rp::file_limit(entry.path));
        rp::require(bytes.size() == entry.size && rp::digest(bytes) == entry.sha256, "source changed: " + entry.path);
        auto target = stage / entry.path;
        directory(target.parent_path()); write_new(target, bytes, &access);
    }
    write_new(stage / rp::manifest_name, manifest.bytes, &access);
    rp::verify_release(stage, manifest.key);
    for (const auto& entry : fs::recursive_directory_iterator(stage))
        if (entry.is_directory()) access.apply(entry.path());
    access.apply(stage);
    checkpoint(hook, "staged");
    rp::require(rename(stage.c_str(), release.c_str()) == 0, "release rename failed");
    sync_directory(store / "releases"); sync_directory(store / "staging");
    checkpoint(hook, "published");
    if (previous == manifest.key) return manifest.key; // idempotent; keep rollback target
    commit(store, {manifest.key, previous}, hook);
    return manifest.key;
}
inline std::string rollback(const fs::path& store, const Checkpoint& hook = {}) {
    rp::require(fs::is_directory(fs::symlink_status(store)), "missing store");
    StoreLock lock(store); auto state = rp::read_state(store);
    rp::require(!state.previous.empty(), "no previous release");
    rp::verify_release(store / "releases" / state.previous, state.previous);
    commit(store, {state.previous, state.active}, hook); return state.previous;
}
} // namespace pack_tool

#ifndef LLC_PACK_NO_MAIN
int main(int argc, char** argv) {
    try {
        namespace rp = resource_pack;
        std::string command = argc > 1 ? argv[1] : "";
        rp::Json result = {{"schema", 1}, {"kind", "local_resource_pack"}, {"command", command}};
        if (command == "manifest" && argc == 5) {
            auto manifest = rp::make_manifest(argv[2], argv[3]);
            pack_tool::write_new(argv[4], manifest.bytes);
            result["key"] = manifest.key; result["files"] = manifest.files.size();
            result["origin"] = "local-input-snapshot";
        } else if (command == "install" && argc == 5) {
            result["key"] = pack_tool::install(argv[2], argv[3], argv[4]);
            result["requires_cold_start"] = true;
        } else if (command == "rollback" && argc == 3) {
            result["key"] = pack_tool::rollback(argv[2]);
            result["requires_cold_start"] = true;
        } else if (command == "verify" && argc == 3) {
            auto manifest = rp::verify_release(argv[2]); result["key"] = manifest.key; result["files"] = manifest.files.size();
        } else if (command == "resolve" && argc == 4) {
            auto selected = rp::select(argv[2], argv[3]);
            result["root"] = selected.root; result["key"] = selected.key; result["mode"] = selected.mode; result["reason"] = selected.reason;
        } else throw std::runtime_error("usage: resource_pack manifest <source> <version> <new-manifest> | install <source> <manifest> <store> | rollback <store> | verify <release> | resolve <store> <legacy>");
        result["status"] = "passed"; std::cout << result.dump(2) << '\n'; return 0;
    } catch (const std::exception& error) {
        std::cout << resource_pack::Json({{"status", "failed"}, {"error", error.what()}}).dump() << '\n'; return 1;
    }
}
#endif
