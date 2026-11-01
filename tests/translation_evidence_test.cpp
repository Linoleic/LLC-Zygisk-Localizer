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

    Json ic_row = {{"id", "\uc774\uc0c1"}, {"name", "\u674e\u7bb1"}, {"description", "Desc YiSang"}, {"sentence", ""}};
    dungeon_reg.source("/data/IntroduceCharacter.json", document(Json::array({ic_row})));
    dungeon_reg.loaded_row("_introduceCharacter", "\xec\x9d\xb4\xec\x83\x81", translation_evidence::source_fields(ic_row, "_introduceCharacter"));
    assert(dungeon_reg.classify("_introduceCharacter", "\xec\x9d\xb4\xec\x83\x81") == RowState::Available);

    Json bpm_row = {{"id", "CLEAR_STAGE"}, {"content", "Clear stage"}};
    dungeon_reg.source("/data/BattlePass_Mission.json", document(Json::array({bpm_row})));
    dungeon_reg.loaded_row("_battlePassMission", "CLEAR_STAGE", Fields{{"content", "Clear stage"}});
    assert(dungeon_reg.classify("_battlePassMission", "CLEAR_STAGE") == RowState::Available);

    Json rds_row = {{"id", "1001"}, {"nameList", Json::array({{{"id", "0"}, {"content", "Station 0"}, {"shortName", "S0"}}})}};
    dungeon_reg.source("/data/RailwayDungeonStationName1001.json", document(Json::array({rds_row})));
    dungeon_reg.loaded_row("_railwayDungeonStationName", "1001", translation_evidence::source_fields(rds_row, "_railwayDungeonStationName"));
    assert(dungeon_reg.classify("_railwayDungeonStationName", "1001") == RowState::Available);

    Json quest_row = {{"id", "1"}, {"content", "Quest 1"}};
    dungeon_reg.source("/data/Quest.json", document(Json::array({quest_row})));
    dungeon_reg.loaded_row("_quest", "1", Fields{{"content", "Quest 1"}});
    assert(dungeon_reg.classify("_quest", "1") == RowState::Available);

    Json rd_row = {{"id", 1}, {"name", "Line 1"}, {"longName", "Line 1 Long"}};
    dungeon_reg.source("/data/RailwayDungeon.json", document(Json::array({rd_row})));
    dungeon_reg.loaded_row("_railwayDungeonText", "1", translation_evidence::source_fields(rd_row, "_railwayDungeonText"));
    assert(dungeon_reg.classify("_railwayDungeonText", "1") == RowState::Available);

    Json rdb_row = {{"id", "BUFF_1"}, {"content", "Buff 1"}};
    dungeon_reg.source("/data/RailwayDungeonBuff.json", document(Json::array({rdb_row})));
    dungeon_reg.loaded_row("_railwayDungeonBuffText", "BUFF_1", Fields{{"content", "Buff 1"}});
    assert(dungeon_reg.classify("_railwayDungeonBuffText", "BUFF_1") == RowState::Available);

    Json nbh_row = {{"id", "1"}, {"content", "Hint Normal 1"}};
    dungeon_reg.source("/data/BattleHint_NormalBattle.json", document(Json::array({nbh_row})));
    dungeon_reg.loaded_row("_normalBattleResultHint", "1", Fields{{"content", "Hint Normal 1"}});
    assert(dungeon_reg.classify("_normalBattleResultHint", "1") == RowState::Available);

    Json abh_row = {{"id", "1"}, {"content", "Hint Abnor 1"}};
    dungeon_reg.source("/data/BattleHint_AbnorBattle.json", document(Json::array({abh_row})));
    dungeon_reg.loaded_row("_abBattleResultHint", "1", Fields{{"content", "Hint Abnor 1"}});
    assert(dungeon_reg.classify("_abBattleResultHint", "1") == RowState::Available);

    Json bgm_row = {{"id", 100001}, {"name", "BGM 1"}, {"songWriter", "Artist 1"}};
    dungeon_reg.source("/data/LobbyBGM.json", document(Json::array({bgm_row})));
    dungeon_reg.loaded_row("<LobbyBGM>k__BackingField", "100001", translation_evidence::source_fields(bgm_row, "<LobbyBGM>k__BackingField"));
    assert(dungeon_reg.classify("<LobbyBGM>k__BackingField", "100001") == RowState::Available);

    Json brh_row = {{"id", "tip_1"}, {"content", "Result Tip 1"}};
    dungeon_reg.source("/data/BattleResultHint-1.json", document(Json::array({brh_row})));
    dungeon_reg.loaded_row("_battleResultHint", "tip_1", Fields{{"content", "Result Tip 1"}});
    assert(dungeon_reg.classify("_battleResultHint", "tip_1") == RowState::Available);

    Json tuto_desc_row = {{"id", "tuto_1"}, {"content", "Tutorial 1"}};
    dungeon_reg.source("/data/TutorialDesc.json", document(Json::array({tuto_desc_row})));
    dungeon_reg.loaded_row("_tutorialDesc", "tuto_1", Fields{{"content", "Tutorial 1"}});
    assert(dungeon_reg.classify("_tutorialDesc", "tuto_1") == RowState::Available);

    Json item_row = {{"id", 1}, {"name", "Item 1"}, {"desc", "Item Desc 1"}, {"flavor", "Flavor 1"}};
    dungeon_reg.source("/data/Items.json", document(Json::array({item_row})));
    dungeon_reg.loaded_row("_itemList", "1", translation_evidence::source_fields(item_row, "_itemList"));
    assert(dungeon_reg.classify("_itemList", "1") == RowState::Available);

    Json gt_row = {{"id", "1"}, {"content", "Gacha 1"}};
    dungeon_reg.source("/data/GachaTitle.json", document(Json::array({gt_row})));
    dungeon_reg.loaded_row("_gachaTitle", "1", Fields{{"content", "Gacha 1"}});
    assert(dungeon_reg.classify("_gachaTitle", "1") == RowState::Available);

    Json mdr_row = {{"id", 1}, {"content", "Rental 1"}};
    dungeon_reg.source("/data/MirrorDungeonRentalName.json", document(Json::array({mdr_row})));
    dungeon_reg.loaded_row("_mirrorDungeonRentalName", "1", Fields{{"content", "Rental 1"}});
    assert(dungeon_reg.classify("_mirrorDungeonRentalName", "1") == RowState::Available);

    Json err_row = {{"id", "err_1"}, {"content", "Error 1"}};
    dungeon_reg.source("/data/ErrorCodeMsg.json", document(Json::array({err_row})));
    dungeon_reg.loaded_row("_errorCodeMsgList", "err_1", Fields{{"content", "Error 1"}});
    assert(dungeon_reg.classify("_errorCodeMsgList", "err_1") == RowState::Available);

    Json dungeon_area_row = {{"id", "area_1"}, {"content", "Area 1"}};
    dungeon_reg.source("/data/DungeonArea1-1.json", document(Json::array({dungeon_area_row})));
    dungeon_reg.loaded_row("_dungeonArea", "area_1", Fields{{"content", "Area 1"}});
    assert(dungeon_reg.classify("_dungeonArea", "area_1") == RowState::Available);

    Json sticker_row = {{"id", 135}, {"content", "Sticker 135"}};
    dungeon_reg.source("/data/IAPSticker.json", document(Json::array({sticker_row})));
    dungeon_reg.loaded_row("_iapStickerText", "135", Fields{{"content", "Sticker 135"}});
    assert(dungeon_reg.classify("_iapStickerText", "135") == RowState::Available);

    Json kw_row = {{"id", "Gcorp"}, {"name", "G Corp"}, {"desc", "G Corp Desc"}};
    dungeon_reg.source("/data/KeywordDictionary.json", document(Json::array({kw_row})));
    dungeon_reg.loaded_row("_keywordDictionary", "Gcorp", translation_evidence::source_fields(kw_row, "_keywordDictionary"));
    assert(dungeon_reg.classify("_keywordDictionary", "Gcorp") == RowState::Available);

    Json smc_row = {{"id", "yi_sang"}, {"name", "Yi Sang"}, {"nickName", "Sinner 1"}};
    dungeon_reg.source("/data/ScenarioModelCodes-AutoCreated.json", document(Json::array({smc_row})));
    dungeon_reg.loaded_row("_scenarioModelCodes", "yi_sang", translation_evidence::source_fields(smc_row, "_scenarioModelCodes"));
    assert(dungeon_reg.classify("_scenarioModelCodes", "yi_sang") == RowState::Available);

    Json bsb_row = {{"id", "bubble_1"}, {"desc", "Desc 1"}, {"dlg", "Dialog 1"}};
    dungeon_reg.source("/data/BattleSpeechBubbleDlg.json", document(Json::array({bsb_row})));
    dungeon_reg.loaded_row("_battleSpeechBubbleText", "bubble_1", translation_evidence::source_fields(bsb_row, "_battleSpeechBubbleText"));
    assert(dungeon_reg.classify("_battleSpeechBubbleText", "bubble_1") == RowState::Available);

    Json iap_row = {{"id", 1}, {"name", "Lunacy"}, {"desc", "140 Paid Lunacy"}, {"subDesc", "Sub"}, {"additionalItemDesc", "Add"}, {"otherItemDesc", "Other"}};
    dungeon_reg.source("/data/IAPProduct.json", document(Json::array({iap_row})));
    dungeon_reg.loaded_row("_iapProductText", "1", translation_evidence::source_fields(iap_row, "_iapProductText"));
    assert(dungeon_reg.classify("_iapProductText", "1") == RowState::Available);

    Json dungeon_node_row = {{"id", 2010000}, {"title", "Node Title"}, {"desc", "Node Desc"}, {"place", "Place"}};
    dungeon_reg.source("/data/MirrorDungeonNode1-1.json", document(Json::array({dungeon_node_row})));
    dungeon_reg.loaded_row("_dungeonNodeText", "2010000", translation_evidence::source_fields(dungeon_node_row, "_dungeonNodeText"));
    assert(dungeon_reg.classify("_dungeonNodeText", "2010000") == RowState::Available);

    Json fdd_row = {{"id", "FDD_1"}, {"title", "FDD Title"}, {"desc", "FDD Desc"}};
    dungeon_reg.source("/data/FileDownloadDesc.json", document(Json::array({fdd_row})));
    dungeon_reg.loaded_row("_fileDownloadDesc", "FDD_1", translation_evidence::source_fields(fdd_row, "_fileDownloadDesc"));
    assert(dungeon_reg.classify("_fileDownloadDesc", "FDD_1") == RowState::Available);

    Json ba_row = {{"id", "AtkMul"}, {"desc", "Desc"}, {"variation", "Var"}, {"variation2", "Var2"}};
    dungeon_reg.source("/data/BuffAbilities.json", document(Json::array({ba_row})));
    dungeon_reg.loaded_row("_buffAbilityList", "AtkMul", translation_evidence::source_fields(ba_row, "_buffAbilityList"));
    assert(dungeon_reg.classify("_buffAbilityList", "AtkMul") == RowState::Available);

    Json gn_row = {{"id", "gn_1"}, {"content", "Notice"}};
    dungeon_reg.source("/data/GachaNotice.json", document(Json::array({gn_row})));
    dungeon_reg.loaded_row("_gachaNotice", "gn_1", Fields{{"content", "Notice"}});
    assert(dungeon_reg.classify("_gachaNotice", "gn_1") == RowState::Available);

    Json lbc_row = {{"id", "lbc_1"}, {"content", "Category"}};
    dungeon_reg.source("/data/LobbyBGMCategory.json", document(Json::array({lbc_row})));
    dungeon_reg.loaded_row("<LobbyBGMCategory>k__BackingField", "lbc_1", Fields{{"content", "Category"}});
    assert(dungeon_reg.classify("<LobbyBGMCategory>k__BackingField", "lbc_1") == RowState::Available);

    Json enemy_row = {{"id", 90152}, {"name", "Bloodbag"}, {"desc", "Minion"}};
    dungeon_reg.source("/data/Enemies-a1c7p1.json", document(Json::array({enemy_row})));
    dungeon_reg.loaded_row("_enemyList", "90152", translation_evidence::source_fields(enemy_row, "_enemyList"));
    assert(dungeon_reg.classify("_enemyList", "90152") == RowState::Available);

    assert(translation_evidence::topic("/data/IntroduceCharacter.json") == "_introduceCharacter");
    assert(translation_evidence::topic("/data/BattlePass_Mission.json") == "_battlePassMission");
    assert(translation_evidence::topic("/data/RailwayDungeonStationName1001.json") == "_railwayDungeonStationName");
    assert(translation_evidence::topic("/data/Quest.json") == "_quest");
    assert(translation_evidence::topic("/data/RailwayDungeon.json") == "_railwayDungeonText");
    assert(translation_evidence::topic("/data/RailwayDungeonBuff.json") == "_railwayDungeonBuffText");
    assert(translation_evidence::topic("/data/BattleHint_NormalBattle.json") == "_normalBattleResultHint");
    assert(translation_evidence::topic("/data/BattleHint_AbnorBattle.json") == "_abBattleResultHint");
    assert(translation_evidence::topic("/data/LobbyBGM.json") == "<LobbyBGM>k__BackingField");
    assert(translation_evidence::topic("/data/BattleResultHint-a1c10p1.json") == "_battleResultHint");
    assert(translation_evidence::topic("/data/TutorialDesc.json") == "_tutorialDesc");
    assert(translation_evidence::topic("/data/Items.json") == "_itemList");
    assert(translation_evidence::topic("/data/GachaTitle.json") == "_gachaTitle");
    assert(translation_evidence::topic("/data/MirrorDungeonRentalName.json") == "_mirrorDungeonRentalName");
    assert(translation_evidence::topic("/data/ErrorCodeMsg.json") == "_errorCodeMsgList");
    assert(translation_evidence::topic("/data/DungeonArea1-1.json") == "_dungeonArea");
    assert(translation_evidence::topic("/data/IAPSticker.json") == "_iapStickerText");
    assert(translation_evidence::topic("/data/IAPProduct-a1c10.json") == "_iapProductText");
    assert(translation_evidence::topic("/data/KeywordDictionary.json") == "_keywordDictionary");
    assert(translation_evidence::topic("/data/ScenarioModelCodes-AutoCreated.json") == "_scenarioModelCodes");
    assert(translation_evidence::topic("/data/BattleSpeechBubbleDlg-a1c10p1.json") == "_battleSpeechBubbleText");
    assert(translation_evidence::topic("/data/MirrorDungeonNode1-1.json") == "_dungeonNodeText");
    assert(translation_evidence::topic("/data/HellsChickenDungeonNode.json") == "_dungeonNodeText");
    assert(translation_evidence::topic("/data/FileDownloadDesc.json") == "_fileDownloadDesc");
    assert(translation_evidence::topic("/data/BuffAbilities.json") == "_buffAbilityList");
    assert(translation_evidence::topic("/data/GachaNotice.json") == "_gachaNotice");
    assert(translation_evidence::topic("/data/LobbyBGMCategory.json") == "<LobbyBGMCategory>k__BackingField");
    assert(translation_evidence::topic("/data/Enemies-a1c7p1.json") == "_enemyList");
    assert(translation_evidence::topic("/data/Enemies.json") == "_enemyList");
    assert(translation_evidence::topic("/data/AbEvents-a1c5p3.json") == "_abnormalityEventList");
    assert(translation_evidence::topic("/data/ActionEvents-a1c5p3.json") == "_actionEvents");
    assert(translation_evidence::topic("/data/AbnormalityGuides-a1c10p2.json") == "_abnormalityCotentData");

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
