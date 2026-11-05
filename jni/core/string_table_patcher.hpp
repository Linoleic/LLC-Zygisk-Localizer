#pragma once
#include "managed_fields.hpp"
#include "content_sync.hpp"
#include <mutex>
#include <unordered_map>

namespace string_table {
inline Il2CppObject* reference(Il2CppObject* owner, const char* name) {
    auto* field = managed_field_named(owner, name);
    auto* type = field ? il2cpp_class_from_type_fn(il2cpp_field_get_type_fn(field)) : nullptr;
    if (!type || il2cpp_class_is_valuetype_fn(type)) return nullptr;
    Il2CppObject* value = nullptr; il2cpp_field_get_value_fn(owner, field, &value); return value;
}
inline bool integer(Il2CppObject* owner, const char* name, int32_t& value) {
    auto* field = managed_field_named(owner, name);
    auto* type = field ? il2cpp_class_from_type_fn(il2cpp_field_get_type_fn(field)) : nullptr;
    if (!type || strcmp(il2cpp_class_get_name_fn(type), "Int32")) return false;
    il2cpp_field_get_value_fn(owner, field, &value); return true;
}
struct Root {
    uintptr_t handle;
    explicit Root(Il2CppObject* object) : handle(object ? il2cpp_gchandle_new_fn(object, false) : 0) {}
    ~Root() { if (handle) il2cpp_gchandle_free_fn(handle); }
    Il2CppObject* get() const { return handle ? il2cpp_gchandle_get_target_fn(handle) : nullptr; }
    Root(const Root&) = delete;
};

// Only opt-in targets may treat this as an observed empty snapshot. Missing or
// malformed metadata must never turn a not-yet-loaded table into a success.
inline ContentStamp verified_empty_stamp(Il2CppObject* container) {
    ContentStamp value;
    auto* dictionary = reference(container, "_dic");
    if (!dictionary || strcmp(il2cpp_class_get_name_fn(il2cpp_object_get_class_fn(dictionary)), "Dictionary`2")) return value;
    int32_t count = -1, version = 0;
    if (!integer(dictionary, "_count", count) || count != 0 ||
        (!integer(dictionary, "_version", version) && !integer(dictionary, "version", version))) return value;
    // The field must exist even when a newly created Dictionary has no array.
    auto* entries_field = managed_field_named(dictionary, "_entries");
    auto* entries_type = entries_field ? il2cpp_class_from_type_fn(il2cpp_field_get_type_fn(entries_field)) : nullptr;
    const char* array_name = entries_type ? il2cpp_class_get_name_fn(entries_type) : nullptr;
    size_t length = array_name ? strlen(array_name) : 0;
    if (length < 2 || strcmp(array_name + length - 2, "[]")) return value;
    auto* entries = reference(dictionary, "_entries");
    if (entries && (il2cpp_object_get_class_fn(entries) != entries_type || il2cpp_array_length_fn(entries) > 500000)) return value;
    value.owner = reinterpret_cast<uintptr_t>(container); value.dictionary = reinterpret_cast<uintptr_t>(dictionary);
    value.entries = reinterpret_cast<uintptr_t>(entries); value.version = version; value.verified_empty = true;
    return value;
}

inline ContentStamp stamp(Il2CppObject* container) {
    ContentStamp value; value.owner = reinterpret_cast<uintptr_t>(container);
    auto* dictionary = reference(container, "_dic");
    if (!dictionary && container) {
        auto* candidate = *reinterpret_cast<Il2CppObject**>(reinterpret_cast<char*>(container) + 16);
        if (candidate && il2cpp_object_get_class_fn) {
            auto* klass = il2cpp_object_get_class_fn(candidate);
            const char* name = klass ? il2cpp_class_get_name_fn(klass) : nullptr;
            if (name && !strcmp(name, "Dictionary`2")) dictionary = candidate;
        }
    }
    if (!dictionary || strcmp(il2cpp_class_get_name_fn(il2cpp_object_get_class_fn(dictionary)), "Dictionary`2")) return value;
    int32_t count = 0;
    auto* entries = reference(dictionary, "_entries");
    if (!entries) {
        auto* candidate = *reinterpret_cast<Il2CppObject**>(reinterpret_cast<char*>(dictionary) + 24);
        if (candidate && il2cpp_object_get_class_fn) {
            auto* klass = il2cpp_object_get_class_fn(candidate);
            const char* name = klass ? il2cpp_class_get_name_fn(klass) : nullptr;
            if (name && strstr(name, "[]")) entries = candidate;
        }
    }
    if (!integer(dictionary, "_count", count)) {
        count = *reinterpret_cast<int32_t*>(reinterpret_cast<char*>(dictionary) + 32);
    }
    if (!entries || count < 0 || count > 500000 ||
        static_cast<uintptr_t>(count) > il2cpp_array_length_fn(entries)) return value;
    value.dictionary = reinterpret_cast<uintptr_t>(dictionary); value.entries = reinterpret_cast<uintptr_t>(entries); value.count = count;
    if (!integer(dictionary, "_version", value.version)) integer(dictionary, "version", value.version);
    return value;
}

// Named metadata and Array.GetValue boxing avoid assuming a generic Entry stride.
// Only existing rows of the expected class and matching row/dictionary ID change.
struct VisitStats { bool complete = false; size_t invalid = 0, unidentified = 0; };
inline const MethodInfo* array_get_value_method(Il2CppClass* klass) {
    for (auto* k = klass; k; k = il2cpp_class_get_parent_fn(k)) {
        void* iterator = nullptr;
        while (auto* method = il2cpp_class_get_methods_fn(k, &iterator)) {
            if (strcmp(il2cpp_method_get_name_fn(method), "GetValue") || il2cpp_method_get_param_count_fn(method) != 1) continue;
            auto* param = il2cpp_class_from_type_fn(il2cpp_method_get_param_fn(method, 0));
            if (param && !strcmp(il2cpp_class_get_name_fn(param), "Int32")) { return method; }
        }
    }
    return nullptr;
}
inline Il2CppObject* array_value(Il2CppObject* array, const MethodInfo* method, int32_t index) {
    if (!array || !method) return nullptr;
    Il2CppException* exception = nullptr;
    void* args[] = {&index};
    auto* value = il2cpp_runtime_invoke_fn(method, array, args, &exception);
    return exception ? nullptr : value;
}
inline Il2CppObject* array_value(Il2CppObject* array, int32_t index) {
    if (!array) return nullptr;
    return array_value(array, array_get_value_method(il2cpp_object_get_class_fn(array)), index);
}
template<class Visitor>
inline int visit(Il2CppObject* container, const char* expected_class, Visitor visitor, bool numeric_ids = false,
                 bool allow_unidentified_rows = true, VisitStats* stats = nullptr) {
    if (stats) *stats = VisitStats{};
    if (!container || !il2cpp_gchandle_new_fn || !il2cpp_gchandle_get_target_fn || !il2cpp_gchandle_free_fn) return 0;
    auto* dictionary = reference(container, "_dic");
    if (!dictionary || strcmp(il2cpp_class_get_name_fn(il2cpp_object_get_class_fn(dictionary)), "Dictionary`2")) return 0;
    int32_t count = 0;
    if (!integer(dictionary, "_count", count) || count <= 0 || count > 500000) return 0;
    auto* entries = reference(dictionary, "_entries");
    if (!entries) return 0;
    auto* klass = il2cpp_object_get_class_fn(entries);
    const char* name = il2cpp_class_get_name_fn(klass);
    size_t length = name ? strlen(name) : 0;
    const uintptr_t capacity = il2cpp_array_length_fn(entries);
    if (length < 2 || strcmp(name + length - 2, "[]") || capacity < static_cast<uintptr_t>(count) || capacity > 500000) return 0;
    Root array(entries); if (!array.get()) return 0;
    const MethodInfo* get_value = array_get_value_method(klass);
    if (!get_value) return 0;
    int matched = 0, active_entries = 0, deleted_entries = 0;
    for (int32_t index = 0; index < static_cast<int32_t>(capacity); ++index) {
        auto* boxed = array_value(array.get(), get_value, index);
        if (!boxed || !il2cpp_class_is_valuetype_fn(il2cpp_object_get_class_fn(boxed))) return 0;
        Root entry(boxed); if (!entry.get()) return 0;
        int32_t hash = -1;
        if (!integer(entry.get(), "hashCode", hash)) return 0;
        if (hash < 0) { ++deleted_entries; continue; }
        auto key = read_named_managed_id(entry.get(), "key");
        auto* row = reference(entry.get(), "value");
        // Unused Dictionary slots are zero-initialized on this runtime. They
        // have hashCode == 0 and no key/value, unlike removed slots (-1).
        if (key.empty() && !row) continue;
        ++active_entries;
        const std::string row_class = row ? il2cpp_class_get_name_fn(il2cpp_object_get_class_fn(row)) : "null";
        const auto row_id = row ? read_named_managed_id(row, "id") : std::string{};
        // Notebook and gift navigation add correctly typed rows without model
        // IDs to their lookup caches. They cannot identify a source record.
        // Leave them intact and report unknown coverage, rather than retrying
        // known translations forever or guessing an ID from a dictionary key.
        if (row && row_class == expected_class && row_id.empty() && allow_unidentified_rows) {
            if (stats) ++stats->unidentified;
            translation_evidence::unidentified_row(); continue;
        }
        if (!row || row_class != expected_class || row_id != key) {
            if (stats) ++stats->invalid;
            translation_evidence::invalid_row("key=" + key + ";class=" + row_class + ";id=" + row_id); continue;
        }
        if (key.empty()) { if (stats) ++stats->unidentified; translation_evidence::unidentified_row(); continue; }
        if (numeric_ids && key == "0") continue; // Numeric zero is a model sentinel, not a missing translation.
        translation_evidence::observe(key);
        Root rooted_row(row); if (!rooted_row.get()) return 0;
        int result = visitor(rooted_row.get(), key);
        if (result < 0) return 0;
        matched += result;
    }
    int32_t free_count = 0;
    const int32_t expected_active = integer(dictionary, "_freeCount", free_count) && free_count >= 0
        ? (count - free_count)
        : (count - deleted_entries);
    if (active_entries != expected_active || expected_active < 0) { if (stats) ++stats->invalid; return 0; }
    translation_evidence::complete();
    if (stats) stats->complete = true;
    return matched;
}
inline int patch(Il2CppObject* container, const std::unordered_map<std::string, std::string>& translations,
                 const char* expected_class, const char* text_field, bool numeric_ids = false,
                 bool allow_unidentified_rows = true) {
    return visit(container, expected_class, [&](Il2CppObject* row, const std::string& key) {
        auto it = translations.find(key);
        if (it == translations.end() || it->second.empty()) return 0;
        return write_managed_text_named(row, text_field, it->second) ? 1 : -1;
    }, numeric_ids, allow_unidentified_rows);
}
} // namespace string_table
