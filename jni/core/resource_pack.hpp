#pragma once
#include "translation_file.hpp"
#include "sha256.hpp"
#include <filesystem>
#include <set>
#include <stdexcept>
#include <vector>

// Local integrity, not upstream completeness, authenticity or game compatibility.
namespace resource_pack {
namespace fs = std::filesystem;
using Json = nlohmann::json;
inline constexpr const char* manifest_name = "_llc_manifest.json";
inline constexpr size_t manifest_limit = 8 * 1024 * 1024;
inline constexpr size_t max_files = 20000;
inline constexpr uint64_t max_bytes = 512ull * 1024 * 1024;
inline void require(bool value, const std::string& reason) { if (!value) throw std::runtime_error(reason); }
inline bool identifier(const std::string& s, size_t limit = 160) {
    if (s.empty() || s.size() > limit || s == "." || s == "..") return false;
    for (unsigned char c : s) if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.')) return false;
    return true;
}
inline bool relative_path(const std::string& s) {
    if (s.empty() || s.size() > 240 || s == manifest_name) return false;
    size_t begin = 0;
    do {
        size_t end = s.find('/', begin);
        if (!identifier(s.substr(begin, end == s.npos ? end : end - begin), 120)) return false;
        if (end == s.npos) return true;
        begin = end + 1;
    } while (begin < s.size());
    return false;
}
inline std::string lower(std::string s) {
    for (auto& c : s) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return s;
}
inline bool digest_valid(const std::string& s) {
    if (s.size() != 64) return false;
    for (char c : s) if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}
inline std::string digest(const std::string& bytes) {
    BuildSha256 hash; hash.update(bytes.data(), bytes.size()); return hash.hex_digest();
}
inline std::string read_bytes(const fs::path& path, size_t limit = 15 * 1024 * 1024) {
    std::string bytes;
    auto error = translation_file::read_bytes(path.string(), bytes, limit);
    require(error == translation_file::Error::None, path.string() + ": " + translation_file::reason(error));
    return bytes;
}
inline Json document(const std::string& bytes) {
    require(bytes.find('\0') == bytes.npos, "NUL in JSON");
    auto doc = Json::parse(bytes, nullptr, false);
    require(doc.is_object(), "invalid JSON object");
    return doc;
}
inline bool override_document(const std::string& path) {
    return path == "static_overrides.json" || path == "title_overrides.json" || path == "custom_overrides.json";
}
inline bool translation(const std::string& path) {
    return path.size() > 5 && path.substr(path.size() - 5) == ".json" && path != "Info/version.json" && !override_document(path);
}
inline size_t file_limit(const std::string& path) {
    return path.rfind("StoryData/", 0) == 0 ? 5 * 1024 * 1024 : 15 * 1024 * 1024;
}
inline void validate_content(const std::string& path, const std::string& bytes) {
    if (override_document(path)) {
        // These root configuration documents are consumed by the override loader,
        // not the dataList loader. Keep them in the hashed snapshot nonetheless.
        document(bytes);
    } else if (translation(path)) {
        auto doc = document(bytes);
        require(doc.contains("dataList") && doc["dataList"].is_array(), "invalid translation: " + path);
    } else if (path == "Info/version.json") {
        auto doc = document(bytes);
        require(doc.contains("version") && (doc["version"].is_number_integer() || doc["version"].is_string()), "invalid version metadata");
    }
}
// Reject symlinks, special files and case-colliding paths; include unconsumed data.
inline std::set<std::string> inventory(const fs::path& root, bool has_manifest) {
    require(fs::is_directory(fs::symlink_status(root)), "pack root must be a real directory");
    std::set<std::string> paths, folded;
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        auto status = entry.symlink_status();
        require(!fs::is_symlink(status), "symlink in pack");
        if (fs::is_directory(status)) continue;
        require(fs::is_regular_file(status), "special file in pack");
        auto path = entry.path().lexically_relative(root).generic_string();
        if (has_manifest && path == manifest_name) continue;
        require(relative_path(path) && lower(path) != manifest_name && folded.insert(lower(path)).second, "unsafe or duplicate path: " + path);
        paths.insert(path);
        require(paths.size() <= max_files, "too many files");
    }
    return paths;
}
struct Entry { std::string path, sha256; uint64_t size; };
struct Manifest { std::string version, bytes, key; std::vector<Entry> files; };
inline Manifest parse_manifest(const std::string& bytes) {
    require(bytes.size() <= manifest_limit, "manifest too large");
    auto doc = document(bytes);
    require(doc.value("schema", Json()) == 1 && doc.contains("version") && doc["version"].is_string() &&
        doc.contains("files") && doc["files"].is_array(), "invalid manifest schema");
    Manifest m; m.bytes = bytes; m.version = doc["version"].get<std::string>();
    require(identifier(m.version, 80), "invalid version identifier");
    require(!doc["files"].empty() && doc["files"].size() <= max_files, "invalid manifest count");
    std::set<std::string> seen;
    uint64_t total = 0; size_t translations = 0;
    for (const auto& item : doc["files"]) {
        require(item.is_object() && item.contains("path") && item["path"].is_string() && item.contains("sha256") &&
            item["sha256"].is_string() && item.contains("size") && item["size"].is_number_unsigned(), "invalid manifest entry");
        Entry e{item["path"].get<std::string>(), item["sha256"].get<std::string>(), item["size"].get<uint64_t>()};
        require(relative_path(e.path) && lower(e.path) != manifest_name && seen.insert(lower(e.path)).second && digest_valid(e.sha256) &&
            e.size > 0 && e.size <= file_limit(e.path), "invalid manifest path/hash/size");
        total += e.size; require(total <= max_bytes, "pack too large");
        translations += translation(e.path); m.files.push_back(std::move(e));
    }
    require(translations > 0, "manifest contains no translations");
    m.key = m.version + "-" + digest(bytes);
    return m;
}
inline Manifest make_manifest(const fs::path& source, const std::string& version) {
    auto paths = inventory(source, false);
    Json doc = {{"schema", 1}, {"version", version}, {"origin", "local-input-snapshot"}, {"files", Json::array()}};
    for (const auto& path : paths) {
        auto bytes = read_bytes(source / path, file_limit(path)); validate_content(path, bytes);
        doc["files"].push_back({{"path", path}, {"size", bytes.size()}, {"sha256", digest(bytes)}});
    }
    require(paths == inventory(source, false), "source inventory changed");
    return parse_manifest(doc.dump(2) + "\n");
}
inline void verify(const fs::path& root, const Manifest& manifest, bool has_manifest) {
    auto actual = inventory(root, has_manifest);
    require(actual.size() == manifest.files.size(), "file count mismatch");
    for (const auto& entry : manifest.files) {
        require(actual.count(entry.path), "missing file: " + entry.path);
        auto bytes = read_bytes(root / entry.path, file_limit(entry.path));
        require(bytes.size() == entry.size && digest(bytes) == entry.sha256, "hash/size mismatch: " + entry.path);
        validate_content(entry.path, bytes);
    }
    require(actual == inventory(root, has_manifest), "inventory changed during validation");
}
inline Manifest verify_release(const fs::path& root, const std::string& expected_key = {}) {
    auto manifest = parse_manifest(read_bytes(root / manifest_name, manifest_limit));
    require(expected_key.empty() || manifest.key == expected_key, "release manifest identity mismatch");
    verify(root, manifest, true); return manifest;
}
struct State { std::string active, previous; };
inline State read_state(const fs::path& store) {
    auto doc = document(read_bytes(store / "state.json", 4096));
    require(doc.value("schema", Json()) == 1 && doc.contains("active") && doc["active"].is_string() &&
        doc.contains("previous") && doc["previous"].is_string(), "invalid activation state");
    State state{doc["active"].get<std::string>(), doc["previous"].get<std::string>()};
    require(identifier(state.active) && (state.previous.empty() || identifier(state.previous)), "invalid activation key");
    return state;
}
struct Selection { std::string root, key, mode, reason; };
inline Selection select(const fs::path& store, const fs::path& legacy) {
    // A managed marker never silently falls back to the mutable legacy directory.
    if (!fs::exists(fs::symlink_status(store / "state.json"))) return {legacy.string(), "", "legacy", "no managed state"};
    require(fs::is_directory(fs::symlink_status(store)) && fs::is_directory(fs::symlink_status(store / "releases")), "managed store must use real directories");
    auto state = read_state(store);
    std::string failure;
    for (const auto& key : {state.active, state.previous}) {
        if (key.empty()) continue;
        try {
            auto root = store / "releases" / key;
            verify_release(root, key);
            return {root.string(), key, key == state.active ? "active" : "previous", failure};
        } catch (const std::exception& error) { failure = error.what(); }
    }
    throw std::runtime_error("no valid managed release: " + failure);
}
} // namespace resource_pack
