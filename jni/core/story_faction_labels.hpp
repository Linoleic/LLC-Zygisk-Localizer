#pragma once
#include <string>
#include <unordered_map>

namespace story_faction {
// English labels verified against the same resource keys in the game export
// e38545d (x1bViolet/Limbus-Localization-Files) and LLC pack 2026110501.
// Only organization/faction labels are listed, not generic filter categories.
// Chinese comes from the active pack. No story, identity, or speaker IDs.
inline constexpr struct { const char* original; const char* key; } aliases[] = {
    {"Limbus Company", "formation_name_filter_Etc_LIMBUS_COMPANY"},
    {"Lobotomy Corp. HQ", "formation_name_filter_Etc_L_CORP"},
    {"H Corp.", "formation_name_filter_Etc_H_CORP"},
    {"N Corp.", "formation_name_filter_Etc_N_CORP"},
    {"R Corp.", "formation_name_filter_Etc_R_CORP"},
    {"T Corp.", "formation_name_filter_Etc_T_CORP"},
    {"W Corp.", "formation_name_filter_Etc_W_CORP"},
    {"Blade Lineage", "formation_name_filter_Etc_BLADE_LINEAGE"},
    {"Blade Lineage", "associationName_BLADE_LINEAGE"},
    {"Kurokumo Clan", "formation_name_filter_Etc_BLACK_CLOUD"},
    {"Kurokumo Clan", "associationName_BLACK_CLOUD"},
    {"Technology Liberation Alliance", "formation_name_filter_Etc_ATL"},
    {"Technology Liberation Alliance", "UnitKeyword_ATL"},
    {"Wuthering Heights", "formation_name_filter_Etc_WUTHERING_HEIGHTS"},
    {"The Pequod", "formation_name_filter_Etc_PEQUOD_CREW"},
    {"The Thumb", "formation_name_filter_Etc_THUMB_FINGER"},
    {"The Index", "formation_name_filter_Etc_INDEX_FINGER"},
    {"The Middle", "formation_name_filter_Etc_MIDDLE_FINGER"},
    {"The Ring", "formation_name_filter_Etc_RING_FINGER"},
    {"The Pinky", "formation_name_filter_Etc_LITTLE_FINGER"},
    {"The House of Spiders", "formation_name_filter_Etc_SPIDER_HOUSE"},
    {"Sisyphe", "formation_name_filter_Etc_SISYPHE"},
    {"Le Rouge", "formation_name_filter_Etc_LE_ROUGE"},
    {"Le Noir", "formation_name_filter_Etc_LE_NOIR"},
    {"Yurodiviye", "associationName_YURODIVY"},
};

inline std::string resolve(const std::string& original,
                           const std::unordered_map<std::string, std::string>& localized) {
    if (original.empty()) return {};
    std::string result;
    bool already_localized = false;
    for (const auto& alias : aliases) {
        const auto row = localized.find(alias.key);
        if (row == localized.end() || row->second.empty()) continue;
        if (row->second == original) already_localized = true;
        if (original != alias.original) continue;
        if (!result.empty() && result != row->second) return {};
        result = row->second;
    }
    // The scenario may already be patched while the current TMP cache is not.
    return result.empty() && already_localized ? original : result;
}
} // namespace story_faction
