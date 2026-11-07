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

    // Test 6: Platform inspection and Android adaptation
    auto mock_bundle = temp_dir / "mock_windows_font.bundle";
    {
        std::vector<char> raw(512, 0);
        // Header magic
        memcpy(raw.data(), "UnityFS\0", 8);
        // Archive-level version at 0x12
        memcpy(raw.data() + 0x12, "2021.3.28f1c1\0", 14);
        // SerializedFile header starting at 0x80
        memcpy(raw.data() + 0x80, "2021.3.28f1c1\0", 14);
        // Target platform = 5 (StandaloneWindows)
        int32_t win_plat = 5;
        memcpy(raw.data() + 0x80 + 14, &win_plat, sizeof(int32_t));
        // m_EnableTypeTree = 1
        raw[0x80 + 14 + 4] = 1;

        std::ofstream f(mock_bundle, std::ios::binary);
        f.write(raw.data(), raw.size());
    }

    assert(font_config::inspect_assetbundle_target_platform(mock_bundle.string()) == 5);
    bool adapted = font_config::adapt_assetbundle_to_android(mock_bundle.string());
    assert(adapted);
    assert(font_config::inspect_assetbundle_target_platform(mock_bundle.string()) == 13);

    // Calling again should be idempotent and return true
    assert(font_config::adapt_assetbundle_to_android(mock_bundle.string()));
    assert(font_config::inspect_assetbundle_target_platform(mock_bundle.string()) == 13);

    // Non-UnityFS file should return -1 and false
    auto non_unity = temp_dir / "not_unity.bin";
    {
        std::ofstream f(non_unity, std::ios::binary);
        f << "SOME_RANDOM_NON_UNITY_DATA_1234567890";
    }
    assert(font_config::inspect_assetbundle_target_platform(non_unity.string()) == -1);
    assert(!font_config::adapt_assetbundle_to_android(non_unity.string()));

    // Clean up
    fs::remove_all(temp_dir);

    printf("font_config: missing, malformed, explicit default, non-existent fallback, valid custom, and android platform adaptation passed\n");
    return 0;
}
