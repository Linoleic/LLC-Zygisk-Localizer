#include "il2cpp_api.hpp"
#include "story_hook.hpp"
#include "managed_fields.hpp"
#include "data_patcher.hpp"
#include <android/log.h>
#include <string>
#include <cstring>

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
