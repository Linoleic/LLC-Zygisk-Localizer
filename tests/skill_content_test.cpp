#include "../jni/core/skill_content.hpp"

#include <cassert>
#include <string>
#include <unordered_map>

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

int main() {
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
    return 0;
}
