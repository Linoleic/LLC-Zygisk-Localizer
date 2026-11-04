#include "il2cpp_api.hpp"
#include "storage.hpp"
#include "json_loader.hpp"
#include "font_injector.hpp"
#include "data_patcher.hpp"
#include "content_sync.hpp"
#include "string_table_patcher.hpp"
#include "auto_registry.hpp"
#include "managed_fields.hpp"
#include "event_dialogue.hpp"
#include "main_thread_bridge.hpp"
#include "content_readback.hpp"
#include "runtime_compatibility.hpp"
#include "story_hook.hpp"
#include "resource_paths.hpp"
#include "localization_status.hpp"
#include "diagnostic_log.hpp"
#include "font_config.hpp"
#include "language_config.hpp"
#include <unistd.h>
#include <pthread.h>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <sys/system_properties.h>
#include <atomic>

static std::atomic<bool> g_engine_running{true};
static void* g_attached_il2cpp_thread = nullptr;

static bool s_last_localization_active = true;
static int s_last_game_lang = -999;
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

static int query_game_language() {
    int lang = language_config::query_language_from_playerprefs();
    if (lang >= 0) {
        return lang;
    }

    // Secondary fallback: query StoryManager._curLang in live memory safely
    Il2CppObject* inst = observed_game();
    if (inst) {
        Il2CppObject* story_progress = observed_reference(inst, "_story", "StoryProgressManager");
        if (story_progress) {
            Il2CppObject* active_sm = observed_reference(story_progress, "_activeStoryManager", "StoryManager");
            if (active_sm) {
                void* lang_f = managed_field_named(active_sm, "_curLang");
                int mem_lang = -1;
                if (lang_f) {
                    il2cpp_field_get_value_fn(active_sm, lang_f, &mem_lang);
                    if (mem_lang >= 0 && mem_lang <= 2) {
                        return mem_lang;
                    }
                }
            }
        }
    }
    return -1;
}

static std::string g_content_check_request, g_content_check_table;
static const char* inspect_content_on_main() {
    auto report = content_readback::inspect(get_live_text_dataset(), g_content_check_table);
    report.update({{"schema", 1}, {"kind", "content_readback"}, {"request_id", g_content_check_request},
        {"pid", getpid()}, {"process_start_ticks", g_runtime_compatibility_report.value("process_start_ticks", std::string{})},
        {"native_callback_tid", gettid()}});
    const char* output = "/sdcard/Android/data/com.ProjectMoon.LimbusCompany/files/llc_content_state.json";
    std::string next = std::string(output) + ".next";
    auto bytes = report.dump(2);
    FILE* file = fopen(next.c_str(), "wb"); if (!file) return "content_report_open_failed";
    bool ok = fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size();
    if (fclose(file)) ok = false;
    if (!ok || rename(next.c_str(), output)) { unlink(next.c_str()); return "content_report_write_failed"; }
    return report["status"] == "passed" ? nullptr : "content_readback_failed";
}

struct ContentTarget {
    const char* name;
    size_t offset;
    int (*patch)(Il2CppObject*);
    ContentSyncState state;
    bool requires_remote = true;
    bool reflected_container = false;
};

static ContentTarget g_content_targets[] = {
    AUTO_TARGET_NO_REMOTE("_skillTagList", patch_skill_tag_list),
    AUTO_TARGET_NO_REMOTE("_battleHint", patch_battle_hint_list),
    AUTO_TARGET("_personalityList", patch_personality_list),
    AUTO_TARGET("_passiveList", patch_passive_list),
    AUTO_TARGET("_bufList", [](Il2CppObject* o) { return patch_buf_list(o, "_bufList"); }),
    AUTO_TARGET("_keywordList", [](Il2CppObject* o) { return patch_buf_list(o, "_keywordList"); }),
    AUTO_TARGET("_egoList", patch_ego_list),
    AUTO_TARGET("_characterList", patch_character_list),
    AUTO_TARGET("_skillList", patch_skill_list),
    AUTO_TARGET("_egoGiftData", patch_ego_gift_list),
    AUTO_TARGET("_danteNoteDesc", patch_dante_note_list),
    AUTO_TARGET("_danteNoteCategoryKeyword", patch_dante_note_category_list),
    AUTO_TARGET("_mirrorDungeonEgoGiftLockedDescList", patch_ego_gift_locked_list),
    AUTO_TARGET("_mirrorDungeonThemeList", patch_mirror_dungeon_theme_list),
    AUTO_TARGET("_egoGiftCategory", patch_ego_gift_category_list),
    AUTO_TARGET("_announcer", patch_announcer_list),
    AUTO_TARGET("_announcerVoiceType", patch_announcer_voice_type_list),
    AUTO_TARGET_LEGACY("_personalityVoiceText", 280, [](Il2CppObject* o) { return patch_voice_list(o, "_personalityVoiceText"); }, true, false),
    AUTO_TARGET_LEGACY("_announcerVoiceText", 288, [](Il2CppObject* o) { return patch_voice_list(o, "_announcerVoiceText"); }, true, false),
    AUTO_TARGET_LEGACY("_egoVoiceText", 296, [](Il2CppObject* o) { return patch_voice_list(o, "_egoVoiceText"); }, true, false),
    AUTO_TARGET_LEGACY("_bgmLyricsText", 304, [](Il2CppObject* o) { return patch_scoped_list(o, true); }, true, false),
    AUTO_TARGET("_storyTheaterList", patch_theater_list),
    AUTO_TARGET("_userBannerList", patch_banner_list),
    AUTO_TARGET("_userTicket_L", patch_user_ticket_l),
    AUTO_TARGET("_userTicket_R", patch_user_ticket_r),
    AUTO_TARGET("_userTicket_EGOBg", patch_user_ticket_egobg),
    AUTO_TARGET("_threadDungeon", patch_thread_dungeon_list),
    AUTO_TARGET("_dungeonName", patch_dungeon_name_list),
    AUTO_TARGET("_attributeList", patch_attribute_list),
    AUTO_TARGET("_dungeonStartBuffs", patch_dungeon_start_buffs),
    AUTO_TARGET("_panicInfo", patch_panic_info_list),
    AUTO_TARGET("_mentalConditionList", patch_mental_condition_list),
    AUTO_TARGET("_danteAbilityDataList", patch_dante_ability_list),
    AUTO_TARGET("_unlockCodeList", patch_unlock_code_list),
    AUTO_TARGET("_projectGSLessonName", patch_project_gs_lesson_list),
    AUTO_TARGET("_projectGSComboName", patch_project_gs_combo_list),
    AUTO_TARGET("_introduceCharacter", patch_introduce_character_list),
    AUTO_TARGET("_battlePassMission", patch_battle_pass_mission_list),
    AUTO_TARGET("_railwayDungeonStationName", patch_railway_dungeon_station_name_list),
    AUTO_TARGET("_quest", patch_quest_list),
    AUTO_TARGET("_railwayDungeonText", patch_railway_dungeon_list),
    AUTO_TARGET("_railwayDungeonBuffText", patch_railway_dungeon_buff_list),
    AUTO_TARGET_NO_REMOTE("_normalBattleResultHint", patch_normal_battle_result_hint_list),
    AUTO_TARGET_NO_REMOTE("_abBattleResultHint", patch_ab_battle_result_hint_list),
    AUTO_TARGET("<LobbyBGM>k__BackingField", patch_lobby_bgm_list),
    AUTO_TARGET_NO_REMOTE("_battleResultHint", patch_battle_result_hint_list),
    AUTO_TARGET_NO_REMOTE("_tutorialDesc", patch_tutorial_desc_list),
    AUTO_TARGET("_itemList", patch_item_list),
    AUTO_TARGET("_gachaTitle", patch_gacha_title_list),
    AUTO_TARGET("_mirrorDungeonRentalName", patch_mirror_dungeon_rental_name_list),
    AUTO_TARGET("_errorCodeMsgList", patch_error_code_msg_list),
    AUTO_TARGET("_dungeonArea", patch_dungeon_area_list),
    AUTO_TARGET("_iapStickerText", patch_iap_sticker_list),
    AUTO_TARGET_NO_REMOTE("_keywordDictionary", patch_keyword_dictionary_list),
    AUTO_TARGET_NO_REMOTE("_scenarioModelCodes", patch_scenario_model_code_list),
    AUTO_TARGET_NO_REMOTE("_battleSpeechBubbleText", patch_battle_speech_bubble_list),
    AUTO_TARGET("_iapProductText", patch_iap_product_list),
    AUTO_TARGET("_dungeonNodeText", patch_dungeon_node_text_list),
    AUTO_TARGET_NO_REMOTE("_fileDownloadDesc", patch_file_download_desc_list),
    AUTO_TARGET_NO_REMOTE("_buffAbilityList", patch_buff_ability_list),
    AUTO_TARGET("_enemyList", patch_enemy_list),
    AUTO_TARGET("_stageChapterList", patch_stage_chapter_list),
    AUTO_TARGET("_stagePart", patch_stage_part_list),
    AUTO_TARGET("_stageNodeText", [](Il2CppObject* o) { return patch_stage_node_list(o, "_stageNodeText"); }),
    AUTO_TARGET_LEGACY("_storyDungeonNodeText", 224, [](Il2CppObject* o) { return patch_scoped_list(o, false); }, true, false),
    AUTO_TARGET("_railwayDungeonNodeText", [](Il2CppObject* o) { return patch_stage_node_list(o, "_railwayDungeonNodeText"); }),
    AUTO_TARGET("_abnormalityEventList", patch_abnormality_event_list),
    AUTO_TARGET("_abnormalityEventCharDlg", patch_event_dialogue_list),
    AUTO_TARGET("_choiceEventResultDesc", patch_choice_event_result_list),
    AUTO_TARGET("_actionEvents", patch_action_event_list),
    AUTO_TARGET("_abnormalityCotentData", patch_abnormality_guide_list)
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
    static std::unordered_map<Il2CppClass*, void*> s_dict_version_fields;
    void* version = nullptr;
    auto it = s_dict_version_fields.find(klass);
    if (it != s_dict_version_fields.end()) {
        version = it->second;
    } else {
        version = il2cpp_class_get_field_from_name_fn(klass, "_version");
        if (!version) version = il2cpp_class_get_field_from_name_fn(klass, "version");
        s_dict_version_fields[klass] = version;
    }
    if (version) il2cpp_field_get_value_fn(dictionary, version, &result.version);
    return result;
}

static Il2CppObject* target_container(Il2CppObject* dataset, const ContentTarget& target) {
    if (!dataset) return nullptr;
    if (target.reflected_container) {
        Il2CppObject* container = string_table::reference(dataset, target.name);
        if (!container && target.name) {
            std::string_view sv(target.name);
            if (sv.length() > 4 && sv.compare(sv.length() - 4, 4, "List") == 0) {
                std::string base(sv.substr(0, sv.length() - 4));
                container = string_table::reference(dataset, base.c_str());
            } else if (sv.length() > 4 && sv.compare(sv.length() - 4, 4, "Text") == 0) {
                std::string base(sv.substr(0, sv.length() - 4));
                container = string_table::reference(dataset, base.c_str());
            }
        }
        if (container) return container;
        if (target.offset != 0) {
            return *reinterpret_cast<Il2CppObject**>(reinterpret_cast<char*>(dataset) + target.offset);
        }
        return nullptr;
    }
    return *reinterpret_cast<Il2CppObject**>(reinterpret_cast<char*>(dataset) + target.offset);
}
static ContentStamp target_stamp(Il2CppObject* list, const ContentTarget& target) {
    if (!strcmp(target.name, "_abnormalityEventCharDlg")) return event_dialogue::inspect(list);
    return target.reflected_container ? string_table::stamp(list) : content_stamp(list);
}

static bool synchronize_content_tables(Il2CppObject* tds, uint64_t tick, bool remote_ready) {
    bool all_applied = true;
    for (auto& target : g_content_targets) {
        if (target.requires_remote && !remote_ready) {
            all_applied = false;
            continue;
        }
        Il2CppObject* list = target_container(tds, target);
        ContentStamp before = target_stamp(list, target);
        if (!(before == target.state.observed)) localization_status::dirty = true;
        if (!target.state.observe(before, tick)) {
            if (!target.state.applied && !target.state.settled_untranslated) {
                all_applied = false;
            }
            continue;
        }
        uint64_t writes_before = g_managed_text_writes;
        int matched;
        translation_evidence::Attempt evidence;
        {
            translation_evidence::Scope scope(target.name);
            matched = target.patch(list);
            evidence = std::move(scope.attempt); evidence.current_row = nullptr;
        }
        uint64_t written = g_managed_text_writes - writes_before;
        ContentStamp after = target_stamp(list, target);
        if (!(before == after)) {
            localization_status::attempts.erase(target.name);
            localization_status::dirty = true;
            target.state.observe(after, tick);
            LOGI("[LLC-SYNC] %s changed during patch; scheduling another pass", target.name);
            all_applied = false;
            continue;
        }
        target.state.record(matched, written, tick, evidence.untranslated_only(), evidence.operation_failed());
        if (!target.state.applied && !target.state.settled_untranslated) {
            all_applied = false;
        }
        for (const auto& row : evidence.rows) {
            if (!evidence.complete) break; // A partial traversal cannot replace a full missing-ID summary.
            using translation_evidence::RowState;
            const char* reason = row.second.state == RowState::MissingLocal ? "missing_local" :
                row.second.state == RowState::MappingGap ? "source_map_mismatch" :
                row.second.state == RowState::SourceUncertain ? "source_uncertain" : nullptr;
            if (reason) evidence.warnings[reason].add(row.first);
        }
        diagnostic_log::submit(target.name, evidence.warnings, evidence.complete);
        localization_status::attempts[target.name] = std::move(evidence);
        localization_status::dirty = true;
        if (target.state.applied || target.state.attempts == 1 || target.state.attempts % 16 == 0) {
            LOGI("[LLC-SYNC] %s: runtime_slots=%d matched_rows=%d strings_written=%llu state=%s",
                 target.name, after.count, matched, (unsigned long long)written,
                 target.state.applied ? "applied-to-snapshot" : target.state.settled_untranslated ? "untranslated-source" : "retry-pending");
        }
    }
    return all_applied;
}

static bool g_status_runtime_ready = false;
static bool g_status_remote_ready = false;
static void publish_localization_status() {
    using namespace localization_status;
    nlohmann::json tables = nlohmann::json::array();
    auto* tds = g_status_runtime_ready ? get_live_text_dataset() : nullptr;
    for (const auto& target : g_content_targets) {
        auto* list = target_container(tds, target);
        auto stamp = target_stamp(list, target);
        bool current = stamp == target.state.observed;
        std::string sync = target.requires_remote && !g_status_remote_ready ? "waiting_for_remote" : !stamp.ready() ? "waiting_for_table" :
            !current ? "snapshot_changed" : target.state.applied ? "applied_to_snapshot" : target.state.settled_untranslated ? "settled_untranslated" : "retry_pending";
        nlohmann::json table = {{"table", target.name}, {"sync_state", sync}, {"runtime_slots", stamp.count},
            {"snapshot_current", current}, {"attempts", target.state.attempts}};
        auto found = attempts.find(target.name);
        if (current && found != attempts.end()) table["evidence"] = found->second.json();
        tables.push_back(std::move(table));
    }
    std::error_code error;
    root_available = !root.empty() && std::filesystem::is_directory(root, error);
    if (!publish(build(getpid(), process_start_ticks(), tables))) LOGW("[LLC-STATUS] Could not publish status snapshot");
    dirty = false;
}

static ContentStamp s_last_ui_stamp;
static ContentStamp s_last_login_ui_stamp;

static void trigger_live_hot_reload(const char* base_lang_dir, Il2CppObject* live_tds, uint64_t loop, bool remote_ready) {
    LOGI("[LLC-HOTRELOAD] Live hot-reload triggered! Refreshing translation maps from %s...", base_lang_dir);
    g_managed_string_pool.clear();
    clear_all_translation_storage();
    diagnostic_log::reset();

    load_all_ui_jsons(base_lang_dir);
    load_all_content_jsons(base_lang_dir);

    localization_status::attempts.clear();
    for (auto& target : g_content_targets) {
        target.state = ContentSyncState{};
    }

    if (live_tds) {
        Il2CppObject* ui_list = *(Il2CppObject**)((char*)live_tds + 16);
        Il2CppObject* login_ui_list = *(Il2CppObject**)((char*)live_tds + 616);
        if (login_ui_list) {
            patch_ui_list(login_ui_list, "_loginUIList");
            s_last_login_ui_stamp = content_stamp(login_ui_list);
        }
        if (ui_list) {
            patch_ui_list(ui_list, "_uiList");
            s_last_ui_stamp = content_stamp(ui_list);
        }
        synchronize_content_tables(live_tds, loop, remote_ready);
        if (!remote_ready) {
            refresh_active_ui_texts(s_last_localization_active);
            refresh_title_screen_loaders();
        }
    }

    localization_status::dirty = true;
    publish_localization_status();
    LOGI("[LLC-HOTRELOAD] Live hot-reload finished successfully! %zu UI keys, %zu strings interned.",
         g_ui_map.size(), g_managed_string_pool.size());
}

static void print_audit_report(Il2CppObject* live_tds) {
    if (!live_tds) return;
    LOGI("[LLC-AUDIT] Localization table snapshots (matches and writes are from the last attempt)");
    for (const auto& target : g_content_targets) {
        Il2CppObject* list = target_container(live_tds, target);
        ContentStamp current = target_stamp(list, target);
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
    const char* base_lang_dir = nullptr;
    try {
        const auto& selected = resource_paths::pinned();
        base_lang_dir = selected.root.c_str();
        localization_status::root = selected.root; localization_status::mode = selected.mode; localization_status::pack_key = selected.key;
        try {
            auto meta = resource_pack::document(resource_pack::read_bytes(std::filesystem::path(selected.root) / "Info/version.json", 16384));
            if (meta.contains("version") && (meta["version"].is_string() || meta["version"].is_number_integer())) {
                localization_status::version = meta["version"]; localization_status::version_state = "read";
            } else localization_status::version_state = "invalid_metadata";
        } catch (const std::exception&) { localization_status::version_state = "unavailable"; }
        LOGI("[LLC-RESOURCE] Pinned mode=%s key=%s root=%s reason=%s", selected.mode.c_str(), selected.key.c_str(), base_lang_dir, selected.reason.c_str());
    } catch (const std::exception& error) {
        localization_status::stage = "resource_unavailable"; localization_status::reason = error.what(); publish_localization_status();
        LOGE("[LLC-RESOURCE] No usable resource pack; stopping localization: %s", error.what());
        return nullptr;
    }
    load_all_ui_jsons(base_lang_dir);
    load_all_content_jsons(base_lang_dir);
    publish_localization_status();

    LOGI("[LLC-CORE] Dynamically synchronizing with Unity engine initialization (adaptive polling)...");
    Il2CppDomain* domain = nullptr;
    size_t asm_count = 0;
    for (int retry = 0; retry < 240; ++retry) {
        domain = il2cpp_domain_get_fn ? il2cpp_domain_get_fn() : nullptr;
        if (domain && il2cpp_domain_get_assemblies_fn) {
            asm_count = 0;
            const Il2CppAssembly** asms = il2cpp_domain_get_assemblies_fn(domain, &asm_count);
            if (asms && asm_count > 10) {
                LOGI("[LLC-CORE] Il2Cpp fully initialized with %zu assemblies (waited ~%d ms)!",
                     asm_count, (retry + 1) * 50);
                break;
            }
        }
        usleep(50000); // 50ms interval
    }

    if (!domain || asm_count <= 10) {
        localization_status::stage = "api_unavailable"; localization_status::reason = "domain_timeout"; publish_localization_status();
        LOGE("[LLC-CORE] Timeout waiting for Il2CppDomain and loaded assemblies!");
        return nullptr;
    }

    g_attached_il2cpp_thread = il2cpp_thread_attach_fn(domain);
    LOGI("[LLC-CORE] Successfully attached to Il2CppDomain: %p (thread=%p)", domain, g_attached_il2cpp_thread);

    // Load Chinese font AssetBundle (custom or fallback default)
    auto font_sel = font_config::resolve_font_selection();
    LOGI("[LLC-CORE] Resolved font selection: name='%s', is_custom=%d, path='%s'",
         font_sel.selected_name.c_str(), font_sel.is_custom ? 1 : 0, font_sel.font_path.c_str());

    bool font_loaded = load_chinese_font_bundle(font_sel.font_path.c_str());
    if (!font_loaded && font_sel.is_custom) {
        LOGW("[LLC-CORE] Failed to load custom font bundle '%s', falling back to default '%s'...",
             font_sel.font_path.c_str(), font_config::default_font_path);
        font_loaded = load_chinese_font_bundle(font_config::default_font_path);
    }
    if (font_loaded) {
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
    localization_status::stage = "running"; localization_status::dirty = true; g_status_runtime_ready = true;
    bool login_patched = false;
    bool remote_patched = false;
    Il2CppObject* last_tds = nullptr;
    bool last_remote = false;
    bool shader_bound = false;
    uint64_t loop = 0;
    int s_fast_ui_ticks = 100; // High responsiveness initial burst (5 seconds at 50ms)

    while (g_engine_running) {
        loop++;

        bool fast_mode = (s_fast_ui_ticks > 0);
        if (fast_mode) {
            s_fast_ui_ticks--;
        }

        if (loop == 1 || loop % 20 == 0) {
            auto lang_sel = language_config::resolve_language_selection();
            int cur_lang = query_game_language();
            bool active = language_config::should_apply_localization(lang_sel, cur_lang);
            if (active != s_last_localization_active || cur_lang != s_last_game_lang) {
                LOGI("[LLC-LANG] Language policy evaluated: cur_game_lang=%d (%s), mode=%s, target=%s -> localize_active=%d",
                     cur_lang, language_config::int_to_lang_code(cur_lang).c_str(),
                     lang_sel.mode.c_str(), lang_sel.target_language.c_str(), active ? 1 : 0);
                s_last_game_lang = cur_lang;
                s_last_localization_active = active;
                inject_fallback_to_all_loaded_fonts(active);
                if (!active) {
                    login_patched = false;
                    remote_patched = false;
                    s_last_ui_stamp = ContentStamp{};
                    s_last_login_ui_stamp = ContentStamp{};
                    for (auto& target : g_content_targets) target.state = ContentSyncState{};
                }
            }
        }

        if (!shader_bound) {
            if (ensure_font_material_valid()) {
                shader_bound = true;
                LOGI("[LLC-CORE] Font material and Android Vulkan shader successfully configured.");
                inject_fallback_to_all_loaded_fonts(s_last_localization_active);
                if (login_patched && s_last_localization_active && !remote_patched) {
                    refresh_title_screen_loaders();
                    refresh_active_ui_texts(s_last_localization_active);
                }
            }
        }

        Il2CppObject* live_tds = get_live_text_dataset();
        bool all_tables_applied = false;

        if (live_tds && s_last_localization_active) {
            bool remote_now = *(bool*)((char*)live_tds + 656);
            if (live_tds != last_tds || (last_remote && !remote_now)) {
                localization_status::attempts.clear(); localization_status::dirty = true;
                for (auto& target : g_content_targets) target.state = ContentSyncState{};
                login_patched = false;
                remote_patched = false;
                s_last_ui_stamp = ContentStamp{};
                s_last_login_ui_stamp = ContentStamp{};
                last_tds = live_tds;
                LOGI("[LLC-SYNC] TextDataSet lifecycle changed; table states reset");
            }
            last_remote = remote_now;
            init_story_hook();

            Il2CppObject* login_ui_list = *(Il2CppObject**)((char*)live_tds + 616);
            Il2CppObject* ui_list = *(Il2CppObject**)((char*)live_tds + 16);
            bool is_remote = *(bool*)((char*)live_tds + 656);

            if (login_ui_list) {
                ContentStamp cur_login_stamp = content_stamp(login_ui_list);
                if (!(cur_login_stamp == s_last_login_ui_stamp)) {
                    s_last_login_ui_stamp = cur_login_stamp;
                    int p = patch_ui_list(login_ui_list, "_loginUIList");
                    if (p > 0) {
                        login_patched = true;
                        LOGI("[LLC-CORE] _loginUIList successfully patched with %d entries!", p);
                        if (shader_bound && !remote_patched) {
                            refresh_title_screen_loaders();
                            refresh_active_ui_texts(s_last_localization_active);
                        }
                    }
                }
            }

            // Periodically refresh title & auth screen texts ONLY while waiting at login / title scene
            if (!remote_patched && (fast_mode || (loop % 4 == 0)) && shader_bound) {
                UIRefreshResult res = refresh_active_ui_texts(s_last_localization_active);
                if (res.title_or_auth_dialog_active) {
                    s_fast_ui_ticks = 40; // Maintain fast 50ms polling while title screen / auth dialog is active
                }
            }

            if (ui_list) {
                ContentStamp cur_ui_stamp = content_stamp(ui_list);
                if (!(cur_ui_stamp == s_last_ui_stamp)) {
                    s_last_ui_stamp = cur_ui_stamp;
                    int p = patch_ui_list(ui_list, "_uiList");
                    if (p > 0) {
                        LOGI("[LLC-CORE] _uiList dynamically synchronized: %d entries (count=%d)!", p, cur_ui_stamp.count);
                    }
                }
            }

            if (!remote_patched && is_remote) {
                remote_patched = true;
                LOGI("[LLC-CORE] Remote localization loaded!");
            }

            all_tables_applied = synchronize_content_tables(live_tds, loop, remote_patched);
            if (g_status_remote_ready != remote_patched) localization_status::dirty = true;
            g_status_remote_ready = remote_patched;
        }

        // =========================================================================
        // Maintenance and on-demand diagnostics
        // =========================================================================
        // Dynamic Story Localization Interception (100% data-driven)
        bool story_active = s_last_localization_active ? check_and_patch_active_story() : false;
        diagnostic_log::flush();
        if (localization_status::dirty) publish_localization_status();

        // Adaptive sleep calculation (P2-1):
        // When auth dialog or title UI is active, use 50ms (20 Hz) high responsiveness mode.
        // When all 65 content tables are fully applied, remote packet loaded, and no active story is playing,
        // back off to 1000ms (1 Hz) to drastically reduce CPU wakeups and power consumption during normal gameplay.
        useconds_t sleep_us = 250000;
        if (fast_mode || s_fast_ui_ticks > 0) {
            sleep_us = 50000; // 50ms high responsiveness mode for instant popup localization
        } else if (remote_patched && all_tables_applied && !story_active) {
            sleep_us = 1000000;
        }
        uint32_t remaining_us = sleep_us;
        while (remaining_us > 0 && g_engine_running) {
            uint32_t step = remaining_us > 50000 ? 50000 : remaining_us;
            usleep(step);
            remaining_us -= step;
        }

        static uint32_t s_idle_accum_us = 0;
        s_idle_accum_us += sleep_us;
        if (s_idle_accum_us >= 1000000) { // Check triggers every 1 second
            s_idle_accum_us = 0;
            const char* reload_flags[] = {
                "/data/user/0/com.ProjectMoon.LimbusCompany/files/llc_reload.flag",
                "/sdcard/Android/data/com.ProjectMoon.LimbusCompany/files/llc_reload.flag",
                "/data/local/tmp/llc_reload.flag"
            };
            for (const char* flag : reload_flags) {
                if (access(flag, F_OK) == 0) {
                    if (unlink(flag) != 0) {
                        struct stat st;
                        if (stat(flag, &st) == 0) {
                            static time_t s_last_flag_mtime = 0;
                            if (st.st_mtime == s_last_flag_mtime) {
                                continue;
                            }
                            s_last_flag_mtime = st.st_mtime;
                        }
                    }
                    trigger_live_hot_reload(base_lang_dir, live_tds, loop, remote_patched);
                    break;
                }
            }

            const char* status_trigger = "/data/user/0/com.ProjectMoon.LimbusCompany/files/llc_status_check";
            if (FILE* request = fopen(status_trigger, "r")) {
                char id[65] = {};
                if (fgets(id, sizeof(id), request)) {
                    id[strcspn(id, "\r\n")] = 0;
                    if (*id && strspn(id, "0123456789abcdef-") == strlen(id)) {
                        localization_status::request_id = id;
                        publish_localization_status();
                    }
                }
                fclose(request); unlink(status_trigger);
            }
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

            const char* check_trigger = "/data/data/com.ProjectMoon.LimbusCompany/files/llc_story_check";
            if (FILE* request = fopen(check_trigger, "r")) {
                char id[65] = {};
                if (!fgets(id, sizeof(id), request)) id[0] = 0;
                fclose(request);
                id[strcspn(id, "\r\n")] = 0;
                observe_story_state("/sdcard/Android/data/com.ProjectMoon.LimbusCompany/files/llc_story_state.json", id);
                unlink(check_trigger);
            }
            const char* probe_trigger = "/data/data/com.ProjectMoon.LimbusCompany/files/llc_main_probe";
            if (!main_thread_bridge::busy()) if (FILE* request = fopen(probe_trigger, "r")) {
                char id[65] = {};
                if (!fgets(id, sizeof(id), request)) id[0] = 0;
                fclose(request);
                id[strcspn(id, "\r\n")] = 0;
                main_thread_bridge::request_probe("/sdcard/Android/data/com.ProjectMoon.LimbusCompany/files/llc_main_probe.json", id);
                unlink(probe_trigger);
            }
            const char* content_trigger = "/data/user/0/com.ProjectMoon.LimbusCompany/files/llc_content_check";
            if (!main_thread_bridge::busy()) if (FILE* request = fopen(content_trigger, "r")) {
                char id[65] = {}, table[65] = {};
                bool valid = fgets(id, sizeof(id), request) && fgets(table, sizeof(table), request);
                fclose(request); unlink(content_trigger);
                id[strcspn(id, "\r\n")] = 0; table[strcspn(table, "\r\n")] = 0;
                if (valid && *id && strspn(id, "0123456789abcdef-") == strlen(id) && content_readback::supported(table)) {
                    g_content_check_request = id; g_content_check_table = table;
                    main_thread_bridge::request_probe("/sdcard/Android/data/com.ProjectMoon.LimbusCompany/files/llc_content_bridge.json",
                        id, inspect_content_on_main, "content_readback_bridge");
                }
            }
            main_thread_bridge::publish_result();
            const char* refresh_trigger = "/data/data/com.ProjectMoon.LimbusCompany/files/llc_story_refresh";
            if (!main_thread_bridge::busy()) if (FILE* request = fopen(refresh_trigger, "r")) {
                char id[65] = {};
                if (!fgets(id, sizeof(id), request)) id[0] = 0;
                fclose(request);
                id[strcspn(id, "\r\n")] = 0;
                request_story_refresh("/sdcard/Android/data/com.ProjectMoon.LimbusCompany/files/llc_story_refresh.json", id);
                unlink(refresh_trigger);
            }
        }
        if (s_last_localization_active) check_and_refresh_active_story();
    }

    if (g_attached_il2cpp_thread && il2cpp_thread_detach_fn) {
        LOGI("[LLC-CORE] Cleanly detaching worker thread from Il2CppDomain on exit...");
        il2cpp_thread_detach_fn(g_attached_il2cpp_thread);
        g_attached_il2cpp_thread = nullptr;
    }
    LOGI("[LLC-CORE] Pure Native Engine Thread terminated gracefully.");
    return nullptr;
}

extern "C" __attribute__((visibility("default"))) void llc_core_init() {
    LOGI("[LLC-CORE] Stage 2 Core Initializing (100%% Data-Only Modular Architecture)...");
    bool allowed = verify_runtime_compatibility();
    localization_status::compatibility = g_runtime_compatibility_report;
    if (!allowed) { localization_status::stage = "disabled"; localization_status::reason = "native_build_not_approved"; publish_localization_status(); return; }
    publish_localization_status();
    if (!init_il2cpp_api()) { localization_status::stage = "api_unavailable"; localization_status::reason = "api_resolution_failed"; publish_localization_status(); return; }
    g_managed_string_pool.enable_gchandles(true);

    pthread_t tid;
    if (pthread_create(&tid, nullptr, pure_native_engine_thread, nullptr) == 0) {
        pthread_detach(tid);
    } else { localization_status::stage = "disabled"; localization_status::reason = "worker_creation_failed"; publish_localization_status(); }
}
