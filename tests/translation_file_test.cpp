#include "translation_file.hpp"
#include <cassert>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
int main() {
    auto root = fs::temp_directory_path() / ("llc-file-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    assert(fs::create_directory(root));
    auto write = [&](const char* name, const std::string& bytes) {
        auto path = root / name;
        std::ofstream file(path, std::ios::binary); file.write(bytes.data(), bytes.size()); file.close();
        return path.string();
    };
    using Error = translation_file::Error;
    std::string output = "unchanged";
    auto rejected = [&](const std::string& path, Error expected, size_t limit = 1024) {
        auto actual = translation_file::read(path, output, limit);
        if (actual != expected) std::fprintf(stderr, "%s: expected %s, got %s\n", path.c_str(), translation_file::reason(expected), translation_file::reason(actual));
        assert(actual == expected);
        assert(output == "unchanged");
    };
    rejected((root / "missing").string(), Error::OpenFailed);
    rejected(write("empty", ""), Error::InvalidSize);
    rejected(write("half", R"({"dataList":[{"id":"first","content":"complete row"},{"id":)"), Error::InvalidJson);
    rejected(write("tail", R"({"dataList":[]}garbage)"), Error::InvalidJson);
    rejected(write("embedded_nul.json", std::string(R"({"dataList":[]})") + std::string("\0hidden", 7)), Error::InvalidJson);
    rejected(write("utf8", std::string("{\"dataList\":[\"\xff\"]}")), Error::InvalidJson);
    rejected(write("wrong", R"({"dataList":{}})"), Error::InvalidDocument);
    rejected(write("array", "[]"), Error::InvalidDocument);
    rejected(write("limit", R"({"dataList":[]})"), Error::InvalidSize, 4);
    assert(translation_file::read(root.string(), output) != Error::None);
    assert(output == "unchanged");
    auto original = write("original", R"({"dataList":[]})");
    auto replacement = write("replacement", R"({"dataList":[{"id":"new","content":"different file"}]})");
    FILE* held = fopen(original.c_str(), "rb"); assert(held);
    assert(translation_file::detail::read_open(held, replacement, output, 1024) == Error::Changed);
    fclose(held); assert(output == "unchanged");
#ifndef _WIN32
    // Same-sized atomic replacement: only descriptor/path identity distinguishes it.
    auto atomic_path = write("atomic.json", R"({"dataList":[]})");
    auto next_path = write("atomic.next", R"({"dataList":[]})");
    held = fopen(atomic_path.c_str(), "rb"); assert(held);
    fs::rename(next_path, atomic_path);
    assert(translation_file::detail::read_open(held, atomic_path, output, 1024) == Error::Changed);
    fclose(held); assert(output == "unchanged");
    auto fifo = (root / "pipe").string();
    assert(mkfifo(fifo.c_str(), 0600) == 0);
    rejected(fifo, Error::NotRegular);
#endif
    FILE* write_only = fopen((root / "write_only").string().c_str(), "wb"); assert(write_only);
    const char* valid = R"({"dataList":[]})";
    assert(fwrite(valid, 1, strlen(valid), write_only) == strlen(valid)); fflush(write_only);
    assert(translation_file::detail::read_open(write_only, (root / "write_only").string(), output, 1024) == Error::ReadIncomplete);
    fclose(write_only); assert(output == "unchanged");
    const std::string source = "\xef\xbb\xbf{\"dataList\":[{\"id\":\"x\",\"content\":\"ok\"}]}\n";
    assert(translation_file::read(write("valid", source), output) == Error::None && output == source);
    for (const auto& entry : fs::directory_iterator(root)) fs::remove(entry.path());
    fs::remove(root);
    std::puts("translation_file: bounded reads, malformed prefixes, NUL/UTF-8, schema, stream failure and path replacement passed");
}
