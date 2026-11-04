#include "content_readback.hpp"
#include <cassert>
#include <cstdio>

int main() {
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
