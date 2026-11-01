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

    std::filesystem::remove_all(tmp_dir);
    std::cout << "language_config: missing, malformed, explicit all, selective kr/en and invalid fallback passed" << std::endl;
    return 0;
}
