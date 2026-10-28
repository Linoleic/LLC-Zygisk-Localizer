#include "scoped_content.hpp"
#include <cassert>
#include <cstdio>

int main() {
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
