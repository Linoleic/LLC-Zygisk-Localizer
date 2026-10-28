#include "il2cpp_api.hpp"
#include "storage.hpp"
#include "json_loader.hpp"
#include "font_injector.hpp"
#include "data_patcher.hpp"
#include "content_sync.hpp"
#include "managed_fields.hpp"
#include "story_hook.hpp"
#include <unistd.h>
#include <pthread.h>
#include <cstdio>
#include <cstring>
#include <sys/system_properties.h>

static Il2CppClass* g_singleton_tds_class = nullptr;
static void* g_singleton_tds_inst_field = nullptr;

static Il2CppObject* get_live_text_dataset() {
    if (g_singleton_tds_inst_field) {
        Il2CppObject* live_obj = nullptr;
        il2cpp_field_static_get_value_fn(g_singleton_tds_inst_field, &live_obj);
        return live_obj;
    }

    if (il2cpp_class_for_each_fn) {
        struct SearchCtx {
            Il2CppClass* s_class;
            void* f_inst;
            Il2CppObject* obj;
        } ctx = { nullptr, nullptr, nullptr };

        il2cpp_class_for_each_fn([](Il2CppClass* k, void* userData) {
            SearchCtx* c = (SearchCtx*)userData;
            if (c->obj) return;
            if (!k) return;
            const char* name = il2cpp_class_get_name_fn(k);
            if (name && strcmp(name, "Singleton`1") == 0) {
                void* inst_f = il2cpp_class_get_field_from_name_fn(k, "_instance");
                if (inst_f) {
                    Il2CppObject* live_obj = nullptr;
                    il2cpp_field_static_get_value_fn(inst_f, &live_obj);
                    if (live_obj) {
                        Il2CppClass* obj_klass = il2cpp_object_get_class_fn(live_obj);
                        const char* obj_name = il2cpp_class_get_name_fn(obj_klass);
                        if (obj_name && strcmp(obj_name, "TextDataSet") == 0) {
                            c->s_class = k;
                            c->f_inst = inst_f;
                            c->obj = live_obj;
                        }
                    }
                }
            }
        }, &ctx);

        if (ctx.obj) {
            g_singleton_tds_class = ctx.s_class;
            g_singleton_tds_inst_field = ctx.f_inst;
            LOGI("[LLC-CORE] Cached Singleton<TextDataSet> class: %p, field: %p",
                 g_singleton_tds_class, g_singleton_tds_inst_field);
            return ctx.obj;
        }
    }
    return nullptr;
}

struct ContentTarget {
    const char* name;
    size_t offset;
    int (*patch)(Il2CppObject*);
    ContentSyncState state;
};

static ContentTarget g_content_targets[] = {
    {"_personalityList", 64, patch_personality_list, {}},
    {"_passiveList", 96, patch_passive_list, {}},
    {"_bufList", 104, [](Il2CppObject* o) { return patch_buf_list(o, "_bufList"); }, {}},
    {"_keywordList", 48, [](Il2CppObject* o) { return patch_buf_list(o, "_keywordList"); }, {}},
    {"_egoList", 80, patch_ego_list, {}},
    {"_characterList", 40, patch_character_list, {}},
    {"_skillList", 88, patch_skill_list, {}},
    {"_personalityVoiceText", 280, [](Il2CppObject* o) { return patch_voice_list(o, "_personalityVoiceText"); }, {}},
    {"_announcerVoiceText", 288, [](Il2CppObject* o) { return patch_voice_list(o, "_announcerVoiceText"); }, {}},
    {"_egoVoiceText", 296, [](Il2CppObject* o) { return patch_voice_list(o, "_egoVoiceText"); }, {}},
    {"_bgmLyricsText", 304, [](Il2CppObject* o) { return patch_voice_list(o, "_bgmLyricsText"); }, {}},
    {"_storyTheaterList", 264, patch_theater_list, {}},
    {"_userBannerList", 416, patch_banner_list, {}},
    {"_stageChapterList", 192, patch_stage_chapter_list, {}},
    {"_stageNodeText", 208, [](Il2CppObject* o) { return patch_stage_node_list(o, "_stageNodeText"); }, {}},
    {"_storyDungeonNodeText", 224, [](Il2CppObject* o) { return patch_stage_node_list(o, "_storyDungeonNodeText"); }, {}},
    {"_railwayDungeonNodeText", 232, [](Il2CppObject* o) { return patch_stage_node_list(o, "_railwayDungeonNodeText"); }, {}}
};

static ContentStamp content_stamp(Il2CppObject* list) {
    ContentStamp result;
    result.owner = reinterpret_cast<uintptr_t>(list);
    if (!list) return result;
    Il2CppObject* dictionary = *(Il2CppObject**)((char*)list + 16);
    if (!dictionary) return result;
    Il2CppClass* klass = il2cpp_object_get_class_fn(dictionary);
    const char* name = il2cpp_class_get_name_fn(klass);
    if (!name || strcmp(name, "Dictionary`2") != 0) return result;
    result.dictionary = reinterpret_cast<uintptr_t>(dictionary);
    Il2CppObject* entries = *(Il2CppObject**)((char*)dictionary + 24);
    result.entries = reinterpret_cast<uintptr_t>(entries);
    result.count = *(int*)((char*)dictionary + 32);
    if (result.count < 0 || (entries && static_cast<uintptr_t>(result.count) > il2cpp_array_length_fn(entries))) {
        result.count = 0;
        result.entries = 0;
        return result;
    }
    void* version = il2cpp_class_get_field_from_name_fn(klass, "_version");
    if (!version) version = il2cpp_class_get_field_from_name_fn(klass, "version");
    if (version) il2cpp_field_get_value_fn(dictionary, version, &result.version);
    return result;
}

static void synchronize_content_tables(Il2CppObject* tds, uint64_t tick) {
    for (auto& target : g_content_targets) {
        Il2CppObject* list = *(Il2CppObject**)((char*)tds + target.offset);
        ContentStamp before = content_stamp(list);
        if (!target.state.observe(before, tick)) continue;
        uint64_t writes_before = g_managed_text_writes;
        int matched = target.patch(list);
        uint64_t written = g_managed_text_writes - writes_before;
        ContentStamp after = content_stamp(list);
        if (!(before == after)) {
            target.state.observe(after, tick);
            LOGI("[LLC-SYNC] %s changed during patch; scheduling another pass", target.name);
            continue;
        }
        target.state.record(matched, written, tick);
        if (target.state.applied || target.state.attempts == 1 || target.state.attempts % 16 == 0) {
            LOGI("[LLC-SYNC] %s: runtime_slots=%d matched_rows=%d strings_written=%llu state=%s",
                 target.name, after.count, matched, (unsigned long long)written,
                 target.state.applied ? "applied-to-snapshot" : "retry-pending");
        }
    }
}

static void print_audit_report(Il2CppObject* live_tds) {
    if (!live_tds) return;
    LOGI("[LLC-AUDIT] Localization table snapshots (matches and writes are from the last attempt)");
    for (const auto& target : g_content_targets) {
        Il2CppObject* list = *(Il2CppObject**)((char*)live_tds + target.offset);
        ContentStamp current = content_stamp(list);
        bool current_applied = target.state.applied && current == target.state.observed;
        LOGI("[LLC-AUDIT] %s: runtime_slots=%d matched_rows=%d strings_written=%llu attempts=%u state=%s",
             target.name, current.count, current_applied ? target.state.matched : 0,
             current_applied ? (unsigned long long)target.state.strings_written : 0ULL,
             target.state.attempts, current_applied ? "applied-to-snapshot" : "pending");
    }
    LOGI("[LLC-AUDIT] Font assets recorded=%d; story watcher=%s (not a translation coverage result)",
         get_patched_font_count(), is_story_hook_installed() ? "initialized" : "not-ready");
}

static void* pure_native_engine_thread(void*) {
    LOGI("[LLC-CORE] Pure Native Engine Thread active. Loading local UI dictionaries...");
    const char* base_lang_dir = "/data/user/0/com.ProjectMoon.LimbusCompany/files/Lang/LLC_zh-CN";
    load_all_ui_jsons(base_lang_dir);
    load_all_content_jsons(base_lang_dir);

    LOGI("[LLC-CORE] Awaiting Unity engine initialization (sleep 6s)...");
    sleep(6);

    LOGI("[LLC-CORE] Polling for Il2CppDomain and loaded assemblies...");
    Il2CppDomain* domain = nullptr;
    for (int retry = 0; retry < 120; ++retry) {
        domain = il2cpp_domain_get_fn ? il2cpp_domain_get_fn() : nullptr;
        if (domain && il2cpp_domain_get_assemblies_fn) {
            size_t asm_count = 0;
            const Il2CppAssembly** asms = il2cpp_domain_get_assemblies_fn(domain, &asm_count);
            if (asms && asm_count > 5) {
                LOGI("[LLC-CORE] Il2Cpp fully initialized with %zu assemblies!", asm_count);
                break;
            }
        }
        usleep(250000); // 250ms
    }

    if (!domain) {
        LOGE("[LLC-CORE] Timeout waiting for Il2CppDomain!");
        return nullptr;
    }

    il2cpp_thread_attach_fn(domain);
    LOGI("[LLC-CORE] Successfully attached to Il2CppDomain: %p", domain);

    // Load Chinese font AssetBundle
    const char* font_bundle_path = "/data/user/0/com.ProjectMoon.LimbusCompany/files/tmpchinesefont";
    if (load_chinese_font_bundle(font_bundle_path)) {
        LOGI("[LLC-CORE] Font bundle loaded successfully.");
    } else {
        LOGW("[LLC-CORE] Font bundle async loading incomplete or font not found yet.");
    }

    // Ensure Chinese font material and shader are valid before injection
    ensure_font_material_valid();

    // Initial font injection into all loaded fonts
    if (g_shs_font) {
        LOGI("[LLC-CORE] Injecting Chinese Font fallback into all loaded fonts in memory...");
        inject_fallback_to_all_loaded_fonts();
        LOGI("[LLC-CORE] Initial font injection complete.");
    }

    LOGI("[LLC-CORE] Entering TextDataSet synchronization watcher...");
    bool login_patched = false;
    bool ui_list_patched = false;
    bool remote_patched = false;
    Il2CppObject* last_tds = nullptr;
    bool last_remote = false;
    bool shader_bound = false;
    uint64_t loop = 0;

    while (true) {
        loop++;

        if (!shader_bound) {
            if (ensure_font_material_valid()) {
                shader_bound = true;
                LOGI("[LLC-CORE] Font material and Android Vulkan shader successfully configured.");
                inject_fallback_to_all_loaded_fonts();
                if (login_patched) {
                    refresh_title_screen_loaders();
                    refresh_active_ui_texts();
                }
            }
        }

        Il2CppObject* live_tds = get_live_text_dataset();

        if (live_tds) {
            bool remote_now = *(bool*)((char*)live_tds + 656);
            if (live_tds != last_tds || (last_remote && !remote_now)) {
                for (auto& target : g_content_targets) target.state = ContentSyncState{};
                login_patched = false;
                ui_list_patched = false;
                remote_patched = false;
                last_tds = live_tds;
                LOGI("[LLC-SYNC] TextDataSet lifecycle changed; table states reset");
            }
            last_remote = remote_now;
            init_story_hook();

            Il2CppObject* login_ui_list = *(Il2CppObject**)((char*)live_tds + 616);
            Il2CppObject* ui_list = *(Il2CppObject**)((char*)live_tds + 16);
            bool is_remote = *(bool*)((char*)live_tds + 656);

            if (!login_patched && login_ui_list) {
                int p = patch_ui_list(login_ui_list, "_loginUIList");
                if (p > 0) {
                    login_patched = true;
                    LOGI("[LLC-CORE] _loginUIList successfully patched with %d entries!", p);
                    if (shader_bound) {
                        refresh_title_screen_loaders();
                        refresh_active_ui_texts();
                    }
                }
            }

            // Periodically refresh title screen buttons while waiting at login / title scene
            if (!remote_patched && (loop % 4 == 0) && shader_bound) {
                refresh_title_screen_loaders();
                refresh_active_ui_texts();
            }

            if (!ui_list_patched && ui_list) {
                int p = patch_ui_list(ui_list, "_uiList");
                if (p > 0) {
                    ui_list_patched = true;
                    LOGI("[LLC-CORE] _uiList initial synchronized with %d entries!", p);
                }
            }

            if (!remote_patched && ui_list && is_remote) {
                int p = patch_ui_list(ui_list, "_uiList (remote)");
                if (p > 0) {
                    remote_patched = true;
                    LOGI("[LLC-CORE] Remote localization loaded! _uiList synchronized with %d entries.", p);
                }
            }

            if (remote_patched) synchronize_content_tables(live_tds, loop);
        }

        // =========================================================================
        // Maintenance and on-demand diagnostics
        // =========================================================================
        // Poll every 250 ms; each content table tracks its own generation.
        usleep(250000);

        // Dynamic Story Localization Interception (100% data-driven)
        check_and_patch_active_story();

        static uint32_t s_idle_ticks = 0;
        s_idle_ticks++;
        if (s_idle_ticks % 4 == 0) { // Check triggers every 1 second
            static std::string s_last_dump_prop = []{
                char b[PROP_VALUE_MAX] = {0};
                __system_property_get("debug.llc.dump", b);
                return std::string(b[0] ? b : "0");
            }();
            static std::string s_last_font_prop = []{
                char b[PROP_VALUE_MAX] = {0};
                __system_property_get("debug.llc.font", b);
                return std::string(b[0] ? b : "0");
            }();
            static std::string s_last_audit_prop = []{
                char b[PROP_VALUE_MAX] = {0};
                __system_property_get("debug.llc.audit", b);
                return std::string(b[0] ? b : "0");
            }();
            static std::string s_last_story_prop = []{
                char b[PROP_VALUE_MAX] = {0};
                __system_property_get("debug.llc.story", b);
                return std::string(b[0] ? b : "0");
            }();

            // 1. Text Dump Trigger
            char dump_prop[PROP_VALUE_MAX] = {0};
            __system_property_get("debug.llc.dump", dump_prop);
            const char* f_trig1 = "/data/data/com.ProjectMoon.LimbusCompany/files/llc_dump";
            const char* f_trig2 = "/sdcard/Android/data/com.ProjectMoon.LimbusCompany/files/llc_dump";
            bool dump_file_active = (access(f_trig1, F_OK) == 0) || (access(f_trig2, F_OK) == 0);
            if (dump_file_active || (dump_prop[0] && strcmp(dump_prop, "0") != 0 && dump_prop != s_last_dump_prop)) {
                s_last_dump_prop = dump_prop;
                LOGI("[LLC-CORE] >>> TMP_Text Dump Trigger Activated! <<<");
                const char* out_p = "/sdcard/Android/data/com.ProjectMoon.LimbusCompany/files/llc_text_dump.txt";
                dump_active_tmp_texts(out_p);
                unlink(f_trig1);
                unlink(f_trig2);
            }
            if (strcmp(dump_prop, "0") == 0) s_last_dump_prop = "0";

            // 2. Font Status Dump Trigger
            char font_prop[PROP_VALUE_MAX] = {0};
            __system_property_get("debug.llc.font", font_prop);
            const char* font_trig1 = "/data/data/com.ProjectMoon.LimbusCompany/files/llc_font";
            const char* font_trig2 = "/sdcard/Android/data/com.ProjectMoon.LimbusCompany/files/llc_font";
            bool font_file_active = (access(font_trig1, F_OK) == 0) || (access(font_trig2, F_OK) == 0);
            if (font_file_active || (font_prop[0] && strcmp(font_prop, "0") != 0 && font_prop != s_last_font_prop)) {
                s_last_font_prop = font_prop;
                LOGI("[LLC-CORE] >>> Font Status Dump Trigger Activated! <<<");
                const char* out_f = "/sdcard/Android/data/com.ProjectMoon.LimbusCompany/files/llc_font_dump.txt";
                dump_all_fonts_status(out_f);
                unlink(font_trig1);
                unlink(font_trig2);
            }
            if (strcmp(font_prop, "0") == 0) s_last_font_prop = "0";

            // 3. Audit Re-print Trigger
            char audit_prop[PROP_VALUE_MAX] = {0};
            __system_property_get("debug.llc.audit", audit_prop);
            const char* audit_trig1 = "/data/data/com.ProjectMoon.LimbusCompany/files/llc_audit";
            const char* audit_trig2 = "/sdcard/Android/data/com.ProjectMoon.LimbusCompany/files/llc_audit";
            bool audit_file_active = (access(audit_trig1, F_OK) == 0) || (access(audit_trig2, F_OK) == 0);
            if (audit_file_active || (audit_prop[0] && strcmp(audit_prop, "0") != 0 && audit_prop != s_last_audit_prop)) {
                s_last_audit_prop = audit_prop;
                LOGI("[LLC-CORE] >>> Audit Report Trigger Activated! <<<");
                print_audit_report(get_live_text_dataset());
                unlink(audit_trig1);
                unlink(audit_trig2);
            }
            if (strcmp(audit_prop, "0") == 0) s_last_audit_prop = "0";

            // 4. Story System Reflection Inspect Trigger
            char story_prop[PROP_VALUE_MAX] = {0};
            __system_property_get("debug.llc.story", story_prop);
            const char* story_trig1 = "/data/data/com.ProjectMoon.LimbusCompany/files/llc_story";
            const char* story_trig2 = "/sdcard/Android/data/com.ProjectMoon.LimbusCompany/files/llc_story";
            bool story_file_active = (access(story_trig1, F_OK) == 0) || (access(story_trig2, F_OK) == 0);
            if (story_file_active || (story_prop[0] && strcmp(story_prop, "0") != 0 && story_prop != s_last_story_prop)) {
                s_last_story_prop = story_prop;
                LOGI("[LLC-CORE] >>> Story Reflection Inspect Trigger Activated! <<<");
                const char* out_s = "/sdcard/Android/data/com.ProjectMoon.LimbusCompany/files/llc_story_inspect.txt";
                inspect_story_system(out_s);
                unlink(story_trig1);
                unlink(story_trig2);
            }
            if (strcmp(story_prop, "0") == 0) s_last_story_prop = "0";
        }
    }

    return nullptr;
}

extern "C" __attribute__((visibility("default"))) void llc_core_init() {
    LOGI("[LLC-CORE] Stage 2 Core Initializing (100%% Data-Only Modular Architecture)...");
    if (!init_il2cpp_api()) return;

    pthread_t tid;
    if (pthread_create(&tid, nullptr, pure_native_engine_thread, nullptr) == 0) {
        pthread_detach(tid);
    }
}
