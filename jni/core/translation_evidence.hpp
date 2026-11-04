#pragma once
#include "../third_party/nlohmann/json.hpp"
#include "json_models.hpp"
#include "sha256.hpp"
#include <map>
#include <set>
#include <charconv>

// Evidence is owned by the engine thread. Only write-attempt association is
// thread_local, so main-thread diagnostic actions cannot pollute a table pass.
namespace translation_evidence {
using Json = nlohmann::json;
using Fields = std::map<std::string, std::string>;
inline std::string fingerprint(const Fields& fields) {
    // Hash exact bytes with length prefixes, including malformed legacy output.
    // Diagnostics must not throw merely because a legacy extractor broke UTF-8.
    BuildSha256 hash;
    for (const auto& field : fields) for (const auto* value : {&field.first, &field.second}) {
        uint64_t size = value->size(); uint8_t length[8];
        for (int i = 0; i < 8; ++i) length[7 - i] = static_cast<uint8_t>(size >> (i * 8));
        hash.update(length, sizeof(length)); hash.update(value->data(), value->size());
    }
    return hash.hex_digest();
}
inline void text(Fields& fields, const Json& object, const char* name, const std::string& prefix = {}) {
    auto item = object.find(name);
    if (item != object.end() && item->is_string()) {
        auto value = item->get<std::string>();
        if (!value.empty() && value.find('\0') == value.npos) fields[prefix + name] = std::move(value);
    }
}
inline Fields source_fields(const Json& row, const std::string& table) {
    Fields fields, structure;
    if (table == "_skillList") {
        auto levels = row.find("levelList");
        if (levels == row.end() || !levels->is_array()) return fields;
        size_t l = 0;
        for (const auto& level : *levels) {
            std::string prefix = "levelList/" + std::to_string(l++) + "/";
            if (!level.is_object()) continue;
            structure[prefix + "@level"] = level.value("level", Json()).dump();
            auto coin_count = level.find("coinlist");
            structure[prefix + "@coin_count"] = std::to_string(coin_count != level.end() && coin_count->is_array() ? coin_count->size() : 0);
            for (auto* name : {"name", "desc", "flavor", "rawDesc", "abName"}) text(fields, level, name, prefix);
            auto coins = level.find("coinlist"); if (coins == level.end() || !coins->is_array()) continue;
            size_t c = 0;
            for (const auto& coin : *coins) {
                auto coin_prefix = prefix + "coinlist/" + std::to_string(c++) + "/coindescs/";
                if (!coin.is_object()) continue;
                auto descriptions = coin.find("coindescs");
                structure[coin_prefix + "@count"] = std::to_string(descriptions != coin.end() && descriptions->is_array() ? descriptions->size() : 0);
                if (descriptions == coin.end() || !descriptions->is_array()) continue;
                size_t d = 0;
                for (const auto& desc : *descriptions) {
                    auto desc_prefix = coin_prefix + std::to_string(d++) + "/";
                    if (desc.is_object()) for (auto* name : {"desc", "summary"}) text(fields, desc, name, desc_prefix);
                }
            }
        }
    } else if (table == "_egoGiftData") {
        for (auto* name : {"name", "desc", "abnormalityName"}) text(fields, row, name);
        auto list = row.find("simpleDesc"); size_t i = 0;
        structure["@ability_count"] = std::to_string(list != row.end() && list->is_array() ? list->size() : 0);
        if (list != row.end() && list->is_array()) for (const auto& item : *list) {
            auto prefix = "simpleDesc/" + std::to_string(i++) + "/";
            if (!item.is_object()) continue;
            auto identity = item.find("abilityID");
            structure[prefix + "@abilityID"] = identity == item.end() || identity->is_null() ? "0" : identity->is_string() ? identity->get<std::string>() : identity->dump();
            text(fields, item, "simpleDesc", prefix);
        }
    } else if (table == "_danteNoteDesc") {
        for (auto* name : {"title", "desc"}) text(fields, row, name);
    } else if (table == "_mirrorDungeonThemeList") {
        for (auto* name : {"name", "specialName"}) text(fields, row, name);
    } else if (table == "_announcerVoiceText" || table == "_personalityVoiceText" || table == "_egoVoiceText") {
        text(fields, row, "desc");
        text(fields, row, "dlg");
        if (!fields.count("dlg")) text(fields, row, "lyrics", "dlg");
    } else if (table == "_abnormalityEventCharDlg") {
        text(fields, row, "dialog");
    } else if (table == "_choiceEventResultDesc") {
        text(fields, row, "content");
    } else if (table == "_threadDungeon" || table == "_dungeonName" || table == "_attributeList") {
        text(fields, row, "name");
    } else if (table == "_dungeonStartBuffs") {
        text(fields, row, "description");
    } else if (table == "_stagePart") {
        text(fields, row, "parttitle");
    } else if (table == "_panicInfo") {
        for (auto* name : {"panicName", "lowMoraleDescription", "panicDescription", "flavor"}) text(fields, row, name);
    } else if (table == "_mentalConditionList") {
        for (auto* name : {"add", "min"}) text(fields, row, name);
    } else if (table == "_danteAbilityDataList") {
        for (auto* name : {"desc", "rawDesc"}) text(fields, row, name);
    } else if (table == "_unlockCodeList") {
        for (auto* name : {"openCondition", "openConditionNumber", "relatedChapterText", "askLevelUp"}) text(fields, row, name);
    } else if (table == "_projectGSLessonName") {
        for (auto* name : {"content", "teacher"}) text(fields, row, name);
    } else if (table == "_projectGSComboName") {
        text(fields, row, "content");
    } else if (table == "_introduceCharacter") {
        for (auto* name : {"name", "subName", "description", "sentence"}) text(fields, row, name);
    } else if (table == "_battlePassMission") {
        text(fields, row, "content");
    } else if (table == "_railwayDungeonStationName") {
        auto list = row.find("nameList"); size_t i = 0;
        structure["@station_count"] = std::to_string(list != row.end() && list->is_array() ? list->size() : 0);
        if (list != row.end() && list->is_array()) for (const auto& item : *list) {
            auto prefix = "nameList/" + std::to_string(i++) + "/";
            if (!item.is_object()) continue;
            auto identity = item.find("id");
            structure[prefix + "@id"] = identity == item.end() || identity->is_null() ? "0" : identity->is_string() ? identity->get<std::string>() : identity->dump();
            for (auto* name : {"content", "shortName"}) text(fields, item, name, prefix);
        }
    } else if (table == "_railwayDungeonText") {
        for (auto* name : {"name", "longName"}) text(fields, row, name);
    } else if (table == "<LobbyBGM>k__BackingField") {
        for (auto* name : {"name", "songWriter"}) text(fields, row, name);
    } else if (table == "_itemList") {
        for (auto* name : {"name", "desc", "flavor"}) text(fields, row, name);
    } else if (table == "_keywordDictionary") {
        for (auto* name : {"name", "desc"}) text(fields, row, name);
    } else if (table == "_scenarioModelCodes") {
        for (auto* name : {"name", "nickName"}) text(fields, row, name);
    } else if (table == "_battleSpeechBubbleText") {
        for (auto* name : {"desc", "dlg"}) text(fields, row, name);
    } else if (table == "_iapProductText") {
        for (auto* name : {"name", "desc", "subDesc", "additionalItemDesc", "otherItemDesc"}) text(fields, row, name);
    } else if (table == "_dungeonNodeText") {
        for (auto* name : {"title", "desc", "place"}) text(fields, row, name);
    } else if (table == "_fileDownloadDesc") {
        for (auto* name : {"title", "desc"}) text(fields, row, name);
    } else if (table == "_buffAbilityList") {
        for (auto* name : {"desc", "variation", "variation2"}) text(fields, row, name);
    } else if (table == "_enemyList") {
        for (auto* name : {"name", "desc"}) text(fields, row, name);
    } else if (table == "_abnormalityEventList") {
        for (auto* name : {"eventDesc", "prevDesc", "behaveDesc", "cancelDesc"}) text(fields, row, name);
        for (auto* arr_name : {"successDesc", "failureDesc"}) {
            auto it = row.find(arr_name);
            if (it != row.end() && it->is_array()) {
                size_t i = 0;
                for (const auto& item : *it) {
                    if (item.is_string() && !item.get<std::string>().empty()) {
                        fields[std::string(arr_name) + "/" + std::to_string(i)] = item.get<std::string>();
                    }
                    ++i;
                }
            }
        }
    } else if (table == "_actionEvents") {
        for (auto* name : {"name", "desc", "subDesc"}) text(fields, row, name);
        auto it = row.find("options");
        if (it != row.end() && it->is_array()) {
            size_t i = 0;
            for (const auto& opt : *it) {
                auto prefix = "options/" + std::to_string(i++) + "/";
                if (!opt.is_object()) continue;
                for (auto* name : {"message", "messageDesc"}) text(fields, opt, name, prefix);
                auto res_it = opt.find("result");
                if (res_it != opt.end() && res_it->is_array()) {
                    size_t j = 0;
                    for (const auto& r : *res_it) {
                        if (r.is_string() && !r.get<std::string>().empty()) {
                            fields[prefix + "result/" + std::to_string(j)] = r.get<std::string>();
                        }
                        ++j;
                    }
                }
            }
        }
    } else if (table == "_abnormalityCotentData") {
        for (auto* name : {"codeName", "name", "clue"}) text(fields, row, name);
        auto it = row.find("storyList");
        if (it != row.end() && it->is_array()) {
            size_t i = 0;
            for (const auto& st : *it) {
                auto prefix = "storyList/" + std::to_string(i++) + "/";
                if (!st.is_object()) continue;
                text(fields, st, "story", prefix);
            }
        }
    } else if (table == "_passiveList" || table == "_bufList" || table == "_keywordList" || table == "_userTicket_L" || table == "_userTicket_R" || table == "_userTicket_EGOBg") {
        for (auto* name : {"name", "desc", "summary", "flavor"}) text(fields, row, name);
    } else text(fields, row, table == "_skillTagList" || table == "_egoGiftCategory" || table == "_announcer" ? "name" : "content");
    if (!fields.empty()) fields.insert(structure.begin(), structure.end());
    return fields;
}
inline Fields loaded_fields(const SkillEntry& skill) {
    Json row = {{"levelList", Json::array()}};
    for (const auto& level : skill.levelList) {
        Json value = {{"level", level.level}, {"name", level.name}, {"desc", level.desc}, {"flavor", level.flavor},
            {"rawDesc", level.rawDesc}, {"abName", level.abName}, {"coinlist", Json::array()}};
        for (const auto& coin : level.coinlist) {
            Json item = {{"coindescs", Json::array()}};
            for (const auto& desc : coin.coindescs) item["coindescs"].push_back({{"desc", desc.desc}, {"summary", desc.summary}});
            value["coinlist"].push_back(std::move(item));
        }
        row["levelList"].push_back(std::move(value));
    }
    return source_fields(row, "_skillList");
}
inline Fields loaded_fields(const PassiveEntry& row) {
    return source_fields(Json({{"name", row.name}, {"desc", row.desc}, {"summary", row.summary}, {"flavor", row.flavor}}), "_passiveList");
}
inline Fields loaded_fields(const BufEntry& row) {
    return source_fields(Json({{"name", row.name}, {"desc", row.desc}, {"summary", row.summary}, {"flavor", row.flavor}}), "_bufList");
}
inline Fields loaded_fields(const EgoGiftEntry& gift) {
    Json row = {{"name", gift.name}, {"desc", gift.desc}, {"abnormalityName", gift.abnormalityName}, {"simpleDesc", Json::array()}};
    for (const auto& ability : gift.simpleDesc) row["simpleDesc"].push_back({{"abilityID", ability.abilityID}, {"simpleDesc", ability.simpleDesc}});
    return source_fields(row, "_egoGiftData");
}
inline Fields loaded_fields(const EnemyEntry& row) {
    return source_fields(Json({{"name", row.name}, {"desc", row.desc}}), "_enemyList");
}
inline Fields loaded_fields(const AbnormalityEventEntry& row) {
    Json doc = {
        {"eventDesc", row.eventDesc},
        {"prevDesc", row.prevDesc},
        {"behaveDesc", row.behaveDesc},
        {"successDesc", row.successDesc},
        {"failureDesc", row.failureDesc}
    };
    if (!row.cancelDesc.empty()) doc["cancelDesc"] = row.cancelDesc;
    return source_fields(doc, "_abnormalityEventList");
}
inline Fields loaded_fields(const ActionEventEntry& row) {
    Json doc = {
        {"name", row.name},
        {"desc", row.desc},
        {"subDesc", row.subDesc},
        {"options", Json::array()}
    };
    for (const auto& opt : row.options) {
        doc["options"].push_back({
            {"message", opt.message},
            {"messageDesc", opt.messageDesc},
            {"result", opt.result}
        });
    }
    return source_fields(doc, "_actionEvents");
}
inline Fields loaded_fields(const AbnormalityGuideEntry& row) {
    Json doc = {
        {"codeName", row.codeName},
        {"name", row.name},
        {"clue", row.clue},
        {"storyList", Json::array()}
    };
    for (const auto& st : row.storyList) {
        doc["storyList"].push_back({
            {"level", st.level},
            {"story", st.story}
        });
    }
    return source_fields(doc, "_abnormalityCotentData");
}
inline std::string topic(const std::string& path) {
    auto name = path.substr(path.find_last_of("/\\") + 1);
    if (name == "SkillTag.json") return "_skillTagList";
    if (name == "BattleHint.json") return "_battleHint";
    if (name.rfind("EGOgift_", 0) == 0) return "_egoGiftData";
    if (name.rfind("StoryTheaterDanteNoteDetail", 0) == 0) return "_danteNoteDesc";
    if (name.rfind("StoryTheaterDanteNote", 0) == 0) return "_danteNoteCategoryKeyword";
    if (name == "MirrorDungeonEgoGiftLockedDesc.json") return "_mirrorDungeonEgoGiftLockedDescList";
    if (name == "MirrorDungeonTheme-1.json") return "_mirrorDungeonThemeList";
    if (path.find("/BattleAnnouncerDlg/") != std::string::npos || path.find("\\BattleAnnouncerDlg\\") != std::string::npos) return "_announcerVoiceText";
    if (path.find("/PersonalityVoiceDlg/") != std::string::npos || path.find("\\PersonalityVoiceDlg\\") != std::string::npos) return "_personalityVoiceText";
    if (path.find("/EGOVoiceDig/") != std::string::npos || path.find("\\EGOVoiceDig\\") != std::string::npos ||
        path.find("/EGOVoiceDlg/") != std::string::npos || path.find("\\EGOVoiceDlg\\") != std::string::npos) return "_egoVoiceText";
    if (name == "EgoGiftCategory.json") return "_egoGiftCategory";
    if (name.rfind("Bufs", 0) == 0) return "_bufList";
    if (name.rfind("BattleKeywords", 0) == 0) return "_keywordList";
    if (name.rfind("Skills", 0) == 0) return "_skillList";
    if (name.rfind("Passive", 0) == 0) return "_passiveList";
    if (name == "AnnouncerVoiceType.json") return "_announcerVoiceType";
    if (name.rfind("Announcer", 0) == 0 && name != "AnnouncerTicketUIPopup.json") return "_announcer";
    if (name.rfind("UserTicket-EGOBg", 0) == 0) return "_userTicket_EGOBg";
    if (name.rfind("UserTicket-L", 0) == 0) return "_userTicket_L";
    if (name.rfind("UserTicket-R", 0) == 0) return "_userTicket_R";
    if (name.rfind("ThreadDungeon", 0) == 0) return "_threadDungeon";
    if (name.rfind("DungeonName", 0) == 0) return "_dungeonName";
    if (name.rfind("StagePartText", 0) == 0) return "_stagePart";
    if (name.rfind("AttributeText", 0) == 0) return "_attributeList";
    if (name.rfind("DungeonStartBuffs", 0) == 0) return "_dungeonStartBuffs";
    if (name.rfind("PanicInfo", 0) == 0) return "_panicInfo";
    if (name.rfind("MentalCondition", 0) == 0 || name.rfind("reward-popup-text", 0) == 0) return "_mentalConditionList";
    if (name.rfind("DanteAbility", 0) == 0 && name.find("UIText") == std::string::npos) return "_danteAbilityDataList";
    if (name.rfind("UnlockCode", 0) == 0) return "_unlockCodeList";
    if (name.rfind("ProjectGSLessonName", 0) == 0) return "_projectGSLessonName";
    if (name.rfind("ProjectGSComboName", 0) == 0) return "_projectGSComboName";
    if (name.rfind("RailwayDungeonStationName", 0) == 0) return "_railwayDungeonStationName";
    if (name == "IntroduceCharacter.json") return "_introduceCharacter";
    if (name.rfind("BattlePass_Mission", 0) == 0) return "_battlePassMission";
    if (name == "Quest.json") return "_quest";
    if (name == "RailwayDungeon.json") return "_railwayDungeonText";
    if (name == "RailwayDungeonBuff.json") return "_railwayDungeonBuffText";
    if (name == "BattleHint_NormalBattle.json") return "_normalBattleResultHint";
    if (name == "BattleHint_AbnorBattle.json") return "_abBattleResultHint";
    if (name.rfind("LobbyBGM", 0) == 0 && name != "LobbyBGMCategory.json" && name != "LobbyBGMUI.json") return "<LobbyBGM>k__BackingField";
    if (name.rfind("BattleResultHint", 0) == 0) return "_battleResultHint";
    if (name.rfind("TutorialDesc", 0) == 0 || name == "TutorialRenewalDesc.json" ||
        name == "TutorialRenewalMainUIDesc.json" || name == "TutorialPopUpDesc.json" ||
        name == "TutorialNewUpperSkillInfoUI.json") return "_tutorialDesc";
    if (name.rfind("Items", 0) == 0) return "_itemList";
    if (name == "GachaTitle.json") return "_gachaTitle";
    if (name == "MirrorDungeonRentalName.json") return "_mirrorDungeonRentalName";
    if (name == "ErrorCodeMsg.json") return "_errorCodeMsgList";
    if (name.rfind("DungeonArea", 0) == 0) return "_dungeonArea";
    if (name == "IAPSticker.json") return "_iapStickerText";
    if (name.rfind("IAPProduct", 0) == 0) return "_iapProductText";
    if (name == "KeywordDictionary.json") return "_keywordDictionary";
    if (name.rfind("ScenarioModelCodes", 0) == 0) return "_scenarioModelCodes";
    if (name.rfind("BattleSpeechBubbleDlg", 0) == 0) return "_battleSpeechBubbleText";
    if (name.rfind("MirrorDungeonNode", 0) == 0 || name == "HellsChickenDungeonNode.json") return "_dungeonNodeText";
    if (name == "FileDownloadDesc.json") return "_fileDownloadDesc";
    if (name == "BuffAbilities.json") return "_buffAbilityList";
    if (name == "GachaNotice.json") return "_gachaNotice";
    if (name == "LobbyBGMCategory.json") return "<LobbyBGMCategory>k__BackingField";
    if (name.rfind("Enemies", 0) == 0 && name.rfind(".json") == name.size() - 5) return "_enemyList";
    if (name.rfind("AbDlg_", 0) == 0) return "_abnormalityEventCharDlg";
    if (name.rfind("AbEventsResultLog", 0) == 0 || name.rfind("ChoiceEventEffect", 0) == 0 ||
        name.rfind("ChoiceEventKeyword", 0) == 0 || name.rfind("ChoiceEventTarget", 0) == 0) return "_choiceEventResultDesc";
    if (name.rfind("AbEvents", 0) == 0 && name.find("ResultLog") == std::string::npos) return "_abnormalityEventList";
    if (name.rfind("ActionEvents", 0) == 0) return "_actionEvents";
    if (name.rfind("AbnormalityGuides", 0) == 0) return "_abnormalityCotentData";
    return {};
}
inline std::string source_id(const Json& row, const std::string& table) {
    auto id = row.find("id"); if (id == row.end()) return {};
    if (table == "_abnormalityEventCharDlg") {
        auto personality = row.find("personalityid");
        if (!id->is_number_integer() || personality == row.end() || !personality->is_number_integer()) return {};
        auto i = id->get<int64_t>(), p = personality->get<int64_t>();
        if (i <= 0 || i > INT32_MAX || p <= 0 || p > INT32_MAX) return {};
        return std::to_string(p) + "/" + std::to_string(i);
    }
    if (table == "_skillList" || table == "_passiveList" || table == "_egoGiftData" || table == "_mirrorDungeonEgoGiftLockedDescList" || table == "_mirrorDungeonThemeList" || table == "_announcer" || table == "_userTicket_L" || table == "_userTicket_R" || table == "_userTicket_EGOBg" || table == "_threadDungeon" || table == "_dungeonName" || table == "_panicInfo" || table == "_danteAbilityDataList" || table == "_unlockCodeList" || table == "_projectGSLessonName" || table == "_projectGSComboName" || table == "_railwayDungeonStationName" || table == "_quest" || table == "_railwayDungeonText" || table == "_normalBattleResultHint" || table == "_abBattleResultHint" || table == "<LobbyBGM>k__BackingField" || table == "_itemList" || table == "_mirrorDungeonRentalName" || table == "_gachaTitle" || table == "_iapStickerText" || table == "_iapProductText" || table == "_dungeonNodeText" || table == "_enemyList" || table == "_abnormalityEventList" || table == "_actionEvents" || table == "_abnormalityCotentData" || table == "_abnormalityEventCharDlg") {
        std::string raw;
        if (id->is_number_integer()) raw = id->dump();
        else if (id->is_string()) raw = id->get<std::string>();
        int value = 0; auto result = std::from_chars(raw.data(), raw.data() + raw.size(), value);
        if (result.ec != std::errc{} || result.ptr != raw.data() + raw.size() || value == 0) return {};
        return std::to_string(value);
    }
    if (!id->is_string()) return {};
    auto value = id->get<std::string>(); return value.find('\0') == value.npos ? value : std::string{};
}
struct SourceRow { std::set<std::string> signatures; bool has_text = false; };
struct File {
    std::string table, digest, error;
    bool accepted = false;
    size_t bytes = 0;
    size_t model_issues = 0;
    std::map<std::string, SourceRow> rows;
};
struct TableSource {
    size_t accepted_files = 0, rejected_files = 0, model_issues = 0;
    std::map<std::string, SourceRow> rows;
};
enum class RowState { Available, MissingLocal, EmptySource, MappingGap, SourceUncertain };
struct Row { RowState state; size_t writes = 0, failures = 0; };
struct Attempt {
    std::map<std::string, Row> rows;
    size_t writes = 0, failures = 0, invalid_rows = 0, unidentified_rows = 0;
    bool complete = false;
    Row* current_row = nullptr;
    std::map<std::string, size_t> failure_reasons;
    std::map<std::string, size_t> unsupported_fields;
    std::map<std::string, size_t> structure_issues;
    std::set<std::string> invalid_examples;
    size_t count(RowState state) const { size_t n = 0; for (auto& item : rows) n += item.second.state == state; return n; }
    bool untranslated_only() const {
        return complete && !rows.empty() && !failures && !invalid_rows && !unidentified_rows && structure_issues.empty() &&
            count(RowState::MissingLocal) + count(RowState::EmptySource) == rows.size();
    }
    bool operation_failed() const {
        // Individual structure mismatches (e.g. rebalanced skill coins in an update)
        // gracefully retain original text for that entry and are reported in status,
        // but must not cause the entire table (thousands of valid entries) to fail synchronization.
        if (failures || invalid_rows) return true;
        for (const auto& item : rows)
            if (item.second.state == RowState::Available && !item.second.writes) return true;
        return false;
    }
    Json json() const {
        Json examples = Json::array();
        size_t no_write = 0;
        for (const auto& item : rows) {
            auto state = item.second.state;
            if (state == RowState::Available && !item.second.writes && !item.second.failures) ++no_write;
            if ((state == RowState::MissingLocal || state == RowState::MappingGap || state == RowState::SourceUncertain) && examples.size() < 20)
                examples.push_back({{"id", item.first}, {"reason", state == RowState::MissingLocal ? "absent_from_validated_local_sources" : state == RowState::MappingGap ? "source_map_mismatch" : "source_evidence_unavailable"}});
        }
        return {{"assessment", complete ? "source_and_map_ids" : "write_counters_only"}, {"traversal_complete", complete},
            {"observed_unique_ids", rows.size()}, {"available_rows", count(RowState::Available)},
            {"missing_local_rows", count(RowState::MissingLocal)}, {"empty_source_rows", count(RowState::EmptySource)},
            {"source_map_mismatches", count(RowState::MappingGap)}, {"source_uncertain_rows", count(RowState::SourceUncertain)},
            {"available_rows_without_write", no_write}, {"successful_writes", writes}, {"failed_writes", failures},
            {"invalid_runtime_rows", invalid_rows}, {"write_failure_reasons", failure_reasons}, {"examples", examples},
            {"unidentified_runtime_rows", unidentified_rows},
            {"unsupported_source_fields", unsupported_fields}, {"invalid_id_examples", invalid_examples},
            {"structure_issues", structure_issues},
            {"scope", "observed_runtime_ids_and_successfully_read_local_files"}};
    }
};
struct Registry {
    std::map<std::string, File> files;
    std::map<std::string, TableSource> sources;
    std::map<std::string, std::map<std::string, std::string>> loaded;
    bool dirty = true;
    void source(const std::string& path, const std::string& bytes, const std::string& error = {}) {
        std::string table = topic(path), digest;
        if (!table.empty() && error.empty()) { BuildSha256 hash; hash.update(bytes.data(), bytes.size()); digest = hash.hex_digest(); }
        auto previous = files.find(path);
        if (previous != files.end() && previous->second.accepted && error.empty() && previous->second.digest == digest) return;
        File file; file.table = table; file.accepted = error.empty(); file.error = error; file.bytes = bytes.size(); file.digest = digest;
        if (!table.empty() && error.empty()) {
            auto doc = Json::parse(bytes, nullptr, false);
            if (!doc.is_object() || !doc.contains("dataList") || !doc["dataList"].is_array()) { file.accepted = false; file.error = "invalid_source_index_document"; }
            else for (const auto& row : doc["dataList"]) {
                if (!row.is_object()) { ++file.model_issues; continue; }
                auto id = source_id(row, table);
                if (id.empty()) {
                    auto value = row.find("id");
                    if (value == row.end() || (!(value->is_number_integer() && *value == 0) && !(value->is_string() && *value == "0"))) ++file.model_issues;
                    continue;
                }
                if (table == "_skillList" && (!row.contains("levelList") || !row["levelList"].is_array())) { ++file.model_issues; continue; }
                auto fields = source_fields(row, table); auto& entry = file.rows[id];
                entry.signatures.insert(fingerprint(fields)); entry.has_text = entry.has_text || !fields.empty();
            }
        }
        files[path] = std::move(file); dirty = true;
    }
    void rebuild() {
        if (!dirty) return;
        sources.clear();
        for (const auto& item : files) {
            const auto& file = item.second; if (file.table.empty()) continue;
            auto& table = sources[file.table];
            if (!file.accepted) { ++table.rejected_files; continue; }
            ++table.accepted_files;
            table.model_issues += file.model_issues;
            for (const auto& pair : file.rows) {
                auto& row = table.rows[pair.first]; row.has_text = row.has_text || pair.second.has_text;
                row.signatures.insert(pair.second.signatures.begin(), pair.second.signatures.end());
            }
        }
        dirty = false;
    }
    void loaded_row(const std::string& table, const std::string& id, const Fields& fields) { loaded[table][id] = fingerprint(fields); }
    RowState classify(const std::string& table, const std::string& id) {
        rebuild(); return classify_snapshot(table, id);
    }
    // Read-only diagnostics use the index prepared by source loading/patching.
    RowState classify_snapshot(const std::string& table, const std::string& id) const {
        if (dirty) return RowState::SourceUncertain;
        auto source = sources.find(table);
        if (source == sources.end()) return RowState::SourceUncertain;
        const auto& src = source->second; auto found = src.rows.find(id);
        if (found == src.rows.end()) return src.accepted_files && !src.rejected_files && !src.model_issues ? RowState::MissingLocal : RowState::SourceUncertain;
        auto map = loaded.find(table);
        const std::string* signature = nullptr;
        if (map != loaded.end()) {
            auto it = map->second.find(id);
            if (it != map->second.end()) signature = &it->second;
        }
        if (!found->second.has_text) {
            if (signature && *signature != fingerprint({})) return RowState::MappingGap;
            return RowState::EmptySource;
        }
        if (!signature || !found->second.signatures.count(*signature) || *signature == fingerprint({})) return RowState::MappingGap;
        return RowState::Available;
    }
    Json file_report() const {
        Json errors = Json::array(); size_t accepted = 0, rejected = 0, model_issue_files = 0;
        for (const auto& item : files) {
            if (item.second.accepted) ++accepted;
            else { ++rejected; if (errors.size() < 20) errors.push_back({{"file", item.first}, {"reason", item.second.error}}); }
            model_issue_files += item.second.model_issues > 0;
        }
        return {{"observed_unique_files", files.size()}, {"accepted_files", accepted}, {"rejected_files", rejected}, {"errors", errors},
            {"index_model_issue_files", model_issue_files},
            {"scope", "files_read_by_this_process_not_an_upstream_completeness_check"}};
    }
};
inline Registry registry;
inline thread_local Attempt* active = nullptr;
inline thread_local std::string active_table;
struct Scope {
    Attempt attempt;
    Attempt* previous;
    std::string previous_table;
    explicit Scope(const std::string& table) : previous(active), previous_table(active_table) { active = &attempt; active_table = table; }
    ~Scope() { active = previous; active_table = previous_table; }
};
inline void observe(const std::string& id) {
    if (!active) return;
    active->current_row = nullptr;
    if (id.empty()) return;
    auto inserted = active->rows.emplace(id, Row{registry.classify(active_table, id), 0, 0});
    active->current_row = &inserted.first->second;
}
inline void invalid_row(const std::string& detail = {}) {
    if (active) {
        ++active->invalid_rows; active->current_row = nullptr;
        if (!detail.empty() && active->invalid_examples.size() < 5) active->invalid_examples.insert(detail);
    }
}
inline void unsupported_field(const char* name) { if (active) ++active->unsupported_fields[name]; }
inline void structure_issue(const char* reason) { if (active) ++active->structure_issues[reason]; }
inline void unidentified_row() { if (active) { ++active->unidentified_rows; active->current_row = nullptr; } }
inline void complete() { if (active) active->complete = true; }
inline void write(bool success, const char* reason = "") {
    if (!active) return;
    if (success) { ++active->writes; if (active->current_row) ++active->current_row->writes; }
    else { ++active->failures; ++active->failure_reasons[reason]; if (active->current_row) ++active->current_row->failures; }
}
} // namespace translation_evidence
