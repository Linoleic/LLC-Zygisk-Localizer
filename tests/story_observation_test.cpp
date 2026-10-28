#include "story_observation.hpp"
#include <cassert>
#include <cstdio>
int main() {
    assert(safe_story_id("S001B"));
    assert(safe_story_id("a_b-9"));
    assert(!safe_story_id("../S001B"));
    assert(!safe_story_id("S001B/other"));
    assert(!safe_story_id("") && !safe_story_id(std::string(128, 'a')));
    const uint16_t text[] = {0x4e2d, 0x6587, '\n', 0xd83d, 0xde00, 0, 'x'};
    assert(observed_utf8(text, 7) == std::string("\xe4\xb8\xad\xe6\x96\x87\n\xf0\x9f\x98\x80\0x", 13));
    const uint16_t invalid[] = {0xd800, 'a', 0xdc00};
    assert(observed_utf8(invalid, 3) == "\xef\xbf\xbd" "a" "\xef\xbf\xbd");
    assert(observed_utf8(nullptr, 0).empty());
    std::puts("story_observation: bounded UTF-16 conversion, surrogate handling and safe story IDs passed");
}
