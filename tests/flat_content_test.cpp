#include "flat_content.hpp"

#include <cassert>
#include <string>
#include <unordered_map>

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

int main() {
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

    return 0;
}
