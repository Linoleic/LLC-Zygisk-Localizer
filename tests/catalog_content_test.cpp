#include "../jni/core/catalog_content.hpp"

#include <cassert>
#include <string>
#include <unordered_map>

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

int main() {
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
    return 0;
}
