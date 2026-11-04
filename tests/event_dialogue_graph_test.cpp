#include "event_dialogue.hpp"
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
struct Array { Il2CppObject object; std::vector<Il2CppObject*> slots; };
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
        return static_cast<Array*>(obj)->slots.at(*static_cast<int32_t*>(args[0]));
    };

    struct List { Il2CppObject object; Il2CppObject* items; int32_t size; int32_t version; };
    struct KeyContainer { Il2CppObject object; Il2CppObject* by_event; Il2CppObject* by_prob; };
    Field list_items{16, "_items", {&array_type}}, list_size{24, "_size", {&integer}}, list_version{28, "_version", {&integer}};
    Il2CppClass list_type{"List`1", nullptr, {&list_items,&list_size,&list_version}, false, {}};
    Field dlg_list{16,"_dlgList",{&list_type}};
    Il2CppClass wrapper_type{"AbEventDlgListContainer",nullptr,{&dlg_list},false,{}};
    Field events{16,"_dict",{&dictionary_type}}, probabilities{24,"_dictByProb",{&dictionary_type}};
    Il2CppClass keys_type{"AbEventKeyDictionaryContainer",nullptr,{&events,&probabilities},false,{}};
    Field personalities{16,"_personalityDict",{&dictionary_type}};
    Il2CppClass root_type{"AbEventCharDlgRoot",nullptr,{&personalities},false,{}};
    Il2CppClass dialogue_type{"TextData_AbnormalityEventCharDlg",nullptr,{},false,{}};
    Il2CppObject row{&dialogue_type};
    Array row_array{{&array_type},{&row,&row}};
    List list{{&list_type},&row_array.object,1,1};
    Container wrapper{{&wrapper_type},&list.object};
    Entry event_entry{{&entry_type},1,0,nullptr,&wrapper.object};
    Array event_array{{&array_type},{&event_entry.object}};
    Dictionary by_event{{&dictionary_type},1,0,&event_array.object};
    Dictionary by_prob{{&dictionary_type},1,0,&event_array.object};
    KeyContainer keys{{&keys_type},&by_event.object,&by_prob.object};
    Entry personality_entry{{&entry_type},1,0,nullptr,&keys.object};
    Entry removed{{&entry_type},-1,0,nullptr,nullptr};
    Entry unused{{&entry_type},0,0,nullptr,nullptr};
    Array root_array{{&array_type},{&personality_entry.object,&removed.object,&unused.object}};
    Dictionary personalities_dict{{&dictionary_type},2,0,&root_array.object};
    Container root{{&root_type},&personalities_dict.object};
    auto before=event_dialogue::inspect(&root.object);
    assert(before.ready() && before.count==1 && handles.empty());
    int calls=0;
    auto patched=event_dialogue::inspect(&root.object,[&](Il2CppObject* value) { assert(value==&row);++calls;return true; });
    assert(patched==before && calls==1 && handles.empty());
    // Shared lists/rows must not be processed twice through event/probability indexes.
    list.size=2;
    auto grown=event_dialogue::inspect(&root.object);
    assert(grown.ready() && grown.count==2 && !(grown==before));
    calls=0;
    assert(event_dialogue::inspect(&root.object,[&](Il2CppObject*) {++calls;return true;}).ready());
    assert(calls==1);
    ++list.version;
    assert(!(event_dialogue::inspect(&root.object)==grown));
    Array replacement{{&array_type},{&row,&row}};
    auto old_items=event_dialogue::inspect(&root.object);
    list.items=&replacement.object;
    assert(!(event_dialogue::inspect(&root.object)==old_items));
    replacement.slots[0]=nullptr;
    assert(!event_dialogue::inspect(&root.object,[](Il2CppObject*) {return true;}).ready());
    replacement.slots[0]=&row;
    assert(!event_dialogue::inspect(&root.object,[](Il2CppObject*) {return false;}).ready());
    list.size=3;
    assert(!event_dialogue::inspect(&root.object).ready());
    list.size=2;
    wrapper.object.klass=&container_type;
    assert(!event_dialogue::inspect(&root.object).ready());
    wrapper.object.klass=&wrapper_type;
    personalities_dict.count=4;
    assert(!event_dialogue::inspect(&root.object).ready());
    personalities_dict.count=2;
    throw_get_value=true;
    assert(!event_dialogue::inspect(&root.object).ready());
    throw_get_value=false;
    assert(handles.empty());
    il2cpp_gchandle_new_fn=nullptr;
    assert(!event_dialogue::inspect(&root.object).ready());
    std::puts("event_dialogue: nested event/probability traversal, deduplication, snapshots, bounds and failure cleanup passed");
}
