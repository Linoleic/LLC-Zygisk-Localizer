#pragma once
#include "json_models.hpp"

namespace content_alignment {
inline size_t ability_source_count(const EgoGiftEntry& gift, int id) {
    size_t count = 0;
    for (const auto& row : gift.simpleDesc) if (row.abilityID == id) ++count;
    return count;
}
inline const SkillLevel* level(const SkillEntry& skill, int value) {
    if (value <= 0) return nullptr;
    const SkillLevel* found = nullptr;
    for (const auto& row : skill.levelList) if (row.level == value) { if (found) return nullptr; found = &row; }
    return found;
}
inline const EgoGiftAbilityEntry* ability(const EgoGiftEntry& gift, int id, size_t occurrence, size_t runtime_count) {
    if (id <= 0) return nullptr;
    const EgoGiftAbilityEntry* found = nullptr; size_t count = 0;
    for (const auto& row : gift.simpleDesc) if (row.abilityID == id) {
        if (count == occurrence) found = &row;
        ++count;
    }
    return count == runtime_count ? found : nullptr;
}
} // namespace content_alignment
