#include "string_table_patcher.hpp"
#include <cassert>
#include <vector>

struct Field;
struct Il2CppClass { const char* name; Il2CppClass* parent; std::vector<Field*> fields; bool value_type = false; std::vector<MethodInfo*> methods; };
struct Il2CppType { Il2CppClass* klass; };
struct Field { uint32_t offset; const char* name; Il2CppType type; };
struct Il2CppObject { Il2CppClass* klass; void* monitor = nullptr; };
struct Il2CppString { Il2CppObject object; std::u16string text; };
struct Row { Il2CppObject object; Il2CppString* id; Il2CppString* text; };
struct Entry { Il2CppObject object; int32_t hash; int32_t padding = 0; Il2CppString* key; Il2CppObject* value; };
struct Array { Il2CppObject object; std::vector<Entry*> slots; };
struct Dictionary { Il2CppObject object; int32_t count; int32_t padding = 0; Il2CppObject* entries; };
struct Container { Il2CppObject object; Il2CppObject* dictionary; };
static std::unordered_map<uintptr_t, Il2CppObject*> handles;
static bool throw_get_value = false;
static Il2CppType* int_parameter;

int main() {
    Il2CppClass integer{"Int32", nullptr, {}, true, {}}, text{"String", nullptr, {}, false, {}};
    Field row_id{16, "id", {&text}}, row_text{24, "name", {&text}}, numeric_text{24, "name", {&integer}};
    Il2CppClass row_type{"TextData_SkillTag", nullptr, {&row_id, &row_text}, false, {}};
    Il2CppClass wrong_field_type{"TextData_SkillTag", nullptr, {&row_id, &numeric_text}, false, {}};
    Field hash{16, "hashCode", {&integer}}, key{24, "key", {&text}}, value{32, "value", {&row_type}};
    Il2CppClass entry_type{"Entry", nullptr, {&hash, &key, &value}, true, {}};
    MethodInfo get_value{};
    Il2CppClass array_type{"Entry[]", nullptr, {}, false, {&get_value}};
    Field count{16, "_count", {&integer}}, entries{24, "_entries", {&array_type}};
    Il2CppClass dictionary_type{"Dictionary`2", nullptr, {&count, &entries}, false, {}};
    Field dic{16, "_dic", {&dictionary_type}};
    Il2CppClass container_type{"JsonDataList`1", nullptr, {&dic}, false, {}};
    Il2CppType parameter{&integer}; int_parameter = &parameter;
    il2cpp_object_get_class_fn = [](Il2CppObject* o) { return o->klass; };
    il2cpp_class_get_name_fn = [](Il2CppClass* k) { return k->name; };
    il2cpp_class_get_parent_fn = [](Il2CppClass* k) { return k->parent; };
    il2cpp_class_is_valuetype_fn = [](const Il2CppClass* k) { return k->value_type; };
    il2cpp_class_get_fields_fn = [](Il2CppClass* k, void** it) -> void* {
        size_t n = reinterpret_cast<uintptr_t>(*it); if (n >= k->fields.size()) return nullptr;
        *it = reinterpret_cast<void*>(n + 1); return k->fields[n];
    };
    il2cpp_field_get_flags_fn = [](void*) { return 0; };
    il2cpp_field_get_name_fn = [](void* f) { return static_cast<Field*>(f)->name; };
    il2cpp_field_get_offset_fn = [](void* f) { return static_cast<Field*>(f)->offset; };
    il2cpp_field_get_type_fn = [](void* f) -> const Il2CppType* { return &static_cast<Field*>(f)->type; };
    il2cpp_class_from_type_fn = [](const Il2CppType* t) { return t->klass; };
    il2cpp_field_get_value_fn = [](Il2CppObject* o, void* f, void* out) {
        auto* field = static_cast<Field*>(f);
        memcpy(out, reinterpret_cast<char*>(o) + field->offset, !strcmp(field->type.klass->name, "Int32") ? 4 : sizeof(void*));
    };
    il2cpp_string_length_fn = [](Il2CppString* s) { return static_cast<int32_t>(s->text.size()); };
    il2cpp_string_chars_fn = [](Il2CppString* s) { return reinterpret_cast<const uint16_t*>(s->text.c_str()); };
    il2cpp_string_new_fn = [](const char* s) { auto* r = new Il2CppString{}; while (*s) r->text.push_back(*s++); return r; };
    il2cpp_gc_wbarrier_set_field_fn = [](Il2CppObject*, void** address, void* v) { *address = v; };
    il2cpp_gchandle_new_fn = [](Il2CppObject* o, bool) { static uintptr_t next = uintptr_t{1} << 40; handles[++next] = o; return next; };
    il2cpp_gchandle_get_target_fn = [](uintptr_t h) { return handles.at(h); };
    il2cpp_gchandle_free_fn = [](uintptr_t h) { handles.erase(h); };
    il2cpp_array_length_fn = [](void* a) { return static_cast<uintptr_t>(static_cast<Array*>(a)->slots.size()); };
    il2cpp_class_get_methods_fn = [](Il2CppClass* k, void** it) -> const MethodInfo* {
        size_t n = reinterpret_cast<uintptr_t>(*it); if (n >= k->methods.size()) return nullptr;
        *it = reinterpret_cast<void*>(n + 1); return k->methods[n];
    };
    il2cpp_method_get_name_fn = [](const MethodInfo*) { return "GetValue"; };
    il2cpp_method_get_param_count_fn = [](const MethodInfo*) { return 1u; };
    il2cpp_method_get_param_fn = [](const MethodInfo*, uint32_t) -> const Il2CppType* { return int_parameter; };
    il2cpp_runtime_invoke_fn = [](const MethodInfo*, void* obj, void** args, Il2CppException** ex) -> Il2CppObject* {
        if (throw_get_value) { *ex = reinterpret_cast<Il2CppException*>(1); return nullptr; }
        return &static_cast<Array*>(obj)->slots.at(*static_cast<int32_t*>(args[0]))->object;
    };
    Il2CppString id{{&text}, u"WhenUse"}, other{{&text}, u"Other"}, original{{&text}, u"original"};
    Row row{{&row_type}, &id, &original};
    Entry entry{{&entry_type}, 10, 0, &id, &row.object};
    Entry deleted{{&entry_type}, -1, 0, &other, &row.object};
    Array array{{&array_type}, {&entry, &deleted}};
    Dictionary dictionary{{&dictionary_type}, 2, 0, &array.object};
    Container container{{&container_type}, &dictionary.object};
    auto snapshot = string_table::stamp(&container.object);
    assert(snapshot.count == 2 && snapshot.entries == reinterpret_cast<uintptr_t>(&array.object));
    dictionary.count = 3;
    assert(!string_table::stamp(&container.object).ready());
    dictionary.count = 2;
    std::unordered_map<std::string, std::string> map{{"WhenUse", "translated"}, {"Other", "wrong"}};
    auto patch = [&] { auto n = string_table::patch(&container.object, map, "TextData_SkillTag", "name"); assert(handles.empty()); return n; };
    assert(patch() == 1 && row.text->text == u"translated"); delete row.text; row.text = &original;
    assert(string_table::patch(&container.object, map, "TextData_UI", "name") == 0 && handles.empty());
    map["WhenUse"] = ""; assert(patch() == 0 && row.text == &original); map["WhenUse"] = "translated";
    row.id = &other; assert(patch() == 0 && row.text == &original); row.id = &id;
    row.object.klass = &wrong_field_type; assert(patch() == 0 && row.text == &original); row.object.klass = &row_type;
    dictionary.count = 3; assert(patch() == 0); dictionary.count = 2;
    throw_get_value = true; assert(patch() == 0); throw_get_value = false;
    array_type.methods.clear(); assert(patch() == 0); array_type.methods.push_back(&get_value);
    il2cpp_gchandle_new_fn = nullptr; assert(patch() == 0);
    std::puts("string_table_patcher: boxed traversal, identity/type guards, deleted slots, bounds, exceptions and root cleanup passed");
}
