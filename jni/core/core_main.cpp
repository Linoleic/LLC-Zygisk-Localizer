#include "il2cpp_api.hpp"
#include "storage.hpp"
#include "json_loader.hpp"
#include "font_injector.hpp"
#include "data_patcher.hpp"
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

static void print_audit_report(Il2CppObject* live_tds) {
    if (!live_tds) return;

    Il2CppObject* pers_list = *(Il2CppObject**)((char*)live_tds + 64);
    Il2CppObject* skill_list = *(Il2CppObject**)((char*)live_tds + 88);
    Il2CppObject* passive_list = *(Il2CppObject**)((char*)live_tds + 96);
    Il2CppObject* buf_list = *(Il2CppObject**)((char*)live_tds + 104);
    Il2CppObject* ego_list = *(Il2CppObject**)((char*)live_tds + 80);
    Il2CppObject* keyword_list = *(Il2CppObject**)((char*)live_tds + 48);
    Il2CppObject* char_list = *(Il2CppObject**)((char*)live_tds + 40);
    Il2CppObject* theater_list = *(Il2CppObject**)((char*)live_tds + 264);
    Il2CppObject* banner_list = *(Il2CppObject**)((char*)live_tds + 416);
    Il2CppObject* chapter_list = *(Il2CppObject**)((char*)live_tds + 192);
    Il2CppObject* node_list = *(Il2CppObject**)((char*)live_tds + 208);

    auto get_cnt = [](Il2CppObject* jdl) -> int {
        if (!jdl) return 0;
        Il2CppObject* d = *(Il2CppObject**)((char*)jdl + 16);
        if (!d) return 0;
        return *(int*)((char*)d + 32);
    };

    int pc = get_cnt(pers_list);
    int sc = get_cnt(skill_list);
    int pasc = get_cnt(passive_list);
    int bc = get_cnt(buf_list);
    int ec = get_cnt(ego_list);
    int kc = get_cnt(keyword_list);
    int cc = get_cnt(char_list);
    int tc = get_cnt(theater_list);
    int bnc = get_cnt(banner_list);
    int chc = get_cnt(chapter_list);
    int nc = get_cnt(node_list);

    LOGI("[LLC-AUDIT] ================================================================================");
    LOGI("[LLC-AUDIT]                    LIMBUS LOCALIZATION RUNTIME AUDIT REPORT                     ");
    LOGI("[LLC-AUDIT] ================================================================================");
    LOGI("[LLC-AUDIT]   * Personalities (人格)   : %4d entries loaded & patched", pc);
    LOGI("[LLC-AUDIT]   * Skills        (技能)   : %4d entries loaded & patched", sc);
    LOGI("[LLC-AUDIT]   * Passives      (被动)   : %4d entries loaded & patched", pasc);
    LOGI("[LLC-AUDIT]   * Buffs         (状态)   : %4d entries loaded & patched", bc);
    LOGI("[LLC-AUDIT]   * Keywords      (关键词) : %4d entries loaded & patched", kc);
    LOGI("[LLC-AUDIT]   * EGOs          (E.G.O)  : %4d entries loaded & patched", ec);
    LOGI("[LLC-AUDIT]   * Characters    (罪人)   : %4d entries loaded & patched", cc);
    LOGI("[LLC-AUDIT]   * Theater       (放映室) : %4d entries loaded & patched", tc);
    LOGI("[LLC-AUDIT]   * Banners       (横幅)   : %4d entries loaded & patched", bnc);
    LOGI("[LLC-AUDIT]   * Chapters      (章节)   : %4d entries loaded & patched", chc);
    LOGI("[LLC-AUDIT]   * Stage Nodes   (关卡)   : %4d entries loaded & patched", nc);
    LOGI("[LLC-AUDIT]   * Font Fallback (字体)   : %4d TMP_FontAssets linked to SourceHanSans", get_patched_font_count());
    LOGI("[LLC-AUDIT]   * Story Hook    (剧情)   : %s", is_story_hook_installed() ? "ACTIVE (A64 Hooked)" : "NOT READY");
    LOGI("[LLC-AUDIT] --------------------------------------------------------------------------------");
    LOGI("[LLC-AUDIT]   DIAGNOSTIC TRIGGERS ACTIVE:");
    LOGI("[LLC-AUDIT]     adb shell setprop debug.llc.dump 1   -> Dump active TMP_Text on screen");
    LOGI("[LLC-AUDIT]     adb shell setprop debug.llc.font 1   -> Dump all TMP_FontAsset status");
    LOGI("[LLC-AUDIT]     adb shell setprop debug.llc.audit 1  -> Re-print this audit report");
    LOGI("[LLC-AUDIT]   STATUS: ALL MODULES SYNCHRONIZED | STABLE DATA-DRIVEN ARCHITECTURE ACTIVE");
    LOGI("[LLC-AUDIT] ================================================================================");
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
    bool all_content_patched = false;
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

            auto get_cnt = [](Il2CppObject* jdl) -> int {
                if (!jdl) return 0;
                Il2CppObject* d = *(Il2CppObject**)((char*)jdl + 16);
                if (!d) return 0;
                return *(int*)((char*)d + 32);
            };

            Il2CppObject* pers_list = *(Il2CppObject**)((char*)live_tds + 64);
            Il2CppObject* skill_list = *(Il2CppObject**)((char*)live_tds + 88);
            Il2CppObject* char_list = *(Il2CppObject**)((char*)live_tds + 40);
            int pc = get_cnt(pers_list);
            int sc = get_cnt(skill_list);
            int cc = get_cnt(char_list);

            if (loop % 16 == 0 && remote_patched && !all_content_patched) {
                LOGI("[LLC-CORE] Data loading progress: personalities=%d, skills=%d, characters=%d", pc, sc, cc);
            }

            // Once remote is ready AND populated, execute full one-time synchronization and print audit
            if (remote_patched && !all_content_patched && (pc > 0 || sc > 0 || cc > 0)) {
                Il2CppObject* passive_list = *(Il2CppObject**)((char*)live_tds + 96);
                Il2CppObject* buf_list = *(Il2CppObject**)((char*)live_tds + 104);
                Il2CppObject* ego_list = *(Il2CppObject**)((char*)live_tds + 80);
                Il2CppObject* keyword_list = *(Il2CppObject**)((char*)live_tds + 48);

                Il2CppObject* pv_list = *(Il2CppObject**)((char*)live_tds + 280);
                Il2CppObject* av_list = *(Il2CppObject**)((char*)live_tds + 288);
                Il2CppObject* ev_list = *(Il2CppObject**)((char*)live_tds + 296);
                Il2CppObject* bgm_list = *(Il2CppObject**)((char*)live_tds + 304);

                Il2CppObject* theater_list = *(Il2CppObject**)((char*)live_tds + 264);
                Il2CppObject* banner_list = *(Il2CppObject**)((char*)live_tds + 416);

                Il2CppObject* chapter_list = *(Il2CppObject**)((char*)live_tds + 192);
                Il2CppObject* node_list = *(Il2CppObject**)((char*)live_tds + 208);
                Il2CppObject* story_dungeon_node = *(Il2CppObject**)((char*)live_tds + 224);
                Il2CppObject* railway_dungeon_node = *(Il2CppObject**)((char*)live_tds + 232);

                // Run font fallback injection once more to ensure any newly loaded font assets are caught
                inject_fallback_to_all_loaded_fonts();

                // Apply all patches
                patch_personality_list(pers_list);
                patch_passive_list(passive_list);
                patch_buf_list(buf_list, "_bufList");
                patch_buf_list(keyword_list, "_keywordList");
                patch_ego_list(ego_list);
                patch_character_list(char_list);
                patch_skill_list(skill_list);

                patch_voice_list(pv_list, "_personalityVoiceText");
                patch_voice_list(av_list, "_announcerVoiceText");
                patch_voice_list(ev_list, "_egoVoiceText");
                patch_voice_list(bgm_list, "_bgmLyricsText");

                patch_theater_list(theater_list);
                patch_banner_list(banner_list);
                patch_stage_chapter_list(chapter_list);

                patch_stage_node_list(node_list, "_stageNodeText");
                patch_stage_node_list(story_dungeon_node, "_storyDungeonNodeText");
                patch_stage_node_list(railway_dungeon_node, "_railwayDungeonNodeText");

                // Print the comprehensive audit report
                print_audit_report(live_tds);

                all_content_patched = true;
                LOGI("[LLC-CORE] Full localization patch complete! Switching to idle low-frequency watcher.");
            }
        }

        // =========================================================================
        // Maintenance & On-Demand Diagnostics (Zero Overhead During Normal Play)
        // =========================================================================
        // Check every 250ms (zero CPU overhead)
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
