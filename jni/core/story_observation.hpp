#pragma once
#include <string>
#include <cstdint>

inline bool safe_story_id(const std::string& id) {
    return !id.empty() && id.size() <= 127 &&
        id.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-") == std::string::npos;
}

inline std::string observed_utf8(const uint16_t* chars, size_t size) {
    std::string out;
    auto append = [&](uint32_t c) {
        if (c < 0x80) out.push_back(static_cast<char>(c));
        else if (c < 0x800) { out.push_back(0xc0 | (c >> 6)); out.push_back(0x80 | (c & 63)); }
        else if (c < 0x10000) {
            out.push_back(0xe0 | (c >> 12)); out.push_back(0x80 | ((c >> 6) & 63)); out.push_back(0x80 | (c & 63));
        } else {
            out.push_back(0xf0 | (c >> 18)); out.push_back(0x80 | ((c >> 12) & 63));
            out.push_back(0x80 | ((c >> 6) & 63)); out.push_back(0x80 | (c & 63));
        }
    };
    if (!chars) return out;
    for (size_t i = 0; i < size; ++i) {
        uint32_t c = chars[i];
        if (c >= 0xd800 && c <= 0xdbff) {
            if (i + 1 < size && chars[i + 1] >= 0xdc00 && chars[i + 1] <= 0xdfff) {
                c = 0x10000 + ((c - 0xd800) << 10) + (chars[++i] - 0xdc00);
            } else c = 0xfffd;
        } else if (c >= 0xdc00 && c <= 0xdfff) c = 0xfffd;
        append(c);
    }
    return out;
}
