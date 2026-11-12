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
#include "story_refresh_schedule.hpp"
#include "storage.hpp"
#include "json_loader.hpp"
#include "resource_paths.hpp"
#include <chrono>
#include <atomic>
#include <dirent.h>
#include <memory>
#include "translation_file.hpp"
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

Il2CppObject* observed_reference(Il2CppObject* owner, const char* name, const char* expected) {
    if (!owner || (reinterpret_cast<uintptr_t>(owner) <= 0x10000)) return nullptr;
    auto* field = managed_field_named(owner, name);
    if (!field) return nullptr;
    auto* klass = il2cpp_class_from_type_fn(il2cpp_field_get_type_fn(field));
    if (!klass || il2cpp_class_is_valuetype_fn(klass)) return nullptr;
    Il2CppObject* value = nullptr;
    il2cpp_field_get_value_fn(owner, field, &value);
    if (!value || (reinterpret_cast<uintptr_t>(value) <= 0x10000)) return nullptr;
    for (auto* k = il2cpp_object_get_class_fn(value); k; k = il2cpp_class_get_parent_fn(k))
        if (strcmp(il2cpp_class_get_name_fn(k), expected) == 0) return value;
    return nullptr;
}

Il2CppObject* observed_game() {
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
                if (game && (reinterpret_cast<uintptr_t>(game) > 0x10000)) break;
                game = nullptr;
            }
        }
    }
    return game;
}

// Replays and identity upgrades can show a live dialog with _isReading
// false. Treat a dialog owned by the live scenario as independent evidence.
static bool story_not_ending(Il2CppObject* manager) {
    auto* field = managed_field_named(manager, "_isEndingScenario");
    if (!field || strcmp(main_thread_bridge::type_name(il2cpp_field_get_type_fn(field)), "Boolean")) return false;
    bool ending = false;
    il2cpp_field_get_value_fn(manager, field, &ending);
    return !ending;
}

static bool live_scenario_dialog(Il2CppObject* manager, Il2CppObject* scenario,
                                 Il2CppObject* dialog, const std::string& id) {
    if (!dialog || !safe_story_id(id) || !story_not_ending(manager)) return false;
    const auto stamp = scenario_stamp(scenario);
    if (!stamp.ready() || stamp.count > 4096) return false;
    auto* const* rows = reinterpret_cast<Il2CppObject* const*>(stamp.entries + 32);
    for (int i = 0; i < stamp.count; ++i) if (rows[i] == dialog) return true;
    return false;
}

// All managed event work is on main. The engine worker publishes a read-only index.
namespace story_playback {
using main_thread_bridge::Handle;
static Handle manager_root = 0, callback_root = 0;
static const MethodInfo* remove_event = nullptr;
static bool testing = false;
static std::atomic<bool> broken{false};
static unsigned self_calls = 0;
static int self_argument = 0;
static std::atomic<bool> pending{false}, armed{false};
static std::atomic<uintptr_t> subscribed_address{0};
struct PlaybackIndex {
    std::unordered_map<std::string, StoryRows> stories;
    decltype(g_scenario_model_codes) models;
    decltype(g_ui_map) labels;
};
static std::shared_ptr<const PlaybackIndex> playback_index;

static bool prepare_index() {
    auto next = std::make_shared<PlaybackIndex>();
    size_t remaining = 32 * 1024 * 1024;
    unsigned scanned = 0;
    for (const auto& root : resource_paths::story_roots()) {
        std::unique_ptr<DIR, int(*)(DIR*)> dir(opendir(root.c_str()), closedir);
        if (!dir) continue;
        while (auto* entry = readdir(dir.get())) {
            std::string name(entry->d_name);
            if (name.size() < 6 || name.compare(name.size() - 5, 5, ".json")) continue;
            auto id = name.substr(0, name.size() - 5);
            if (!safe_story_id(id) || next->stories.count(id)) continue;
            if (++scanned > 4096 || !remaining) return false;
            std::string source;
            const auto limit = std::min<size_t>(5 * 1024 * 1024, remaining);
            if (translation_file::read(root + name, source, limit) != translation_file::Error::None) continue;
            remaining -= source.size();
            StoryRows rows;
            if (parse_story_document(source, rows) && !rows.empty()) next->stories.emplace(std::move(id), std::move(rows));
        }
    }
    if (next->stories.empty()) return false;
    next->models = g_scenario_model_codes;
    next->labels = g_ui_map;
    LOGI("[LLC-STORY-PLAYBACK] playback index ready stories=%zu bytes=%zu",
         next->stories.size(), 32 * 1024 * 1024 - remaining);
    std::atomic_store(&playback_index, std::shared_ptr<const PlaybackIndex>(std::move(next)));
    return true;
}

static unsigned apply_playback_fields(Il2CppObject* play, Il2CppObject* dialog, Il2CppObject* scenario,
                                      const std::string& id, const std::string& line) {
    const auto index = std::atomic_load(&playback_index);
    if (!index || !play || !dialog || !safe_story_id(id) || line.empty()) return 0;
    auto story = index->stories.find(id);
    if (story == index->stories.end()) {
        auto upper = id;
        for (char& c : upper) if (c >= 'a' && c <= 'z') c -= ('a' - 'A');
        story = index->stories.find(upper);
    }
    if (story == index->stories.end()) return 0;
    const auto* row = scenario_dialogue_translation(scenario, dialog, story->second, line);
    if (!row) return 0;
    const auto model = read_named_managed_id(dialog, "Model");
    const auto play_model = read_named_managed_id(play, "Model");
    if (!model.empty() && !play_model.empty() && model != play_model) return 0;
    const auto resolved = resolve_story_fields(*row, model,
        read_named_managed_id(play, "Teller"), read_named_managed_id(play, "Title"),
        index->models, index->labels);
    struct Change { const char* name; const std::string* value; uint32_t offset = 0; Handle root = 0; };
    Change changes[] = {{"Content", &resolved.content}, {"Teller", &resolved.speaker},
        {"Title", &resolved.title}, {"Place", &resolved.place}};
    for (auto& change : changes) {
        if (change.value->empty() || read_named_managed_id(play, change.name) == *change.value) continue;
        auto* field = managed_field_named(play, change.name);
        if (!field) return 0;
        const auto offset = il2cpp_field_get_offset_fn(field);
        if (managed_field_at(play, offset).kind != ManagedField::String) return 0;
        change.offset = offset;
    }
    struct Release {
        Change (&changes)[4];
        ~Release() { for (const auto& c : changes) if (c.root) main_thread_bridge::free_handle(c.root); }
    } release{changes};
    for (auto& change : changes) {
        if (!change.offset) continue;
        auto* str = il2cpp_string_new_fn(change.value->c_str());
        if (!str || !(change.root = main_thread_bridge::new_handle(reinterpret_cast<Il2CppObject*>(str), false))) return 0;
    }
    unsigned written = 0;
    for (const auto& change : changes) {
        if (!change.root) continue;
        il2cpp_gc_wbarrier_set_field_fn(play,
            reinterpret_cast<void**>(reinterpret_cast<char*>(play) + change.offset),
            main_thread_bridge::get_target(change.root));
        ++written;
    }
    return written;
}

static void callback(void*, int position, const MethodInfo*) noexcept {
    if (testing) { ++self_calls; self_argument = position; return; }
    try {
        if (!armed.load() || broken.load() || !manager_root) return;
        auto* manager = main_thread_bridge::get_target(manager_root);
        auto* progress = observed_reference(observed_game(), "_story", "StoryProgressManager");
        if (manager != observed_reference(progress, "_activeStoryManager", "StoryManager")) return;
        auto* dialog = observed_reference(manager, "_dialog", "Dialog");
        auto* play = observed_reference(manager, "playData", "StoryPlayData");
        const auto id = read_named_managed_id(manager, "_storyID");
        const auto line = read_named_managed_id(dialog, "id");
        const auto written = apply_playback_fields(play, dialog,
            observed_reference(manager, "_readingScenario", "Scenario"), id, line);
        LOGI("[LLC-STORY-PLAYBACK] callback story=%s dialog=%s position=%d play_data=%d writes=%u tid=%d",
            id.c_str(), line.c_str(), position, play != nullptr, written, gettid());
    } catch (...) {}
}

static bool detach() {
    using namespace main_thread_bridge;
    if (!manager_root) return true;
    Il2CppException* exception = nullptr;
    void* args[] = {get_target(callback_root)};
    il2cpp_runtime_invoke_fn(remove_event, get_target(manager_root), args, &exception);
    if (exception) return false;
    free_handle(manager_root); free_handle(callback_root);
    manager_root = callback_root = 0;
    subscribed_address.store(0);
    remove_event = nullptr;
    LOGI("[LLC-STORY-PLAYBACK] detached");
    return true;
}

static const char* attach(Il2CppObject* manager) {
    using namespace main_thread_bridge;
    auto* manager_type = il2cpp_object_get_class_fn(manager);
    auto* event = field_in_hierarchy(manager_type, "OnDialogStarted");
    auto* callback_type = event ? il2cpp_class_from_type_fn(il2cpp_field_get_type_fn(event)) : nullptr;
    auto* invoke = callback_type ? il2cpp_class_get_method_from_name_fn(callback_type, "Invoke", 1) : nullptr;
    auto* add = find_method_in_hierarchy(manager_type, "add_OnDialogStarted", 1);
    auto* remove = find_method_in_hierarchy(manager_type, "remove_OnDialogStarted", 1);
    auto* app = find_class_any("UnityEngine", "Application");
    auto* template_method = app ? il2cpp_class_get_method_from_name_fn(app, "set_targetFrameRate", 1) : nullptr;
    auto exact_event = [&](const MethodInfo* method) {
        return method && il2cpp_method_is_instance_fn(method) &&
            il2cpp_class_from_type_fn(il2cpp_method_get_param_fn(method, 0)) == callback_type &&
            !strcmp(type_name(il2cpp_method_get_return_type_fn(method)), "Void");
    };
    if (!invoke || !exact_event(add) || !exact_event(remove) ||
        strcmp(type_name(il2cpp_method_get_param_fn(invoke, 0)), "Int32") ||
        strcmp(type_name(il2cpp_method_get_return_type_fn(invoke)), "Void") ||
        !template_method || il2cpp_method_is_instance_fn(template_method) ||
        strcmp(type_name(il2cpp_method_get_param_fn(template_method, 0)), "Int32") ||
        strcmp(type_name(il2cpp_method_get_return_type_fn(template_method)), "Void")) return "event_schema_mismatch";
    auto* ptr_field = field_in_hierarchy(callback_type, "method_ptr");
    auto* invoke_field = field_in_hierarchy(callback_type, "invoke_impl");
    for (auto* field : {ptr_field, invoke_field})
        if (!field || (il2cpp_field_get_flags_fn(field) & 0x10) ||
            strcmp(type_name(il2cpp_field_get_type_fn(field)), "IntPtr")) return "delegate_schema_mismatch";
    auto* delegate = find_class_any("System", "Delegate");
    const MethodInfo* create = nullptr;
    void* iter = nullptr;
    while (delegate) {
        auto* candidate = il2cpp_class_get_methods_fn(delegate, &iter);
        if (!candidate) break;
        if (!strcmp(il2cpp_method_get_name_fn(candidate), "CreateDelegate") &&
            !il2cpp_method_is_instance_fn(candidate) && il2cpp_method_get_param_count_fn(candidate) == 2 &&
            !strcmp(type_name(il2cpp_method_get_param_fn(candidate, 0)), "Type") &&
            !strcmp(type_name(il2cpp_method_get_param_fn(candidate, 1)), "MethodInfo")) { create = candidate; break; }
    }
    if (!create) return "create_delegate_missing";
    auto mgr = new_handle(manager, false);
    if (!mgr) return "manager_root_failed";
    auto* reflected = method_object(template_method, app);
    auto meth = reflected ? new_handle(reflected, false) : 0;
    if (!meth) { free_handle(mgr); return "template_root_failed"; }
    auto* type = il2cpp_type_get_object_fn(il2cpp_class_get_type_fn(callback_type));
    Il2CppException* exc = nullptr;
    void* args[] = {type, get_target(meth)};
    auto* obj = type ? il2cpp_runtime_invoke_fn(create, nullptr, args, &exc) : nullptr;
    free_handle(meth);
    auto handle = obj && !exc && il2cpp_object_get_class_fn(obj) == callback_type ? new_handle(obj, false) : 0;
    if (!handle) { free_handle(mgr); return "delegate_creation_failed"; }
    void* native = reinterpret_cast<void*>(callback);
    il2cpp_field_set_value_fn(get_target(handle), ptr_field, &native);
    il2cpp_field_set_value_fn(get_target(handle), invoke_field, &native);
    int sentinel = 947132;
    void* test_args[] = {&sentinel};
    self_calls = 0;
    testing = true;
    il2cpp_runtime_invoke_fn(invoke, get_target(handle), test_args, &exc);
    testing = false;
    if (exc || self_calls != 1 || self_argument != sentinel) {
        free_handle(handle); free_handle(mgr); return "delegate_abi_selftest_failed";
    }
    void* add_args[] = {get_target(handle)};
    il2cpp_runtime_invoke_fn(add, get_target(mgr), add_args, &exc);
    if (exc) { free_handle(handle); free_handle(mgr); return "event_add_failed"; }
    manager_root = mgr; callback_root = handle; remove_event = remove;
    subscribed_address.store(reinterpret_cast<uintptr_t>(manager));
    const auto dialog = read_named_managed_id(observed_reference(manager, "_dialog", "Dialog"), "id");
    LOGI("[LLC-STORY-PLAYBACK] attached story=%s dialog=%s first_line=%s tid=%d",
        read_named_managed_id(manager, "_storyID").c_str(), dialog.c_str(),
        dialog.empty() ? "not_bound" : "already_bound", gettid());
    return nullptr;
}

static void tick(bool enabled) {
    if (broken.load() || pending.exchange(true)) return;
    auto reset = [] { pending.store(false); };
    if (!main_thread_bridge::post_task([enabled, reset] {
        reset();
        if (broken.load()) return;
        auto* progress = observed_reference(observed_game(), "_story", "StoryProgressManager");
        auto* manager = enabled && armed.load() ? observed_reference(progress, "_activeStoryManager", "StoryManager") : nullptr;
        if (manager_root && manager == main_thread_bridge::get_target(manager_root)) return;
        if (!detach()) {
            broken = true;
            LOGW("[LLC-STORY-PLAYBACK] detach_failed");
            return;
        }
        if (manager) {
            if (const auto* error = attach(manager)) {
                broken = true;
                LOGW("[LLC-STORY-PLAYBACK] %s", error);
            }
        }
    }, reset)) reset();
}
} // namespace story_playback

void reload_story_playback_index(const std::string& game_digest) {
    // The callback's call order was verified on this exact Android 1.119.0
    // binary. Ordinary localization remains available on other builds.
    constexpr const char* verified_digest =
        "772a310dd3f6409524882cd41cc0b44dc936dd6a9520355e859c43ea9a288d2b";
    if (game_digest == verified_digest) {
        try {
            if (story_playback::prepare_index()) return;
        } catch (const std::exception& error) {
            LOGW("[LLC-STORY-PLAYBACK] index failed: %s", error.what());
        }
    }
    std::atomic_store(&story_playback::playback_index, std::shared_ptr<const story_playback::PlaybackIndex>{});
    LOGI("[LLC-STORY-PLAYBACK] using existing watcher: %s",
         game_digest == verified_digest ? "index_unavailable" : "game_build_unverified");
}

void update_story_playback_subscription(bool localization_active) {
    const bool enabled = localization_active && !story_playback::broken.load() &&
        std::atomic_load(&story_playback::playback_index) != nullptr;
    story_playback::armed.store(enabled);
    const auto subscribed = story_playback::subscribed_address.load();
    if (!enabled && !subscribed) return;
    auto* progress = enabled ? observed_reference(observed_game(), "_story", "StoryProgressManager") : nullptr;
    const auto desired = reinterpret_cast<uintptr_t>(observed_reference(progress, "_activeStoryManager", "StoryManager"));
    if (desired != subscribed) story_playback::tick(enabled);
}

bool check_and_patch_active_story() {
    static StorySyncState sync;
    static uint64_t tick = 0;
    ++tick;

    Il2CppObject* ggm_inst = observed_game();
    if (!ggm_inst) { sync.reset(); return false; }

    Il2CppObject* story_progress = observed_reference(ggm_inst, "_story", "StoryProgressManager");
    if (!story_progress) { sync.reset(); return false; }

    Il2CppObject* active_sm = observed_reference(story_progress, "_activeStoryManager", "StoryManager");
    if (!active_sm) { sync.reset(); return false; }

    bool is_reading = true;
    if (auto* field = managed_field_named(active_sm, "_isReading"))
        il2cpp_field_get_value_fn(active_sm, field, &is_reading);

    Il2CppObject* reading_scn = observed_reference(active_sm, "_readingScenario", "Scenario");
    if (!reading_scn) { sync.reset(); return false; }

    std::string id = read_named_managed_id(active_sm, "_storyID");
    if (id.empty()) id = read_named_managed_id(reading_scn, "ID");
    if (id.empty()) id = read_named_managed_id(reading_scn, "id");
    if (id.empty() || !safe_story_id(id)) { sync.reset(); return false; }

    auto* dialog = observed_reference(active_sm, "_dialog", "Dialog");
    bool before_first_dialog = false;
    if (!is_reading && !live_scenario_dialog(active_sm, reading_scn, dialog, id)) {
        // Without a dialog, verify the scenario's own ID before writing. The
        // manager can still hold the previous scenario during a transition.
        auto scenario_id = read_named_managed_id(reading_scn, "ID");
        if (scenario_id.empty()) scenario_id = read_named_managed_id(reading_scn, "id");
        before_first_dialog = !dialog && scenario_id == id &&
            story_not_ending(active_sm) && scenario_stamp(reading_scn).ready();
        if (!before_first_dialog) {
            sync.reset();
            return false;
        }
    }
    auto before = scenario_stamp(reading_scn);
    auto manager = reinterpret_cast<uintptr_t>(active_sm);
    if (!sync.observe(manager, id, before, tick)) return true;
    auto result = patch_scenario_object(reading_scn, id.c_str());
    auto after = scenario_stamp(reading_scn);
    if (!(before == after)) {
        sync.observe(manager, id, after, tick);
        LOGI("[LLC-STORY] %s list changed during patch; scheduling another pass", id.c_str());
        return true;
    }
    sync.record(result, tick);
    LOGI("[LLC-STORY] %s rows=%d matched=%d writes=%llu failed_fields=%d attempt=%u state=%s phase=%s",
         id.c_str(), result.rows, result.matched, (unsigned long long)result.strings_written,
         result.failed_writes, sync.table.attempts, sync.table.applied ? "applied-to-snapshot" : "retry-pending",
         before_first_dialog ? "before-first-dialog" : "active-dialog");
    return true;
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

static const char* observed_refresh_mode(Il2CppObject* controller) {
    auto* field = managed_field_named(controller, "_isBlackScreen");
    bool schema = field && !strcmp(main_thread_bridge::type_name(il2cpp_field_get_type_fn(field)), "Boolean");
    bool black = false;
    if (schema) il2cpp_field_get_value_fn(controller, field, &black);
    bool subtitle = observed_reference(controller, "_subtitleText", "TextMeshProUGUI") != nullptr;
    auto ruby = observed_text(observed_reference(controller, black ? "tmp_blackScreenSerifRuby" : "tmp_serifRuby", "CustomRubyTextMeshProUGUI"), "m_text");
    return story_refresh_mode_error(controller != nullptr, schema, black, subtitle, ruby.is_string() && !ruby.get<std::string>().empty());
}



// On-demand only. Unity object enumeration runs on the verified main thread.
static void observe_story_managers(std::string output, std::string request) {
    main_thread_bridge::post_task([output = std::move(output), request = std::move(request)] {
        using nlohmann::json;
        json report = {{"pid", getpid()}, {"request_id", request},
            {"observer_tid", gettid()}, {"status", "schema_unavailable"},
            {"instances", json::array()}};
        auto* progress = observed_reference(observed_game(), "_story", "StoryProgressManager");
        auto* active = observed_reference(progress, "_activeStoryManager", "StoryManager");
        report["active_manager"] = reinterpret_cast<uintptr_t>(active);
        auto* resources = find_class_any("UnityEngine", "Resources");
        auto* manager_type = find_class_any("StorySystem", "StoryManager");
        const auto* find = resources ? il2cpp_class_get_method_from_name_fn(resources, "FindObjectsOfTypeAll", 1) : nullptr;
        if (manager_type && find && !il2cpp_method_is_instance_fn(find) &&
            !strcmp(main_thread_bridge::type_name(il2cpp_method_get_param_fn(find, 0)), "Type")) {
            auto* type = il2cpp_type_get_object_fn(il2cpp_class_get_type_fn(manager_type));
            Il2CppException* exc = nullptr;
            void* args[] = {type};
            auto* objects = type ? il2cpp_runtime_invoke_fn(find, nullptr, args, &exc) : nullptr;
            if (objects && !exc) {
                const auto count = il2cpp_array_length_fn(objects);
                report["total"] = count;
                report["status"] = count > 64 ? "instance_limit" : "observed";
                if (count <= 64) {
                    auto* const* entries = reinterpret_cast<Il2CppObject* const*>(reinterpret_cast<char*>(objects) + sizeof(void*) * 4);
                    for (uintptr_t i = 0; i < count; ++i) {
                        auto* obj = entries[i];
                        if (!obj) continue;
                        const auto* id_method = find_method_in_hierarchy(il2cpp_object_get_class_fn(obj), "GetInstanceID", 0);
                        Il2CppException* id_exc = nullptr;
                        auto* boxed = id_method ? il2cpp_runtime_invoke_fn(id_method, obj, nullptr, &id_exc) : nullptr;
                        json unity_id = nullptr;
                        if (boxed && !id_exc && !strcmp(il2cpp_class_get_name_fn(il2cpp_object_get_class_fn(boxed)), "Int32"))
                            unity_id = *static_cast<int32_t*>(il2cpp_object_unbox_fn(boxed));
                        report["instances"].push_back({{"address", reinterpret_cast<uintptr_t>(obj)},
                            {"unity_instance_id", unity_id}, {"active", obj == active},
                            {"story_id", read_named_managed_id(obj, "_storyID")},
                            {"dialog_id", read_named_managed_id(observed_reference(obj, "_dialog", "Dialog"), "id")}});
                    }
                }
            }
        }
        auto tmp = output + ".next";
        const auto bytes = report.dump();
        if (FILE* file = fopen(tmp.c_str(), "wb")) {
            bool ok = fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size();
            if (fclose(file) != 0) ok = false;
            if (!ok || rename(tmp.c_str(), output.c_str()) != 0) unlink(tmp.c_str());
        }
    });
}

void observe_story_state(const char* output_file, const char* request_id) {
    using nlohmann::json;
    if (request_id && std::string(request_id).rfind("instances-", 0) == 0)
        observe_story_managers(std::string(output_file) + ".instances", request_id);
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
    auto* manager_type = manager ? il2cpp_object_get_class_fn(manager) : find_class_any("StorySystem", "StoryManager");
    auto* event_field = manager_type ? main_thread_bridge::field_in_hierarchy(manager_type, "OnDialogStarted") : nullptr;
    auto* event_type = event_field ? il2cpp_class_from_type_fn(il2cpp_field_get_type_fn(event_field)) : nullptr;
    const auto* invoke = event_type ? il2cpp_class_get_method_from_name_fn(event_type, "Invoke", 1) : nullptr;
    const auto* add = manager_type ? il2cpp_class_get_method_from_name_fn(manager_type, "add_OnDialogStarted", 1) : nullptr;
    const auto* remove = manager_type ? il2cpp_class_get_method_from_name_fn(manager_type, "remove_OnDialogStarted", 1) : nullptr;
    report["dialog_started_event"] = {
        {"delegate_type", event_type ? il2cpp_class_get_name_fn(event_type) : ""},
        {"argument_type", invoke ? main_thread_bridge::type_name(il2cpp_method_get_param_fn(invoke, 0)) : ""},
        {"return_type", invoke ? main_thread_bridge::type_name(il2cpp_method_get_return_type_fn(invoke)) : ""},
        {"add_available", add != nullptr}, {"remove_available", remove != nullptr}};
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
        const auto& resource = story_translation(id);
        const auto& translations = resource.rows;
        report["resource_found"] = resource.found;
        report["resource_rows"] = translations.size();
        const bool ordered_aligned = scenario_ordered_aligned(stamp, translations);
        report["match_mode"] = translations.ordered.empty() ? "explicit_id" :
            ordered_aligned ? "ordered_verified_layout" : "ordered_layout_mismatch";
        report["resource_skipped_rows"] = translations.skipped;
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
                auto* found = story_row_at(translations, key, i, ordered_aligned);
                if (!found) continue;
                ++matched;
                compare(rows[i], number, "content", found->content);
                compare(rows[i], number, "place", found->place);
                compare(rows[i], number, "teller", found->teller);
                compare(rows[i], number, "title", found->title);
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
            {"teller", observed_text(dialog, "teller")}, {"title", observed_text(dialog, "title")},
            {"model", observed_text(dialog, "Model")}};
        // Do not invoke GetStoryPlayData: that would generate a new value,
        // rather than observing the one the game actually kept.
        auto* play_field = managed_field_named(manager, "playData");
        auto* play_type = play_field ? il2cpp_class_from_type_fn(il2cpp_field_get_type_fn(play_field)) : nullptr;
        auto* play_data = observed_reference(manager, "playData", "StoryPlayData");
        report["play_data"] = {{"present", play_data != nullptr},
            {"declared_type", play_type ? il2cpp_class_get_name_fn(play_type) : ""},
            {"value_type", play_type && il2cpp_class_is_valuetype_fn(play_type)},
            {"text_fields", json::object()}};
        if (play_data) {
            for (auto* k = il2cpp_object_get_class_fn(play_data); k; k = il2cpp_class_get_parent_fn(k)) {
                void* field_iter = nullptr;
                while (auto* field = il2cpp_class_get_fields_fn(k, &field_iter)) {
                    if (il2cpp_field_get_flags_fn(field) & 0x10) continue;
                    auto* name = il2cpp_field_get_name_fn(field);
                    auto* type = il2cpp_class_from_type_fn(il2cpp_field_get_type_fn(field));
                    if (name && type && !strcmp(il2cpp_class_get_name_fn(type), "String"))
                        report["play_data"]["text_fields"][name] = observed_text(play_data, name);
                }
            }
        }
        report["display_fields"] = observed_tmp_fields(observed_reference(manager, "_dialogCon", "DialogController"));
        auto* mode_error = observed_refresh_mode(observed_reference(manager, "_dialogCon", "DialogController"));
        report["cache_refresh_mode"] = mode_error ? mode_error : "normal";
        report["cache_refresh_mode_scope"] = "normal_path_guard_not_visibility_or_whole_story_certification";
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
    if (!manager) return {};
    auto* scenario = observed_reference(manager, "_readingScenario", "Scenario");
    dialog = observed_reference(manager, "_dialog", "Dialog");
    auto id = read_named_managed_id(manager, "_storyID");
    bool is_reading = true;
    if (auto* field = managed_field_named(manager, "_isReading"))
        il2cpp_field_get_value_fn(manager, field, &is_reading);
    if (!is_reading && !live_scenario_dialog(manager, scenario, dialog, id)) return {};
    return {reinterpret_cast<uintptr_t>(manager), reinterpret_cast<uintptr_t>(scenario), reinterpret_cast<uintptr_t>(dialog),
            id, read_named_managed_id(dialog, "id")};
}

static const char* apply_refresh_on_main() {
    RefreshTicket ticket;
    { std::lock_guard<std::mutex> lock(refresh_mutex); ticket = refresh_ticket; }
    if (ticket.error) return ticket.error;
    if (std::chrono::steady_clock::now() > ticket.deadline) return "refresh_ticket_expired";
    Il2CppObject* manager = nullptr;
    Il2CppObject* dialog = nullptr;
    if (!(ticket.identity == live_identity(manager, dialog))) return "story_changed_since_request";
    if ((!ticket.expected.content.empty() && observed_text(dialog, "content") != ticket.expected.content) ||
        (!ticket.expected.place.empty() && observed_text(dialog, "place") != ticket.expected.place)) return "dialog_text_changed_since_request";
    auto* controller = observed_reference(manager, "_dialogCon", "DialogController");
    auto* animator = observed_reference(manager, "_uiAnimator", "StoryManagerUIAnimator");
    if (const char* error = observed_refresh_mode(controller)) return error;
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
    unsigned setters = 0;
    for (const auto& change : ticket.changes) {
        auto* target = observed_reference(change.animator ? animator : controller, change.field, "TextMeshProUGUI");
        if (observed_text(target, "m_text") == change.after) continue;
        auto* setter = find_method_in_hierarchy(il2cpp_object_get_class_fn(target), "set_text", 1);
        auto* text = il2cpp_string_new_fn(change.after.c_str());
        if (!text) return "text_allocation_failed";
        auto root = main_thread_bridge::new_handle(reinterpret_cast<Il2CppObject*>(text), false);
        if (!root) return "text_root_failed";
        void* args[] = {main_thread_bridge::get_target(root)};
        il2cpp_runtime_invoke_fn(setter, target, args, &exception);
        main_thread_bridge::free_handle(root);
        if (exception || observed_text(target, "m_text") != change.after) return "tmp_setter_readback_failed";
        ++setters;
        {
            std::lock_guard<std::mutex> lock(main_thread_bridge::state_mutex);
            if (main_thread_bridge::state.kind == "automatic_story_refresh")
                main_thread_bridge::state.action_context["fields_written"] = setters;
        }
    }
    LOGI("[LLC-STORY-REFRESH] story=%s dialog=%s setters=%u", ticket.identity.story_id.c_str(), ticket.identity.dialog_id.c_str(), setters);
    return nullptr;
}

static RefreshTicket prepare_story_refresh() {
    RefreshTicket ticket;
    ticket.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    Il2CppObject* manager = nullptr;
    Il2CppObject* dialog = nullptr;
    ticket.identity = live_identity(manager, dialog);
    if (!ticket.identity.valid() || !safe_story_id(ticket.identity.story_id)) ticket.error = "no_active_story";
    if (!ticket.error) {
        const auto& translations = story_translation(ticket.identity.story_id).rows;
        auto* found = scenario_dialogue_translation(
            observed_reference(manager, "_readingScenario", "Scenario"), dialog,
            translations, ticket.identity.dialog_id);
        if (!found) ticket.error = "current_dialog_resource_missing";
        else {
            const auto& text = *found;
            ticket.expected = text;
            if ((!text.content.empty() && observed_text(dialog, "content") != text.content) ||
                (!text.place.empty() && observed_text(dialog, "place") != text.place)) ticket.error = "dialog_data_not_localized";
            auto* controller = observed_reference(manager, "_dialogCon", "DialogController");
            auto* animator = observed_reference(manager, "_uiAnimator", "StoryManagerUIAnimator");
            auto add = [&](const char* name, bool upper, const std::string& translated) {
                if (translated.empty() || ticket.error) return;
                auto* component = observed_reference(upper ? animator : controller, name, "TextMeshProUGUI");
                auto old = observed_text(component, "m_text");
                if (!component || !old.is_string()) { ticket.error = "text_cache_unavailable"; return; }
                // Keep every target for execution-time validation, including
                // a no-op ticket whose caches already matched during preparation.
                ticket.changes.push_back({name, upper, reinterpret_cast<uintptr_t>(component), old.get<std::string>(), translated});
            };
            const auto fields = resolve_story_fields(text,
                read_named_managed_id(dialog, "Model"), read_named_managed_id(dialog, "Teller"),
                read_named_managed_id(dialog, "Title"), g_scenario_model_codes, g_ui_map);
            add("tmp_serif", false, fields.content);
            add("tmp_checkSerif", false, fields.content);
            add("tmp_name", false, fields.speaker);
            add("tmp_nickName", false, fields.title);
            add("tmp_mapText", true, fields.place);
        }
    }
    return ticket;
}

void request_story_refresh(const char* output_file, const char* request_id) {
    if (main_thread_bridge::busy()) return;
    auto ticket = prepare_story_refresh();
    { std::lock_guard<std::mutex> lock(refresh_mutex); refresh_ticket = std::move(ticket); }
    main_thread_bridge::request_probe(output_file, request_id, apply_refresh_on_main);
}

void check_and_refresh_active_story() {
    static StoryRefreshSchedule schedule;
    static std::string request;
    static uint64_t serial = 0;
    const auto now = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
    Il2CppObject* manager = nullptr;
    Il2CppObject* dialog = nullptr;
    schedule.observe(live_identity(manager, dialog));
    if (schedule.in_flight) {
        bool completed = false; const char* error = nullptr;
        {
            std::lock_guard<std::mutex> lock(main_thread_bridge::state_mutex);
            const auto& state = main_thread_bridge::state;
            completed = state.request == request && state.done;
            if (completed) error = state.error;
        }
        if (completed || schedule.expired(now)) {
            if (!completed) error = "automatic_refresh_timeout";
            schedule.record(error, now);
            LOGI("[LLC-STORY-AUTO] story=%s dialog=%s state=%s error=%s attempts=%u",
                schedule.identity.story_id.c_str(), schedule.identity.dialog_id.c_str(),
                schedule.settled ? "settled" : schedule.blocked ? "blocked" : "retry", error ? error : "none", schedule.attempts);
        }
    }
    // Preserve manual diagnostics' turn on the shared bridge, including a
    // completed result which has not yet been published by the worker.
    if (!schedule.ready(now, main_thread_bridge::busy()) ||
        access("/data/user/0/com.ProjectMoon.LimbusCompany/files/llc_story_refresh", F_OK) == 0 ||
        access("/data/user/0/com.ProjectMoon.LimbusCompany/files/llc_main_probe", F_OK) == 0 ||
        access("/data/user/0/com.ProjectMoon.LimbusCompany/files/llc_content_check", F_OK) == 0) return;
    auto ticket = prepare_story_refresh();
    if (!(ticket.identity == schedule.identity)) { schedule.record("story_changed_since_request", now); return; }
    if (ticket.error) { schedule.record(ticket.error, now); return; }
    char token[48]; snprintf(token, sizeof(token), "%016llx%016llx", static_cast<unsigned long long>(now), static_cast<unsigned long long>(++serial));
    request = token;
    size_t requested_changes = 0;
    for (const auto& target : ticket.changes) requested_changes += target.before != target.after;
    nlohmann::json context = {{"story_id", ticket.identity.story_id}, {"dialog_id", ticket.identity.dialog_id},
        {"manager", ticket.identity.manager}, {"scenario", ticket.identity.scenario}, {"dialog", ticket.identity.dialog},
        {"requested_changes", requested_changes}, {"fields_written", 0}};
    { std::lock_guard<std::mutex> lock(refresh_mutex); refresh_ticket = std::move(ticket); }
    schedule.queued(now);
    main_thread_bridge::request_probe("/sdcard/Android/data/com.ProjectMoon.LimbusCompany/files/llc_story_auto_refresh.json",
        request.c_str(), apply_refresh_on_main, "automatic_story_refresh", context);
}
