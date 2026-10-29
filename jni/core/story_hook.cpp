#include "il2cpp_api.hpp"
#include "story_hook.hpp"
#include "managed_fields.hpp"
#include "data_patcher.hpp"
#include <android/log.h>
#include <string>
#include <cstring>
#include "story_observation.hpp"
#include "main_thread_bridge.hpp"
#include "story_refresh_guard.hpp"
#include "storage.hpp"
#include "json_loader.hpp"
#include "resource_paths.hpp"
#include <chrono>
#include <unistd.h>
#include <cerrno>

#undef LOG_TAG
#define LOG_TAG "LLC_STORY"

static bool g_story_hook_initialized = false;

bool is_story_hook_installed() {
    return g_story_hook_initialized;
}

void init_story_hook() {
    if (g_story_hook_initialized) return;
    g_story_hook_initialized = true;
    LOGI("[LLC-STORY] Story system watcher initialized");
}

void check_and_patch_active_story() {
    static StorySyncState sync;
    static uint64_t tick = 0;
    ++tick;
    static Il2CppClass* ggm_class = nullptr;
    static const MethodInfo* get_inst_mi = nullptr;
    static void* inst_field = nullptr;
    static bool ggm_resolved = false;

    if (!ggm_resolved) {
        ggm_class = find_class_any("", "GlobalGameManager");
        if (!ggm_class) ggm_class = find_class_by_name("GlobalGameManager");
        if (ggm_class) {
            get_inst_mi = find_method_in_hierarchy(ggm_class, "get_Instance", 0);
            inst_field = il2cpp_class_get_field_from_name_fn(ggm_class, "_instance");
            if (!inst_field) inst_field = il2cpp_class_get_field_from_name_fn(ggm_class, "instance");
            ggm_resolved = true;
        }
    }
    if (!ggm_class) return;

    Il2CppObject* ggm_inst = nullptr;
    if (get_inst_mi) {
        Il2CppException* exc = nullptr;
        ggm_inst = il2cpp_runtime_invoke_fn(get_inst_mi, nullptr, nullptr, &exc);
    }
    if (!ggm_inst && inst_field && il2cpp_field_static_get_value_fn) {
        il2cpp_field_static_get_value_fn(inst_field, &ggm_inst);
    }
    if (!ggm_inst) { sync.reset(); return; }

    // Field: _story at offset 216
    Il2CppObject* story_progress = *(Il2CppObject**)((char*)ggm_inst + 216);
    if (!story_progress) { sync.reset(); return; }

    // In StoryProgressManager: _activeStoryManager at offset 56
    Il2CppObject* active_sm = *(Il2CppObject**)((char*)story_progress + 56);
    if (!active_sm) { sync.reset(); return; }

    // In StoryManager:
    // _readingScenario at offset 608
    // _storyID at offset 672
    // _isReading at offset 737
    Il2CppObject* reading_scn = *(Il2CppObject**)((char*)active_sm + 608);

    if (!reading_scn) { sync.reset(); return; }
    std::string id = read_named_managed_id(active_sm, "_storyID");
    if (id.empty()) id = read_named_managed_id(reading_scn, "ID");
    if (id.empty()) id = read_named_managed_id(reading_scn, "id");
    auto before = scenario_stamp(reading_scn);
    auto manager = reinterpret_cast<uintptr_t>(active_sm);
    if (!sync.observe(manager, id, before, tick)) return;
    auto result = patch_scenario_object(reading_scn, id.c_str());
    auto after = scenario_stamp(reading_scn);
    if (!(before == after)) {
        sync.observe(manager, id, after, tick);
        LOGI("[LLC-STORY] %s list changed during patch; scheduling another pass", id.c_str());
        return;
    }
    sync.record(result, tick);
    LOGI("[LLC-STORY] %s rows=%d matched=%d writes=%llu failed_fields=%d attempt=%u state=%s",
         id.c_str(), result.rows, result.matched, (unsigned long long)result.strings_written,
         result.failed_writes, sync.table.attempts, sync.table.applied ? "applied-to-snapshot" : "retry-pending");

}

static void inspect_class_api(FILE* fp, Il2CppClass* klass) {
    if (!fp || !klass) return;
    fprintf(fp, "\nAPI: %s.%s\n", il2cpp_class_get_namespace_fn(klass), il2cpp_class_get_name_fn(klass));
    void* iter = nullptr;
    while (auto* field = il2cpp_class_get_fields_fn(klass, &iter)) {
        auto* type = il2cpp_class_from_type_fn(il2cpp_field_get_type_fn(field));
        fprintf(fp, " Field %s offset=%u type=%s.%s flags=%x\n", il2cpp_field_get_name_fn(field),
                il2cpp_field_get_offset_fn(field), type ? il2cpp_class_get_namespace_fn(type) : "?",
                type ? il2cpp_class_get_name_fn(type) : "?", il2cpp_field_get_flags_fn(field));
    }
    iter = nullptr;
    while (auto* method = il2cpp_class_get_methods_fn(klass, &iter)) {
        auto* ret = il2cpp_class_from_type_fn(il2cpp_method_get_return_type_fn(method));
        fprintf(fp, " Method %s %s(", ret ? il2cpp_class_get_name_fn(ret) : "?", il2cpp_method_get_name_fn(method));
        for (uint32_t i = 0; i < il2cpp_method_get_param_count_fn(method); ++i) {
            auto* type = il2cpp_class_from_type_fn(il2cpp_method_get_param_fn(method, i));
            fprintf(fp, "%s%s.%s %s", i ? ", " : "", type ? il2cpp_class_get_namespace_fn(type) : "?",
                    type ? il2cpp_class_get_name_fn(type) : "?", il2cpp_method_get_param_name_fn(method, i));
        }
        fprintf(fp, ") instance=%d ptr=%p\n", il2cpp_method_is_instance_fn(method), method->methodPointer);
    }
}

void inspect_story_system(const char* output_file) {
    FILE* fp = output_file ? fopen(output_file, "w") : nullptr;
    if (fp) {
        fprintf(fp, "================================================================================\n");
        fprintf(fp, "Limbus Company GlobalGameManager & Story Architecture Inspection\n");
        fprintf(fp, "================================================================================\n\n");
    }

    Il2CppClass* ggm_class = find_class_any("", "GlobalGameManager");
    if (!ggm_class) ggm_class = find_class_by_name("GlobalGameManager");
    Il2CppObject* inst_obj = nullptr;

    if (ggm_class && fp) {
        fprintf(fp, "Class: GlobalGameManager\n");
        void* f_iter = nullptr;
        void* f = nullptr;
        while ((f = il2cpp_class_get_fields_fn(ggm_class, &f_iter))) {
            const char* fn = il2cpp_field_get_name_fn(f);
            uint32_t off = il2cpp_field_get_offset_fn(f);
            fprintf(fp, "  Field: %s (offset: %u)\n", fn ? fn : "", off);
        }
        fprintf(fp, "\n");

        // Try to get static Instance field or get_Instance method
        const MethodInfo* get_inst_mi = find_method_in_hierarchy(ggm_class, "get_Instance", 0);
        if (get_inst_mi) {
            Il2CppException* exc = nullptr;
            inst_obj = il2cpp_runtime_invoke_fn(get_inst_mi, nullptr, nullptr, &exc);
            fprintf(fp, "  get_Instance() returned: %p (exc: %p)\n", inst_obj, exc);
        }
        if (!inst_obj) {
            void* inst_f = il2cpp_class_get_field_from_name_fn(ggm_class, "_instance");
            if (!inst_f) inst_f = il2cpp_class_get_field_from_name_fn(ggm_class, "instance");
            if (inst_f && il2cpp_field_static_get_value_fn) {
                il2cpp_field_static_get_value_fn(inst_f, &inst_obj);
                fprintf(fp, "  Static field _instance returned: %p\n", inst_obj);
            }
        }

        if (inst_obj) {
            fprintf(fp, "  Instance object valid at: %p\n", inst_obj);
            // Check fields on inst_obj
            f_iter = nullptr;
            while ((f = il2cpp_class_get_fields_fn(ggm_class, &f_iter))) {
                const char* fn = il2cpp_field_get_name_fn(f);
                uint32_t off = il2cpp_field_get_offset_fn(f);
                if (fn && (strstr(fn, "Story") || strstr(fn, "story") || strstr(fn, "Progress") || strstr(fn, "progress"))) {
                    void* val = *(void**)((char*)inst_obj + off);
                    fprintf(fp, "    -> Matched Field: %s (offset %u) = %p\n", fn, off, val);
                }
            }
        }
    }

    Il2CppClass* spm_class = find_class_any("StorySystem", "StoryProgressManager");
    if (!spm_class) spm_class = find_class_by_name("StoryProgressManager");
    if (spm_class && fp) {
        fprintf(fp, "\nClass: StoryProgressManager\n");
        void* f_iter = nullptr;
        void* f = nullptr;
        while ((f = il2cpp_class_get_fields_fn(spm_class, &f_iter))) {
            const char* fn = il2cpp_field_get_name_fn(f);
            uint32_t off = il2cpp_field_get_offset_fn(f);
            fprintf(fp, "  Field: %s (offset: %u)\n", fn ? fn : "", off);
        }

        // If we have inst_obj from GlobalGameManager, let's inspect the live _story object
        if (inst_obj) {
            Il2CppObject* story_obj = *(Il2CppObject**)((char*)inst_obj + 216);
            fprintf(fp, "\nLive StoryProgressManager at: %p\n", story_obj);
            if (story_obj) {
                f_iter = nullptr;
                while ((f = il2cpp_class_get_fields_fn(spm_class, &f_iter))) {
                    const char* fn = il2cpp_field_get_name_fn(f);
                    uint32_t off = il2cpp_field_get_offset_fn(f);
                    void* val = *(void**)((char*)story_obj + off);
                    fprintf(fp, "  Live Field %s (offset %u) = %p\n", fn ? fn : "", off, val);
                }

                Il2CppObject* active_sm = *(Il2CppObject**)((char*)story_obj + 56);
                fprintf(fp, "\nLive _activeStoryManager at: %p\n", active_sm);
                if (active_sm) {
                    Il2CppObject* r_scn = *(Il2CppObject**)((char*)active_sm + 608);
                    Il2CppString* s_id = *(Il2CppString**)((char*)active_sm + 672);
                    bool is_reading = *(bool*)((char*)active_sm + 737);
                    fprintf(fp, "  _readingScenario: %p, _storyID: %p, _isReading: %d\n", r_scn, s_id, is_reading);
                }
            }
        }
    }

    Il2CppClass* sm_class = find_class_any("StorySystem", "StoryManager");
    if (!sm_class) sm_class = find_class_by_name("StoryManager");
    if (sm_class && fp) {
        fprintf(fp, "\nClass: StoryManager\n");
        void* f_iter = nullptr;
        void* f = nullptr;
        while ((f = il2cpp_class_get_fields_fn(sm_class, &f_iter))) {
            const char* fn = il2cpp_field_get_name_fn(f);
            uint32_t off = il2cpp_field_get_offset_fn(f);
            fprintf(fp, "  Field: %s (offset: %u)\n", fn ? fn : "", off);
        }
    }

    Il2CppClass* sdp_class = find_class_any("StorySystem", "StoryDataParser");
    if (!sdp_class) sdp_class = find_class_by_name("StoryDataParser");
    if (sdp_class && fp) {
        fprintf(fp, "\nClass: StoryDataParser\n");
        void* f_iter = nullptr;
        void* f = nullptr;
        while ((f = il2cpp_class_get_fields_fn(sdp_class, &f_iter))) {
            const char* fn = il2cpp_field_get_name_fn(f);
            uint32_t off = il2cpp_field_get_offset_fn(f);
            fprintf(fp, "  Field: %s (offset: %u)\n", fn ? fn : "", off);
        }
        void* m_iter = nullptr;
        const MethodInfo* m = nullptr;
        while ((m = il2cpp_class_get_methods_fn(sdp_class, &m_iter))) {
            const char* mn = il2cpp_method_get_name_fn(m);
            uint32_t pc = il2cpp_method_get_param_count_fn ? il2cpp_method_get_param_count_fn(m) : 0;
            fprintf(fp, "  Method: %s (params: %u, ptr: %p)\n", mn ? mn : "", pc, m->methodPointer);
        }
    }

    auto* upper = find_class_any("StorySystem", "StoryUiUpperSet");
    if (!upper) upper = find_class_by_name("StoryUiUpperSet");
    inspect_class_api(fp, upper);
    inspect_class_api(fp, sm_class);
    inspect_class_api(fp, sdp_class);
    inspect_class_api(fp, find_class_any("UnityEngine", "UnitySynchronizationContext"));
    if (sm_class && fp) {
        void* iter = nullptr;
        while (auto* field = il2cpp_class_get_fields_fn(sm_class, &iter)) {
            const char* name = il2cpp_field_get_name_fn(field);
            if (name && (strstr(name, "UI") || strstr(name, "Ui") || strstr(name, "Dialog") || strstr(name, "dialog") || strstr(name, "Subtitle")))
                inspect_class_api(fp, il2cpp_class_from_type_fn(il2cpp_field_get_type_fn(field)));
        }
    }

    if (fp) {
        fprintf(fp, "\n================================================================================\n");
        fclose(fp);
    }
    LOGI("[LLC-STORY] Architecture inspection written to %s", output_file ? output_file : "logcat");
}


// This diagnostic reads metadata and managed fields; it deliberately invokes no
// Unity/TMP methods on the worker thread. A snapshot is observational, not atomic.

static Il2CppObject* observed_reference(Il2CppObject* owner, const char* name, const char* expected) {
    auto* field = managed_field_named(owner, name);
    if (!field) return nullptr;
    auto* klass = il2cpp_class_from_type_fn(il2cpp_field_get_type_fn(field));
    if (!klass || il2cpp_class_is_valuetype_fn(klass)) return nullptr;
    Il2CppObject* value = nullptr;
    il2cpp_field_get_value_fn(owner, field, &value);
    if (!value) return nullptr;
    for (auto* k = il2cpp_object_get_class_fn(value); k; k = il2cpp_class_get_parent_fn(k))
        if (strcmp(il2cpp_class_get_name_fn(k), expected) == 0) return value;
    return nullptr;
}

static nlohmann::json observed_text(Il2CppObject* owner, const char* name) {
    auto* field = managed_field_named(owner, name);
    if (!field || managed_field_at(owner, il2cpp_field_get_offset_fn(field)).kind != ManagedField::String) return nullptr;
    Il2CppString* value = nullptr;
    il2cpp_field_get_value_fn(owner, field, &value);
    if (!value) return nullptr;
    int size = il2cpp_string_length_fn(value);
    if (size < 0 || size > 1024 * 1024) return nullptr;
    return observed_utf8(il2cpp_string_chars_fn(value), static_cast<size_t>(size));
}

static nlohmann::json observed_tmp_fields(Il2CppObject* owner) {
    nlohmann::json texts = nlohmann::json::object();
    if (!owner) return texts;
    for (auto* k = il2cpp_object_get_class_fn(owner); k; k = il2cpp_class_get_parent_fn(k)) {
        void* iter = nullptr;
        while (auto* field = il2cpp_class_get_fields_fn(k, &iter)) {
            if (il2cpp_field_get_flags_fn(field) & 0x10) continue;
            auto* type = il2cpp_class_from_type_fn(il2cpp_field_get_type_fn(field));
            bool tmp = false;
            for (auto* base = type; base; base = il2cpp_class_get_parent_fn(base)) {
                if (strcmp(il2cpp_class_get_name_fn(base), "TMP_Text") == 0 &&
                    strcmp(il2cpp_class_get_namespace_fn(base), "TMPro") == 0) { tmp = true; break; }
            }
            if (!tmp) continue;
            Il2CppObject* value = nullptr;
            il2cpp_field_get_value_fn(owner, field, &value);
            texts[il2cpp_field_get_name_fn(field)] = observed_text(value, "m_text");
        }
    }
    return texts;
}

static Il2CppObject* observed_game() {
    auto* klass = find_class_any("", "GlobalGameManager");
    if (!klass) klass = find_class_by_name("GlobalGameManager");
    Il2CppObject* game = nullptr;
    for (auto* k = klass; k && !game; k = il2cpp_class_get_parent_fn(k)) {
        void* iter = nullptr;
        while (auto* field = il2cpp_class_get_fields_fn(k, &iter)) {
            if (!(il2cpp_field_get_flags_fn(field) & 0x10)) continue;
            auto* type = il2cpp_class_from_type_fn(il2cpp_field_get_type_fn(field));
            if (type && strcmp(il2cpp_class_get_name_fn(type), "GlobalGameManager") == 0) {
                il2cpp_field_static_get_value_fn(field, &game);
                if (game) break;
            }
        }
    }
    return game;
}

void observe_story_state(const char* output_file, const char* request_id) {
    using nlohmann::json;
    json report = {{"schema", 1}, {"request_id", request_id ? request_id : ""},
        {"pid", getpid()}, {"observer_tid", gettid()}, {"observer_is_unity_main", nullptr},
        {"read_only", true}, {"status", "no_singleton"},
        {"monotonic_ms", std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()}};
    auto* game = observed_game();
    auto* progress = observed_reference(game, "_story", "StoryProgressManager");
    auto* manager = observed_reference(progress, "_activeStoryManager", "StoryManager");
    auto* scenario = observed_reference(manager, "_readingScenario", "Scenario");
    auto* dialog = observed_reference(manager, "_dialog", "Dialog");
    auto stamp = scenario_stamp(scenario);
    report["manager"] = reinterpret_cast<uintptr_t>(manager);
    report["scenario"] = reinterpret_cast<uintptr_t>(scenario);
    report["dialog"] = reinterpret_cast<uintptr_t>(dialog);
    report["story_id"] = read_named_managed_id(manager, "_storyID");
    report["dialog_id"] = read_named_managed_id(dialog, "id");
    report["runtime_rows"] = stamp.count;
    report["list_version"] = stamp.version;
    if (game) report["status"] = "no_story";
    if (scenario && manager) {
        std::string id = report["story_id"].get<std::string>();
        if (id.empty()) id = read_named_managed_id(scenario, "ID");
        report["story_id"] = id;
        std::unordered_map<int, ScenarioDialogue> translations;
        bool resource_found = false;
        if (safe_story_id(id)) {
            std::string upper = id;
            for (auto& c : upper) if (c >= 'a' && c <= 'z') c -= ('a' - 'A');
            const auto roots = resource_paths::story_roots();
            for (const auto& candidate : {id, upper}) {
                for (const auto& root : roots) {
                    std::string path = std::string(root) + candidate + ".json";
                    if (access(path.c_str(), R_OK) == 0) {
                        resource_found = true;
                        parse_scenario_json_file(path, translations);
                        break;
                    }
                }
                if (resource_found) break;
            }
        }
        report["resource_found"] = resource_found;
        report["resource_rows"] = translations.size();
        int matched = 0, equal_fields = 0, checked_fields = 0, invalid_ids = 0;
        json mismatches = json::array();
        auto compare = [&](Il2CppObject* row, int number, const char* name, const std::string& expected) {
            if (expected.empty()) return;
            ++checked_fields;
            auto actual = observed_text(row, name);
            if (actual.is_string() && actual.get<std::string>() == expected) ++equal_fields;
            else if (mismatches.size() < 20) mismatches.push_back({{"id", number}, {"field", name}, {"actual", actual}, {"expected", expected}});
        };
        if (stamp.ready()) {
            auto** rows = reinterpret_cast<Il2CppObject**>(stamp.entries + 32);
            for (int i = 0; i < stamp.count; ++i) {
                auto key = read_named_managed_id(rows[i], "id");
                int number = 0;
                auto parsed = std::from_chars(key.data(), key.data() + key.size(), number);
                if (key.empty() || parsed.ec != std::errc{} || parsed.ptr != key.data() + key.size()) { ++invalid_ids; continue; }
                auto found = translations.find(number);
                if (found == translations.end()) continue;
                ++matched;
                compare(rows[i], number, "content", found->second.content);
                compare(rows[i], number, "place", found->second.place);
                compare(rows[i], number, "teller", found->second.teller);
                compare(rows[i], number, "title", found->second.title);
            }
        }
        report["matched_rows"] = matched;
        report["invalid_runtime_ids"] = invalid_ids;
        report["checked_fields"] = checked_fields;
        report["equal_fields"] = equal_fields;
        report["mismatches"] = mismatches;
        report["snapshot_unchanged"] = stamp == scenario_stamp(scenario) &&
            manager == observed_reference(progress, "_activeStoryManager", "StoryManager") &&
            progress == observed_reference(game, "_story", "StoryProgressManager") &&
            scenario == observed_reference(manager, "_readingScenario", "Scenario") &&
            dialog == observed_reference(manager, "_dialog", "Dialog");
        report["data_matches_resource"] = stamp.ready() && matched == stamp.count && checked_fields > 0 &&
            equal_fields == checked_fields && report["snapshot_unchanged"].get<bool>();
        report["current_data"] = {{"content", observed_text(dialog, "content")}, {"place", observed_text(dialog, "place")},
            {"teller", observed_text(dialog, "teller")}, {"title", observed_text(dialog, "title")}};
        report["display_fields"] = observed_tmp_fields(observed_reference(manager, "_dialogCon", "DialogController"));
        report["upper_fields"] = observed_tmp_fields(observed_reference(manager, "_uiUpperSet", "StoryUiUpperSet"));
        report["controller_texts"] = json::object();
        auto* manager_class = il2cpp_object_get_class_fn(manager);
        void* iter = nullptr;
        while (auto* field = il2cpp_class_get_fields_fn(manager_class, &iter)) {
            if (il2cpp_field_get_flags_fn(field) & 0x10) continue;
            auto* type = il2cpp_class_from_type_fn(il2cpp_field_get_type_fn(field));
            if (!type || il2cpp_class_is_valuetype_fn(type) ||
                strcmp(il2cpp_class_get_namespace_fn(type), "StorySystem") != 0) continue;
            Il2CppObject* controller = nullptr;
            il2cpp_field_get_value_fn(manager, field, &controller);
            auto texts = observed_tmp_fields(controller);
            if (!texts.empty()) report["controller_texts"][il2cpp_field_get_name_fn(field)] = std::move(texts);
        }
        report["status"] = "observed";
    }
    std::string temporary = std::string(output_file) + ".next";
    std::string serialized = report.dump(2);
    if (FILE* file = fopen(temporary.c_str(), "wb")) {
        bool ok = fwrite(serialized.data(), 1, serialized.size(), file) == serialized.size();
        if (fclose(file) != 0) ok = false;
        if (!ok || rename(temporary.c_str(), output_file) != 0) {
            LOGE("[LLC-STORY-CHECK] Failed to publish report: errno=%d", errno);
            unlink(temporary.c_str());
        } else LOGI("[LLC-STORY-CHECK] request=%s status=%s", request_id ? request_id : "", report["status"].get<std::string>().c_str());
    }
}


struct RefreshChange { const char* field; bool animator; uintptr_t component; std::string before, after; };
struct RefreshTicket {
    StoryRefreshIdentity identity;
    ScenarioDialogue expected;
    std::vector<RefreshChange> changes;
    std::chrono::steady_clock::time_point deadline;
    const char* error = nullptr;
};
static std::mutex refresh_mutex;
static RefreshTicket refresh_ticket;

static StoryRefreshIdentity live_identity(Il2CppObject*& manager, Il2CppObject*& dialog) {
    auto* progress = observed_reference(observed_game(), "_story", "StoryProgressManager");
    manager = observed_reference(progress, "_activeStoryManager", "StoryManager");
    auto* scenario = observed_reference(manager, "_readingScenario", "Scenario");
    dialog = observed_reference(manager, "_dialog", "Dialog");
    return {reinterpret_cast<uintptr_t>(manager), reinterpret_cast<uintptr_t>(scenario), reinterpret_cast<uintptr_t>(dialog),
            read_named_managed_id(manager, "_storyID"), read_named_managed_id(dialog, "id")};
}

static const char* apply_refresh_on_main() {
    RefreshTicket ticket;
    { std::lock_guard<std::mutex> lock(refresh_mutex); ticket = refresh_ticket; }
    if (ticket.error) return ticket.error;
    if (std::chrono::steady_clock::now() > ticket.deadline) return "refresh_ticket_expired";
    Il2CppObject* manager = nullptr;
    Il2CppObject* dialog = nullptr;
    if (!(ticket.identity == live_identity(manager, dialog))) return "story_changed_since_request";
    if (observed_text(dialog, "content") != ticket.expected.content ||
        (!ticket.expected.place.empty() && observed_text(dialog, "place") != ticket.expected.place)) return "dialog_text_changed_since_request";
    auto* controller = observed_reference(manager, "_dialogCon", "DialogController");
    auto* animator = observed_reference(manager, "_uiAnimator", "StoryManagerUIAnimator");
    if (!controller) return "normal_dialog_controller_missing";
    auto* black = managed_field_named(controller, "_isBlackScreen");
    if (!black || strcmp(main_thread_bridge::type_name(il2cpp_field_get_type_fn(black)), "Boolean")) return "dialog_mode_schema_mismatch";
    bool is_black = false;
    il2cpp_field_get_value_fn(controller, black, &is_black);
    if (is_black) return "black_screen_mode_unsupported";
    if (observed_reference(controller, "_subtitleText", "TextMeshProUGUI")) return "subtitle_mode_unsupported";
    auto ruby = observed_text(observed_reference(controller, "tmp_serifRuby", "CustomRubyTextMeshProUGUI"), "m_text");
    if (ruby.is_string() && !ruby.get<std::string>().empty()) return "ruby_mode_unsupported";
    auto* tween = find_method_in_hierarchy(il2cpp_object_get_class_fn(controller), "GetIsTween", 0);
    if (!tween || strcmp(main_thread_bridge::type_name(il2cpp_method_get_return_type_fn(tween)), "Boolean")) return "typing_state_method_missing";
    Il2CppException* exception = nullptr;
    auto* boxed = il2cpp_runtime_invoke_fn(tween, controller, nullptr, &exception);
    if (exception || !boxed) return "typing_state_unavailable";
    if (*static_cast<bool*>(il2cpp_object_unbox_fn(boxed))) return "typing_active_retry_when_idle";
    // Validate all targets and old cache values before the first setter call.
    for (const auto& change : ticket.changes) {
        auto* target = observed_reference(change.animator ? animator : controller, change.field, "TextMeshProUGUI");
        if (!target || reinterpret_cast<uintptr_t>(target) != change.component) return "text_target_changed";
        auto current = observed_text(target, "m_text");
        if (!current.is_string() || (current.get<std::string>() != change.before && current.get<std::string>() != change.after))
            return "text_cache_changed_since_request";
        auto* setter = find_method_in_hierarchy(il2cpp_object_get_class_fn(target), "set_text", 1);
        if (!setter || strcmp(main_thread_bridge::type_name(il2cpp_method_get_param_fn(setter, 0)), "String")) return "tmp_setter_schema_mismatch";
    }
    for (const auto& change : ticket.changes) {
        auto* target = observed_reference(change.animator ? animator : controller, change.field, "TextMeshProUGUI");
        auto* setter = find_method_in_hierarchy(il2cpp_object_get_class_fn(target), "set_text", 1);
        auto* text = il2cpp_string_new_fn(change.after.c_str());
        if (!text) return "text_allocation_failed";
        auto root = main_thread_bridge::new_handle(reinterpret_cast<Il2CppObject*>(text), false);
        if (!root) return "text_root_failed";
        void* args[] = {main_thread_bridge::get_target(root)};
        il2cpp_runtime_invoke_fn(setter, target, args, &exception);
        main_thread_bridge::free_handle(root);
        if (exception || observed_text(target, "m_text") != change.after) return "tmp_setter_readback_failed";
    }
    return nullptr;
}

void request_story_refresh(const char* output_file, const char* request_id) {
    if (main_thread_bridge::busy()) return;
    RefreshTicket ticket;
    ticket.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    Il2CppObject* manager = nullptr;
    Il2CppObject* dialog = nullptr;
    ticket.identity = live_identity(manager, dialog);
    if (!ticket.identity.valid() || !safe_story_id(ticket.identity.story_id)) ticket.error = "no_active_story";
    std::unordered_map<int, ScenarioDialogue> translations;
    if (!ticket.error) {
        const auto roots = resource_paths::story_roots();
        for (const auto& root : roots) {
            std::string path = std::string(root) + ticket.identity.story_id + ".json";
            if (access(path.c_str(), R_OK) == 0) { parse_scenario_json_file(path, translations); break; }
        }
        int id = 0;
        const auto& key = ticket.identity.dialog_id;
        auto parsed = std::from_chars(key.data(), key.data() + key.size(), id);
        auto found = translations.find(id);
        if (parsed.ec != std::errc{} || parsed.ptr != key.data() + key.size() || found == translations.end() || found->second.content.empty()) ticket.error = "current_dialog_resource_missing";
        else {
            const auto& text = found->second;
            ticket.expected = text;
            if (observed_text(dialog, "content") != text.content ||
                (!text.place.empty() && observed_text(dialog, "place") != text.place)) ticket.error = "dialog_data_not_localized";
            auto* controller = observed_reference(manager, "_dialogCon", "DialogController");
            auto* animator = observed_reference(manager, "_uiAnimator", "StoryManagerUIAnimator");
            auto add = [&](const char* name, bool upper, const std::string& translated) {
                if (translated.empty() || ticket.error) return;
                auto* component = observed_reference(upper ? animator : controller, name, "TextMeshProUGUI");
                auto old = observed_text(component, "m_text");
                if (!component || !old.is_string()) { ticket.error = "text_cache_unavailable"; return; }
                ticket.changes.push_back({name, upper, reinterpret_cast<uintptr_t>(component), old.get<std::string>(), translated});
            };
            add("tmp_serif", false, text.content);
            add("tmp_checkSerif", false, text.content);
            add("tmp_name", false, text.teller);
            add("tmp_nickName", false, text.title);
            add("tmp_mapText", true, text.place);
        }
    }
    { std::lock_guard<std::mutex> lock(refresh_mutex); refresh_ticket = std::move(ticket); }
    main_thread_bridge::request_probe(output_file, request_id, apply_refresh_on_main);
}
