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
    assert(g_ego_gifts.at("9001").desc == "gift desc" && g_ego_gifts.at("9001").simpleDesc.at(0).abilityID == 2);
    assert(g_dante_notes.at("Note1").desc == "note body");
    assert(g_dante_note_categories.at("NoteCategory") == "note category");
    assert(g_ego_gift_categories.at("Combustion") == "gift category");
    assert(g_ego_gift_locked.at("9088") == "gift unlock hint");
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
