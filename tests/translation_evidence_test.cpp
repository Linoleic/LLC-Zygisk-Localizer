#include "translation_evidence.hpp"

#include <cassert>
#include <string>
#include <thread>

using translation_evidence::Fields;
using translation_evidence::Json;
using translation_evidence::RowState;

static std::string document(const Json& rows) {
    return Json{{"dataList", rows}}.dump();
}

static void source_row(translation_evidence::Registry& registry, const std::string& path,
                       const Json& row, const std::string& error = {}) {
    registry.source(path, document(Json::array({row})), error);
}

int main() {
    translation_evidence::Registry registry;

    // A validated source can prove an ID is absent; a rejected file cannot.
    source_row(registry, "/data/Skills-main.json", Json{{"id", 1}, {"levelList", Json::array({Json{{"name", "source"}}})}});
    assert(registry.classify("_skillList", "2") == RowState::MissingLocal);
    registry.source("/data/Skills-broken.json", "not-json", "read_failed");
    assert(registry.classify("_skillList", "2") == RowState::SourceUncertain);

    // Nonempty source text with a lost or altered loaded field is a mapping gap.
    translation_evidence::Registry fields_registry;
    Json source = {{"id", "tag-1"}, {"name", "A\\nB"}};
    source_row(fields_registry, "/data/SkillTag.json", source);
    auto exact_fields = translation_evidence::source_fields(source, "_skillTagList");
    fields_registry.loaded_row("_skillTagList", "tag-1", exact_fields);
    assert(fields_registry.classify("_skillTagList", "tag-1") == RowState::Available);
    fields_registry.loaded_row("_skillTagList", "tag-1", Fields{});
    assert(fields_registry.classify("_skillTagList", "tag-1") == RowState::MappingGap);
    fields_registry.loaded_row("_skillTagList", "tag-1", Fields{{"name", "A\nB"}});
    assert(fields_registry.classify("_skillTagList", "tag-1") == RowState::MappingGap);

    // Rows whose source fields are all empty remain intentionally untranslated.
    translation_evidence::Registry empty_registry;
    source_row(empty_registry, "/data/SkillTag.json", Json{{"id", "tag-empty"}, {"name", ""}});
    assert(empty_registry.classify("_skillTagList", "tag-empty") == RowState::EmptySource);
    empty_registry.loaded_row("_skillTagList", "tag-empty", Fields{{"name", "invented by bad parser"}});
    assert(empty_registry.classify("_skillTagList", "tag-empty") == RowState::MappingGap);

    // Unknown source models cannot prove that a runtime key has no translation.
    translation_evidence::Registry unknown_model;
    source_row(unknown_model, "/data/Skills-new.json", Json{{"id", 1}, {"levelList", Json::object()}});
    assert(unknown_model.classify("_skillList", "2") == RowState::SourceUncertain);
    assert(unknown_model.file_report()["index_model_issue_files"] == 1);
    assert(translation_evidence::fingerprint(Fields{{"name", std::string("\xff", 1)}}) != translation_evidence::fingerprint(Fields{{"name", "valid"}}));

    // The same ID is interpreted independently in each table.
    translation_evidence::Registry table_registry;
    Json tag = {{"id", "shared"}, {"name", "Tag source"}};
    Json hint = {{"id", "shared"}, {"content", "Hint source"}};
    source_row(table_registry, "/data/SkillTag.json", tag);
    source_row(table_registry, "/data/BattleHint.json", hint);
    table_registry.loaded_row("_skillTagList", "shared", translation_evidence::source_fields(tag, "_skillTagList"));
    assert(table_registry.classify("_skillTagList", "shared") == RowState::Available);
    assert(table_registry.classify("_battleHint", "shared") == RowState::MappingGap);

    // A loaded variant is valid when it exactly matches any complete source signature.
    translation_evidence::Registry variants;
    Json variant_a = {{"id", "v"}, {"name", "Alpha"}};
    Json variant_b = {{"id", "v"}, {"name", "Beta"}};
    variants.source("/data/SkillTag.json", document(Json::array({variant_a, variant_b})));
    variants.loaded_row("_skillTagList", "v", translation_evidence::source_fields(variant_b, "_skillTagList"));
    assert(variants.classify("_skillTagList", "v") == RowState::Available);
    variants.loaded_row("_skillTagList", "v", Fields{{"name", "Gamma"}});
    assert(variants.classify("_skillTagList", "v") == RowState::MappingGap);

    // Legacy skill fingerprints include level fields and nested coin descriptions.
    Json skill_source = {{"id", 7}, {"levelList", Json::array({Json{
        {"level", 1}, {"name", "Skill"}, {"desc", "Description"}, {"rawDesc", "Raw"},
        {"coinlist", Json::array({Json{{"coindescs", Json::array({Json{{"desc", "Coin"}, {"summary", "Summary"}}})}}})}
    }})}};
    translation_evidence::Registry skill_registry;
    source_row(skill_registry, "/data/Skills-legacy.json", skill_source);
    SkillEntry loaded_skill;
    SkillLevel level;
    level.name = "Skill";
    level.desc = "Description";
    level.rawDesc = "Raw";
    SkillCoin coin;
    coin.coindescs.push_back({"Coin", "Summary"});
    level.coinlist.push_back(coin);
    loaded_skill.levelList.push_back(level);
    skill_registry.loaded_row("_skillList", "7", translation_evidence::loaded_fields(loaded_skill));
    assert(skill_registry.classify("_skillList", "7") == RowState::Available);
    loaded_skill.levelList[0].level = 4;
    skill_registry.loaded_row("_skillList", "7", translation_evidence::loaded_fields(loaded_skill));
    assert(skill_registry.classify("_skillList", "7") == RowState::MappingGap);
    loaded_skill.levelList[0].level = 1;
    loaded_skill.levelList[0].coinlist[0].coindescs[0].summary = "Changed";
    skill_registry.loaded_row("_skillList", "7", translation_evidence::loaded_fields(loaded_skill));
    assert(skill_registry.classify("_skillList", "7") == RowState::MappingGap);

    translation_evidence::Registry flat_registry;
    flat_registry.source("Bufs_test.json", R"({"dataList":[{"id":"same","name":"buf","summary":"summary"}]})");
    flat_registry.source("BattleKeywords_test.json", R"({"dataList":[{"id":"same","name":"keyword"}]})");
    flat_registry.loaded_row("_bufList","same",translation_evidence::loaded_fields(BufEntry{"same","buf","","summary",""}));
    flat_registry.loaded_row("_keywordList","same",translation_evidence::loaded_fields(BufEntry{"same","keyword","","",""}));
    assert(flat_registry.classify("_bufList","same")==RowState::Available);
    assert(flat_registry.classify("_keywordList","same")==RowState::Available);
    flat_registry.loaded_row("_bufList","same",translation_evidence::loaded_fields(BufEntry{"same","buf","","changed",""}));
    assert(flat_registry.classify("_bufList","same")==RowState::MappingGap);
    assert(flat_registry.classify("_keywordList","same")==RowState::Available);

    // Announcer and voice type evidence registration
    translation_evidence::Registry announcer_reg;
    Json ann_row = {{"id", 41}, {"name", "Araya"}};
    announcer_reg.source("/data/Announcer-a1c9p3.json", document(Json::array({ann_row})));
    announcer_reg.loaded_row("_announcer", "41", Fields{{"name", "Araya"}});
    assert(announcer_reg.classify("_announcer", "41") == RowState::Available);

    Json vt_row = {{"id", "Danger"}, {"content", "Danger Warning"}};
    announcer_reg.source("/data/AnnouncerVoiceType.json", document(Json::array({vt_row})));
    announcer_reg.loaded_row("_announcerVoiceType", "Danger", Fields{{"content", "Danger Warning"}});
    assert(announcer_reg.classify("_announcerVoiceType", "Danger") == RowState::Available);

    // UserTicket evidence registration
    translation_evidence::Registry ticket_reg;
    Json ticket_row = {{"id", -1}, {"name", "LC Base L"}, {"desc", "Base desc L"}};
    ticket_reg.source("/data/UserTicket-L.json", document(Json::array({ticket_row})));
    ticket_reg.loaded_row("_userTicket_L", "-1", Fields{{"name", "LC Base L"}, {"desc", "Base desc L"}});
    assert(ticket_reg.classify("_userTicket_L", "-1") == RowState::Available);

    // ThreadDungeon, DungeonName, and StagePart evidence registration
    translation_evidence::Registry dungeon_reg;
    Json td_row = {{"id", 1}, {"name", "Thread Dungeon 1"}};
    dungeon_reg.source("/data/ThreadDungeon.json", document(Json::array({td_row})));
    dungeon_reg.loaded_row("_threadDungeon", "1", Fields{{"name", "Thread Dungeon 1"}});
    assert(dungeon_reg.classify("_threadDungeon", "1") == RowState::Available);

    Json dn_row = {{"id", 910301}, {"name", "Dungeon 910301"}};
    dungeon_reg.source("/data/DungeonName_Event.json", document(Json::array({dn_row})));
    dungeon_reg.loaded_row("_dungeonName", "910301", Fields{{"name", "Dungeon 910301"}});
    assert(dungeon_reg.classify("_dungeonName", "910301") == RowState::Available);

    Json sp_row = {{"id", "part_1"}, {"parttitle", "Part 1"}};
    dungeon_reg.source("/data/StagePartText.json", document(Json::array({sp_row})));
    dungeon_reg.loaded_row("_stagePart", "part_1", Fields{{"parttitle", "Part 1"}});
    assert(dungeon_reg.classify("_stagePart", "part_1") == RowState::Available);

    Json attr_row = {{"id", "CRIMSON"}, {"name", "\u66b4\u6012"}};
    dungeon_reg.source("/data/AttributeText.json", document(Json::array({attr_row})));
    dungeon_reg.loaded_row("_attributeList", "CRIMSON", Fields{{"name", "\u66b4\u6012"}});
    assert(dungeon_reg.classify("_attributeList", "CRIMSON") == RowState::Available);

    Json buff_row = {{"id", "START_BUFF_1"}, {"description", "Buff 1"}};
    dungeon_reg.source("/data/DungeonStartBuffs.json", document(Json::array({buff_row})));
    dungeon_reg.loaded_row("_dungeonStartBuffs", "START_BUFF_1", Fields{{"description", "Buff 1"}});
    assert(dungeon_reg.classify("_dungeonStartBuffs", "START_BUFF_1") == RowState::Available);

    Json panic_row = {{"id", 1001}, {"panicName", "\u81ea\u6b8b"}, {"lowMoraleDescription", "Low"}, {"panicDescription", "Panic"}, {"flavor", "Flavor"}};
    dungeon_reg.source("/data/PanicInfo.json", document(Json::array({panic_row})));
    dungeon_reg.loaded_row("_panicInfo", "1001", Fields{{"panicName", "\u81ea\u6b8b"}, {"lowMoraleDescription", "Low"}, {"panicDescription", "Panic"}, {"flavor", "Flavor"}});
    assert(dungeon_reg.classify("_panicInfo", "1001") == RowState::Available);

    Json mc_row = {{"id", "MC_1"}, {"add", "Add 1"}, {"min", "Min 1"}};
    dungeon_reg.source("/data/MentalCondition.json", document(Json::array({mc_row})));
    dungeon_reg.loaded_row("_mentalConditionList", "MC_1", Fields{{"add", "Add 1"}, {"min", "Min 1"}});
    assert(dungeon_reg.classify("_mentalConditionList", "MC_1") == RowState::Available);

    Json da_row = {{"id", 90101}, {"desc", "Desc 90101"}, {"rawDesc", "Raw 90101"}};
    dungeon_reg.source("/data/DanteAbility.json", document(Json::array({da_row})));
    dungeon_reg.loaded_row("_danteAbilityDataList", "90101", Fields{{"desc", "Desc 90101"}, {"rawDesc", "Raw 90101"}});
    assert(dungeon_reg.classify("_danteAbilityDataList", "90101") == RowState::Available);

    Json uc_row = {{"id", 101}, {"openCondition", "Cond 101"}, {"openConditionNumber", "1"}, {"relatedChapterText", "Canto I"}, {"askLevelUp", "Up 1"}};
    dungeon_reg.source("/data/UnlockCode-1.json", document(Json::array({uc_row})));
    dungeon_reg.loaded_row("_unlockCodeList", "101", Fields{{"openCondition", "Cond 101"}, {"openConditionNumber", "1"}, {"relatedChapterText", "Canto I"}, {"askLevelUp", "Up 1"}});
    assert(dungeon_reg.classify("_unlockCodeList", "101") == RowState::Available);

    Json pgl_row = {{"id", 1}, {"content", "Lesson 1"}, {"teacher", "Faust"}};
    dungeon_reg.source("/data/ProjectGSLessonName.json", document(Json::array({pgl_row})));
    dungeon_reg.loaded_row("_projectGSLessonName", "1", Fields{{"content", "Lesson 1"}, {"teacher", "Faust"}});
    assert(dungeon_reg.classify("_projectGSLessonName", "1") == RowState::Available);

    Json pgc_row = {{"id", 1}, {"content", "Combo 1"}};
    dungeon_reg.source("/data/ProjectGSComboName.json", document(Json::array({pgc_row})));
    dungeon_reg.loaded_row("_projectGSComboName", "1", Fields{{"content", "Combo 1"}});
    assert(dungeon_reg.classify("_projectGSComboName", "1") == RowState::Available);

    // Scope counters follow the current row and active thread, not ambient writes.
    using namespace translation_evidence;
    Attempt scoped_attempt;
    {
        Scope scope("_skillTagList");
        active->rows.emplace("first", Row{RowState::Available, 0, 0});
        active->rows.emplace("second", Row{RowState::Available, 0, 0});
        observe("first");
        write(true);
        observe("second");
        std::thread unrelated([] { write(false, "outside_scope"); });
        unrelated.join();
        write(false, "row_failure");
        complete();
        scoped_attempt = *active;
    }
    assert(scoped_attempt.writes == 1 && scoped_attempt.failures == 1);
    assert(scoped_attempt.rows.at("first").writes == 1 && scoped_attempt.rows.at("first").failures == 0);
    assert(scoped_attempt.rows.at("second").writes == 0 && scoped_attempt.rows.at("second").failures == 1);
    assert(scoped_attempt.failure_reasons.at("row_failure") == 1);
    assert(scoped_attempt.operation_failed());
    {
        Scope scope("_skillTagList");
        active->rows.emplace("valid", Row{RowState::Available, 0, 0});
        observe("valid");
        assert(active->operation_failed());
        write(true);
        assert(!active->operation_failed());
        observe(""); // A model-defined placeholder cannot inherit the prior ID.
        write(true);
        assert(active->rows.at("valid").writes == 1);
        assert(active->writes == 2);
        unsupported_field("summary");
        assert(active->unsupported_fields.at("summary") == 1);
        assert(!active->operation_failed());
    }
    write(false, "no_active_scope");

    // A definitive untranslated-only conclusion requires complete, clean evidence.
    Attempt all_untranslated;
    all_untranslated.complete = true;
    all_untranslated.rows.emplace("missing", Row{RowState::MissingLocal, 0, 0});
    all_untranslated.rows.emplace("empty", Row{RowState::EmptySource, 0, 0});
    assert(all_untranslated.untranslated_only());
    all_untranslated.complete = false;
    assert(!all_untranslated.untranslated_only());
    all_untranslated.complete = true;
    all_untranslated.rows.emplace("gap", Row{RowState::MappingGap, 0, 0});
    assert(!all_untranslated.untranslated_only());
    all_untranslated.rows.erase("gap");
    all_untranslated.failures = 1;
    assert(!all_untranslated.untranslated_only());
    all_untranslated.failures = 0;
    all_untranslated.unidentified_rows = 1;
    assert(!all_untranslated.untranslated_only());
    all_untranslated.unidentified_rows = 0;
    all_untranslated.invalid_rows = 1;
    assert(!all_untranslated.untranslated_only());

    return 0;
}
