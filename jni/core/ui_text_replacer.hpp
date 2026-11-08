#pragma once
#include "il2cpp_api.hpp"

struct UIRefreshResult {
    int replaced_count = 0;
    int font_swapped_count = 0;
    bool title_or_auth_dialog_active = false;
};

void refresh_title_screen_loaders();
UIRefreshResult refresh_active_ui_texts(bool localization_active = true);
