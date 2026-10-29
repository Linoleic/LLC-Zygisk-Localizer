#include "ui_content.hpp"
#include <cassert>
#include <cstdio>

int main() {
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
