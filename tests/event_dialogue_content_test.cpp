#include "event_dialogue_content.hpp"
#include "translation_evidence.hpp"
#include <cassert>

int main() {
    std::unordered_map<std::string, EventDialogueEntry> map;
    std::string source = R"({"dataList":[{"id":40,"personalityid":10301,"dialog":"\u82f1\u96c4"},{"id":41,"personalityid":10301,"dialog":null}]})";
    assert(parse_event_dialogue_document(source, map));
    assert(map.size() == 2 && map.at("10301/40").personality_id == 10301 && map.at("10301/40").dialog == "英雄");
    assert(map.at("10301/41").dialog.empty());
    for (auto invalid : {
        R"({"dataList":[{"id":40,"personalityid":10301,"dialog":"partial"},)",
        R"({"dataList":[{"id":40,"personalityid":10301},{"id":40,"personalityid":10301}]})",
        R"({"dataList":[{"id":42,"personalityid":-1,"dialog":"bad"}]})",
        R"({"dataList":[{"id":42,"personalityid":"10301","dialog":"bad"}]})",
        R"({"dataList":[{"id":42,"personalityid":10301,"dialog":3}]})",
        R"({"dataList":[{"id":42,"personalityid":10301,"dialog":"bad\u0000text"}]})"
    }) {
        assert(!parse_event_dialogue_document(invalid, map));
        assert(map.size() == 2 && map.at("10301/40").dialog == "英雄");
    }
    auto& evidence = translation_evidence::registry;
    evidence = {};
    evidence.source("/test/AbDlg_DonQuixote.json", source);
    evidence.loaded_row("_abnormalityEventCharDlg", "10301/40", {{"dialog", map.at("10301/40").dialog}});
    evidence.loaded_row("_abnormalityEventCharDlg", "10301/41", {});
    evidence.rebuild();
    assert(evidence.classify("_abnormalityEventCharDlg", "10301/40") == translation_evidence::RowState::Available);
    assert(evidence.classify("_abnormalityEventCharDlg", "10301/41") == translation_evidence::RowState::EmptySource);
    assert(translation_evidence::topic("/test/ChoiceEventEffect.json") == "_choiceEventResultDesc");
    assert(translation_evidence::topic("/test/AbEventsResultLog_Mirror.json") == "_choiceEventResultDesc");
    assert(parse_event_dialogue_document(R"({"dataList":[{"id":40,"personalityid":10201,"dialog":"different sinner"}]})", map));
    assert(map.size() == 3 && map.at("10301/40").dialog == "英雄" && map.at("10201/40").dialog == "different sinner");
    auto fields = translation_evidence::source_fields({{"content", "获得{reward} × {stack}"}}, "_choiceEventResultDesc");
    assert(fields.at("content") == "获得{reward} × {stack}");
}
