#pragma once
#include "il2cpp_api.hpp"
#include "translation_evidence.hpp"
#include "story_observation.hpp"
#include <cstdlib>
#include <charconv>
#include <string>
#include <unordered_map>
#include <mutex>

struct ManagedField {
    void* field = nullptr;
    enum Kind { Unsupported, String, Int32, UInt32 } kind = Unsupported;
};

inline void* managed_field_named(Il2CppObject* object, const char* name) {
    if (!object || !name) return nullptr;
    Il2CppClass* klass = il2cpp_object_get_class_fn ? il2cpp_object_get_class_fn(object) : nullptr;
    if (!klass) return nullptr;

    static std::mutex cache_mutex;
    std::lock_guard<std::mutex> lock(cache_mutex);
    static std::unordered_map<Il2CppClass*, std::unordered_map<std::string, void*>> cache;
    auto& field_map = cache[klass];
    auto it = field_map.find(name);
    if (it != field_map.end()) return it->second;

    void* found_field = nullptr;
    for (auto* k = klass; k; k = il2cpp_class_get_parent_fn(k)) {
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
            if (!*left && !*right) {
                found_field = field;
                break;
            }
        }
        if (found_field) break;
    }
    field_map[name] = found_field;
    return found_field;
}

inline ManagedField managed_field_at(Il2CppObject* object, uint32_t offset) {
    if (!object) return {};
    static std::mutex cache_mutex;
    std::lock_guard<std::mutex> lock(cache_mutex);
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

// IDs in localization JSON are typically ASCII, but some tables (e.g. IntroduceCharacter)
// use Unicode/Korean IDs. Fast-path ASCII and fall back to observed_utf8 for non-ASCII.
inline std::string managed_ascii_string(Il2CppString* value) {
    if (!value) return {};
    int length = il2cpp_string_length_fn(value);
    if (length <= 0 || length > 256) return {};
    const uint16_t* chars = il2cpp_string_chars_fn(value);
    if (!chars) return {};
    bool ascii_only = true;
    for (int i = 0; i < length; ++i) {
        if (chars[i] > 127) { ascii_only = false; break; }
    }
    if (ascii_only) {
        std::string result;
        result.reserve(length);
        for (int i = 0; i < length; ++i) result.push_back(static_cast<char>(chars[i]));
        return result;
    }
    return observed_utf8(chars, static_cast<size_t>(length));
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

class ManagedStringPool {
private:
    std::mutex mutex_;
    bool use_gchandles_ = false;
    struct Entry {
        uintptr_t handle = 0;
        Il2CppString* raw = nullptr;
    };
    std::unordered_map<std::string, Entry> pool_;
public:
    void enable_gchandles(bool enable) {
        std::lock_guard<std::mutex> lock(mutex_);
        use_gchandles_ = enable;
    }

    Il2CppString* intern(const std::string& text) {
        if (!il2cpp_string_new_fn) return nullptr;
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = pool_.find(text);
        if (it != pool_.end()) {
            if (it->second.handle && il2cpp_gchandle_get_target_fn) {
                Il2CppObject* obj = il2cpp_gchandle_get_target_fn(it->second.handle);
                if (obj) return reinterpret_cast<Il2CppString*>(obj);
            } else if (it->second.raw) {
                return it->second.raw;
            }
        }
        Il2CppString* s = il2cpp_string_new_fn(text.c_str());
        if (!s) return nullptr;
        Entry entry;
        if (use_gchandles_ && il2cpp_gchandle_new_fn) {
            entry.handle = il2cpp_gchandle_new_fn(reinterpret_cast<Il2CppObject*>(s), false);
        } else {
            entry.raw = s;
        }
        pool_[text] = entry;
        return s;
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (il2cpp_gchandle_free_fn) {
            for (auto& pair : pool_) {
                if (pair.second.handle) {
                    il2cpp_gchandle_free_fn(pair.second.handle);
                }
            }
        }
        pool_.clear();
    }

    size_t size() {
        std::lock_guard<std::mutex> lock(mutex_);
        return pool_.size();
    }
};

inline ManagedStringPool g_managed_string_pool;

inline bool read_managed_numeric_id(Il2CppObject* object, int& result) {
    std::string text = read_managed_id(object);
    if (text.empty()) return false;
    auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
}

inline bool managed_id_absent(Il2CppObject* object) {
    ManagedField field = managed_field_at(object, 16);
    if (field.kind != ManagedField::String) return false;
    Il2CppString* value = nullptr;
    il2cpp_field_get_value_fn(object, field.field, &value);
    return !value || il2cpp_string_length_fn(value) == 0;
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
        translation_evidence::write(false, "non_string_or_missing_field");
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
    Il2CppString* value = g_managed_string_pool.intern(text);
    if (!value) { translation_evidence::write(false, "string_allocation_failed"); return false; }
    il2cpp_gc_wbarrier_set_field_fn(object, reinterpret_cast<void**>(reinterpret_cast<char*>(object) + offset), value);
    ++g_managed_text_writes;
    translation_evidence::write(true);
    return true;
}

inline bool write_managed_text_named(Il2CppObject* object, const char* name, const std::string& text) {
    auto* field = managed_field_named(object, name);
    if (!field) { translation_evidence::unsupported_field(name); return false; }
    return write_managed_text(object, il2cpp_field_get_offset_fn(field), text);
}
