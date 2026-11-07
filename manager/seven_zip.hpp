#pragma once
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <cstring>
#ifdef _WIN32
#include <process.h>
#define getpid _getpid
#else
#include <unistd.h>
#endif

extern "C" {
#include "../third_party/lzma/7z.h"
#include "../third_party/lzma/7zAlloc.h"
#include "../third_party/lzma/7zCrc.h"
#include "../third_party/lzma/7zFile.h"
}

namespace seven_zip {

inline std::string utf16_to_utf8(const UInt16* wstr) {
    if (!wstr) return "";
    std::string out;
    for (size_t i = 0; wstr[i] != 0; ++i) {
        UInt16 c = wstr[i];
        if (c < 0x80) {
            out.push_back(static_cast<char>(c));
        } else if (c < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (c >> 6)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xE0 | (c >> 12)));
            out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        }
    }
    return out;
}

inline bool is_7z_archive(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return false;
    unsigned char header[6] = {0};
    file.read(reinterpret_cast<char*>(header), sizeof(header));
    if (file.gcount() < 6) return false;
    return header[0] == '7' && header[1] == 'z' &&
           header[2] == 0xBC && header[3] == 0xAF &&
           header[4] == 0x27 && header[5] == 0x1C;
}

// Extracts a file whose name matches target_pattern (or the first file if pattern empty)
// directly to dest_path.
inline bool extract_file(const std::string& archive_path,
                         const std::string& target_pattern,
                         const std::string& dest_path) {
    CrcGenerateTable();

    CFileInStream archiveStream;
    CLookToRead2 lookStream;
    CSzArEx db;
    ISzAlloc allocImp = { SzAlloc, SzFree };
    ISzAlloc allocTempImp = { SzAllocTemp, SzFreeTemp };

    FileInStream_CreateVTable(&archiveStream);
    archiveStream.wres = 0;
    LookToRead2_CreateVTable(&lookStream, False);
    lookStream.buf = nullptr;

    constexpr size_t kInputBufSize = (1 << 18);
    lookStream.buf = static_cast<Byte*>(ISzAlloc_Alloc(&allocImp, kInputBufSize));
    if (!lookStream.buf) return false;
    lookStream.bufSize = kInputBufSize;
    lookStream.realStream = &archiveStream.vt;
    LookToRead2_INIT(&lookStream);

    SzArEx_Init(&db);

    if (InFile_Open(&archiveStream.file, archive_path.c_str()) != 0) {
        ISzAlloc_Free(&allocImp, lookStream.buf);
        return false;
    }

    SRes res = SzArEx_Open(&db, &lookStream.vt, &allocImp, &allocTempImp);
    if (res != SZ_OK) {
        File_Close(&archiveStream.file);
        ISzAlloc_Free(&allocImp, lookStream.buf);
        return false;
    }

    UInt32 blockIndex = 0xFFFFFFFF;
    Byte* outBuffer = nullptr;
    size_t outBufferSize = 0;
    bool extracted = false;

    std::vector<UInt16> nameBuf;

    for (UInt32 i = 0; i < db.NumFiles; ++i) {
        if (SzArEx_IsDir(&db, i)) continue;

        size_t nameLen = SzArEx_GetFileNameUtf16(&db, i, nullptr);
        if (nameLen > nameBuf.size()) nameBuf.resize(nameLen + 1);
        SzArEx_GetFileNameUtf16(&db, i, nameBuf.data());
        std::string filename = utf16_to_utf8(nameBuf.data());

        bool match = target_pattern.empty();
        if (!match) {
            if (filename.find(target_pattern) != std::string::npos) match = true;
            auto slash = filename.find_last_of("/\\");
            std::string base = (slash == std::string::npos) ? filename : filename.substr(slash + 1);
            if (base == target_pattern) match = true;
        }

        if (match) {
            size_t offset = 0;
            size_t outSizeProcessed = 0;
            res = SzArEx_Extract(&db, &lookStream.vt, i,
                                 &blockIndex, &outBuffer, &outBufferSize,
                                 &offset, &outSizeProcessed,
                                 &allocImp, &allocTempImp);
            if (res == SZ_OK && outBuffer) {
                std::filesystem::path dest(dest_path);
                std::error_code ec;
                std::filesystem::create_directories(dest.parent_path(), ec);

                std::string tmp = dest_path + ".tmp-" + std::to_string(getpid());
                std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
                if (out.is_open()) {
                    out.write(reinterpret_cast<const char*>(outBuffer + offset), outSizeProcessed);
                    out.close();
                    if (!out.fail()) {
                        std::filesystem::rename(tmp, dest_path, ec);
                        if (!ec) extracted = true;
                    }
                }
                if (!extracted) {
                    std::filesystem::remove(tmp, ec);
                }
            }
            break;
        }
    }

    if (outBuffer) {
        ISzAlloc_Free(&allocImp, outBuffer);
    }
    SzArEx_Free(&db, &allocImp);
    File_Close(&archiveStream.file);
    if (lookStream.buf) {
        ISzAlloc_Free(&allocImp, lookStream.buf);
    }
    return extracted;
}

} // namespace seven_zip
