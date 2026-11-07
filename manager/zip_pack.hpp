#pragma once
#include "../jni/core/resource_pack.hpp"
#include <zlib.h>
#include <algorithm>
#include <map>

// A deliberately restricted ZIP reader. Metadata is never restored as filesystem
// permissions/links. All paths and resource limits are checked before output.
namespace zip_pack {
namespace rp = resource_pack;
using rp::require;
inline constexpr size_t archive_limit = 64 * 1024 * 1024;
inline constexpr const char* language_prefix = "LimbusCompany_Data/Lang/LLC_zh-CN/";
inline uint16_t u16(const std::string& b, size_t p) {
    require(p <= b.size() && b.size() - p >= 2, "truncated_zip");
    return static_cast<unsigned char>(b[p]) | (static_cast<unsigned char>(b[p + 1]) << 8);
}
inline uint32_t u32(const std::string& b, size_t p) { return u16(b, p) | (static_cast<uint32_t>(u16(b, p + 2)) << 16); }
struct Entry {
    std::string path, relative;
    uint32_t crc = 0, compressed = 0, size = 0, offset = 0;
    uint16_t method = 0, flags = 0;
    size_t data = 0, end = 0;
    bool directory = false;
};
inline void extras(const std::string& bytes, size_t at, size_t length) {
    require(at <= bytes.size() && length <= bytes.size() - at, "truncated_zip_extra");
    size_t end = at + length;
    while (at < end) {
        require(end - at >= 4, "invalid_zip_extra");
        require(u16(bytes, at) != 1, "zip64_unsupported");
        size_t count = u16(bytes, at + 2); at += 4;
        require(count <= end - at, "invalid_zip_extra_length"); at += count;
    }
}
inline std::vector<Entry> catalog(const std::string& bytes) {
    require(bytes.size() >= 22 && bytes.size() <= archive_limit, "invalid_zip_size");
    size_t eocd = bytes.size() - 22;
    bool found = false;
    for (size_t count = 0; count <= 65535 && count <= bytes.size() - 22; ++count) {
        size_t at = bytes.size() - 22 - count;
        if (u32(bytes, at) == 0x06054b50 && u16(bytes, at + 20) == count) { eocd = at; found = true; break; }
    }
    require(found && !u16(bytes, eocd + 4) && !u16(bytes, eocd + 6) && u16(bytes, eocd + 8) == u16(bytes, eocd + 10), "invalid_zip_end");
    size_t files = u16(bytes, eocd + 10), central_size = u32(bytes, eocd + 12), central = u32(bytes, eocd + 16);
    require(files && files <= rp::max_files && central <= eocd && central_size == eocd - central, "invalid_zip_directory");
    std::vector<Entry> entries; std::set<std::string> names, regular; uint64_t total = 0; size_t selected = 0;
    size_t at = central;
    for (size_t index = 0; index < files; ++index) {
        require(at <= eocd && eocd - at >= 46 && u32(bytes, at) == 0x02014b50, "invalid_zip_central_entry");
        Entry entry; entry.flags = u16(bytes, at + 8); entry.method = u16(bytes, at + 10);
        entry.crc = u32(bytes, at + 16); entry.compressed = u32(bytes, at + 20); entry.size = u32(bytes, at + 24); entry.offset = u32(bytes, at + 42);
        size_t name_length = u16(bytes, at + 28), extra = u16(bytes, at + 30), comment = u16(bytes, at + 32);
        require(name_length && name_length <= 512 && name_length + extra + comment <= eocd - at - 46 && !u16(bytes, at + 34), "invalid_zip_entry_lengths");
        require(!(entry.flags & ~0x080e) && (entry.method == 0 || entry.method == 8), "encrypted_or_unsupported_zip");
        entry.path = bytes.substr(at + 46, name_length); entry.directory = entry.path.back() == '/';
        auto normalized = entry.directory ? entry.path.substr(0, entry.path.size() - 1) : entry.path;
        require(rp::relative_path(normalized) && names.insert(rp::lower(normalized)).second, "unsafe_or_duplicate_zip_path");
        auto mode = (u32(bytes, at + 38) >> 16) & 0170000;
        require(!mode || mode == 0100000 || mode == 0040000, "zip_link_or_special_file");
        require(mode != 0040000 || entry.directory, "zip_directory_mismatch");
        if (entry.directory) require(!entry.size && !entry.compressed, "nonempty_zip_directory");
        else regular.insert(rp::lower(normalized));
        total += entry.size; require(total <= rp::max_bytes && entry.compressed <= archive_limit, "zip_expansion_limit");
        if (entry.path.rfind(language_prefix, 0) == 0 && !entry.directory) {
            entry.relative = entry.path.substr(strlen(language_prefix));
            require(rp::relative_path(entry.relative) && rp::lower(entry.relative) != rp::manifest_name &&
                (rp::translation(entry.relative) || entry.relative == "Info/version.json" || entry.relative == "Info/LICENSE") &&
                entry.size > 0 && entry.size <= rp::file_limit(entry.relative), "unsupported_language_member");
            ++selected;
        }
        extras(bytes, at + 46 + name_length, extra);
        auto off = static_cast<size_t>(entry.offset);
        require(off <= central && central - off >= 30 && u32(bytes, off) == 0x04034b50 && u16(bytes, off + 6) == entry.flags &&
            u16(bytes, off + 8) == entry.method && u16(bytes, off + 26) == name_length, "zip_local_header_mismatch");
        size_t local_extra = u16(bytes, off + 28);
        require(name_length + local_extra <= central - off - 30 && bytes.compare(off + 30, name_length, entry.path) == 0, "zip_local_name_mismatch");
        extras(bytes, off + 30 + name_length, local_extra);
        entry.data = off + 30 + name_length + local_extra;
        require(entry.compressed <= central - entry.data, "zip_data_outside_archive"); entry.end = entry.data + entry.compressed;
        if (entry.flags & 8) {
            auto descriptor = entry.end;
            if (central - descriptor >= 4 && u32(bytes, descriptor) == 0x08074b50) descriptor += 4;
            require(central - descriptor >= 12 && u32(bytes, descriptor) == entry.crc && u32(bytes, descriptor + 4) == entry.compressed &&
                u32(bytes, descriptor + 8) == entry.size, "invalid_zip_descriptor"); entry.end = descriptor + 12;
        } else require(u32(bytes, off + 14) == entry.crc && u32(bytes, off + 18) == entry.compressed && u32(bytes, off + 22) == entry.size, "zip_local_sizes_mismatch");
        if (!entry.method) require(entry.size == entry.compressed, "invalid_stored_zip_size");
        entries.push_back(std::move(entry)); at += 46 + name_length + extra + comment;
    }
    require(at == eocd && selected > 0, "zip_directory_count_or_language_missing");
    for (const auto& name : names) {
        size_t slash = 0;
        while ((slash = name.find('/', slash)) != name.npos) { require(!regular.count(name.substr(0, slash)), "zip_file_directory_collision"); ++slash; }
    }
    std::vector<std::pair<size_t, size_t>> ranges;
    for (const auto& entry : entries) ranges.push_back({entry.offset, entry.end});
    std::sort(ranges.begin(), ranges.end());
    size_t end = 0;
    for (const auto& range : ranges) { require(range.first == end && range.second >= range.first, "overlapping_or_hidden_zip_data"); end = range.second; }
    require(end == central, "unreferenced_zip_data");
    return entries;
}
inline std::string inflate(const std::string& bytes, const Entry& entry) {
    require(!entry.directory && !entry.relative.empty() && entry.size <= rp::file_limit(entry.relative) &&
        entry.data <= bytes.size() && entry.compressed <= bytes.size() - entry.data, "invalid_zip_extraction_request");
    std::string result(entry.size, '\0');
    if (!entry.method) result.assign(bytes, entry.data, entry.compressed);
    else {
        z_stream stream{};
        require(inflateInit2(&stream, -MAX_WBITS) == Z_OK, "deflate_init_failed");
        stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(bytes.data() + entry.data)); stream.avail_in = entry.compressed;
        stream.next_out = reinterpret_cast<Bytef*>(result.data()); stream.avail_out = entry.size;
        int status = ::inflate(&stream, Z_FINISH);
        bool valid = status == Z_STREAM_END && stream.total_out == entry.size && stream.total_in == entry.compressed;
        inflateEnd(&stream); require(valid, "deflate_data_invalid");
    }
    require(crc32(0, reinterpret_cast<const Bytef*>(result.data()), result.size()) == entry.crc, "zip_crc_mismatch");
    rp::validate_content(entry.relative, result); return result;
}
} // namespace zip_pack
