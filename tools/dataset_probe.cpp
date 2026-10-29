// Standalone, one-shot diagnostic for the explicitly pinned 1.118.0 build.
// Normal localization code is not linked. Live data is read on UnityMain;
// writes are explicit build flags: an unpublished object and, optionally, one
// known login UI field restored immediately within the same main-thread action.
#include "../jni/core/main_thread_bridge.hpp"
#include "../jni/core/sha256.hpp"
#include <pthread.h>
#include <sys/stat.h>
#include <chrono>

using json = nlohmann::json;
static json action_report;
static constexpr const char* report_path = "/sdcard/Android/data/com.ProjectMoon.LimbusCompany/files/llc_dataset_probe.json";
static constexpr const char* native_hash = "25e9a8a0d08c8d83366c993e16a9d9ef3f7ce5dd444aa540f689317a31f99593";
struct Root {
    main_thread_bridge::Handle handle = 0;
    ~Root() { if (handle) main_thread_bridge::free_handle(handle); }
    Il2CppObject* get() const { return handle ? main_thread_bridge::get_target(handle) : nullptr; }
};

static bool matches_diagnostic_build() {
    void* library = dlopen("libil2cpp.so", RTLD_NOW | RTLD_NOLOAD);
    Dl_info info{};
    void* symbol = library ? dlsym(library, "il2cpp_init") : nullptr;
    bool matches = false;
    if (symbol && dladdr(symbol, &info) && info.dli_fname) {
        if (FILE* file = fopen(info.dli_fname, "rb")) {
            struct stat before{}, after{};
            if (!fstat(fileno(file), &before) && S_ISREG(before.st_mode) && before.st_size > 0) {
                BuildSha256 hash;
                uint8_t bytes[65536];
                uint64_t total = 0;
                size_t size;
                while ((size = fread(bytes, 1, sizeof(bytes), file)) != 0) { hash.update(bytes, size); total += size; }
                matches = !ferror(file) && !fstat(fileno(file), &after) && before.st_size == after.st_size &&
                    before.st_mtime == after.st_mtime && total == static_cast<uint64_t>(before.st_size) && hash.hex_digest() == native_hash;
            }
            fclose(file);
        }
    }
    if (library) dlclose(library);
    return matches;
}

static void publish(const json& report) {
    auto text = report.dump(2);
    std::string next = std::string(report_path) + ".next";
    if (FILE* file = fopen(next.c_str(), "wb")) {
        bool ok = fwrite(text.data(), 1, text.size(), file) == text.size();
        if (fclose(file)) ok = false;
        if (!ok || rename(next.c_str(), report_path)) unlink(next.c_str());
    }
    LOGI("[LLC-DATA-PROBE] pid=%d status=%s", getpid(), report["status"].get<std::string>().c_str());
}

static Il2CppObject* read_reference(Il2CppObject* owner, const char* name) {
    auto* field = managed_field_named(owner, name);
    auto* klass = field ? il2cpp_class_from_type_fn(il2cpp_field_get_type_fn(field)) : nullptr;
    if (!klass || il2cpp_class_is_valuetype_fn(klass)) return nullptr;
    Il2CppObject* value = nullptr;
    il2cpp_field_get_value_fn(owner, field, &value);
    return value;
}

static bool read_int(Il2CppObject* owner, const char* name, int32_t& value) {
    auto* field = managed_field_named(owner, name);
    if (!field || strcmp(main_thread_bridge::type_name(il2cpp_field_get_type_fn(field)), "Int32")) return false;
    il2cpp_field_get_value_fn(owner, field, &value);
    return true;
}

static Il2CppObject* dataset() {
    struct Search { Il2CppObject* value = nullptr; } search;
    il2cpp_class_for_each_fn([](Il2CppClass* klass, void* data) {
        auto& state = *static_cast<Search*>(data);
        if (state.value || !klass || strcmp(il2cpp_class_get_name_fn(klass), "Singleton`1")) return;
        auto* field = il2cpp_class_get_field_from_name_fn(klass, "_instance");
        auto* type = field ? il2cpp_class_from_type_fn(il2cpp_field_get_type_fn(field)) : nullptr;
        if (!field || !(il2cpp_field_get_flags_fn(field) & 0x10) || !type || il2cpp_class_is_valuetype_fn(type) ||
            strcmp(il2cpp_class_get_name_fn(type), "TextDataSet")) return;
        Il2CppObject* value = nullptr;
        il2cpp_field_static_get_value_fn(field, &value);
        if (value && !strcmp(il2cpp_class_get_name_fn(il2cpp_object_get_class_fn(value)), "TextDataSet")) state.value = value;
    }, &search);
    return search.value;
}

static json dictionary_snapshot(Il2CppObject* data, const char* table, Root* live_candidate) {
    json result = {{"table", table}, {"status", "not_loaded"}};
    auto* list = read_reference(data, table);
    if (!list) return result;
    result["list_type"] = il2cpp_class_get_name_fn(il2cpp_object_get_class_fn(list));
    auto* dictionary = read_reference(list, "_dic");
    if (!dictionary) { result["status"] = "dictionary_missing"; return result; }
    auto* klass = il2cpp_object_get_class_fn(dictionary);
    if (strcmp(il2cpp_class_get_name_fn(klass), "Dictionary`2")) { result["status"] = "unexpected_dictionary_type"; return result; }
    int32_t count = 0, version = 0;
    if (!read_int(dictionary, "_count", count) || !read_int(dictionary, "_version", version) || count < 0 || count > 500000) {
        result["status"] = "invalid_dictionary_header"; return result;
    }
    auto* entries = read_reference(dictionary, "_entries");
    if (entries) {
        const char* name = il2cpp_class_get_name_fn(il2cpp_object_get_class_fn(entries));
        size_t size = name ? strlen(name) : 0;
        if (size < 2 || strcmp(name + size - 2, "[]")) { result["status"] = "unexpected_entries_type"; return result; }
    }
    auto length = entries ? il2cpp_array_length_fn(entries) : 0;
    result["slots"] = count;
    result["entries_length"] = length;
    result["version"] = version;
    result["layout"] = json::array();
    for (auto* name : {"_buckets", "_entries", "_count", "_version"}) {
        auto* field = managed_field_named(dictionary, name);
        if (field) result["layout"].push_back({{"name", name}, {"offset", il2cpp_field_get_offset_fn(field)}});
    }
    if (static_cast<uintptr_t>(count) > length) { result["status"] = "entries_shorter_than_count"; return result; }
    result["status"] = count ? "header_valid" : "empty";
    if (!count) return result;

    // Let System.Array box the actual closed generic Entry; never assume its
    // stride, field packing, or the array data offset.
    const MethodInfo* get_value = nullptr;
    for (auto* k = il2cpp_object_get_class_fn(entries); k && !get_value; k = il2cpp_class_get_parent_fn(k)) {
        void* iter = nullptr;
        while (auto* method = il2cpp_class_get_methods_fn(k, &iter)) {
            if (!strcmp(il2cpp_method_get_name_fn(method), "GetValue") && il2cpp_method_get_param_count_fn(method) == 1 &&
                !strcmp(main_thread_bridge::type_name(il2cpp_method_get_param_fn(method, 0)), "Int32")) { get_value = method; break; }
        }
    }
    if (!get_value) { result["sample_error"] = "array_get_value_unavailable"; return result; }
    result["samples"] = json::array();
    for (int32_t index = 0; index < count && index < 8; ++index) {
        Il2CppException* exception = nullptr;
        void* args[] = {&index};
        auto* entry = il2cpp_runtime_invoke_fn(get_value, entries, args, &exception);
        if (exception || !entry || !il2cpp_class_is_valuetype_fn(il2cpp_object_get_class_fn(entry))) {
            result["sample_error"] = "entry_box_failed"; break;
        }
        auto handle = main_thread_bridge::new_handle(entry, false);
        if (!handle) { result["sample_error"] = "entry_root_failed"; break; }
        entry = main_thread_bridge::get_target(handle);
        int32_t hash_code = -1;
        if (!read_int(entry, "hashCode", hash_code)) result["sample_error"] = "entry_hash_type_mismatch";
        else if (hash_code >= 0) {
            auto* row = read_reference(entry, "value");
            json sample = {{"slot", index}, {"key", read_named_managed_id(entry, "key")},
                {"entry_type", il2cpp_class_get_name_fn(il2cpp_object_get_class_fn(entry))}};
            if (row) sample["row_type"] = il2cpp_class_get_name_fn(il2cpp_object_get_class_fn(row));
#ifdef LLC_PROBE_WRITE_LIVE_ONE
            if (live_candidate && !live_candidate->handle && row && !strcmp(table, "_loginUIList") &&
                sample["key"] == "loginui_sign_in_with_google" && sample["row_type"] == "TextData_UI") {
                live_candidate->handle = main_thread_bridge::new_handle(row, false);
            }
#else
            (void)live_candidate;
#endif
            result["samples"].push_back(std::move(sample));
        }
        main_thread_bridge::free_handle(handle);
        if (result.contains("sample_error")) break;
    }
    int32_t after = 0;
    result["snapshot_unchanged"] = read_int(dictionary, "_version", after) && after == version;
    return result;
}

static const char* inspect_on_main() {
    auto* live = dataset();
    action_report = {{"live_dataset", live != nullptr}, {"live_game_fields_written", 0}, {"tables", json::array()}};
    if (!live) return "dataset_not_ready";
    if (auto* field = managed_field_named(live, "_isLoadedRemote")) {
        if (!strcmp(main_thread_bridge::type_name(il2cpp_field_get_type_fn(field)), "Boolean")) {
            bool loaded = false;
            il2cpp_field_get_value_fn(live, field, &loaded);
            action_report["remote_loaded"] = loaded;
        }
    }
    Root live_candidate;
    for (auto* name : {"_loginUIList", "_uiList", "_personalityList", "_skillList", "_passiveList", "_keywordList", "_storyDungeonNodeText"})
        action_report["tables"].push_back(dictionary_snapshot(live, name, &live_candidate));
    int validated = 0;
    bool invalid = false;
    for (const auto& table : action_report["tables"]) {
        const auto status = table["status"].get<std::string>();
        if (status == "not_loaded" || status == "empty") continue;
        if (status != "header_valid" || table.contains("sample_error") ||
            !table.value("snapshot_unchanged", false) || !table.contains("samples") || table["samples"].empty()) invalid = true;
        else ++validated;
    }
    action_report["validated_nonempty_tables"] = validated;
    if (invalid) return "container_validation_failed";
    if (!validated) return "no_nonempty_table_to_validate";
#ifdef LLC_PROBE_WAIT_REMOTE
    if (!action_report.value("remote_loaded", false)) return "awaiting_remote_tables";
    if (validated < 7) return "awaiting_content_tables";
#endif
#ifdef LLC_PROBE_WRITE_OWNED
    {
    auto* klass = find_class_any("", "TextData_UI");
    if (!klass || il2cpp_class_is_valuetype_fn(klass)) return "owned_type_missing";
    auto* object = il2cpp_object_new_fn(klass);
    auto handle = object ? main_thread_bridge::new_handle(object, false) : 0;
    if (!handle) return "owned_object_root_failed";
    object = main_thread_bridge::get_target(handle);
    auto* field = managed_field_named(object, "content");
    if (!field || strcmp(main_thread_bridge::type_name(il2cpp_field_get_type_fn(field)), "String")) {
        main_thread_bridge::free_handle(handle); return "owned_field_schema_mismatch";
    }
    Il2CppString* original = nullptr;
    il2cpp_field_get_value_fn(object, field, &original);
    // An unpublished object with no constructor run must start with a null field.
    if (original) { main_thread_bridge::free_handle(handle); return "owned_field_not_initially_null"; }
    auto* text = il2cpp_string_new_fn("LLC_SANDBOX_1_118_0");
    auto text_handle = text ? main_thread_bridge::new_handle(reinterpret_cast<Il2CppObject*>(text), false) : 0;
    if (!text_handle) { main_thread_bridge::free_handle(handle); return "owned_text_root_failed"; }
    object = main_thread_bridge::get_target(handle);
    auto** address = reinterpret_cast<void**>(reinterpret_cast<char*>(object) + il2cpp_field_get_offset_fn(field));
    auto* target = main_thread_bridge::get_target(text_handle);
    il2cpp_gc_wbarrier_set_field_fn(object, address, target);
    Il2CppObject* read_back = nullptr;
    il2cpp_field_get_value_fn(object, field, &read_back);
    bool written = read_back == target;
    il2cpp_gc_wbarrier_set_field_fn(object, address, nullptr);
    il2cpp_field_get_value_fn(object, field, &read_back);
    bool restored = read_back == nullptr;
    action_report["owned_write"] = {{"object_published", false}, {"field", "content"},
        {"field_offset", il2cpp_field_get_offset_fn(field)}, {"readback_matches", written}, {"restored_to_null", restored}};
    main_thread_bridge::free_handle(text_handle);
    main_thread_bridge::free_handle(handle);
    if (!written || !restored) return "owned_write_or_restore_failed";
    }
#endif
#ifdef LLC_PROBE_WRITE_LIVE_ONE
    {
    if (!action_report.value("remote_loaded", false) || !live_candidate.get()) return "live_trial_not_ready";
    auto* object = live_candidate.get();
    auto* field = managed_field_named(object, "content");
    if (!field || strcmp(main_thread_bridge::type_name(il2cpp_field_get_type_fn(field)), "String")) return "live_field_schema_mismatch";
    Il2CppObject* original = nullptr;
    il2cpp_field_get_value_fn(object, field, &original);
    if (!original || strcmp(il2cpp_class_get_name_fn(il2cpp_object_get_class_fn(original)), "String")) return "live_original_missing";
    Root original_root, marker_root;
    original_root.handle = main_thread_bridge::new_handle(original, false);
    auto* marker = il2cpp_string_new_fn("LLC_LIVE_PROBE_RESTORED");
    marker_root.handle = marker ? main_thread_bridge::new_handle(reinterpret_cast<Il2CppObject*>(marker), false) : 0;
    if (!original_root.handle || !marker_root.handle) return "live_trial_root_failed";
    object = live_candidate.get();
    auto** address = reinterpret_cast<void**>(reinterpret_cast<char*>(object) + il2cpp_field_get_offset_fn(field));
    il2cpp_gc_wbarrier_set_field_fn(object, address, marker_root.get());
    Il2CppObject* read_back = nullptr;
    il2cpp_field_get_value_fn(object, field, &read_back);
    bool written = read_back == marker_root.get();
    // Restore before allocating the report or returning control to the PlayerLoop.
    il2cpp_gc_wbarrier_set_field_fn(object, address, original_root.get());
    il2cpp_field_get_value_fn(object, field, &read_back);
    bool restored = read_back == original_root.get();
    action_report["live_game_fields_written"] = 1;
    action_report["live_ui_trial"] = {{"key", "loginui_sign_in_with_google"}, {"field", "content"},
        {"readback_matches", written}, {"original_reference_restored", restored}, {"ui_cache_modified", false}};
    if (!written || !restored) return "live_write_or_restore_failed";
    }
#endif
    return nullptr;
}

static void* run(void*) {
    json report = {{"schema", 1}, {"kind", "dataset_sandbox_probe"}, {"pid", getpid()},
        {"normal_localization_enabled", false}, {"status", "failed"}};
    if (!matches_diagnostic_build()) { report["error"] = "diagnostic_native_build_mismatch"; publish(report); return nullptr; }
    sleep(6);
    if (!init_il2cpp_api()) { report["error"] = "api_resolution_failed"; publish(report); return nullptr; }
    auto* domain = il2cpp_domain_get_fn();
    if (!domain || !il2cpp_thread_attach_fn(domain)) { report["error"] = "domain_not_ready"; publish(report); return nullptr; }
    sleep(5);
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(
#ifdef LLC_PROBE_WAIT_REMOTE
        45
#else
        10
#endif
    );
    bool request = true;
    while (std::chrono::steady_clock::now() < deadline) {
        if (request) { main_thread_bridge::request_probe("", "dataset_sandbox_once", inspect_on_main); request = false; }
        bool retry = false;
        {
            std::lock_guard<std::mutex> lock(main_thread_bridge::state_mutex);
            const auto& state = main_thread_bridge::state;
            if (state.done) {
                retry = state.error && (!strcmp(state.error, "awaiting_remote_tables") || !strcmp(state.error, "awaiting_content_tables"));
                report["status"] = retry ? "waiting_for_tables" : (state.error ? "failed" : "passed");
                report["error"] = state.error ? json(state.error) : json(nullptr);
                report["bridge"] = {{"sender_tid", state.sender}, {"callback_tid", state.callback},
                    {"managed_id", state.managed}, {"unity_main_id", state.main}, {"context_matches", state.context_equal},
                    {"delegate_abi_selftest", main_thread_bridge::delegate_handle != 0}, {"action_attempted", state.action_attempted}};
                report["inspection"] = action_report;
                publish(report);
                if (!retry) return nullptr;
            }
        }
        if (retry) { sleep(2); request = true; }
        else usleep(100000);
    }
    report["status"] = "failed";
    report["error"] = request ? "content_readiness_timeout" : "main_queue_or_readiness_timeout";
    publish(report);
    return nullptr;
}

extern "C" __attribute__((visibility("default"))) void llc_core_init() {
    pthread_t thread;
    if (pthread_create(&thread, nullptr, run, nullptr) == 0) pthread_detach(thread);
}
