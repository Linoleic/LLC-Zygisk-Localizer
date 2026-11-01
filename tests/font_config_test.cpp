#include "font_config.hpp"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <filesystem>

int main() {
    namespace fs = std::filesystem;
    auto temp_dir = fs::temp_directory_path() / "llc_font_test";
    fs::create_directories(temp_dir);

    auto dummy_default = temp_dir / "tmpchinesefont";
    {
        std::ofstream f(dummy_default);
        f << "DUMMY_DEFAULT_ASSETBUNDLE";
    }

    auto dummy_custom = temp_dir / "custom_font.bundle";
    {
        std::ofstream f(dummy_custom);
        f << "DUMMY_CUSTOM_ASSETBUNDLE";
    }

    auto config_file = temp_dir / "font_config.json";

    // Test 1: Non-existent config file -> default font
    fs::remove(config_file);
    {
        auto sel = font_config::resolve_font_selection(config_file.string(), dummy_default.string());
        assert(!sel.is_custom);
        assert(sel.font_path == dummy_default.string());
        assert(sel.selected_name == "default");
    }

    // Test 2: Malformed JSON -> default font
    {
        std::ofstream f(config_file);
        f << "{ not valid json ...";
    }
    {
        auto sel = font_config::resolve_font_selection(config_file.string(), dummy_default.string());
        assert(!sel.is_custom);
        assert(sel.font_path == dummy_default.string());
    }

    // Test 3: Explicit default in config -> default font
    {
        std::ofstream f(config_file);
        f << R"({"schema": 1, "selected_font": "default", "font_path": "default"})";
    }
    {
        auto sel = font_config::resolve_font_selection(config_file.string(), dummy_default.string());
        assert(!sel.is_custom);
        assert(sel.font_path == dummy_default.string());
    }

    // Test 4: Custom path pointing to non-existent file -> fallback to default font
    {
        std::ofstream f(config_file);
        f << R"({"schema": 1, "selected_font": "missing", "font_path": "/non/existent/font.bundle"})";
    }
    {
        auto sel = font_config::resolve_font_selection(config_file.string(), dummy_default.string());
        assert(!sel.is_custom);
        assert(sel.font_path == dummy_default.string());
    }

    // Test 5: Valid custom font file -> successfully resolved
    {
        std::ofstream f(config_file);
        nlohmann::json j = {
            {"schema", 1},
            {"selected_font", "霞鹜文楷"},
            {"font_path", dummy_custom.string()}
        };
        f << j.dump();
    }
    {
        auto sel = font_config::resolve_font_selection(config_file.string(), dummy_default.string());
        assert(sel.is_custom);
        assert(sel.font_path == dummy_custom.string());
        assert(sel.selected_name == "霞鹜文楷");
        assert(font_config::resolve_active_font_bundle_path(config_file.string(), dummy_default.string()) == dummy_custom.string());
    }

    // Clean up
    fs::remove_all(temp_dir);

    printf("font_config: missing, malformed, explicit default, non-existent fallback, and valid custom passed\n");
    return 0;
}
