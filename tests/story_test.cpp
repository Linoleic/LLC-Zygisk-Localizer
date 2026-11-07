#include "story_observation.hpp"
#include "story_sync.hpp"
#include "scoped_content.hpp"
#include "story_refresh_guard.hpp"
#include "../jni/core/story_refresh_schedule.hpp"
#include "event_dialogue_content.hpp"
#include "event_dialogue.hpp"
#include "translation_evidence.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

static void test_story_observation() {
    assert(safe_story_id("S001B"));
    assert(safe_story_id("a_b-9"));
    assert(!safe_story_id("../S001B"));
    assert(!safe_story_id("S001B/other"));
    assert(!safe_story_id("") && !safe_story_id(std::string(128, 'a')));
    const uint16_t text[] = {0x4e2d, 0x6587, '\n', 0xd83d, 0xde00, 0, 'x'};
    assert(observed_utf8(text, 7) == std::string("\xe4\xb8\xad\xe6\x96\x87\n\xf0\x9f\x98\x80\0x", 13));
    const uint16_t invalid[] = {0xd800, 'a', 0xdc00};
    assert(observed_utf8(invalid, 3) == "\xef\xbf\xbd" "a" "\xef\xbf\xbd");
    assert(observed_utf8(nullptr, 0).empty());
    std::puts("story_observation: bounded UTF-16 conversion, surrogate handling and safe story IDs passed");
}

static void test_story_sync() {
    StorySyncState state;
    ContentStamp stamp{1, 2, 3, 92, 1};
    assert(!state.observe(9, "S001B", stamp, 0));
    assert(state.observe(9, "S001B", stamp, 1));
    state.record({92, 0, 0, 0}, 1); // File unavailable; same instance must retry.
    assert(!state.table.applied);
    assert(!state.observe(9, "S001B", stamp, 2));
    assert(state.observe(9, "S001B", stamp, 5));
    state.record({92, 91, 0, 100}, 5); // Partial coverage is not completion.
    assert(!state.table.applied);
    assert(state.observe(9, "S001B", stamp, 13));
    state.record({92, 92, 1, 100}, 13); // A failed field is retryable too.
    assert(!state.table.applied);
    assert(state.observe(9, "S001B", stamp, 29));
    state.record({92, 92, 0, 0}, 29);
    assert(!state.table.applied);
    assert(state.observe(9, "S001B", stamp, 61));
    state.record({92, 92, 0, 150}, 61);
    assert(state.table.applied);
    assert(!state.observe(9, "S001B", stamp, 62));
    ++stamp.version;
    assert(!state.observe(9, "S001B", stamp, 63));
    assert(state.observe(9, "S001B", stamp, 64));
    state.record({92, 92, 0, 150}, 64);
    assert(!state.observe(9, "S002B", stamp, 65)); // ID changes on same object.
    assert(state.observe(9, "S002B", stamp, 66));
    assert(!state.observe(10, "S002B", stamp, 67)); // Manager replacement.
    assert(state.observe(10, "S002B", stamp, 68));
    assert(!state.observe(0, "", {}, 69));
    assert(state.manager == 0 && state.id.empty());
    assert(!state.observe(10, "S002B", stamp, 70));
    assert(state.observe(10, "S002B", stamp, 71));

    std::unordered_map<int, ScenarioDialogue> lines;
    assert(parse_story_document(R"({"dataList":[{"content":"\u4e2d\u6587\n\ud83d\ude00","id":0,"teller":"speaker"},{"id":"7","place":"here"},{"id":"7x","content":"invalid id"}]})", lines));
    assert(lines.size() == 2 && lines.at(0).content == "\xe4\xb8\xad\xe6\x96\x87\n\xf0\x9f\x98\x80");
    assert(lines.at(0).teller == "speaker" && lines.at(7).place == "here");
    assert(!parse_story_document("{bad", lines));
    assert(lines.size() == 2);
    std::puts("story_sync: missing resource, partial/zero/failed writes, same-instance retry, lifecycle and story JSON passed");
}

static void test_story_refresh_guard() {
    StoryRefreshIdentity expected{1, 2, 3, "S001B", "0"};
    assert(expected == expected);
    assert(!(StoryRefreshIdentity{} == StoryRefreshIdentity{}));
    auto changed = expected;
    changed.dialog = 4;
    assert(!(expected == changed));
    changed = expected; changed.dialog_id = "1";
    assert(!(expected == changed));
    changed = expected; changed.story_id = "S002B";
    assert(!(expected == changed));
    changed = expected; changed.manager = 8;
    assert(!(expected == changed));
    changed = expected; changed.scenario = 9;
    assert(!(expected == changed));
    assert(!story_refresh_mode_error(true, true, false, false, false));
    assert(std::string(story_refresh_mode_error(false, true, false, false, false)) == "normal_dialog_controller_missing");
    assert(std::string(story_refresh_mode_error(true, false, false, false, false)) == "dialog_mode_schema_mismatch");
    assert(std::string(story_refresh_mode_error(true, true, true, false, false)) == "black_screen_mode_unsupported");
    assert(std::string(story_refresh_mode_error(true, true, false, true, false)) == "subtitle_mode_unsupported");
    assert(std::string(story_refresh_mode_error(true, true, false, false, true)) == "ruby_mode_unsupported");
    std::puts("story_refresh_guard: stale scene, manager and dialog tickets rejected");
}

static StoryRefreshIdentity schedule_id(uintptr_t manager = 1, uintptr_t scenario = 2,
                                        uintptr_t dialog = 3, const char* story = "S001B",
                                        const char* line = "17") {
    return {manager, scenario, dialog, story, line};
}

static void test_story_refresh_schedule() {
    StoryRefreshSchedule schedule;
    const auto first = schedule_id();

    schedule.observe(first);
    assert(schedule.stable == 1 && !schedule.ready(0, false));
    schedule.observe(first);
    assert(schedule.stable == 2 && schedule.ready(0, false));
    assert(!schedule.ready(0, true));

    schedule.queued(100);
    assert(schedule.in_flight && schedule.attempts == 1);
    assert(!schedule.ready(5100, false));
    assert(!schedule.expired(5100));
    assert(schedule.expired(5101));
    schedule.record("typing_active_retry_when_idle", 200);
    assert(!schedule.in_flight && schedule.failures == 1);
    assert(schedule.next_attempt_ms == 450);
    assert(!schedule.ready(449, false) && schedule.ready(450, false));

    // Exponential retry delay is capped at 4 seconds.
    const uint64_t delays[] = {500, 1000, 2000, 4000, 4000};
    for (uint64_t delay : delays) {
        schedule.queued(1000);
        schedule.record("automatic_refresh_timeout", 1000);
        assert(schedule.next_attempt_ms == 1000 + delay);
    }

    schedule.queued(6000);
    schedule.record(nullptr, 6000);
    assert(schedule.settled && !schedule.ready(100000, false));

    StoryRefreshSchedule permanent;
    permanent.observe(first);
    permanent.observe(first);
    permanent.queued(10);
    permanent.record("bridge_method_schema_mismatch", 11);
    assert(permanent.blocked && !permanent.ready(100000, false));

    // Unsupported presentation modes must stay retryable even for a no-op
    // cache ticket, so a later normal mode on the same dialog can recover.
    for (const char* mode : {"black_screen_mode_unsupported", "subtitle_mode_unsupported", "ruby_mode_unsupported"}) {
        StoryRefreshSchedule unsupported;
        unsupported.observe(first); unsupported.observe(first); unsupported.queued(10);
        unsupported.record(mode, 11);
        assert(!unsupported.settled && !unsupported.blocked && !unsupported.ready(12, false));
        assert(unsupported.ready(261, false));
    }

    StoryRefreshSchedule changed;
    changed.observe(first);
    changed.observe(first);
    changed.queued(20);
    changed.record(nullptr, 21);
    changed.observe(schedule_id(1, 2, 3, "S001B", "18"));
    assert(changed.stable == 1 && changed.attempts == 0 && !changed.settled && !changed.blocked);
    changed.observe(schedule_id(2, 2, 3, "S001B", "18"));
    assert(changed.stable == 1 && changed.attempts == 0);

    StoryRefreshSchedule invalid;
    invalid.observe(first);
    invalid.observe(first);
    invalid.queued(30);
    invalid.observe(StoryRefreshIdentity{});
    assert(!invalid.identity.valid() && invalid.stable == 0 && invalid.attempts == 0 && !invalid.in_flight);

    std::puts("story_refresh_schedule: exponential backoff, stability, modes and retry passed");
}

static void test_event_dialogue_content() {
    std::unordered_map<std::string, EventDialogueEntry> map;
    std::string source = R"({"dataList":[{"id":40,"personalityid":10301,"dialog":"\u82f1\u96c4"},{"id":41,"personalityid":10301,"dialog":null}]})";
    assert(parse_event_dialogue_document(source, map));
    assert(map.size() == 2 && map.at("10301/40").personality_id == 10301 && map.at("10301/40").dialog == "英雄");
    assert(map.at("10301/41").dialog.empty());
    for (auto invalid : {
        R"({"dataList":[{"id":40,"personalityid":10301,"dialog":"partial"},)",
        R"({"dataList":[{"id":40,"personalityid":10301},{"id":40,"personalityid":10301}]})",
        R"({"dataList":[{"id":42,"personalityid":-1,"dialog":"bad"}]})",
        R"({"dataList":[{"id":42,"personalityid":"10301","dialog":"bad"}]})",
        R"({"dataList":[{"id":42,"personalityid":10301,"dialog":3}]})",
        R"({"dataList":[{"id":42,"personalityid":10301,"dialog":"bad\u0000text"}]})"
    }) {
        assert(!parse_event_dialogue_document(invalid, map));
        assert(map.size() == 2 && map.at("10301/40").dialog == "英雄");
    }
    auto& evidence = translation_evidence::registry;
    evidence = {};
    evidence.source("/test/AbDlg_DonQuixote.json", source);
    evidence.loaded_row("_abnormalityEventCharDlg", "10301/40", {{"dialog", map.at("10301/40").dialog}});
    evidence.loaded_row("_abnormalityEventCharDlg", "10301/41", {});
    evidence.rebuild();
    assert(evidence.classify("_abnormalityEventCharDlg", "10301/40") == translation_evidence::RowState::Available);
    assert(evidence.classify("_abnormalityEventCharDlg", "10301/41") == translation_evidence::RowState::EmptySource);
    assert(translation_evidence::topic("/test/ChoiceEventEffect.json") == "_choiceEventResultDesc");
    assert(translation_evidence::topic("/test/AbEventsResultLog_Mirror.json") == "_choiceEventResultDesc");
    assert(parse_event_dialogue_document(R"({"dataList":[{"id":40,"personalityid":10201,"dialog":"different sinner"}]})", map));
    assert(map.size() == 3 && map.at("10301/40").dialog == "英雄" && map.at("10201/40").dialog == "different sinner");
    auto fields = translation_evidence::source_fields({{"content", "获得{reward} × {stack}"}}, "_choiceEventResultDesc");
    assert(fields.at("content") == "获得{reward} × {stack}");
    std::puts("event_dialogue_content: document parse, sinner map and evidence classification passed");
}

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

static void test_event_dialogue_graph() {
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

int main() {
    test_story_observation();
    test_story_sync();
    test_story_refresh_guard();
    test_story_refresh_schedule();
    test_event_dialogue_content();
    test_event_dialogue_graph();
    std::puts("All story tests passed successfully.");
    return 0;
}
