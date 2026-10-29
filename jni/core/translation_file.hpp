#pragma once
#include "../third_party/nlohmann/json.hpp"
#include <cstdio>
#include <cstdint>
#include <string>
#include <sys/stat.h>
#include <fcntl.h>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace translation_file {
enum class Error { None, OpenFailed, NotRegular, InvalidSize, ReadIncomplete, Changed, InvalidJson, InvalidDocument };
inline const char* reason(Error error) {
    switch (error) {
        case Error::None: return "ok";
        case Error::OpenFailed: return "open_failed";
        case Error::NotRegular: return "not_regular_file";
        case Error::InvalidSize: return "invalid_size";
        case Error::ReadIncomplete: return "read_incomplete";
        case Error::Changed: return "file_changed";
        case Error::InvalidJson: return "invalid_json";
        case Error::InvalidDocument: return "invalid_document";
    }
    return "unknown";
}

namespace detail {
inline bool same_snapshot(const struct stat& a, const struct stat& b) {
    bool same = a.st_size == b.st_size && a.st_mtime == b.st_mtime && a.st_ctime == b.st_ctime;
#ifndef _WIN32
    // Windows CRT fstat/stat encode st_dev differently and do not provide a
    // usable st_ino. Android retains the descriptor/path identity checks.
    same = same && a.st_dev == b.st_dev && a.st_ino == b.st_ino;
#endif
#if defined(__ANDROID__) || defined(__linux__)
    same = same && a.st_mtim.tv_nsec == b.st_mtim.tv_nsec && a.st_ctim.tv_nsec == b.st_ctim.tv_nsec;
#endif
    return same;
}

// Caller owns this stream. Output is committed only after every check passes.
inline Error read_open(FILE* file, const std::string& path, std::string& output, size_t limit) {
    struct stat before{}, after{}, named{};
    if (fstat(fileno(file), &before) || !S_ISREG(before.st_mode)) return Error::NotRegular;
    if (before.st_size <= 0 || static_cast<uintmax_t>(before.st_size) > limit) return Error::InvalidSize;
    if (fseek(file, 0, SEEK_SET)) return Error::ReadIncomplete;
    std::string source(static_cast<size_t>(before.st_size), '\0');
    if (fread(source.data(), 1, source.size(), file) != source.size() || ferror(file)) return Error::ReadIncomplete;
    // A growing file must not be accepted as the earlier complete prefix.
    if (fgetc(file) != EOF) return Error::Changed;
    if (ferror(file)) return Error::ReadIncomplete;
    if (fstat(fileno(file), &after) || stat(path.c_str(), &named) ||
        !same_snapshot(before, after) || !same_snapshot(after, named)) return Error::Changed;
    // Some parsers use NUL as EOF; never allow it to hide a trailing document.
    if (source.find('\0') != std::string::npos) return Error::InvalidJson;
    auto document = nlohmann::json::parse(source, nullptr, false);
    if (document.is_discarded()) return Error::InvalidJson;
    if (!document.is_object() || !document.contains("dataList") || !document["dataList"].is_array()) return Error::InvalidDocument;
    output.swap(source);
    return Error::None;
}
} // namespace detail

// Nonblocking open prevents a non-regular path from hanging before fstat.
// The immutable buffer is validated, not the pathname at a later parse time.
inline Error read(const std::string& path, std::string& output, size_t limit = 15 * 1024 * 1024) {
#ifdef _WIN32
    int descriptor = _open(path.c_str(), _O_RDONLY | _O_BINARY | _O_NOINHERIT);
#else
    int descriptor = open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK);
#endif
    if (descriptor < 0) return Error::OpenFailed;
    FILE* file = fdopen(descriptor, "rb");
    if (!file) {
#ifdef _WIN32
        _close(descriptor);
#else
        close(descriptor);
#endif
        return Error::OpenFailed;
    }
    struct Close { FILE* file; ~Close() { fclose(file); } } close_file{file};
    return detail::read_open(file, path, output, limit);
}
} // namespace translation_file
