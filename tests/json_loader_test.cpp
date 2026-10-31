#include "json_loader.hpp"
#include "storage.hpp"
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
int main() {
    auto root = fs::temp_directory_path() / ("llc-loader-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    assert(fs::create_directory(root));
    auto write = [&](const fs::path& name, const std::string& bytes) {
        std::ofstream file(root / name, std::ios::binary); file.write(bytes.data(), bytes.size());
    };
    g_ui_map["keep"] = "previous";
    write("UI_good.json", R"({"dataList":[{"content":"\u4e2d\u6587","id":"good"},{"id":"keep","content":""}]})");
    write("_llc_manifest.json", R"({"schema":1,"files":[],"dataList":[{"id":"manifest_only","content":"not translation data"}]})");
    write("UI_half.json", R"({"dataList":[{"id":"keep","content":"bad"},{"id":"partial","content":"must not commit"},)");
    write("Personalities.json", R"({"dataList":[{"id":101,"name":"personality"}]})");
    write("Personalities_half.json", R"({"dataList":[{"id":101,"name":"bad"},{"id":102,"name":"partial"}])");
    write("Skills.json", R"({"dataList":[{"id":301,"levelList":[{"level":1,"name":"skill","desc":"description"}]}]})");
    write("Passive_half.json", R"({"dataList":[{"id":401,"name":"partial"}])");
    write("SkillTag.json", R"({"dataList":[{"name":"[translated tag]","id":"WhenUse"},{"id":"empty","name":""}]})");
    write("BattleHint.json", R"({"dataList":[{"id":"1","content":"specific hint"}]})");
    write("EGOgift_Test.json", R"({"dataList":[{"id":9001,"name":"gift","desc":"gift desc","simpleDesc":[{"abilityID":2,"simpleDesc":"second"},{"abilityID":1,"simpleDesc":"first"}]}]})");
    write("StoryTheaterDanteNoteDetail_Test.json", R"({"dataList":[{"id":"Note1","title":"note title","desc":"note body"}]})");
    write("StoryTheaterDanteNote_Test.json", R"({"dataList":[{"id":"NoteCategory","content":"note category"}]})");
    write("EgoGiftCategory.json", R"({"dataList":[{"id":"Combustion","name":"gift category"}]})");
    write("MirrorDungeonEgoGiftLockedDesc.json", R"({"dataList":[{"id":9088,"content":"gift unlock hint"}]})");
    write("Bufs_test.json", R"({"dataList":[{"desc":"buf desc","summary":"buf summary","id":"shared","name":"buf name"}]})");
    write("Bufs_more.json", R"({"dataList":[{"id":"another","name":"additional buf"}]})");
    write("BattleKeywords_test.json", R"({"dataList":[{"desc":"keyword desc","id":"shared","name":"keyword name"}]})");
    write("Announcer.json", R"({"dataList":[{"id":1,"name":"Dante"},{"id":41,"name":"\uff1f\uff1f"}]})");
    write("Announcer-a1c9p3.json", R"({"dataList":[{"id":41,"name":"Araya"}]})");
    write("AnnouncerVoiceType.json", R"({"dataList":[{"id":"Danger","content":"Danger Warning"}]})");
    write("UserTicket-L.json", R"({"dataList":[{"id":-1,"name":"LC Base L","desc":"Base desc L"}]})");
    write("UserTicket-R.json", R"({"dataList":[{"id":1,"name":"Ticket R","desc":"Desc R"}]})");
    write("UserTicket-EGOBg.json", R"({"dataList":[{"id":80,"name":"EGOBg 80","desc":"Desc 80"}]})");
    write("ThreadDungeon.json", R"({"dataList":[{"id":1,"name":"Thread Dungeon 1"}]})");
    write("DungeonName_Event.json", R"({"dataList":[{"id":910301,"name":"Dungeon 910301"}]})");
    write("StagePartText.json", R"({"dataList":[{"id":"part_1","parttitle":"Part 1"}]})");
    write("AttributeText.json", R"({"dataList":[{"id":"CRIMSON","name":"\u66b4\u6012"}]})");
    write("DungeonStartBuffs.json", R"({"dataList":[{"id":"START_BUFF_1","description":"Buff 1"}]})");
    write("DungeonStartBuffs_MD7.json", R"({"dataList":[{"id":"START_BUFF_1","description":"Buff 1 MD7"},{"id":"START_BUFF_2","description":"Buff 2"}]})");
    write("PanicInfo.json", R"({"dataList":[{"id":1001,"panicName":"\u81ea\u6b8b","lowMoraleDescription":"Low Morale 1001","panicDescription":"Panic 1001","flavor":"Flavor 1001"}]})");
    fs::create_directory(root / "BgmLyrics");
    write("BgmLyrics/BgmLyrics_Test.json", R"({"dataList":[{"id":0,"content":"lyric"}]})");
    write("BgmLyrics/BgmLyrics_Half.json", R"({"dataList":[{"id":0,"content":"partial"}])");
    load_all_ui_jsons(root.string().c_str());
    g_ui_map["1"] = "unrelated UI string";
    load_all_content_jsons(root.string().c_str());
    assert(g_ui_map.at("keep") == "previous" && g_ui_map.at("good") == "\xe4\xb8\xad\xe6\x96\x87");
    assert(!g_ui_map.count("partial"));
    assert(!g_ui_map.count("manifest_only"));
    assert(g_personalities.at(101).name == "personality" && !g_personalities.count(102));
    assert(g_skills.at(301).levelList.at(0).name == "skill" && !g_passives.count(401));
    assert(g_skill_tags.at("WhenUse") == "[translated tag]" && !g_skill_tags.count("empty"));
    assert(g_battle_hints.at("1") == "specific hint");
    assert(g_ui_map.at("1") == "unrelated UI string");
    assert(g_bufs.at("shared").summary=="buf summary" && g_bufs.at("another").name=="additional buf");
    assert(g_battle_keywords.at("shared").name=="keyword name" && g_bufs.at("shared").name=="buf name");
    assert(g_ego_gifts.at("9001").desc == "gift desc" && g_ego_gifts.at("9001").simpleDesc.at(0).abilityID == 2);
    assert(g_dante_notes.at("Note1").desc == "note body");
    assert(g_dante_note_categories.at("NoteCategory") == "note category");
    assert(g_ego_gift_categories.at("Combustion") == "gift category");
    assert(g_ego_gift_locked.at("9088") == "gift unlock hint");
    assert(g_announcers.at("1") == "Dante");
    assert(g_announcers.at("41") == "Araya");
    assert(g_announcer_voice_types.at("Danger") == "Danger Warning");
    assert(g_user_ticket_l_map.at("-1").name == "LC Base L" && g_user_ticket_l_map.at("-1").desc == "Base desc L");
    assert(g_user_ticket_r_map.at("1").name == "Ticket R" && g_user_ticket_r_map.at("1").desc == "Desc R");
    assert(g_user_ticket_egobg_map.at("80").name == "EGOBg 80" && g_user_ticket_egobg_map.at("80").desc == "Desc 80");
    assert(g_thread_dungeons.at("1") == "Thread Dungeon 1");
    assert(g_dungeon_names.at("910301") == "Dungeon 910301");
    assert(g_stage_parts.at("part_1") == "Part 1");
    assert(g_attribute_list.at("CRIMSON") == "\xe6\x9a\xb4\xe6\x80\x92");
    assert(g_dungeon_start_buffs.at("START_BUFF_1") == "Buff 1 MD7");
    assert(g_dungeon_start_buffs.at("START_BUFF_2") == "Buff 2");
    assert(g_panic_infos.at("1001").panic_name == "\xe8\x87\xaa\xe6\xae\x8b" && g_panic_infos.at("1001").flavor == "Flavor 1001");
    assert(!g_theater_map.count("Note1") && !g_theater_map.count("NoteCategory"));
    assert(g_bgm_lyrics.at("BgmLyrics_Test").at("0") == "lyric" && !g_bgm_lyrics.count("BgmLyrics_Half"));
    std::unordered_map<int, ScenarioDialogue> story;
    write("Story.json", R"({"dataList":[{"id":1,"content":"story"}]})");
    parse_scenario_json_file((root / "Story.json").string(), story);
    assert(story.at(1).content == "story");
    write("Story.json", R"({"dataList":[{"id":1,"content":"partial overwrite"}])");
    parse_scenario_json_file((root / "Story.json").string(), story);
    assert(story.size() == 1 && story.at(1).content == "story");
    write("Story.json", R"({"dataList":[{"id":1,"content":"restored complete"}]})");
    parse_scenario_json_file((root / "Story.json").string(), story);
    assert(story.at(1).content == "restored complete");
    for (const auto& entry : fs::directory_iterator(root / "BgmLyrics")) fs::remove(entry.path());
    fs::remove(root / "BgmLyrics");
    for (const auto& entry : fs::directory_iterator(root)) fs::remove(entry.path());
    fs::remove(root);
    std::puts("json_loader: real loaders reject partial files without overwriting valid maps; complete restore passed");
}
