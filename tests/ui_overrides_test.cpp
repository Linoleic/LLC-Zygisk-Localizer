#include <cassert>
#include <cstdio>
#include <iostream>
#include <string>
#include "ui_overrides.hpp"
#include "storage.hpp"


static void test_string_and_cjk_helpers() {
    // 1. trim_copy
    assert(trim_copy("   ") == "");
    assert(trim_copy("\t\r\n") == "");
    assert(trim_copy("  Hello World  \r\n") == "Hello World");
    assert(trim_copy("Limbus Company") == "Limbus Company");

    // 2. to_lower_ascii
    assert(to_lower_ascii("HELLO World 123!") == "hello world 123!");

    // 3. is_cjk_codepoint
    assert(is_cjk_codepoint(0x4E00)); // '一'
    assert(is_cjk_codepoint(0x8FDE)); // '连'
    assert(is_cjk_codepoint(0x6E05)); // '清'
    assert(!is_cjk_codepoint('A'));
    assert(!is_cjk_codepoint('9'));

    // 4. utf8_has_cjk
    assert(utf8_has_cjk("你好世界"));
    assert(utf8_has_cjk("Touch to Start (点击开始)"));
    assert(!utf8_has_cjk("Touch to Start"));
    assert(!utf8_has_cjk("1234567890 !@#$%^&*()"));

    // 5. is_cjk_body_font_name
    assert(is_cjk_body_font_name("Pretendard-Regular SDF"));
    assert(is_cjk_body_font_name("HigashiOme-Gothic SDF"));
    assert(is_cjk_body_font_name("SCDream5 SDF"));
    assert(!is_cjk_body_font_name("LiberationSans SDF"));
    assert(!is_cjk_body_font_name("BebasKai SDF"));
    assert(!is_cjk_body_font_name(nullptr));
}

static void test_path_and_scope_filtering() {
    // Whitelisted title/login paths
    assert(is_title_or_login_obj_path("/LoginSceneManager/Canvas/Button_SwitchAccount"));
    assert(is_title_or_login_obj_path("/Title/DownloadPopup/ConfirmButton"));
    assert(is_title_or_login_obj_path("/NetworkingUI/Dialog/TextOkPopup"));
    assert(is_title_or_login_obj_path("/Canvas/UserAgreement/ScrollRect"));

    // Blacklisted in-game paths
    assert(!is_title_or_login_obj_path("/MainLobby/Canvas/Button_StageMap"));
    assert(!is_title_or_login_obj_path("/Battle/UI/Personality"));
    assert(!is_title_or_login_obj_path("/Manual/Canvas/Text"));
    assert(!is_title_or_login_obj_path("/Gacha/Canvas/Banner"));
    assert(!is_title_or_login_obj_path("/Formation/Canvas/SinnerSlot"));
    assert(!is_title_or_login_obj_path("/Dungeon/FloorMap"));
    assert(!is_title_or_login_obj_path("/Story/DialogueBox"));

    // Empty path
    assert(!is_title_or_login_obj_path(""));

    // is_title_auth_element
    assert(is_title_auth_element("/Canvas/Button_SwitchAccount", "Switch"));
    assert(is_title_auth_element("/Canvas/UserAgreement", "Agree"));
    assert(is_title_auth_element("/Canvas/Button", "切换账号"));
    assert(is_title_auth_element("/Canvas/Button", "清除所有缓存"));
    assert(is_title_auth_element("/Canvas/Button", "Google登录"));
    assert(is_title_auth_element("/Canvas/Button", "TOUCH TO START"));
    assert(!is_title_auth_element("/Canvas/Button", "Start Game"));
}

static void test_ui_overrides_evaluation() {
    reset_ui_overrides();

    // 1. Built-in string mapping evaluation (without JSON overrides)
    std::string out;
    bool matched = evaluate_ui_replacement("/Title/Button", "Sign in with Google", out);
    assert(matched);
    assert(out == "Google登录");

    matched = evaluate_ui_replacement("/Title/Button", "terms of service", out);
    assert(matched);
    assert(out == "服务条款");

    matched = evaluate_ui_replacement("/Title/Button", "Customer Support", out);
    assert(matched);
    assert(out == "客服中心");

    // 2. Built-in path mapping evaluation
    matched = evaluate_ui_replacement("/Title/Canvas/Button_SwitchAccount/Text", "Old Account", out);
    assert(matched);
    assert(out == "切换账号");

    // 3. Dynamic resolution via g_ui_map
    g_ui_map["switch_account"] = "更换账号";
    publish_ui_translation_snapshot();
    const auto before_reload = ui_translation_snapshot();
    g_ui_map.clear();
    assert(ui_translation_snapshot()->at("switch_account") == "更换账号");
    g_ui_map["switch_account"] = "切换账号";
    publish_ui_translation_snapshot();
    assert(ui_translation_snapshot()->at("switch_account") == "切换账号");
    assert(before_reload->at("switch_account") == "更换账号");
    g_ui_map["switch_account"] = "更换账号";
    publish_ui_translation_snapshot();
    matched = evaluate_ui_replacement("/Title/Canvas/Button_SwitchAccount/Text", "Old Account", out);
    assert(matched);
    assert(out == "更换账号"); // Dynamically resolved from g_ui_map!

    // 4. Custom JSON overrides
    const char* json_override = R"({
        "force_font_unify": true,
        "direct_strings": {
            "Special Announce": "特别公告"
        },
        "paths": {
            "NoticeHeader": "通知栏标题"
        }
    })";
    load_ui_overrides_from_json_string(json_override, false);
    assert(is_force_font_unify_enabled());

    matched = evaluate_ui_replacement("/Title/Notice", "Special Announce", out);
    assert(matched);
    assert(out == "特别公告");

    matched = evaluate_ui_replacement("/Title/NoticeHeader/Text", "Generic Title", out);
    assert(matched);
    assert(out == "通知栏标题");

    // 5. Already translated text returns false (no redundant replacement)
    matched = evaluate_ui_replacement("/Title/Notice", "特别公告", out);
    assert(!matched);

    reset_ui_overrides();
    assert(!is_force_font_unify_enabled());
}

int main() {
    std::cout << "Running ui_overrides tests..." << std::endl;
    test_string_and_cjk_helpers();
    test_path_and_scope_filtering();
    test_ui_overrides_evaluation();
    std::cout << "All ui_overrides tests passed successfully." << std::endl;
    return 0;
}
