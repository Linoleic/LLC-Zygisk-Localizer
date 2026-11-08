#pragma once
#include "il2cpp_api.hpp"
#include "ui_overrides.hpp"
#include "ui_text_replacer.hpp"
#include "font_diagnostics.hpp"

// Global font references
extern Il2CppObject* g_shs_font;
extern Il2CppObject* g_shs_material;
extern Il2CppObject* g_shs_atlas;
extern Il2CppObject* g_empty_font;
extern Il2CppObject* g_empty_material;

// AssetBundle loading & Shader/Material APIs
bool load_chinese_font_bundle(const char* font_bundle_path);
Il2CppObject* find_game_font_shader();
bool ensure_font_material_valid();

// Font Fallback injection APIs
void inject_fallback_font_into(Il2CppObject* font_asset, bool high_priority = true);
int inject_fallback_to_all_loaded_fonts(bool high_priority = true);
int get_patched_font_count();

// Thread-safe main-thread scheduled APIs (safe to call from background worker thread)
void schedule_ui_refresh(bool localization_active = true);
bool is_title_auth_dialog_active();
void schedule_font_fallback(bool high_priority = true);
void schedule_ensure_material_valid();
bool is_shader_bound();
void schedule_dump_active_tmp_texts(const char* output_file);
void schedule_dump_all_fonts_status(const char* output_file);
