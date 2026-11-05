#include "text_schema_policy.hpp"
#include <cassert>
using namespace text_schema_policy;
int main() {
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
