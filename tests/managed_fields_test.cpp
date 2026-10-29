#include "managed_fields.hpp"
#include <cassert>
#include <cstdio>
#include <vector>

struct Field;
struct Il2CppClass {
    const char* name;
    Il2CppClass* parent;
    std::vector<Field*> fields;
};
struct Il2CppType { Il2CppClass* klass; };
struct Field { uint32_t offset; int flags; Il2CppType type; const char* name = "test_field"; };
struct Il2CppObject { Il2CppClass* klass; void* monitor = nullptr; };
struct Il2CppString { std::u16string text; };
struct Row { Il2CppObject object; int32_t id; int32_t padding = 0; Il2CppString* text = nullptr; };
struct StringRow { Il2CppObject object; Il2CppString* id; };

int main() {
    Il2CppClass int_type{"Int32", nullptr, {}};
    Il2CppClass string_type{"String", nullptr, {}};
    Il2CppClass double_type{"Double", nullptr, {}};
    Field static_decoy{16, 0x10, {&string_type}};
    Field id{16, 0, {&int_type}};
    Field text{24, 0, {&string_type}, "Flavor"};
    Il2CppClass row_type{"LocalizedRow", nullptr, {&static_decoy, &id, &text}};
    Il2CppClass derived_type{"DerivedRow", &row_type, {}};
    Field string_id{16, 0, {&string_type}};
    Il2CppClass string_row_type{"StringRow", nullptr, {&string_id}};
    Field unsupported_id{16, 0, {&double_type}};
    Il2CppClass unsupported_type{"UnsupportedRow", nullptr, {&unsupported_id}};

    il2cpp_object_get_class_fn = [](Il2CppObject* o) { return o->klass; };
    il2cpp_class_get_name_fn = [](Il2CppClass* k) { return k->name; };
    il2cpp_class_get_parent_fn = [](Il2CppClass* k) { return k->parent; };
    il2cpp_class_get_fields_fn = [](Il2CppClass* k, void** iter) -> void* {
        uintptr_t index = reinterpret_cast<uintptr_t>(*iter);
        if (index >= k->fields.size()) return nullptr;
        *iter = reinterpret_cast<void*>(index + 1);
        return k->fields[index];
    };
    il2cpp_class_get_field_from_name_fn = [](Il2CppClass* k, const char* name) -> void* {
        for (auto* field : k->fields) if (strcmp(field->name, name) == 0) return field;
        return nullptr;
    };
    il2cpp_field_get_flags_fn = [](void* f) { return static_cast<Field*>(f)->flags; };
    il2cpp_field_get_name_fn = [](void* f) { return static_cast<Field*>(f)->name; };
    il2cpp_field_get_offset_fn = [](void* f) { return static_cast<Field*>(f)->offset; };
    il2cpp_field_get_type_fn = [](void* f) -> const Il2CppType* { return &static_cast<Field*>(f)->type; };
    il2cpp_class_from_type_fn = [](const Il2CppType* t) { return t->klass; };
    il2cpp_field_get_value_fn = [](Il2CppObject* o, void* f, void* dest) {
        auto* field = static_cast<Field*>(f);
        size_t size = strcmp(field->type.klass->name, "Int32") == 0 ? sizeof(int32_t) : sizeof(void*);
        memcpy(dest, reinterpret_cast<char*>(o) + field->offset, size);
    };
    il2cpp_string_length_fn = [](Il2CppString* s) { return static_cast<int32_t>(s->text.size()); };
    il2cpp_string_chars_fn = [](Il2CppString* s) { return reinterpret_cast<const uint16_t*>(s->text.c_str()); };
    il2cpp_string_new_fn = [](const char* s) {
        auto* result = new Il2CppString;
        while (*s) result->text.push_back(*s++);
        return result;
    };
    static unsigned barriers = 0;
    il2cpp_gc_wbarrier_set_field_fn = [](Il2CppObject*, void** address, void* value) {
        ++barriers;
        *address = value;
    };

    Row row{{&row_type}, 1011501};
    assert(read_managed_id(&row.object) == "1011501");
    struct StoryRow { Il2CppObject object; int32_t index; int32_t id; };
    Field story_id{20, 0, {&int_type}, "Id"};
    Il2CppClass story_type{"StoryRow", nullptr, {&story_id}};
    StoryRow story_row{{&story_type}, 999, 42};
    assert(read_named_managed_id(&story_row.object, "id") == "42");
    assert(!write_managed_text(&row.object, 16, "must not overwrite the integer"));
    assert(row.id == 1011501 && barriers == 0);
    assert(write_managed_text(&row.object, 24, "translated"));
    assert(row.text->text == u"translated" && barriers == 1 && g_managed_text_writes == 1);
    assert(!write_managed_text_named(&row.object, "summary", "must not overwrite flavor"));
    assert(row.text->text == u"translated");
    assert(write_managed_text_named(&row.object, "flavor", "flavor text"));
    assert(row.text->text == u"flavor text");
    Row derived{{&derived_type}, 2558};
    assert(read_managed_id(&derived.object) == "2558");
    Il2CppString string_value{u"S001B"};
    StringRow string_row{{&string_row_type}, &string_value};
    assert(read_managed_id(&string_row.object) == "S001B");
    int numeric_id = 0;
    assert(!read_managed_numeric_id(&string_row.object, numeric_id));
    assert(!managed_id_absent(&string_row.object));
    string_row.id = nullptr;
    assert(managed_id_absent(&string_row.object));
    assert(!managed_id_absent(&row.object));
    assert(read_managed_numeric_id(&row.object, numeric_id) && numeric_id == 1011501);
    Row unsupported{{&unsupported_type}, 1011501};
    assert(read_managed_id(&unsupported.object).empty());
    assert(!write_managed_text(&unsupported.object, 24, "missing field"));
    assert(barriers == 2);
    std::puts("managed_fields: numeric/string IDs, static/inherited fields, type guards and write barrier passed");
}
