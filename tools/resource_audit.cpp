// Standalone file-only audit. No game process attachment or managed API calls.
#include "../jni/core/translation_file.hpp"
#include "../jni/core/json_loader.hpp"
#include "../jni/core/storage.hpp"
#include "../jni/core/translation_evidence.hpp"
#include <filesystem>
#include <chrono>
#include <iostream>
#include <cstring>

int main(int argc, char** argv) {
    if (argc != 2) { std::cerr << "usage: resource_audit <language-root>\n"; return 2; }
    namespace fs = std::filesystem;
    auto start = std::chrono::steady_clock::now();
    fs::path root(argv[1]);
    if (!fs::is_directory(root)) return 2;
    nlohmann::json report = {{"schema", 1}, {"kind", "translation_file_audit"}, {"file_only", true},
        {"files", 0}, {"accepted", 0}, {"rejected", nlohmann::json::array()}, {"errors_by_reason", nlohmann::json::object()}};
    uint64_t bytes = 0;
    for (const char* folder : {"", "StoryData", "BgmLyrics", "PersonalityVoiceDlg", "BattleAnnouncerDlg", "EGOVoiceDlg", "EGOVoiceDig"}) {
        fs::path directory = *folder ? root / folder : root;
        if (!fs::is_directory(directory)) continue;
        for (const auto& entry : fs::directory_iterator(directory)) {
            if (entry.path().extension() != ".json") continue;
            report["files"] = report["files"].get<int>() + 1;
            std::string source;
            auto error = translation_file::read(entry.path().string(), source, !strcmp(folder, "StoryData") ? 5 * 1024 * 1024 : 15 * 1024 * 1024);
            if (error == translation_file::Error::None) {
                report["accepted"] = report["accepted"].get<int>() + 1;
                bytes += source.size();
            } else {
                const char* reason = translation_file::reason(error);
                report["errors_by_reason"][reason] = report["errors_by_reason"].value(reason, 0) + 1;
                if (report["rejected"].size() < 20) report["rejected"].push_back({{"file", entry.path().lexically_relative(root).generic_string()}, {"reason", reason}});
            }
        }
    }
    load_all_ui_jsons(root.string().c_str());
    load_all_content_jsons(root.string().c_str());
    std::unordered_map<int, ScenarioDialogue> story;
    if (fs::is_regular_file(root / "StoryData/S001B.json")) parse_scenario_json_file((root / "StoryData/S001B.json").string(), story);
    report["maps"] = {{"ui", g_ui_map.size()}, {"personalities", g_personalities.size()}, {"skills", g_skills.size()},
        {"passives", g_passives.size()}, {"voices", g_voice_dlgs.size()}, {"songs", g_bgm_lyrics.size()},
        {"dungeon_groups", g_dungeon_nodes.size()}, {"S001B_rows", story.size()}};
    report["accepted_bytes"] = bytes;
    auto& evidence = translation_evidence::registry;
    evidence.rebuild();
    report["loader_field_comparison"] = nlohmann::json::object();
    for (const auto& table : evidence.sources) {
        translation_evidence::Attempt attempt;
        attempt.complete = true;
        for (const auto& row : table.second.rows)
            attempt.rows.emplace(row.first, translation_evidence::Row{evidence.classify(table.first, row.first), 0, 0});
        report["loader_field_comparison"][table.first] = attempt.json();
    }
    report["loaded_file_checks"] = evidence.file_report();
    report["elapsed_ms"] = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    report["all_accepted"] = report["files"] == report["accepted"];
    std::cout << report.dump(2) << '\n';
    return report["all_accepted"].get<bool>() ? 0 : 1;
}
