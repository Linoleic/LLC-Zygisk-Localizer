#include <cassert>
#include <iostream>
#include <fstream>
#include <filesystem>
#include "../jni/core/language_config.hpp"

int main() {
    auto tmp_dir = std::filesystem::temp_directory_path() / "llc_lang_cfg_test";
    std::filesystem::create_directories(tmp_dir);
    auto cfg_path = (tmp_dir / "language_config.json").string();

    std::filesystem::remove(cfg_path);

    // 1. Missing config file fallback
    {
        auto sel = language_config::resolve_language_selection(cfg_path);
        assert(sel.mode == "all");
        assert(sel.target_language == "kr");
        assert(language_config::should_apply_localization(sel, 0) == true);
        assert(language_config::should_apply_localization(sel, 1) == true);
        assert(language_config::should_apply_localization(sel, 2) == true);
        assert(language_config::should_apply_localization(sel, -1) == true);
    }

    // 2. Corrupt / invalid json fallback
    {
        std::ofstream f(cfg_path);
        f << "{ this is not valid json! }";
        f.close();

        auto sel = language_config::resolve_language_selection(cfg_path);
        assert(sel.mode == "all");
        assert(sel.target_language == "kr");
    }

    // 3. Explicit "all"
    {
        std::ofstream f(cfg_path);
        f << R"({ "schema": 1, "mode": "all", "target_language": "kr" })";
        f.close();

        auto sel = language_config::resolve_language_selection(cfg_path);
        assert(sel.mode == "all");
        assert(language_config::should_apply_localization(sel, 0) == true);
        assert(language_config::should_apply_localization(sel, 1) == true);
        assert(language_config::should_apply_localization(sel, 2) == true);
    }

    // 4. "selective" with target_language == "kr"
    {
        std::ofstream f(cfg_path);
        f << R"({ "schema": 1, "mode": "selective", "target_language": "kr" })";
        f.close();

        auto sel = language_config::resolve_language_selection(cfg_path);
        assert(sel.mode == "selective");
        assert(sel.target_language == "kr");
        assert(language_config::should_apply_localization(sel, 0) == true);  // KR -> matches
        assert(language_config::should_apply_localization(sel, 1) == false); // EN -> skipped
        assert(language_config::should_apply_localization(sel, 2) == false); // JP -> skipped
        assert(language_config::should_apply_localization(sel, -1) == true); // unknown -> allowed
    }

    // 5. "selective" with target_language == "en"
    {
        std::ofstream f(cfg_path);
        f << R"({ "schema": 1, "mode": "selective", "target_language": "en" })";
        f.close();

        auto sel = language_config::resolve_language_selection(cfg_path);
        assert(sel.mode == "selective");
        assert(sel.target_language == "en");
        assert(language_config::should_apply_localization(sel, 0) == false); // KR -> skipped
        assert(language_config::should_apply_localization(sel, 1) == true);  // EN -> matches
        assert(language_config::should_apply_localization(sel, 2) == false); // JP -> skipped
        assert(language_config::should_apply_localization(sel, -1) == false); // unknown with target en -> skipped
    }

    // 6. "selective" with invalid target_language defaults to "kr"
    {
        std::ofstream f(cfg_path);
        f << R"({ "schema": 1, "mode": "selective", "target_language": "es" })";
        f.close();

        auto sel = language_config::resolve_language_selection(cfg_path);
        assert(sel.mode == "selective");
        assert(sel.target_language == "kr"); // falls back to kr
    }

    // 7. Parse playerprefs XML content (URL encoded and raw json formats)
    {
        std::string sample_en_xml = R"(<?xml version='1.0' encoding='utf-8' standalone='yes' ?>
<map>
    <string name="LocalSave.LocalGameOptionData">%7B%22key%22%3A%22abc%22%2C%22_language%22%3A1%2C%22_isLangSaved%22%3Atrue%7D</string>
</map>)";
        assert(language_config::parse_language_from_playerprefs_content(sample_en_xml) == 1);

        std::string sample_kr_xml = R"(<?xml version='1.0' encoding='utf-8' standalone='yes' ?>
<map>
    <string name="LocalSave.LocalGameOptionData">%7B%22_language%22%3A0%2C%22_isLangSaved%22%3Atrue%7D</string>
</map>)";
        assert(language_config::parse_language_from_playerprefs_content(sample_kr_xml) == 0);

        std::string sample_jp_xml = R"(<?xml version='1.0' encoding='utf-8' standalone='yes' ?>
<map>
    <string name="LocalSave.LocalGameOptionData">%7B%22_language%22%3A2%2C%22_isLangSaved%22%3Atrue%7D</string>
</map>)";
        assert(language_config::parse_language_from_playerprefs_content(sample_jp_xml) == 2);

        std::string sample_raw_xml = R"(<?xml version='1.0' encoding='utf-8' standalone='yes' ?>
<map>
    <string name="LocalSave.LocalGameOptionData">{"_language": 1}</string>
</map>)";
        assert(language_config::parse_language_from_playerprefs_content(sample_raw_xml) == 1);

        std::string sample_empty = "";
        assert(language_config::parse_language_from_playerprefs_content(sample_empty) == -1);

        std::string sample_invalid = "<map><string name=\"foo\">bar</string></map>";
        assert(language_config::parse_language_from_playerprefs_content(sample_invalid) == -1);

        auto prefs_test_path = (tmp_dir / "test_playerprefs.xml").string();
        std::ofstream pf(prefs_test_path);
        pf << sample_en_xml;
        pf.close();
        assert(language_config::query_language_from_playerprefs(prefs_test_path) == 1);
    }

    std::filesystem::remove_all(tmp_dir);
    std::cout << "language_config: missing, malformed, explicit all, selective kr/en and invalid fallback passed" << std::endl;
    return 0;
}
