#pragma once
#include <string>
#include <vector>
#include <unordered_map>

struct PersonalityEntry {
    int id = 0;
    std::string title;
    std::string name;
    std::string desc;
};

struct PassiveEntry {
    int id = 0;
    std::string name;
    std::string desc;
    std::string summary;
    std::string flavor;
};

struct BufEntry {
    std::string id;
    std::string name;
    std::string desc;
    std::string summary;
    std::string flavor;
};

struct EgoEntry {
    int id = 0;
    std::string name;
    std::string desc;
};

struct EgoGiftAbilityEntry { int abilityID = 0; std::string simpleDesc; };
struct EgoGiftEntry {
    std::string name, desc, abnormalityName;
    std::vector<EgoGiftAbilityEntry> simpleDesc;
};

struct DanteNoteEntry { std::string title, desc; };
struct MirrorDungeonThemeEntry { std::string name, specialName; };

struct CharacterEntry {
    int id = 0;
    std::string name;
};

struct SkillCoinDesc {
    std::string desc;
    std::string summary;
};

struct SkillCoin {
    std::vector<SkillCoinDesc> coindescs;
};

struct SkillLevel {
    int level = 1;
    std::string name;
    std::string desc;
    std::string flavor;
    std::string rawDesc;
    std::string abName;
    std::vector<SkillCoin> coinlist;
};

struct SkillEntry {
    int id = 0;
    std::vector<SkillLevel> levelList;
};

struct VoiceDlgEntry {
    std::string id;
    std::string desc;
    std::string dlg;
};

struct TheaterEntry {
    std::string id;
    std::string title;
    std::string desc;
};

struct BannerEntry {
    int id = 0;
    std::string name;
    std::string desc;
    std::string flavor;
};

struct StageChapterEntry {
    std::string id;
    std::string company;
    std::string area;
    std::string chaptertitle;
    std::string chapter;
    std::string chapterNumber;
    std::string timeline;
};

struct StageNodeEntry {
    int id = 0;
    std::string title;
    std::string desc;
    std::string place;
};

struct ScenarioDialogue {
    int id = 0;
    std::string content;
    std::string place;
    std::string teller;
    std::string title;
};
