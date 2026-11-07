#include "sha256.hpp"
#include "compatibility_policy.hpp"
#include "localization_status.hpp"
#include "managed_fields.hpp"
#include "warning_summary.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// === Helpers from localization_status_test.cpp ===
using localization_status::Json;

static Json table(const std::string& sync_state, const Json& evidence = Json::object()) {
    return {{"sync_state", sync_state}, {"evidence", evidence}};
}

static std::string coverage_state(const std::string& stage, const Json& tables) {
    localization_status::stage = stage;
    return localization_status::build(101, "2026", tables)["coverage"]["state"].get<std::string>();
}

// === Helpers from managed_fields_test.cpp ===
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

static void test_compatibility() {
BuildSha256 empty;
    assert(empty.hex_digest() == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    empty.update("a",1);
    auto partial = empty.hex_digest();
    empty.update("bc",2);
    assert(empty.hex_digest() == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    assert(partial != empty.hex_digest());
    const char* multi="abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    BuildSha256 boundary;
    for (size_t i=0;i<strlen(multi);++i) boundary.update(multi+i,1);
    assert(boundary.hex_digest() == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    BuildSha256 million;
    std::string chunk(1000,'a');
    for(int i=0;i<1000;++i) million.update(chunk.data(),chunk.size());
    assert(million.hex_digest() == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    assert(approved_native_build(kVerifiedIl2CppSha256));
    assert(approved_native_build(kFullTrial1180Sha256));
    assert(std::string(approved_native_label(kFullTrial1180Sha256)) == "1.118.0 / versionCode 487");
    assert(std::string(native_validation_scope(kFullTrial1180Sha256)) == "full_runtime_trial");
    assert(approved_native_label(std::string(64, '0')) == nullptr);
    assert(native_validation_scope("") == nullptr);
    assert(!approved_native_build(""));
    assert(!approved_native_build(std::string(64,'0')));
    assert(!approved_native_build(std::string(kVerifiedIl2CppSha256)+" "));
    auto altered = std::string(kFullTrial1180Sha256); altered[0] = '0';
    assert(!approved_native_build(altered));
    assert(!approved_native_build(std::string(kFullTrial1180Sha256) + " "));

    // Evaluate compatibility: Tier 1 built-in
    auto v_builtin = evaluate_compatibility(kFullTrial1180Sha256);
    assert(v_builtin.approved && v_builtin.status == "approved" && v_builtin.scope == "full_runtime_trial");

    // Evaluate compatibility: Tier 2 custom config
    std::string fake_new_hash = "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789";
    nlohmann::json cfg_tier2 = {
        {"allow_adaptive_compatibility", false},
        {"custom_approved_builds", {{fake_new_hash, "Custom 1.119.0"}}}
    };
    auto v_tier2 = evaluate_compatibility(fake_new_hash, cfg_tier2);
    assert(v_tier2.approved && v_tier2.status == "approved_by_config" && v_tier2.label == "Custom 1.119.0");

    // Evaluate compatibility: Tier 3 adaptive mode
    std::string unknown_hash = "1111111122222222333333334444444455555555666666667777777788888888";
    nlohmann::json cfg_adaptive = {{"allow_adaptive_compatibility", true}};
    auto v_adaptive = evaluate_compatibility(unknown_hash, cfg_adaptive);
    assert(v_adaptive.approved && v_adaptive.status == "adaptive_approved");

    // Evaluate compatibility: unknown build with adaptive disabled
    nlohmann::json cfg_strict = {{"allow_adaptive_compatibility", false}};
    auto v_strict = evaluate_compatibility(unknown_hash, cfg_strict);
    assert(!v_strict.approved && v_strict.status == "unknown_native_build");

    // Evaluate compatibility: malformed hash
    assert(!evaluate_compatibility("not-hex-at-all", cfg_adaptive).approved);
    assert(!evaluate_compatibility(std::string(63, 'a'), cfg_adaptive).approved);

    std::puts("compatibility: SHA-256 vectors/streaming and unknown-build rejection passed");
}

static void test_localization_status() {
using namespace localization_status;
    translation_evidence::registry = translation_evidence::Registry{};
    root_available = true;
    root = "/data/language";
    mode = "legacy";
    stage = "running";
    auto only_empty=coverage(Json::array({table("observed_empty", {{"traversal_complete", true}, {"successful_writes", 99}})}));
    assert(only_empty["state"]=="observed_empty_only" && only_empty["waiting_or_retrying_tables"]==0);
    assert(only_empty["observed_empty_tables"]==1 && only_empty["id_assessed_tables"]==0 && only_empty["applied_to_snapshot_tables"]==0);
    auto mixed_empty=coverage(Json::array({table("observed_empty"),table("applied_to_snapshot",{{"traversal_complete",true}})}));
    assert(mixed_empty["state"]=="observed_applied" && mixed_empty["id_assessed_tables"]==1 && mixed_empty["observed_empty_tables"]==1);
    assert(coverage(Json::array({table("checking_empty")}))["waiting_or_retrying_tables"]==1);
    auto nested = coverage(Json::array({table("applied_to_snapshot", {{"missing_local_subrows", 1}, {"retained_original_fields", 1}})}));
    assert(nested["state"] == "partial_translation" && nested["missing_local_subrows"] == 1 && nested["retained_original_fields"] == 1);
    request_id = "status-request";
    assert(build(101, "2026", Json::array())["resources"]["state"] == "no_usable_files");
    translation_evidence::registry.source("/data/UI.json", R"({"dataList":[]})");

    Json partial_evidence = {{"traversal_complete", true}, {"missing_local_rows", 2},
                             {"source_map_mismatches", 0}, {"failed_writes", 0},
                             {"source_uncertain_rows", 0}, {"available_rows_without_write", 0},
                             {"invalid_runtime_rows", 0}};
    Json partial_tables = Json::array({table("applied_to_snapshot", partial_evidence)});
    Json partial = build(101, "2026", partial_tables);
    assert(partial["coverage"]["state"] == "partial_translation");
    assert(partial["coverage"]["missing_local_rows"] == 2);
    assert(partial["coverage"]["whole_game_coverage_known"] == false);
    auto empty_evidence = partial_evidence;
    empty_evidence["missing_local_rows"] = 0;
    empty_evidence["empty_source_rows"] = 1;
    auto empty_report = build(101, "2026", Json::array({table("settled_untranslated", empty_evidence)}));
    assert(empty_report["coverage"]["state"] == "partial_translation");
    assert(empty_report["coverage"]["empty_source_rows"] == 1);
    auto limited_evidence = partial_evidence;
    limited_evidence["unsupported_source_fields"] = {{"summary", 251}};
    auto limited = build(101, "2026", Json::array({table("applied_to_snapshot", limited_evidence)}));
    assert(limited["coverage"]["state"] == "module_coverage_limited");
    assert(limited["coverage"]["unsupported_source_fields"] == 251);
    assert(limited["coverage"]["missing_local_rows"] == 2);

    Json issue_evidence = partial_evidence;
    issue_evidence["source_map_mismatches"] = 1;
    assert(coverage_state("running", Json::array({table("applied_to_snapshot", issue_evidence)})) == "module_issue");
    issue_evidence["source_map_mismatches"] = 0;
    issue_evidence["structure_issues"] = {{"skill_coin_count_mismatch", 1}};
    auto structure_report = coverage(Json::array({table("applied_to_snapshot", issue_evidence)}));
    assert(structure_report["state"] == "module_issue" && structure_report["structure_mismatches"] == 1);
    assert(structure_report["failed_writes"] == 0);
    assert(coverage_state("running", Json::array({table("queued")})) == "waiting_or_retrying");
    assert(coverage_state("disabled", Json::array()) == "disabled");

    // Assessment follows the supplied evidence; it does not invent totals from slot deltas.
    Json no_assessable_evidence = Json::array({table("queued")});
    stage = "running";
    Json waiting = build(101, "2026", no_assessable_evidence);
    assert(waiting["coverage"]["state"] == "waiting_or_retrying");
    assert(waiting["coverage"]["id_assessed_tables"] == 0);
    assert(waiting["coverage"]["whole_game_coverage_known"] == false);

    Json report = build(101, "2026", partial_tables);
    assert(report["build"] == build_info::json());
    assert(report["build"]["git_revision"].is_string());
    assert(report["updates"]["state"] == "not_checked");
    assert(report["updates"]["last_successful_check_unix"].is_null());
    assert(report["coverage"]["whole_game_coverage_known"] == false);
    assert(report["runtime"]["stage"] == "running");
    assert(report["request_id"] == "status-request");

    // Resource state differentiates unmanaged loaded files from read/parse errors.
    Json unmanaged = build(101, "2026", Json::array());
    assert(unmanaged["resources"]["state"] == "loaded_unmanaged");
    translation_evidence::registry.source("/data/Skills-broken.json", "not-json");
    Json with_errors = build(101, "2026", Json::array());
    assert(with_errors["resources"]["state"] == "loaded_with_file_errors");
    assert(with_errors["resources"]["file_checks"]["rejected_files"] == 1);

    return;
}

static void test_managed_fields() {
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

    // Test managed string pool interning & deduplication
    Il2CppString* s1 = g_managed_string_pool.intern("reuse_me");
    Il2CppString* s2 = g_managed_string_pool.intern("reuse_me");
    assert(s1 != nullptr && s1 == s2);
    assert(g_managed_string_pool.size() >= 1);
    g_managed_string_pool.clear();
    assert(g_managed_string_pool.size() == 0);

    // Test with GC handles enabled
    static std::unordered_map<uintptr_t, Il2CppObject*> mock_handles;
    mock_handles.clear();
    il2cpp_gchandle_new_fn = [](Il2CppObject* o, bool) { static uintptr_t h = 100; mock_handles[++h] = o; return h; };
    il2cpp_gchandle_get_target_fn = [](uintptr_t h) { return mock_handles.at(h); };
    il2cpp_gchandle_free_fn = [](uintptr_t h) { mock_handles.erase(h); };
    g_managed_string_pool.enable_gchandles(true);
    Il2CppString* s3 = g_managed_string_pool.intern("pinned_string");
    assert(s3 != nullptr && mock_handles.size() == 1);
    Il2CppString* s4 = g_managed_string_pool.intern("pinned_string");
    assert(s3 == s4 && mock_handles.size() == 1);
    auto create_handle = il2cpp_gchandle_new_fn;
    auto get_target = il2cpp_gchandle_get_target_fn;
    auto release_handle = il2cpp_gchandle_free_fn;
    // Any missing API must refuse even a cached string, without allocating handles.
    il2cpp_gchandle_new_fn = nullptr;
    assert(!g_managed_string_pool.intern("pinned_string"));
    il2cpp_gchandle_new_fn = create_handle;
    il2cpp_gchandle_get_target_fn = nullptr;
    assert(!g_managed_string_pool.intern("missing_get"));
    il2cpp_gchandle_get_target_fn = get_target;
    il2cpp_gchandle_free_fn = nullptr;
    assert(!g_managed_string_pool.intern("missing_free"));
    assert(mock_handles.size() == 1);
    // clear uses the release function captured when the handle was created.
    g_managed_string_pool.clear();
    assert(mock_handles.empty() && g_managed_string_pool.size() == 0);
    il2cpp_gchandle_free_fn = release_handle;
    il2cpp_gchandle_new_fn = [](Il2CppObject*, bool) -> uintptr_t { return 0; };
    auto previous_text = row.text;
    assert(!write_managed_text(&row.object, 24, "handle_failure"));
    assert(row.text == previous_text && g_managed_string_pool.size() == 0);
    il2cpp_gchandle_new_fn = create_handle;
    assert(g_managed_string_pool.intern("lost_target"));
    mock_handles.begin()->second = nullptr;
    assert(g_managed_string_pool.intern("lost_target"));
    assert(mock_handles.size() == 1); // stale handle released before replacement
    g_managed_string_pool.enable_gchandles(false);
    assert(mock_handles.empty() && g_managed_string_pool.size() == 0);
    assert(g_managed_string_pool.intern("raw_before_enable"));
    g_managed_string_pool.enable_gchandles(true);
    assert(g_managed_string_pool.size() == 0);
    g_managed_string_pool.enable_gchandles(false);
    il2cpp_gchandle_new_fn = nullptr;
    il2cpp_gchandle_get_target_fn = nullptr;
    il2cpp_gchandle_free_fn = nullptr;

    std::puts("managed_fields: numeric/string IDs, static/inherited fields, type guards and write barrier passed");
}

static void test_warning_summary() {
using namespace warning_summary;
State state;
    Batch batch;
    for (int i = 0; i < 10000; ++i) batch["missing_local"].add("id-" + std::to_string(i));
    assert(batch.at("missing_local").count == 10000 && batch.at("missing_local").examples.size() == 3);
    auto first = state.submit("table", batch, 0);
    assert(first.size() == 1 && first[0].group.count == 10000 && first[0].suppressed == 0);
    for (int i = 1; i <= 1000; ++i) assert(state.submit("table", batch, i).empty());
    assert(state.flush(60000).empty()); // Unchanged retries never produce periodic noise.
    auto changed = batch;
    changed["missing_local"].add("a-new-id-outside-original-samples");
    auto update = state.submit("table", changed, 60001);
    assert(update.size() == 1 && update[0].suppressed == 1000 && update[0].group.count == 10001);
    changed["missing_local"].add("another-id");
    assert(state.submit("table", changed, 60002).empty());
    assert(state.flush(90000).empty());
    auto pending = state.flush(90001); // Timer publishes even if patching has stopped.
    assert(pending.size() == 1 && pending[0].group.count == 10002 && pending[0].suppressed == 1);
    auto recovery = state.submit("table", {}, 90002);
    assert(recovery.size() == 1 && recovery[0].recovered && state.size() == 0);
    assert(state.submit("table", batch, 90003).size() == 1); // Recurrence is visible immediately.
    assert(state.submit("other_table", batch, 90004).size() == 1);
    assert(state.submit("other_table", {}, 90005, false).empty());
    assert(state.size() == 2); // An incomplete traversal cannot claim a previous issue recovered.

    State revert;
    assert(revert.submit("table", batch, 0).size() == 1);
    assert(revert.submit("table", changed, 1).empty());
    assert(revert.submit("table", batch, 2).empty());
    assert(revert.flush(30000).empty()); // Do not publish a superseded intermediate state.

    Group forward, reverse;
    forward.add("alpha"); forward.add("beta");
    reverse.add("beta"); reverse.add("alpha");
    assert(forward == reverse && forward.examples == reverse.examples);
    Group same_count_new_id = forward;
    same_count_new_id = {}; same_count_new_id.add("alpha"); same_count_new_id.add("gamma");
    assert(!(same_count_new_id == forward));

    State bounded;
    Batch one; one["reason"].add("sample");
    for (size_t i = 0; i < State::capacity; ++i) assert(bounded.submit(std::to_string(i), one, 0).size() == 1);
    auto overflow = bounded.submit("overflow", one, 1);
    assert(overflow.size() == 1 && overflow[0].reason == "group_limit");
    assert(bounded.submit("overflow_again", one, 2).empty());
    assert(bounded.size() == State::capacity && bounded.dropped_groups == 2);
    bounded.reset();
    assert(bounded.size() == 0 && bounded.dropped_groups == 0);
    assert(bounded.submit("overflow", one, 3).size() == 1);

    assert(sample("one\ntwo\tthree\r") == "one two three ");
    assert(sample(std::string(159, 'a') + "\xe4\xb8\xad").size() == 159);
    std::puts("warning_summary: bounded samples, retry suppression, timed flush, recovery, reset and overflow passed");
}

int main() {
    test_compatibility();
    test_localization_status();
    test_managed_fields();
    test_warning_summary();
    std::puts("All status diagnostics tests passed successfully.");
    return 0;
}
