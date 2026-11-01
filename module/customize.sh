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
