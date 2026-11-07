#!/system/bin/sh
# LLC-Zygisk-Localizer Uninstallation Script
# Executed automatically by Magisk / KernelSU / APatch upon module removal.

PKG="com.ProjectMoon.LimbusCompany"
ROOT="${LLC_TEST_ROOT:-}"

# 1. Clean game private data directories
for app_dir in "$ROOT/data/data/$PKG" "$ROOT/data/user/0/$PKG"; do
    if [ -d "$app_dir" ]; then
        # Remove injected core and cache
        rm -f "$app_dir/cache/libllc_core.so" 2>/dev/null

        # Remove internal Chinese font fallback and configurations
        rm -f "$app_dir/files/tmpchinesefont" 2>/dev/null
        rm -f "$app_dir/files/font_config.json" 2>/dev/null
        rm -f "$app_dir/files/language_config.json" 2>/dev/null
        rm -f "$app_dir/files/static_overrides.json" 2>/dev/null
        rm -f "$app_dir/files/llc_reload.flag" 2>/dev/null

        # Remove translation packs and legacy directories
        rm -rf "$app_dir/files/Lang/.llc-packs" 2>/dev/null
        rm -rf "$app_dir/files/Lang/LLC_zh-CN" 2>/dev/null
        rm -f "$app_dir/files/Lang/static_overrides.json" 2>/dev/null

        # If Lang directory is now empty, remove it
        rmdir "$app_dir/files/Lang" 2>/dev/null
    fi
done

# 2. Clean external diagnostic status dump (keep /sdcard/.../files/fonts/ for reuse & protecting custom fonts)
rm -f "$ROOT/sdcard/Android/data/$PKG/files/llc_status.json" 2>/dev/null

# 3. Clean hidden staging and persistent backup
rm -rf "$ROOT/data/local/tmp/llc" 2>/dev/null
rm -f "$ROOT"/data/local/tmp/llc_font_* 2>/dev/null
rm -f "$ROOT"/data/local/tmp/llc_chunk_* 2>/dev/null

# 4. Clean module runtime state, download archives and candidates
rm -rf "$ROOT/data/adb/llc_zygisk" 2>/dev/null

exit 0
