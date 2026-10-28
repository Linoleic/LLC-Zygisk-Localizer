#include "il2cpp_api.hpp"
#include "story_hook.hpp"
#include "And64InlineHook.hpp"
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
    LOGI("[LLC-STORY] Story system watcher initialized (100%% dynamic data-driven)");
}

void check_and_patch_active_story() {
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
    if (!ggm_inst) return;

    // Field: _story at offset 216
    Il2CppObject* story_progress = *(Il2CppObject**)((char*)ggm_inst + 216);
    if (!story_progress) return;

    // In StoryProgressManager: _activeStoryManager at offset 56
    Il2CppObject* active_sm = *(Il2CppObject**)((char*)story_progress + 56);
    if (!active_sm) return;

    // In StoryManager:
    // _readingScenario at offset 608
    // _storyID at offset 672
    // _isReading at offset 737
    Il2CppObject* reading_scn = *(Il2CppObject**)((char*)active_sm + 608);

    static Il2CppObject* s_last_patched_scn = nullptr;
    if (!reading_scn) {
        s_last_patched_scn = nullptr;
        return;
    }

    if (reading_scn == s_last_patched_scn) {
        return; // Already patched this scenario instance
    }

    // Extract story ID if available
    char scen_id[128] = {0};
    Il2CppString* id_str = *(Il2CppString**)((char*)active_sm + 672);
    if (id_str && (uintptr_t)id_str > 0x10000 && il2cpp_string_chars_fn) {
        const uint16_t* u = il2cpp_string_chars_fn(id_str);
        if (u) {
            for (int x = 0; x < 127 && u[x]; ++x) scen_id[x] = (char)u[x];
        }
    }

    LOGI("[LLC-STORY] Detected active story scenario: %s (scn=%p). Patching dialogues...",
         scen_id[0] ? scen_id : "from_scenario", reading_scn);
    patch_scenario_object(reading_scn, scen_id[0] ? scen_id : nullptr);
    s_last_patched_scn = reading_scn;
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

    if (fp) {
        fprintf(fp, "\n================================================================================\n");
        fclose(fp);
    }
    LOGI("[LLC-STORY] Architecture inspection written to %s", output_file ? output_file : "logcat");
}
