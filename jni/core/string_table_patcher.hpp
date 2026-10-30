#pragma once
#include "managed_fields.hpp"
#include "content_sync.hpp"

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

inline ContentStamp stamp(Il2CppObject* container) {
    ContentStamp value; value.owner = reinterpret_cast<uintptr_t>(container);
    auto* dictionary = reference(container, "_dic");
    if (!dictionary || strcmp(il2cpp_class_get_name_fn(il2cpp_object_get_class_fn(dictionary)), "Dictionary`2")) return value;
    int32_t count = 0;
    auto* entries = reference(dictionary, "_entries");
    if (!entries || !integer(dictionary, "_count", count) || count < 0 || count > 500000 ||
        static_cast<uintptr_t>(count) > il2cpp_array_length_fn(entries)) return value;
    value.dictionary = reinterpret_cast<uintptr_t>(dictionary); value.entries = reinterpret_cast<uintptr_t>(entries); value.count = count;
    if (!integer(dictionary, "_version", value.version)) integer(dictionary, "version", value.version);
    return value;
}

// Named metadata and Array.GetValue boxing avoid assuming a generic Entry stride.
// Only existing rows of the expected class and matching row/dictionary ID change.
template<class Visitor>
inline int visit(Il2CppObject* container, const char* expected_class, Visitor visitor, bool numeric_ids = false,
                 bool allow_unidentified_rows = false) {
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
    if (length < 2 || strcmp(name + length - 2, "[]") || static_cast<uintptr_t>(count) > il2cpp_array_length_fn(entries)) return 0;
    Root array(entries); if (!array.get()) return 0;
    const MethodInfo* get_value = nullptr;
    for (auto* k = klass; k && !get_value; k = il2cpp_class_get_parent_fn(k)) {
        void* iterator = nullptr;
        while (auto* method = il2cpp_class_get_methods_fn(k, &iterator)) {
            if (strcmp(il2cpp_method_get_name_fn(method), "GetValue") || il2cpp_method_get_param_count_fn(method) != 1) continue;
            auto* param = il2cpp_class_from_type_fn(il2cpp_method_get_param_fn(method, 0));
            if (param && !strcmp(il2cpp_class_get_name_fn(param), "Int32")) { get_value = method; break; }
        }
    }
    if (!get_value) return 0;
    int matched = 0;
    for (int32_t index = 0; index < count; ++index) {
        Il2CppException* exception = nullptr; void* args[] = {&index};
        auto* boxed = il2cpp_runtime_invoke_fn(get_value, array.get(), args, &exception);
        if (exception || !boxed || !il2cpp_class_is_valuetype_fn(il2cpp_object_get_class_fn(boxed))) return 0;
        Root entry(boxed); if (!entry.get()) return 0;
        int32_t hash = -1;
        if (!integer(entry.get(), "hashCode", hash)) return 0;
        if (hash < 0) continue;
        auto key = read_named_managed_id(entry.get(), "key");
        auto* row = reference(entry.get(), "value");
        const std::string row_class = row ? il2cpp_class_get_name_fn(il2cpp_object_get_class_fn(row)) : "null";
        const auto row_id = row ? read_named_managed_id(row, "id") : std::string{};
        // Opening Dante's notebook adds correctly typed rows with numeric keys
        // but no model ID. They cannot be associated with a source record.
        // Leave them intact and report unknown coverage, rather than retrying
        // known translations forever or guessing an ID from a dictionary key.
        if (row && row_class == expected_class && row_id.empty() && allow_unidentified_rows) {
            translation_evidence::unidentified_row(); continue;
        }
        if (!row || row_class != expected_class || row_id != key) {
            translation_evidence::invalid_row("key=" + key + ";class=" + row_class + ";id=" + row_id); continue;
        }
        if (key.empty()) { translation_evidence::unidentified_row(); continue; }
        if (numeric_ids && key == "0") continue; // Numeric zero is a model sentinel, not a missing translation.
        translation_evidence::observe(key);
        Root rooted_row(row); if (!rooted_row.get()) return 0;
        int result = visitor(rooted_row.get(), key);
        if (result < 0) return 0;
        matched += result;
    }
    translation_evidence::complete();
    return matched;
}
inline int patch(Il2CppObject* container, const std::unordered_map<std::string, std::string>& translations,
                 const char* expected_class, const char* text_field) {
    return visit(container, expected_class, [&](Il2CppObject* row, const std::string& key) {
        auto it = translations.find(key);
        if (it == translations.end() || it->second.empty()) return 0;
        return write_managed_text_named(row, text_field, it->second) ? 1 : -1;
    });
}
} // namespace string_table
