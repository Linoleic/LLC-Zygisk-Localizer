#pragma once
#include <string_view>

struct StringTableRule {
    std::string_view filename;
    size_t prefix_length;
    std::string_view excluded_prefix;
    const char* field;
    bool numeric;

    bool matches(std::string_view name) const {
        if (!excluded_prefix.empty() && name.substr(0, excluded_prefix.size()) == excluded_prefix)
            return false;
        if (!prefix_length) return name == filename;
        return name.substr(0, prefix_length) == filename.substr(0, prefix_length) &&
            (prefix_length <= filename.size() || name.size() == filename.size());
    }
};
