SKIPUNZIP=0

ui_print "- Installing LLC Zygisk Localizer (Probe)..."
ui_print "- Architecture: arm64-v8a"
ui_print "- Mode: Runtime Zygisk Hook"

set_perm_recursive $MODPATH 0 0 0755 0644
[ -f $MODPATH/zygisk/arm64-v8a.so ] && set_perm $MODPATH/zygisk/arm64-v8a.so 0 0 0755
