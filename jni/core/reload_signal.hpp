#pragma once
#include <map>
#include <string>
#include <vector>
#include <sys/stat.h>
#include <unistd.h>

// Compatibility paths are aliases for one request, not independent reload jobs.
class ReloadSignals {
    struct Stamp {
        dev_t device; ino_t inode; time_t seconds; long nanos; off_t size;
        bool operator==(const Stamp& other) const {
            return device == other.device && inode == other.inode && seconds == other.seconds &&
                nanos == other.nanos && size == other.size;
        }
    };
    std::map<std::string, Stamp> unremovable_;
    int (*remove_)(const char*);
public:
    explicit ReloadSignals(int (*remove_file)(const char*) = ::unlink) : remove_(remove_file) {}
    bool consume(const std::vector<std::string>& paths) {
        bool requested = false;
        for (const auto& path : paths) {
            struct stat st{};
            if (lstat(path.c_str(), &st) || !S_ISREG(st.st_mode)) { unremovable_.erase(path); continue; }
            if (!remove_(path.c_str())) { unremovable_.erase(path); requested = true; continue; }
            Stamp stamp{st.st_dev, st.st_ino, st.st_mtim.tv_sec, st.st_mtim.tv_nsec, st.st_size};
            auto found = unremovable_.find(path);
            if (found == unremovable_.end() || !(found->second == stamp)) requested = true;
            unremovable_.insert_or_assign(path, stamp);
        }
        return requested;
    }
};
