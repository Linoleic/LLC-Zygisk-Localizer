#include "../tools/zip_pack.hpp"

#include <cassert>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {
struct Member {
    std::string path;
    std::string data;
    uint16_t method = 0;
    uint16_t flags = 0;
    uint32_t mode = 0100644;
};

static void put16(std::string& out, uint16_t value) {
    out.push_back(static_cast<char>(value)); out.push_back(static_cast<char>(value >> 8));
}
static void put32(std::string& out, uint32_t value) {
    put16(out, static_cast<uint16_t>(value)); put16(out, static_cast<uint16_t>(value >> 16));
}
static void set16(std::string& out, size_t at, uint16_t value) {
    out.at(at) = static_cast<char>(value); out.at(at + 1) = static_cast<char>(value >> 8);
}
static void set32(std::string& out, size_t at, uint32_t value) {
    set16(out, at, static_cast<uint16_t>(value)); set16(out, at + 2, static_cast<uint16_t>(value >> 16));
}
static uint32_t crc(const std::string& value) {
    return static_cast<uint32_t>(crc32(0, reinterpret_cast<const Bytef*>(value.data()), static_cast<uInt>(value.size())));
}
static std::string raw_deflate(const std::string& input) {
    z_stream stream{};
    assert(deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY) == Z_OK);
    std::string result(compressBound(static_cast<uLong>(input.size())) + 32, '\0');
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(input.data()));
    stream.avail_in = static_cast<uInt>(input.size());
    stream.next_out = reinterpret_cast<Bytef*>(result.data());
    stream.avail_out = static_cast<uInt>(result.size());
    const int status = deflate(&stream, Z_FINISH);
    assert(status == Z_STREAM_END);
    result.resize(stream.total_out);
    deflateEnd(&stream);
    return result;
}

static std::string archive(const std::vector<Member>& members) {
    struct Record { Member member; std::string compressed; uint32_t crc; uint32_t offset; };
    std::vector<Record> records;
    std::string out;
    for (const auto& member : members) {
        Record r{member, member.method == 8 ? raw_deflate(member.data) : member.data, crc(member.data), static_cast<uint32_t>(out.size())};
        put32(out, 0x04034b50); put16(out, 20); put16(out, member.flags); put16(out, member.method);
        put16(out, 0); put16(out, 0); put32(out, r.crc); put32(out, static_cast<uint32_t>(r.compressed.size()));
        put32(out, static_cast<uint32_t>(member.data.size())); put16(out, static_cast<uint16_t>(member.path.size())); put16(out, 0);
        out += member.path; out += r.compressed; records.push_back(std::move(r));
    }
    const uint32_t central_offset = static_cast<uint32_t>(out.size());
    for (const auto& r : records) {
        put32(out, 0x02014b50); put16(out, static_cast<uint16_t>((3 << 8) | 20)); put16(out, 20);
        put16(out, r.member.flags); put16(out, r.member.method); put16(out, 0); put16(out, 0);
        put32(out, r.crc); put32(out, static_cast<uint32_t>(r.compressed.size())); put32(out, static_cast<uint32_t>(r.member.data.size()));
        put16(out, static_cast<uint16_t>(r.member.path.size())); put16(out, 0); put16(out, 0); put16(out, 0); put16(out, 0);
        put32(out, r.member.mode << 16); put32(out, r.offset); out += r.member.path;
    }
    const uint32_t central_size = static_cast<uint32_t>(out.size()) - central_offset;
    put32(out, 0x06054b50); put16(out, 0); put16(out, 0); put16(out, static_cast<uint16_t>(records.size()));
    put16(out, static_cast<uint16_t>(records.size())); put32(out, central_size); put32(out, central_offset); put16(out, 0);
    return out;
}

static Member ui(uint16_t method = 0) {
    return {"LimbusCompany_Data/Lang/LLC_zh-CN/UI.json", R"({"dataList":[]})", method, 0, 0100644};
}
static std::string valid_pack(uint16_t method = 0) {
    return archive({ui(method), {"LimbusCompany_Data/Lang/LLC_zh-CN/Info/version.json",
        R"({"version":2026101602,"notice":""})", 8, 0, 0100644}});
}
template <typename Function>
static bool throws(Function function) {
    try { function(); } catch (const std::exception&) { return true; }
    return false;
}
static zip_pack::Entry selected(const std::vector<zip_pack::Entry>& entries, const std::string& path) {
    for (const auto& entry : entries) if (entry.path == path) return entry;
    assert(false); return {};
}

} // namespace

int main(int argc, char** argv) {
    using namespace zip_pack;
    assert(argc <= 2);
    if (argc == 2) {
        std::ifstream input(argv[1], std::ios::binary);
        assert(input.good());
        input.seekg(0, std::ios::end);
        const auto length = input.tellg();
        assert(length >= 0 && static_cast<uint64_t>(length) <= archive_limit);
        input.seekg(0, std::ios::beg);
        std::string bytes(static_cast<size_t>(length), '\0');
        input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        assert(input.good() || input.eof());
        assert(static_cast<size_t>(input.gcount()) == bytes.size());
        const auto official_entries = catalog(bytes);
        size_t selected_count = 0;
        uint64_t extracted_total = 0;
        for (const auto& entry : official_entries) {
            if (entry.relative.empty()) continue;
            const auto content = inflate(bytes, entry);
            ++selected_count;
            extracted_total += content.size();
        }
        assert(selected_count > 0);
        std::cout << "selected_entries=" << selected_count
                  << " extracted_bytes=" << extracted_total << '\n';
        return 0;
    }
    const std::string ui_path = "LimbusCompany_Data/Lang/LLC_zh-CN/UI.json";
    const std::string version_path = "LimbusCompany_Data/Lang/LLC_zh-CN/Info/version.json";

    auto stored = valid_pack();
    auto entries = catalog(stored);
    assert(entries.size() == 2);
    auto ui_entry = selected(entries, ui_path);
    assert(ui_entry.relative == "UI.json");
    assert(inflate(stored, ui_entry) == R"({"dataList":[]})");
    assert(inflate(stored, selected(entries, version_path)).find("2026101602") != std::string::npos);

    auto deflated = valid_pack(8);
    auto deflated_entries = catalog(deflated);
    assert(inflate(deflated, selected(deflated_entries, ui_path)) == R"({"dataList":[]})");

    // Keep an accepted language member in every malformed-path fixture so rejection
    // exercises the offending entry rather than only the required-language check.
    const auto rejects_archive = [](const std::vector<Member>& extra) {
        auto members = std::vector<Member>{ui()}; members.insert(members.end(), extra.begin(), extra.end());
        return throws([&] { catalog(archive(members)); });
    };
    assert(rejects_archive({{"LimbusCompany_Data/../escape.json", "x"}}));
    assert(rejects_archive({{"/LimbusCompany_Data/absolute.json", "x"}}));
    assert(rejects_archive({{"LimbusCompany_Data\\Lang\\LLC_zh-CN\\evil.json", "x"}}));
    assert(rejects_archive({{"LimbusCompany_Data/Lang/LLC_zh-CN/ui.JSON", R"({"dataList":[]})"}}));
    assert(rejects_archive({{ui_path, R"({"dataList":[]})"}}));
    assert(rejects_archive({{"LimbusCompany_Data/Folder", "x", 0, 0, 0120000}}));
    assert(rejects_archive({{"LimbusCompany_Data/Folder", "x", 0, 0, 0010000}}));
    assert(rejects_archive({{"LimbusCompany_Data/Folder/", "x", 0, 0, 0040755}}));
    assert(rejects_archive({{"LimbusCompany_Data/Folder", "", 0, 0, 0040755}}));
    assert(rejects_archive({{"LimbusCompany_Data/Folder", "x", 12, 0, 0100644}}));
    assert(rejects_archive({{"LimbusCompany_Data/Folder", "x", 0, 1, 0100644}}));
    assert(rejects_archive({{"LimbusCompany_Data/Folder", "x", 0, 0, 0100644},
        {"LimbusCompany_Data/Folder/child.json", "x"}}));
    assert(throws([] { catalog(std::string(archive_limit + 1, '\0')); }));

    auto truncated = stored; truncated.pop_back();
    assert(throws([&] { catalog(truncated); }));

    auto local_name_mismatch = stored;
    auto central = local_name_mismatch.find("PK\x01\x02", 0);
    assert(central != std::string::npos);
    auto first_local_name_length = static_cast<unsigned char>(local_name_mismatch[26]) |
        (static_cast<unsigned char>(local_name_mismatch[27]) << 8);
    assert(first_local_name_length > 0);
    local_name_mismatch[30] = 'X';
    assert(throws([&] { catalog(local_name_mismatch); }));

    auto local_central_mismatch = stored;
    central = local_central_mismatch.find("PK\x01\x02", 0);
    assert(central != std::string::npos);
    set16(local_central_mismatch, central + 10, 8);
    assert(throws([&] { catalog(local_central_mismatch); }));

    auto overlapping = archive({ui(), {"LimbusCompany_Data/other.json", "x"}});
    central = overlapping.find("PK\x01\x02", 0);
    assert(central != std::string::npos);
    uint32_t first_offset = static_cast<unsigned char>(overlapping[central + 42]) |
        (static_cast<uint32_t>(static_cast<unsigned char>(overlapping[central + 43])) << 8) |
        (static_cast<uint32_t>(static_cast<unsigned char>(overlapping[central + 44])) << 16) |
        (static_cast<uint32_t>(static_cast<unsigned char>(overlapping[central + 45])) << 24);
    const size_t central_name_length = static_cast<unsigned char>(overlapping[central + 28]) |
        (static_cast<unsigned char>(overlapping[central + 29]) << 8);
    const size_t second_central = central + 46 + central_name_length;
    set32(overlapping, second_central + 42, first_offset);
    assert(throws([&] { catalog(overlapping); }));

    auto bad_crc = stored;
    auto crc_entry = selected(catalog(bad_crc), ui_path);
    bad_crc[crc_entry.data] ^= 1;
    assert(throws([&] { inflate(bad_crc, crc_entry); }));

    auto bad_deflate = deflated;
    auto deflate_entry = selected(catalog(bad_deflate), ui_path);
    bad_deflate[deflate_entry.data] ^= static_cast<char>(0xff);
    assert(throws([&] { inflate(bad_deflate, deflate_entry); }));

    auto invalid_content = archive({{"LimbusCompany_Data/Lang/LLC_zh-CN/UI.json", "not JSON"}});
    assert(throws([&] { inflate(invalid_content, selected(catalog(invalid_content), ui_path)); }));

    auto oversized = archive({ui()});
    central = oversized.find("PK\x01\x02", 0);
    assert(central != std::string::npos);
    set32(oversized, central + 24, 15 * 1024 * 1024 + 1);
    set32(oversized, 22, 15 * 1024 * 1024 + 1);
    assert(throws([&] { catalog(oversized); }));
}
