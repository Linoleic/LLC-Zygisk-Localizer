#pragma once
#include "string_table_patcher.hpp"
#include "content_alignment.hpp"
#include "storage.hpp"
#include <set>

namespace structured_content {
using string_table::Root;
struct List {
    Root array;
    int32_t size = 0;
    bool valid = false;
    explicit List(Il2CppObject* list) : array(string_table::reference(list, "_items")) {
        valid = list && !strcmp(il2cpp_class_get_name_fn(il2cpp_object_get_class_fn(list)), "List`1") &&
            string_table::integer(list, "_size", size) && size >= 0 && size <= 500000 && array.get() &&
            static_cast<uintptr_t>(size) <= il2cpp_array_length_fn(array.get());
    }
    Il2CppObject* at(int32_t index) const {
        return valid && index >= 0 && index < size ? reinterpret_cast<Il2CppObject**>(reinterpret_cast<char*>(array.get()) + 4 * sizeof(void*))[index] : nullptr;
    }
};
inline bool expected(Il2CppObject* object, const char* name) {
    return object && !strcmp(il2cpp_class_get_name_fn(il2cpp_object_get_class_fn(object)), name);
}
inline void field(Root& row, const char* name, const std::string& text) {
    if (!text.empty()) write_managed_text_named(row.get(), name, text);
}
inline bool skill_level(Il2CppObject* object, const SkillLevel& source) {
    Root level(object);
    field(level, "name", source.name); field(level, "desc", source.desc); field(level, "flavor", source.flavor);
    field(level, "rawDesc", source.rawDesc); field(level, "abName", source.abName);
    auto* coin_list = read_named_managed_list(level.get(), "coinlist");
    if (!coin_list && source.coinlist.empty()) return true;
    List coins(coin_list);
    if (!coins.valid || static_cast<size_t>(coins.size) != source.coinlist.size()) {
        translation_evidence::structure_issue("skill_coin_count_mismatch"); return false;
    }
    // Anonymous descendants have no semantic key. Validate the entire subtree
    // before writing any of its rows; never zip unequal prefixes together.
    for (int c = 0; c < coins.size; ++c) {
        if (!expected(coins.at(c), "TextData_Skill_Coins")) { translation_evidence::structure_issue("skill_coin_class_mismatch"); return false; }
        auto* descriptions = read_named_managed_list(coins.at(c), "coindescs");
        if (!descriptions && source.coinlist[c].coindescs.empty()) continue;
        List descs(descriptions);
        if (!descs.valid || static_cast<size_t>(descs.size) != source.coinlist[c].coindescs.size()) {
            translation_evidence::structure_issue("skill_description_count_mismatch"); return false;
        }
        for (int d = 0; d < descs.size; ++d) if (!expected(descs.at(d), "TextData_Skill_CoinDesc")) {
            translation_evidence::structure_issue("skill_description_class_mismatch"); return false;
        }
    }
    for (int c = 0; c < coins.size; ++c) {
        List descs(read_named_managed_list(coins.at(c), "coindescs"));
        if (!descs.valid && source.coinlist[c].coindescs.empty()) continue;
        for (int d = 0; d < descs.size; ++d) {
            Root row(descs.at(d)); const auto& text = source.coinlist[c].coindescs[d];
            field(row, "desc", text.desc); field(row, "summary", text.summary);
        }
    }
    return true;
}
inline int skills(Il2CppObject* container) {
    return string_table::visit(container, "TextData_Skill", [&](Il2CppObject* object, const std::string& key) {
        int id = 0; auto parsed = std::from_chars(key.data(), key.data() + key.size(), id);
        if (parsed.ec != std::errc{} || parsed.ptr != key.data() + key.size()) return 0;
        auto source = g_skills.find(id); if (source == g_skills.end()) return 0;
        List levels(read_named_managed_list(object, "levelList"));
        if (!levels.valid) { translation_evidence::structure_issue("skill_levels_unavailable"); return 1; }
        std::map<int, size_t> counts;
        for (int i = 0; i < levels.size; ++i) {
            int32_t level = 0;
            if (!expected(levels.at(i), "TextData_SkillPerLevel") || !string_table::integer(levels.at(i), "level", level) || level <= 0) {
                translation_evidence::structure_issue("skill_level_identity_invalid"); continue;
            }
            ++counts[level];
        }
        for (int i = 0; i < levels.size; ++i) {
            int32_t value = 0;
            if (!expected(levels.at(i), "TextData_SkillPerLevel") || !string_table::integer(levels.at(i), "level", value) || value <= 0) continue;
            auto* matched = content_alignment::level(source->second, value);
            if (counts[value] != 1 || !matched) { translation_evidence::structure_issue("skill_level_missing_or_ambiguous"); continue; }
            skill_level(levels.at(i), *matched);
        }
        return 1;
    }, true);
}
inline int gifts(Il2CppObject* container) {
    return string_table::visit(container, "TextData_EgoGift", [&](Il2CppObject* object, const std::string& key) {
        auto source = g_ego_gifts.find(key); if (source == g_ego_gifts.end()) return 0;
        Root row(object); const auto& gift = source->second;
        field(row, "name", gift.name); field(row, "desc", gift.desc); field(row, "abnormalityName", gift.abnormalityName);
        auto* list = read_named_managed_list(row.get(), "simpleDesc");
        if (!list && gift.simpleDesc.empty()) return 1;
        List abilities(list);
        if (!abilities.valid) { translation_evidence::structure_issue("gift_abilities_unavailable"); return 1; }
        std::map<int, size_t> counts, occurrences;
        for (int i = 0; i < abilities.size; ++i) {
            int32_t id = 0;
            if (!expected(abilities.at(i), "TextData_EgoGiftAbility") || !string_table::integer(abilities.at(i), "abilityID", id)) {
                translation_evidence::structure_issue("gift_ability_identity_invalid"); continue;
            }
            if (id > 0) ++counts[id];
        }
        for (int i = 0; i < abilities.size; ++i) {
            int32_t id = 0;
            if (!expected(abilities.at(i), "TextData_EgoGiftAbility") || !string_table::integer(abilities.at(i), "abilityID", id) || id <= 0) continue;
            auto* text = content_alignment::ability(gift, id, occurrences[id]++, counts[id]);
            if (!text) { translation_evidence::structure_issue("gift_ability_missing_or_ambiguous"); continue; }
            Root ability(abilities.at(i)); field(ability, "simpleDesc", text->simpleDesc);
        }
        return 1;
    });
}
inline int flat(Il2CppObject* container, bool keywords) {
    const auto& translations = keywords ? g_battle_keywords : g_bufs;
    return string_table::visit(container, keywords ? "TextData_BattleKeywords" : "TextData_Buf",
        [&](Il2CppObject* object, const std::string& key) {
            auto source = translations.find(key); if (source == translations.end()) return 0;
            Root row(object); const auto& text = source->second;
            field(row, "name", text.name); field(row, "desc", text.desc);
            field(row, "summary", text.summary); field(row, "flavor", text.flavor); return 1;
        }, false, true);
}
inline int notes(Il2CppObject* container) {
    return string_table::visit(container, "TextData_DanteNote", [&](Il2CppObject* object, const std::string& key) {
        auto source = g_dante_notes.find(key); if (source == g_dante_notes.end()) return 0;
        Root row(object); field(row, "title", source->second.title); field(row, "desc", source->second.desc); return 1;
    }, false, true);
}
} // namespace structured_content
