#pragma once
#include "string_table_patcher.hpp"
#include <functional>
#include <unordered_set>

// AbEventCharDlgRoot -> personality dictionary -> event/probability dictionaries
// -> AbEventDlgListContainer -> List<TextData_AbnormalityEventCharDlg>.
// Follow only the verified named schema. A shared list/row is visited once.
namespace event_dialogue {
inline bool is(Il2CppObject* object, const char* name) {
    return object && !strcmp(il2cpp_class_get_name_fn(il2cpp_object_get_class_fn(object)), name);
}
class Walker {
    std::unordered_set<Il2CppObject*> dictionaries_, lists_, rows_;
    size_t budget_ = 0;
    uint64_t hash_ = 1469598103934665603ULL;
    ContentStamp result_;
    const std::function<bool(Il2CppObject*)>& visitor_;
    void mix(uint64_t value) { hash_ = (hash_ ^ value) * 1099511628211ULL; }
    bool list(Il2CppObject* wrapper) {
        if (!is(wrapper, "AbEventDlgListContainer")) return false;
        auto* list = string_table::reference(wrapper, "_dlgList");
        if (!is(list, "List`1")) return false;
        if (!lists_.insert(list).second) return true;
        auto* items = string_table::reference(list, "_items");
        if (!items) return false;
        const char* item_name = il2cpp_class_get_name_fn(il2cpp_object_get_class_fn(items));
        size_t item_name_length = item_name ? strlen(item_name) : 0;
        if (item_name_length < 2 || strcmp(item_name + item_name_length - 2, "[]")) return false;
        int32_t size = 0;
        if (!items || !string_table::integer(list, "_size", size) || size < 0 ||
            size > 20000 || static_cast<uintptr_t>(size) > il2cpp_array_length_fn(items) ||
            (budget_ += size) > 50000) return false;
        mix(reinterpret_cast<uintptr_t>(list)); mix(reinterpret_cast<uintptr_t>(items)); mix(size);
        int32_t version = 0; string_table::integer(list, "_version", version); mix(version);
        result_.count += size;
        if (!visitor_) return true;
        string_table::Root array(items);
        if (!array.get()) return false;
        auto* get = string_table::array_get_value_method(il2cpp_object_get_class_fn(items));
        if (!get) return false;
        for (int32_t i = 0; i < size; ++i) {
            auto* row = string_table::array_value(array.get(), get, i);
            if (!is(row, "TextData_AbnormalityEventCharDlg")) return false;
            if (rows_.insert(row).second && !visitor_(row)) return false;
        }
        return true;
    }
    bool dictionary(Il2CppObject* object, int depth) {
        if (!is(object, "Dictionary`2")) return false;
        if (!dictionaries_.insert(object).second) return true;
        int32_t count = 0, version = 0;
        auto* entries = string_table::reference(object, "_entries");
        if (!string_table::integer(object, "_count", count) || count < 0 || count > 20000) return false;
        string_table::integer(object, "_version", version);
        mix(reinterpret_cast<uintptr_t>(object)); mix(reinterpret_cast<uintptr_t>(entries)); mix(count); mix(version);
        if (!count) return true;
        if (!entries) return false;
        auto* klass = il2cpp_object_get_class_fn(entries);
        const char* name = il2cpp_class_get_name_fn(klass);
        size_t length = name ? strlen(name) : 0;
        if (length < 2 || strcmp(name + length - 2, "[]")) return false;
        auto capacity = il2cpp_array_length_fn(entries);
        if (capacity < static_cast<uintptr_t>(count) ||
            capacity > 20000 || (budget_ += capacity) > 50000) return false;
        auto* get = string_table::array_get_value_method(klass);
        if (!get) return false;
        string_table::Root array(entries);
        if (!array.get()) return false;
        int active = 0, deleted = 0;
        for (size_t i = 0; i < capacity; ++i) {
            auto* boxed = string_table::array_value(array.get(), get, static_cast<int32_t>(i));
            if (!boxed || !il2cpp_class_is_valuetype_fn(il2cpp_object_get_class_fn(boxed))) return false;
            string_table::Root entry(boxed);
            if (!entry.get()) return false;
            int32_t hash = -1;
            if (!string_table::integer(entry.get(), "hashCode", hash)) return false;
            if (hash < 0) { ++deleted; continue; }
            auto* value = string_table::reference(entry.get(), "value");
            if (!value) continue; // unused capacity slot
            ++active;
            if (depth == 0) {
                if (!is(value, "AbEventKeyDictionaryContainer") ||
                    !dictionary(string_table::reference(value, "_dict"), 1) ||
                    !dictionary(string_table::reference(value, "_dictByProb"), 1)) return false;
            } else if (!list(value)) return false;
        }
        return active + deleted == count;
    }
public:
    explicit Walker(const std::function<bool(Il2CppObject*)>& visitor) : visitor_(visitor) {}
    ContentStamp inspect(Il2CppObject* root) {
        if (!is(root, "AbEventCharDlgRoot") || !il2cpp_gchandle_new_fn ||
            !il2cpp_gchandle_get_target_fn || !il2cpp_gchandle_free_fn) return {};
        auto* dict = string_table::reference(root, "_personalityDict");
        result_.owner = reinterpret_cast<uintptr_t>(root);
        result_.dictionary = reinterpret_cast<uintptr_t>(dict);
        result_.entries = reinterpret_cast<uintptr_t>(string_table::reference(dict, "_entries"));
        string_table::Root pinned(root);
        if (!pinned.get() || !dictionary(dict, 0)) return {};
        result_.version = static_cast<int32_t>(hash_ ^ (hash_ >> 32));
        return result_;
    }
};
inline ContentStamp inspect(Il2CppObject* root, const std::function<bool(Il2CppObject*)>& visitor = {}) {
    return Walker(visitor).inspect(root);
}
} // namespace event_dialogue
