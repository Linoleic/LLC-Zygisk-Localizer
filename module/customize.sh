SKIPUNZIP=0

# Request KernelSU dynamic hot-install without requiring full phone reboot
export MODULE_HOT_INSTALL_REQUEST=true

ui_print "- Installing LLC Zygisk Localizer (Development)..."
ui_print "- Architecture: arm64-v8a"
ui_print "- Mode: Runtime Zygisk Hook (Hot-Reload Enabled)"

set_perm_recursive "$MODPATH" 0 0 0755 0644
[ -f "$MODPATH/zygisk/arm64-v8a.so" ] && set_perm "$MODPATH/zygisk/arm64-v8a.so" 0 0 0755
set_perm "$MODPATH/bin/llc-manager" 0 0 0755
set_perm "$MODPATH/action.sh" 0 0 0755
set_perm "$MODPATH/bin/llc-manager-native" 0 0 0755

# Synchronize latest libllc_core.so to game application cache and hot-reload directories
for app_dir in /data/data/com.ProjectMoon.LimbusCompany /data/user/0/com.ProjectMoon.LimbusCompany; do
    if [ -d "$app_dir/cache" ]; then
        ui_print "- Syncing libllc_core.so to $app_dir/cache..."
        cp -f "$MODPATH/libllc_core.so" "$app_dir/cache/libllc_core.so"
        owner=$(stat -c %u:%g "$app_dir/cache" 2>/dev/null)
        [ -n "$owner" ] && chown "$owner" "$app_dir/cache/libllc_core.so" 2>/dev/null
        con=$(stat -c %C "$app_dir/cache" 2>/dev/null)
        [ -n "$con" ] && [ "$con" != "?" ] && chcon "$con" "$app_dir/cache/libllc_core.so" 2>/dev/null
        chmod 755 "$app_dir/cache/libllc_core.so"
    fi
done

if [ -d "/data/local/tmp/llc" ]; then
    cp -f "$MODPATH/libllc_core.so" "/data/local/tmp/llc/libllc_core.so"
    chmod 755 "/data/local/tmp/llc/libllc_core.so"
fi
