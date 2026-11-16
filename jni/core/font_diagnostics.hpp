#pragma once
#include <string>
#include <vector>

void dump_all_fonts_status(const char* output_file = nullptr);
void dump_active_tmp_texts(const char* output_file = nullptr);

struct VisibleStoryHistory {
    std::vector<std::string> contents;
    std::vector<std::string> speakers;
};
VisibleStoryHistory read_visible_story_history();
