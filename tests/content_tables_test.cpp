#include "flat_content.hpp"
#include "scoped_content.hpp"
#include "skill_content.hpp"
#include "catalog_content.hpp"
#include "ui_content.hpp"
#include "content_readback.hpp"
#include "content_sync.hpp"
#include "string_table_patcher.hpp"
#include "text_schema_policy.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

// === Helpers from flat_content_test.cpp ===
using FlatMap = std::unordered_map<std::string, BufEntry>;

// BufEntry has no equality operator, so compare the observable fields explicitly.
static bool same_entries(const FlatMap& left, const FlatMap& right) {
    if (left.size() != right.size()) return false;
    for (const auto& item : left) {
        const auto found = right.find(item.first);
        if (found == right.end()) return false;
        const auto& a = item.second;
        const auto& b = found->second;
        if (a.id != b.id || a.name != b.name || a.desc != b.desc ||
            a.summary != b.summary || a.flavor != b.flavor) return false;
    }
    return true;
}

static bool rejects_without_change_checked(const std::string& source, const FlatMap& original) {
    FlatMap output = original;
    return !parse_flat_content_document(source, output) && same_entries(output, original);
}

// === Helpers from skill_content_test.cpp ===
namespace {

bool rejects_without_mutation(const std::string& source) {
    std::unordered_map<int, SkillEntry> output;
    SkillEntry existing;
    existing.id = 91;
    SkillLevel old_level;
    old_level.level = 4;
    old_level.name = "preserve";
    existing.levelList.push_back(old_level);
    output.emplace(91, existing);

    const bool accepted = parse_skill_document(source, output);
    assert(!accepted);
    assert(output.size() == 1);
    assert(output.at(91).id == 91);
    assert(output.at(91).levelList.size() == 1);
    assert(output.at(91).levelList.front().name == "preserve");
    return true;
}

}  // namespace

// === Helpers from catalog_content_test.cpp ===
namespace {

void rejects_gifts_without_mutation(const std::string& source) {
    std::unordered_map<std::string, EgoGiftEntry> output;
    EgoGiftEntry existing;
    existing.name = "keep";
    existing.simpleDesc.push_back({8, "existing ability"});
    output.emplace("existing", existing);

    assert(!catalog_content::gifts(source, output));
    assert(output.size() == 1);
    assert(output.at("existing").name == "keep");
    assert(output.at("existing").simpleDesc.size() == 1);
    assert(output.at("existing").simpleDesc.front().abilityID == 8);
    assert(output.at("existing").simpleDesc.front().simpleDesc == "existing ability");
}

void rejects_notes_without_mutation(const std::string& source) {
    std::unordered_map<std::string, DanteNoteEntry> output;
    DanteNoteEntry existing;
    existing.title = "keep title";
    existing.desc = "keep description";
    output.emplace("existing", existing);

    assert(!catalog_content::notes(source, output));
    assert(output.size() == 1);
    assert(output.at("existing").title == "keep title");
    assert(output.at("existing").desc == "keep description");
}

}  // namespace

// === Helpers from string_table_patcher_test.cpp ===
struct Field;
struct Il2CppClass { const char* name; Il2CppClass* parent; std::vector<Field*> fields; bool value_type = false; std::vector<MethodInfo*> methods; };
struct Il2CppType { Il2CppClass* klass; };
struct Field { uint32_t offset; const char* name; Il2CppType type; };
struct Il2CppObject { Il2CppClass* klass; void* monitor = nullptr; };
struct Il2CppString { Il2CppObject object; std::u16string text; };
struct Row { Il2CppObject object; Il2CppString* id; Il2CppString* text; };
struct Entry { Il2CppObject object; int32_t hash; int32_t padding = 0; Il2CppString* key; Il2CppObject* value; };
struct Array { Il2CppObject object; std::vector<Entry*> slots; };
struct Dictionary { Il2CppObject object; int32_t count; int32_t padding = 0; Il2CppObject* entries; };
struct Container { Il2CppObject object; Il2CppObject* dictionary; };
static std::unordered_map<uintptr_t, Il2CppObject*> handles;
static bool throw_get_value = false;
static Il2CppType* int_parameter;

static void test_flat_content() {
FlatMap output;
    const std::string valid =
        R"({"dataList":[{"name":"中文🍵","id":"first","flavor":"风味","summary":"摘要","desc":"描述"},)"
        R"({"id":"empty","name":"","desc":null,"summary":"","flavor":null}]})";
    assert(parse_flat_content_document(valid, output));
    assert(output.size() == 2);
    assert(output.at("first").id == "first");
    assert(output.at("first").name == "中文🍵");
    assert(output.at("first").desc == "描述");
    assert(output.at("first").summary == "摘要");
    assert(output.at("first").flavor == "风味");
    assert(output.at("empty").name.empty() && output.at("empty").desc.empty());
    assert(output.at("empty").summary.empty() && output.at("empty").flavor.empty());

    FlatMap independent_table;
    assert(parse_flat_content_document(R"({"dataList":[{"id":"first","name":"other table"}]})", independent_table));
    assert(independent_table.size() == 1 && independent_table.at("first").name == "other table");
    assert(output.at("first").name == "中文🍵");

    FlatMap merged;
    assert(parse_flat_content_document(R"({"dataList":[{"id":"retain","name":"old"},{"id":"replace","name":"before"}]})", merged));
    assert(parse_flat_content_document(R"({"dataList":[{"id":"added","name":"new"},{"id":"replace","name":"after"}]})", merged));
    assert(merged.size() == 3);
    assert(merged.at("retain").name == "old");
    assert(merged.at("added").name == "new");
    assert(merged.at("replace").name == "after");

    const FlatMap preserved = output;
    assert(rejects_without_change_checked(
        R"({"dataList":[{"id":"duplicate"},{"id":"duplicate","name":"second"}]})", preserved));
    assert(rejects_without_change_checked(R"({"dataList":[{"id":""}]})", preserved));
    assert(rejects_without_change_checked(R"({"dataList":[{"id":12}]})", preserved));
    assert(rejects_without_change_checked(R"({"dataList":[{"id":"bad","name":7}]})", preserved));
    assert(rejects_without_change_checked(R"({"dataList":[{"id":"bad","desc":false}]})", preserved));
    assert(rejects_without_change_checked(R"({"dataList":[{"id":"bad","summary":[]}]})", preserved));
    assert(rejects_without_change_checked(R"({"dataList":[{"id":"bad","flavor":{}}]})", preserved));
    assert(rejects_without_change_checked(R"({"dataList":[{"id":"bad","name":"x\u0000y"}]})", preserved));
    assert(rejects_without_change_checked(R"({"dataList":[{"id":"bad\u0000id"}]})", preserved));
    assert(rejects_without_change_checked(std::string("{\"dataList\":[]}") + '\0', preserved));
    assert(rejects_without_change_checked(R"({"dataList":[{"id":"good"},)" , preserved));
    assert(rejects_without_change_checked(R"({"dataList":{}})", preserved));

    FlatMap empty_rows = preserved;
    assert(parse_flat_content_document(R"({"dataList":[]})", empty_rows));
    assert(same_entries(empty_rows, preserved));

    return;
}

static void test_scoped_content() {
LyricsMap songs;
    DungeonMap dungeons;
    assert(parse_scoped_content(R"({"dataList":[{"content":"\u4e2d\u6587","id":0}]})", "BgmLyrics_A", true, songs, dungeons));
    assert(parse_scoped_content(R"({"dataList":[{"id":0,"content":"other song"}]})", "BgmLyrics_B", true, songs, dungeons));
    assert(songs.at("BgmLyrics_A").at("0") == "\xe4\xb8\xad\xe6\x96\x87");
    assert(songs.at("BgmLyrics_B").at("0") == "other song");
    assert(!parse_scoped_content("{invalid", "BgmLyrics_A", true, songs, dungeons));
    assert(songs.at("BgmLyrics_A").size() == 1);
    assert(parse_scoped_content(R"({"dataList":[
        {"stageList":[{"desc":"first","id":"10","title":"one"}],"id":10101},
        {"id":"10201","stageList":[{"id":10,"title":"two"},null,{"id":{},"title":"bad"}]}
    ]})", "", false, songs, dungeons));
    assert(dungeons.at("10101").at("10").title == "one");
    assert(dungeons.at("10101").at("10").desc == "first");
    assert(dungeons.at("10201").at("10").title == "two");
    assert(dungeons.at("10201").size() == 1);
    std::puts("scoped_content: song and dungeon ID isolation, zero IDs, Unicode, field order and invalid input passed");
}

static void test_skill_content() {
{
        std::unordered_map<int, SkillEntry> skills;
        const std::string source = R"({"dataList":[{"id":301,"levelList":[
            {"level":4,"name":"four","desc":"D4"},
            {"level":1,"desc":"D1","name":"one"},
            {"level":2,"name":"two","flavor":"F2"}
        ]}]})";
        assert(parse_skill_document(source, skills));
        assert(skills.size() == 1);
        const auto& levels = skills.at(301).levelList;
        assert(levels.size() == 3);
        assert(levels[0].level == 4 && levels[0].name == "four");
        assert(levels[1].level == 1 && levels[1].name == "one");
        assert(levels[2].level == 2 && levels[2].flavor == "F2");
    }
    {
        std::unordered_map<int, SkillEntry> skills;
        const std::string source = R"({"dataList":[{"levelList":[{"level":1,"coinlist":[
            {"coindescs":[{"desc":"nested only"}]}
        ],"name":"later top-level","desc":"top desc"},{"level":2,"name":"中文技能 🌟"}],"id":"-42"}]})";
        assert(parse_skill_document(source, skills));
        const auto& levels = skills.at(-42).levelList;
        assert(levels.size() == 2);
        assert(levels[0].name == "later top-level");
        assert(levels[0].desc == "top desc");
        assert(levels[0].level == 1);
        assert(levels[0].coinlist.size() == 1);
        assert(levels[0].coinlist[0].coindescs.size() == 1);
        assert(levels[0].coinlist[0].coindescs[0].desc == "nested only");
        assert(levels[1].name == "中文技能 🌟");
    }
    {
        std::unordered_map<int, SkillEntry> skills;
        assert(parse_skill_document(R"({"dataList":[{"id":7,"levelList":[{"level":2,"coinlist":[{}, {"coindescs":[{}]}]}]}]})", skills));
        const auto& coins = skills.at(7).levelList[0].coinlist;
        assert(coins.size() == 2);
        assert(coins[0].coindescs.empty());
        assert(coins[1].coindescs.size() == 1);
        assert(coins[1].coindescs[0].desc.empty());
        assert(coins[1].coindescs[0].summary.empty());
    }
    {
        std::unordered_map<int, SkillEntry> skills;
        assert(parse_skill_document(R"({"dataList":[{"id":0,"levelList":"ignored"},{"id":"12","levelList":[{"level":1}]}]})", skills));
        assert(skills.size() == 1 && skills.count(12) == 1);
    }

    rejects_without_mutation(R"({"dataList":[{"id":1,"levelList":[{"level":1},{"level":1}]}]})");
    rejects_without_mutation(R"({"dataList":[{"id":1,"levelList":[{"name":"missing level"}]}]})");
    rejects_without_mutation(R"({"dataList":[{"id":1,"levelList":[{"level":0}]}]})");
    rejects_without_mutation(R"({"dataList":[{"id":1,"levelList":[{"level":-1}]}]})");
    rejects_without_mutation(R"({"dataList":[{"id":1,"levelList":[{"level":2147483648}]}]})");
    rejects_without_mutation(R"({"dataList":[{"id":1,"levelList":[{"level":1,"name":false}]}]})");
    rejects_without_mutation(R"({"dataList":[{"id":1,"levelList":{}}]})");
    rejects_without_mutation(R"({"dataList":[{"id":1,"levelList":[{"level":1,"coinlist":{}}]}]})");
    rejects_without_mutation(R"({"dataList":[{"id":1,"levelList":[{"level":1,"coinlist":[null]}]}]})");
    rejects_without_mutation(R"({"dataList":[{"id":1,"levelList":[{"level":1,"coinlist":[{"coindescs":[false]}]}]}]})");
    rejects_without_mutation(R"({"dataList":[{"id":1,"levelList":[]},{"id":"1","levelList":[]}]})");
    rejects_without_mutation(R"({"dataList":[{"id":1.5,"levelList":[]}]})");
    rejects_without_mutation(R"({"dataList":[{"id":2147483648,"levelList":[]}]})");
    rejects_without_mutation(R"({"dataList":[{"id":1,"levelList":[{"level":1}]},)");
    rejects_without_mutation(R"({"dataList":{}})");

    {
        std::unordered_map<int, SkillEntry> skills;
        skills.emplace(44, SkillEntry{});
        std::string with_nul = R"({"dataList":[]})";
        with_nul.push_back('\0');
        assert(!parse_skill_document(with_nul, skills));
        assert(skills.size() == 1 && skills.count(44) == 1);
    }
    return;
}

static void test_catalog_content() {
{
        std::unordered_map<std::string, EgoGiftEntry> gifts;
        const std::string source = R"({"dataList":[
            {"simpleDesc":[{"simpleDesc":"能力乙一","abilityID":"22"},{"simpleDesc":"能力甲","abilityID":11},
                            {"simpleDesc":"能力乙二","abilityID":22},{}],
             "abnormalityName":"异常名","desc":"礼物说明","name":"礼物名称","id":123},
            {"id":"gift-B","name":"第二件","desc":"说明B","abnormalityName":""}
        ]})";
        assert(catalog_content::gifts(source, gifts));
        assert(gifts.size() == 2);
        const auto& gift = gifts.at("123");
        assert(gift.name == "礼物名称");
        assert(gift.desc == "礼物说明");
        assert(gift.abnormalityName == "异常名");
        assert(gift.simpleDesc.size() == 4);
        assert(gift.simpleDesc[0].abilityID == 22 && gift.simpleDesc[0].simpleDesc == "能力乙一");
        assert(gift.simpleDesc[1].abilityID == 11 && gift.simpleDesc[1].simpleDesc == "能力甲");
        assert(gift.simpleDesc[2].abilityID == 22 && gift.simpleDesc[2].simpleDesc == "能力乙二");
        assert(gift.simpleDesc[3].abilityID == 0 && gift.simpleDesc[3].simpleDesc.empty());
        assert(gifts.at("gift-B").name == "第二件");
        assert(gifts.at("gift-B").abnormalityName.empty());
        assert(gifts.at("gift-B").simpleDesc.empty());
    }
    {
        std::unordered_map<std::string, DanteNoteEntry> notes;
        const std::string source = R"({"dataList":[
            {"desc":"备忘录说明","title":"标题中文","id":7},
            {"id":"note-2","title":"第二条"},
            {"id":"note-empty"}
        ]})";
        assert(catalog_content::notes(source, notes));
        assert(notes.size() == 3);
        assert(notes.at("7").title == "标题中文");
        assert(notes.at("7").desc == "备忘录说明");
        assert(notes.at("note-2").title == "第二条");
        assert(notes.at("note-2").desc.empty());
        assert(notes.at("note-empty").title.empty());
        assert(notes.at("note-empty").desc.empty());
    }

    rejects_gifts_without_mutation(R"({"dataList":[{"id":1,"simpleDesc":[]},{"id":"1","simpleDesc":[]}]})");
    {
        std::unordered_map<std::string, EgoGiftEntry> gifts;
        assert(catalog_content::gifts(R"({"dataList":[{"id":1,"simpleDesc":[{"abilityID":5,"simpleDesc":"a"},{"abilityID":"5","simpleDesc":"b"}]}]})", gifts));
        const auto& entries = gifts.at("1").simpleDesc;
        assert(entries.size() == 2);
        assert(entries[0].abilityID == 5 && entries[0].simpleDesc == "a");
        assert(entries[1].abilityID == 5 && entries[1].simpleDesc == "b");
    }
    {
        std::unordered_map<std::string, EgoGiftEntry> gifts;
        assert(catalog_content::gifts(R"({"dataList":[{"id":1,"simpleDesc":[{}]}]})", gifts));
        const auto& entries = gifts.at("1").simpleDesc;
        assert(entries.size() == 1 && entries[0].abilityID == 0 && entries[0].simpleDesc.empty());
    }
    rejects_gifts_without_mutation(R"({"dataList":[{"id":1,"simpleDesc":[{"abilityID":6,"simpleDesc":"\u0000"}]}]})");
    rejects_gifts_without_mutation(R"({"dataList":[{"id":1,"name":false}]})");
    rejects_gifts_without_mutation(R"({"dataList":[{"id":1,"simpleDesc":{}}]})");
    rejects_gifts_without_mutation(R"({"dataList":[{"id":1,"simpleDesc":[null]}]})");
    rejects_gifts_without_mutation(R"({"dataList":[{"id":1,"simpleDesc":[{"abilityID":-1,"simpleDesc":"x"}]}]})");
    rejects_gifts_without_mutation(R"({"dataList":[{"id":1,"simpleDesc":[{"simpleDesc":"unidentified nonempty text"}]}]})");
    rejects_gifts_without_mutation(R"({"dataList":[{"id":1,"name":"truncated"},)");
    rejects_gifts_without_mutation(R"({"dataList":{}})");
    rejects_notes_without_mutation(R"({"dataList":[{"id":1,"title":"a"},{"id":"1","title":"b"}]})");
    rejects_notes_without_mutation(R"({"dataList":[{"id":1,"title":false}]})");
    rejects_notes_without_mutation(R"({"dataList":[{"id":"note","desc":"\u0000"}]})");
    rejects_notes_without_mutation(R"({"dataList":[{"id":1,"title":"truncated"},)");

    {
        std::unordered_map<std::string, EgoGiftEntry> gifts;
        gifts.emplace("old", EgoGiftEntry{});
        std::string source = R"({"dataList":[]})";
        source.push_back('\0');
        assert(!catalog_content::gifts(source, gifts));
        assert(gifts.size() == 1 && gifts.count("old") == 1);
    }
    {
        std::unordered_map<std::string, DanteNoteEntry> notes;
        notes.emplace("old", DanteNoteEntry{});
        std::string source = R"({"dataList":[]})";
        source.push_back('\0');
        assert(!catalog_content::notes(source, notes));
        assert(notes.size() == 1 && notes.count("old") == 1);
    }
    return;
}

static void test_ui_content() {
std::unordered_map<std::string, std::string> output = {{"keep", "previous"}};
    assert(!parse_ui_document(R"({"dataList":[{"id":"partial","content":"prefix"},)", output));
    assert(output.size() == 1 && output.at("keep") == "previous");
    assert(!parse_ui_document(R"({"dataList":{}})", output));
    assert(!parse_ui_document(std::string(R"({"dataList":[]})") + std::string("\0junk", 5), output));
    assert(parse_ui_document(R"({"dataList":[
        {"content":"\u4e2d\u6587 \ud83d\ude00","id":"unicode"},
        {"id":"quoted","content":"say \"hi\"\nnext"},
        {"id":"keep","content":""},{"id":"keep","content":null},
        {"id":7,"content":"numeric ignored"},{"id":"missing"},
        {"id":"nul","content":"bad\u0000text"},null,
        {"id":"duplicate","content":"first"},{"id":"duplicate","content":"last"}
    ]})", output));
    assert(output.at("unicode") == "\xe4\xb8\xad\xe6\x96\x87 \xf0\x9f\x98\x80");
    assert(output.at("quoted") == "say \"hi\"\nnext");
    assert(output.at("keep") == "previous" && output.at("duplicate") == "last");
    assert(!output.count("7") && !output.count("missing") && !output.count("nul"));
    std::unordered_map<std::string, std::string> tags;
    assert(parse_string_table_document(R"({"dataList":[{"name":"\u4f7f\u7528","id":"WhenUse"},{"id":"TabExplain","name":""}]})", tags, "name"));
    assert(tags.at("WhenUse") == "\xe4\xbd\xbf\xe7\x94\xa8" && !tags.count("TabExplain"));
    std::puts("ui_content: full-document rejection, Unicode, field order, empty fallback and valid duplicate selection passed");
}

static void test_content_readback_source() {
auto& registry = translation_evidence::registry;
    registry = {};
    registry.source("ChoiceEventEffect.json", R"({"dataList":[{"id":"Empty","content":""},{"id":"Available","content":"translated"},{"id":"MappingGap","content":"unloaded"}]})");
    registry.loaded_row("_choiceEventResultDesc", "Available", {{"content", "translated"}});
    registry.rebuild();
    const auto file_count = registry.files.size(), loaded_count = registry.loaded.size();
    content_readback::Check check;
    for (const auto* id : {"Empty", "Available", "MappingGap", "Missing"}) check.source("_choiceEventResultDesc", id);
    check.source("_unknown", "id");
    assert(check.empty == 1 && check.missing == 1 && check.mapping_gaps == 1 && check.uncertain == 1);
    assert(registry.files.size() == file_count && registry.loaded.size() == loaded_count && !registry.dirty);
    registry.source("ChoiceEventEffect-broken.json", "bad", "invalid_source");
    content_readback::Check dirty;
    dirty.source("_choiceEventResultDesc", "Empty");
    assert(dirty.uncertain == 1 && dirty.empty == 0 && registry.dirty);
    registry.rebuild();
    content_readback::Check rejected;
    rejected.source("_choiceEventResultDesc", "Missing");
    assert(rejected.uncertain == 1 && rejected.missing == 0);
    std::puts("content_readback: empty, missing, mapping and uncertain source classification is read-only");
}

static void test_content_sync() {
ContentSyncState personality;
    ContentSyncState skill;
    ContentStamp ready{1, 2, 3, 204, 1};
    ContentStamp late{4, 5, 0, 0, 0};
    assert(!personality.observe(ready, 0));
    assert(!skill.observe(late, 0));
    assert(personality.observe(ready, 1));
    personality.record(204, 612, 1);
    assert(personality.applied);
    assert(!skill.observe(late, 1));

    // The original failure: another table becomes ready after personality succeeded.
    late.entries = 6;
    late.count = 4476;
    late.version = 7;
    assert(!skill.observe(late, 2));
    assert(skill.observe(late, 3));
    skill.record(4474, 20000, 3);
    assert(skill.applied);
    assert(!skill.observe(late, 4));

    // Equal counts do not mean equal data: dictionary generations must be tracked.
    ++late.version;
    assert(!skill.observe(late, 5));
    assert(!skill.applied);
    assert(skill.observe(late, 6));
    skill.record(4474, 20000, 6);
    late.dictionary = 8;
    assert(!skill.observe(late, 7));
    assert(skill.observe(late, 8));

    // A matching ID with no valid text fields is not success and must retry.
    skill.record(4474, 0, 8);
    assert(!skill.applied);
    assert(!skill.observe(late, 9));
    assert(skill.observe(late, 12));
    skill.record(0, 0, 12);
    assert(!skill.observe(late, 13));
    assert(!skill.observe(late, 16));
    assert(skill.observe(late, 20));

    // Growing tables and cleared tables invalidate the previous applied snapshot.
    skill.record(4474, 20000, 20);
    ++late.count;
    assert(!skill.observe(late, 21));
    assert(!skill.applied);
    assert(skill.observe(late, 22));
    assert(!skill.observe({}, 23));
    assert(!skill.applied);
    assert(skill.attempts == 0);
    assert(!skill.observe(late, 24));
    assert(skill.observe(late, 25));
    ContentSyncState untranslated;
    assert(!untranslated.observe(late, 1));
    assert(untranslated.observe(late, 2));
    untranslated.record(0, 0, 2, true);
    assert(!untranslated.applied && untranslated.settled_untranslated);
    assert(!untranslated.observe(late, 1000));
    ++late.version;
    assert(!untranslated.observe(late, 1001));
    assert(untranslated.observe(late, 1002));
    untranslated.record(1, 1, 1002, false, true);
    assert(!untranslated.applied && !untranslated.settled_untranslated);
    assert(untranslated.observe(late, 1006));
    // An explicit validated empty dictionary is a stable observation, never an
    // applied translation. An absent or unverified zero-count table still waits.
    ContentSyncState optional;
    ContentStamp empty{100, 200, 0, 0, 1, true};
    assert(!optional.observe({100, 0, 0, 0, 1}, 0) && !optional.observed_empty);
    assert(!optional.observe({100, 200, 0, 0, 1}, 1) && !optional.observed_empty);
    assert(!optional.observe(empty, 2) && !optional.observed_empty);
    assert(!optional.observe(empty, 3) && optional.observed_empty && !optional.applied && optional.attempts==0);
    assert(!optional.observe(empty, 999) && optional.observed_empty);
    ++empty.version;
    assert(!optional.observe(empty, 1000) && !optional.observed_empty);
    assert(!optional.observe(empty, 1001) && optional.observed_empty);
    auto populated=empty; populated.count=2; populated.entries=300; populated.verified_empty=false;
    assert(!optional.observe(populated, 1002) && !optional.observed_empty);
    assert(optional.observe(populated, 1003));
    optional.record(0,0,1003,true); // New rows without local translations retain originals.
    assert(optional.settled_untranslated && !optional.applied);
    assert(!optional.observe(populated, 1004));
    ++populated.version; assert(!optional.observe(populated,1005));
    assert(optional.observe(populated,1006)); optional.record(2,2,1006);
    assert(optional.applied && !optional.observed_empty);
    assert(!optional.observe(empty,1007) && !optional.applied);
    assert(!optional.observe(empty,1008) && optional.observed_empty);
    assert(!optional.observe({},1009) && !optional.observed_empty);
    std::puts("content_sync: delayed readiness, replacement, mutation, retry and reset passed");
}

static void test_string_table_patcher() {
Il2CppClass integer{"Int32", nullptr, {}, true, {}}, text{"String", nullptr, {}, false, {}};
    Field row_id{16, "id", {&text}}, row_text{24, "name", {&text}}, numeric_text{24, "name", {&integer}};
    Il2CppClass row_type{"TextData_SkillTag", nullptr, {&row_id, &row_text}, false, {}};
    Il2CppClass wrong_field_type{"TextData_SkillTag", nullptr, {&row_id, &numeric_text}, false, {}};
    Field hash{16, "hashCode", {&integer}}, key{24, "key", {&text}}, value{32, "value", {&row_type}};
    Il2CppClass entry_type{"Entry", nullptr, {&hash, &key, &value}, true, {}};
    MethodInfo get_value{};
    Il2CppClass array_type{"Entry[]", nullptr, {}, false, {&get_value}};
    Field count{16, "_count", {&integer}}, entries{24, "_entries", {&array_type}};
    Il2CppClass dictionary_type{"Dictionary`2", nullptr, {&count, &entries}, false, {}};
    Field dic{16, "_dic", {&dictionary_type}};
    Il2CppClass container_type{"JsonDataList`1", nullptr, {&dic}, false, {}};
    Il2CppType parameter{&integer}; int_parameter = &parameter;
    il2cpp_object_get_class_fn = [](Il2CppObject* o) { return o->klass; };
    il2cpp_class_get_name_fn = [](Il2CppClass* k) { return k->name; };
    il2cpp_class_get_parent_fn = [](Il2CppClass* k) { return k->parent; };
    il2cpp_class_is_valuetype_fn = [](const Il2CppClass* k) { return k->value_type; };
    il2cpp_class_get_fields_fn = [](Il2CppClass* k, void** it) -> void* {
        size_t n = reinterpret_cast<uintptr_t>(*it); if (n >= k->fields.size()) return nullptr;
        *it = reinterpret_cast<void*>(n + 1); return k->fields[n];
    };
    il2cpp_field_get_flags_fn = [](void*) { return 0; };
    il2cpp_field_get_name_fn = [](void* f) { return static_cast<Field*>(f)->name; };
    il2cpp_field_get_offset_fn = [](void* f) { return static_cast<Field*>(f)->offset; };
    il2cpp_field_get_type_fn = [](void* f) -> const Il2CppType* { return &static_cast<Field*>(f)->type; };
    il2cpp_class_from_type_fn = [](const Il2CppType* t) { return t->klass; };
    il2cpp_field_get_value_fn = [](Il2CppObject* o, void* f, void* out) {
        auto* field = static_cast<Field*>(f);
        memcpy(out, reinterpret_cast<char*>(o) + field->offset, !strcmp(field->type.klass->name, "Int32") ? 4 : sizeof(void*));
    };
    il2cpp_string_length_fn = [](Il2CppString* s) { return static_cast<int32_t>(s->text.size()); };
    il2cpp_string_chars_fn = [](Il2CppString* s) { return reinterpret_cast<const uint16_t*>(s->text.c_str()); };
    il2cpp_string_new_fn = [](const char* s) { auto* r = new Il2CppString{}; while (*s) r->text.push_back(*s++); return r; };
    il2cpp_gc_wbarrier_set_field_fn = [](Il2CppObject*, void** address, void* v) { *address = v; };
    il2cpp_gchandle_new_fn = [](Il2CppObject* o, bool) { static uintptr_t next = uintptr_t{1} << 40; handles[++next] = o; return next; };
    il2cpp_gchandle_get_target_fn = [](uintptr_t h) { return handles.at(h); };
    il2cpp_gchandle_free_fn = [](uintptr_t h) { handles.erase(h); };
    il2cpp_array_length_fn = [](void* a) { return static_cast<uintptr_t>(static_cast<Array*>(a)->slots.size()); };
    il2cpp_class_get_methods_fn = [](Il2CppClass* k, void** it) -> const MethodInfo* {
        size_t n = reinterpret_cast<uintptr_t>(*it); if (n >= k->methods.size()) return nullptr;
        *it = reinterpret_cast<void*>(n + 1); return k->methods[n];
    };
    il2cpp_method_get_name_fn = [](const MethodInfo*) { return "GetValue"; };
    il2cpp_method_get_param_count_fn = [](const MethodInfo*) { return 1u; };
    il2cpp_method_get_param_fn = [](const MethodInfo*, uint32_t) -> const Il2CppType* { return int_parameter; };
    il2cpp_runtime_invoke_fn = [](const MethodInfo*, void* obj, void** args, Il2CppException** ex) -> Il2CppObject* {
        if (throw_get_value) { *ex = reinterpret_cast<Il2CppException*>(1); return nullptr; }
        return &static_cast<Array*>(obj)->slots.at(*static_cast<int32_t*>(args[0]))->object;
    };
    Il2CppString id{{&text}, u"WhenUse"}, other{{&text}, u"Other"}, original{{&text}, u"original"};
    Row row{{&row_type}, &id, &original};
    Entry entry{{&entry_type}, 10, 0, &id, &row.object};
    Entry deleted{{&entry_type}, -1, 0, &other, &row.object};
    Array array{{&array_type}, {&entry, &deleted}};
    Dictionary dictionary{{&dictionary_type}, 2, 0, &array.object};
    Container container{{&container_type}, &dictionary.object};
    auto snapshot = string_table::stamp(&container.object);
    assert(snapshot.count == 2 && snapshot.entries == reinterpret_cast<uintptr_t>(&array.object));
    dictionary.count = 3;
    assert(!string_table::stamp(&container.object).ready());
    dictionary.count = 2;
    std::unordered_map<std::string, std::string> map{{"WhenUse", "translated"}, {"Other", "wrong"}};
    auto patch = [&] { auto n = string_table::patch(&container.object, map, "TextData_SkillTag", "name"); assert(handles.empty()); return n; };
    assert(patch() == 1 && row.text->text == u"translated"); delete row.text; row.text = &original;
    assert(string_table::patch(&container.object, map, "TextData_UI", "name") == 0 && handles.empty());
    map["WhenUse"] = ""; assert(patch() == 0 && row.text == &original); map["WhenUse"] = "translated";
    row.id = &other; assert(patch() == 0 && row.text == &original); row.id = &id;
    row.object.klass = &wrong_field_type; assert(patch() == 0 && row.text == &original); row.object.klass = &row_type;
    dictionary.count = 3; assert(patch() == 0); dictionary.count = 2;
    throw_get_value = true; assert(patch() == 0); throw_get_value = false;
    array_type.methods.clear(); assert(patch() == 0); array_type.methods.push_back(&get_value);
    il2cpp_gchandle_new_fn = nullptr; assert(patch() == 0);
    std::puts("string_table_patcher: boxed traversal, identity/type guards, deleted slots, bounds, exceptions and root cleanup passed");
}

static void test_text_schema() {
using namespace text_schema_policy;
Json first = {{"status", "complete"}, {"native_sha256", "old"}, {"catalog_signature", "catalog"},
        {"containers", Json::array({{{"name", "_known"}, {"type", "TextDataDictionary_A"}, {"offset", 16}, {"registered", true}}})},
        {"classes", Json::array({{{"name", "TextData_A"}, {"fields", Json::array({{{"name", "name"}, {"type", "System.String"}}})}}})}};
    auto game = Json({{"version", "1"}});
    auto baseline = advance(nullptr, first, game);
    assert(baseline["first_baseline"] == true && baseline["delta"].is_null());
    assert(advance(baseline, first, game)["first_baseline"] == true);
    assert(advance(baseline, first, Json({{"version", "2"}}))["first_baseline"] == false);
    auto second = first; second["native_sha256"] = "new";
    second["containers"][0]["type"] = "TextDataDictionary_A2";
    second["containers"].push_back({{"name", "_new"}, {"type", "TextDataDictionary_New"}, {"offset", 24}, {"registered", false}});
    second["classes"].push_back({{"name", "TextData_New"}, {"fields", Json::array()}});
    auto updated = advance(baseline, second, game);
    assert(updated["first_baseline"] == false);
    assert(updated["delta"]["added_containers"].size() == 1 && updated["delta"]["changed_containers"].size() == 1);
    assert(updated["delta"]["added_classes"].size() == 1);
    assert(advance(updated, second, game)["delta"] == updated["delta"]);
    auto registered = second; registered["containers"][1]["registered"] = true;
    assert(difference(second, registered)["changed_containers"].empty());
    auto partial = second; partial["status"] = "partial";
    bool refused = false; try { advance(updated, partial, game); } catch (...) { refused = true; }
    assert(refused && updated["current"] == second);
    assert(candidate_type("TextDataDictionary_New") && !candidate_type("System.String"));
    assert(registered_field({"_stageChapterList"}, "_stageChapter"));
    assert(registered_field({"_storyTheaterList"}, "_storyTheater"));
    assert(registered_field({"_fieldText"}, "_field"));
    assert(registered_field({"_known"}, "_KNOWN"));
    assert(!registered_field({"_knownList"}, "_unrelated"));
    assert(candidate_field({"_stageChapterList"}, "_stageChapter", "RenamedContainer"));
    assert(!candidate_field({"_knownList"}, "_unrelated", "System.Int32"));
}

int main() {
    test_flat_content();
    test_scoped_content();
    test_skill_content();
    test_catalog_content();
    test_ui_content();
    test_content_readback_source();
    test_content_sync();
    test_string_table_patcher();
    test_text_schema();
    std::puts("All content tables tests passed successfully.");
    return 0;
}
