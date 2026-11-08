#pragma once
#include <string>
#include <cstdint>
#include <vector>
#include <utility>

// String & CJK helpers
std::string trim_copy(const std::string& s);
std::string to_lower_ascii(const std::string& s);
bool is_cjk_codepoint(uint16_t c);
bool utf8_has_cjk(const std::string& str);
bool is_cjk_body_font_name(const char* fnbuf);
bool is_title_or_login_obj_path(const std::string& path);

// Cache & reload management
void check_and_reload_ui_overrides();
bool is_force_font_unify_enabled();
void reset_ui_overrides();
void load_ui_overrides_from_json_string(const std::string& json_str, bool is_custom = false);

// Replacement matching & evaluation
bool evaluate_ui_replacement(const std::string& obj_path, const std::string& utf8_text, std::string& out_replacement);
bool is_title_auth_element(const std::string& obj_path, const std::string& trimmed_text);
