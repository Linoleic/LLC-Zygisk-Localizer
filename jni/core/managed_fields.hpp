#pragma once
#include "il2cpp_api.hpp"
#include <cstdlib>
#include <charconv>
#include <string>
#include <unordered_map>

struct ManagedField {
    void* field = nullptr;
    enum Kind { Unsupported, String, Int32, UInt32 } kind = Unsupported;
};

inline void* managed_field_named(Il2CppObject* object, const char* name) {
    if (!object) return nullptr;
    for (auto* k = il2cpp_object_get_class_fn(object); k; k = il2cpp_class_get_parent_fn(k)) {
        void* iter = nullptr;
        while (void* field = il2cpp_class_get_fields_fn(k, &iter)) {
            if (il2cpp_field_get_flags_fn(field) & 0x10) continue;
            const char* candidate = il2cpp_field_get_name_fn(field);
            if (!candidate) continue;
            const char* left = candidate;
            const char* right = name;
            while (*left && *right) {
                char a = *left >= 'A' && *left <= 'Z' ? *left + ('a' - 'A') : *left;
                char b = *right >= 'A' && *right <= 'Z' ? *right + ('a' - 'A') : *right;
                if (a != b) break;
                ++left; ++right;
            }
            if (!*left && !*right) return field;
        }
    }
    return nullptr;
}

inline ManagedField managed_field_at(Il2CppObject* object, uint32_t offset) {
    if (!object) return {};
    static std::unordered_map<Il2CppClass*, std::unordered_map<uint32_t, ManagedField>> cache;
    Il2CppClass* klass = il2cpp_object_get_class_fn(object);
    auto& fields = cache[klass];
    auto cached = fields.find(offset);
    if (cached != fields.end()) return cached->second;

    ManagedField result;
    for (Il2CppClass* k = klass; k && !result.field; k = il2cpp_class_get_parent_fn(k)) {
        void* iter = nullptr;
        while (void* field = il2cpp_class_get_fields_fn(k, &iter)) {
            if ((il2cpp_field_get_flags_fn(field) & 0x10) ||
                il2cpp_field_get_offset_fn(field) != offset) continue;
            result.field = field;
            Il2CppClass* type = il2cpp_class_from_type_fn(il2cpp_field_get_type_fn(field));
            const char* name = type ? il2cpp_class_get_name_fn(type) : nullptr;
            if (name && strcmp(name, "String") == 0) result.kind = ManagedField::String;
            else if (name && strcmp(name, "Int32") == 0) result.kind = ManagedField::Int32;
            else if (name && strcmp(name, "UInt32") == 0) result.kind = ManagedField::UInt32;
            break;
        }
    }
    fields.emplace(offset, result);
    return result;
}

// IDs in the localization JSON are ASCII. Inspect the field metadata before
// reading it; an integer greater than 0x10000 is not a managed string pointer.
inline std::string managed_ascii_string(Il2CppString* value) {
    if (!value) return {};
    int length = il2cpp_string_length_fn(value);
    if (length <= 0 || length > 256) return {};
    const uint16_t* chars = il2cpp_string_chars_fn(value);
    std::string result;
    for (int i = 0; i < length; ++i) {
        if (chars[i] > 127) return {};
        result.push_back(static_cast<char>(chars[i]));
    }
    return result;
}

inline std::string read_managed_id(Il2CppObject* object, uint32_t offset = 16) {
    ManagedField field = managed_field_at(object, offset);
    if (field.kind == ManagedField::Int32) {
        int32_t value = 0;
        il2cpp_field_get_value_fn(object, field.field, &value);
        return std::to_string(value);
    }
    if (field.kind == ManagedField::UInt32) {
        uint32_t value = 0;
        il2cpp_field_get_value_fn(object, field.field, &value);
        return std::to_string(value);
    }
    if (field.kind != ManagedField::String) return {};
    Il2CppString* value = nullptr;
    il2cpp_field_get_value_fn(object, field.field, &value);
    return managed_ascii_string(value);
}

inline std::string read_named_managed_id(Il2CppObject* object, const char* name) {
    auto* field = managed_field_named(object, name);
    return field ? read_managed_id(object, il2cpp_field_get_offset_fn(field)) : std::string{};
}

inline Il2CppObject* read_named_managed_list(Il2CppObject* object, const char* name) {
    if (!object) return nullptr;
    for (auto* k = il2cpp_object_get_class_fn(object); k; k = il2cpp_class_get_parent_fn(k)) {
        auto* field = il2cpp_class_get_field_from_name_fn(k, name);
        if (!field || (il2cpp_field_get_flags_fn(field) & 0x10)) continue;
        auto* type = il2cpp_class_from_type_fn(il2cpp_field_get_type_fn(field));
        if (!type || strcmp(il2cpp_class_get_name_fn(type), "List`1") != 0) return nullptr;
        Il2CppObject* list = nullptr;
        il2cpp_field_get_value_fn(object, field, &list);
        return list;
    }
    return nullptr;
}

inline uint64_t g_managed_text_writes = 0;

inline bool read_managed_numeric_id(Il2CppObject* object, int& result) {
    std::string text = read_managed_id(object);
    if (text.empty()) return false;
    auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
}

inline Il2CppObject* read_managed_list(Il2CppObject* object, uint32_t offset) {
    ManagedField field = managed_field_at(object, offset);
    if (!field.field) return nullptr;
    Il2CppClass* type = il2cpp_class_from_type_fn(il2cpp_field_get_type_fn(field.field));
    const char* name = type ? il2cpp_class_get_name_fn(type) : nullptr;
    if (!name || strcmp(name, "List`1") != 0) return nullptr;
    Il2CppObject* value = nullptr;
    il2cpp_field_get_value_fn(object, field.field, &value);
    return value;
}

inline bool write_managed_text(Il2CppObject* object, uint32_t offset, const std::string& text) {
    ManagedField field = managed_field_at(object, offset);
    if (field.kind != ManagedField::String) {
        // Log once per class/offset, rather than silently counting a match as a write.
        static std::unordered_map<Il2CppClass*, std::unordered_map<uint32_t, bool>> warned;
        Il2CppClass* klass = object ? il2cpp_object_get_class_fn(object) : nullptr;
        if (!warned[klass][offset]) {
            warned[klass][offset] = true;
            LOGW("[LLC-DATA] Skipping non-string/missing field: class=%s offset=%u",
                 klass ? il2cpp_class_get_name_fn(klass) : "null", offset);
            if (klass && warned[klass].size() == 1) {
                void* iter = nullptr;
                while (void* info = il2cpp_class_get_fields_fn(klass, &iter)) {
                    Il2CppClass* type = il2cpp_class_from_type_fn(il2cpp_field_get_type_fn(info));
                    LOGI("[LLC-SCHEMA] %s.%s offset=%u type=%s", il2cpp_class_get_name_fn(klass),
                         il2cpp_field_get_name_fn(info), il2cpp_field_get_offset_fn(info),
                         type ? il2cpp_class_get_name_fn(type) : "unknown");
                }
            }
        }
        return false;
    }
    Il2CppString* value = il2cpp_string_new_fn(text.c_str());
    if (!value) return false;
    il2cpp_gc_wbarrier_set_field_fn(object, reinterpret_cast<void**>(reinterpret_cast<char*>(object) + offset), value);
    ++g_managed_text_writes;
    return true;
}

inline bool write_managed_text_named(Il2CppObject* object, const char* name, const std::string& text) {
    auto* field = managed_field_named(object, name);
    return field && write_managed_text(object, il2cpp_field_get_offset_fn(field), text);
}
