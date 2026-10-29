#pragma once
#include "managed_fields.hpp"

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
    explicit Root(Il2CppObject* object) : handle(il2cpp_gchandle_new_fn(object, false)) {}
    ~Root() { if (handle) il2cpp_gchandle_free_fn(handle); }
    Il2CppObject* get() const { return handle ? il2cpp_gchandle_get_target_fn(handle) : nullptr; }
    Root(const Root&) = delete;
};

// Named metadata and Array.GetValue boxing avoid assuming a generic Entry stride.
// Only existing rows of the expected class and matching row/dictionary ID change.
inline int patch(Il2CppObject* container, const std::unordered_map<std::string, std::string>& translations,
                 const char* expected_class, const char* text_field) {
    if (!container || translations.empty() || !il2cpp_gchandle_new_fn || !il2cpp_gchandle_get_target_fn || !il2cpp_gchandle_free_fn) return 0;
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
        auto it = translations.find(key);
        if (it == translations.end() || it->second.empty()) continue;
        auto* row = reference(entry.get(), "value");
        if (!row || strcmp(il2cpp_class_get_name_fn(il2cpp_object_get_class_fn(row)), expected_class) || read_named_managed_id(row, "id") != key) continue;
        if (!write_managed_text_named(row, text_field, it->second)) return 0;
        ++matched;
    }
    return matched;
}
} // namespace string_table
