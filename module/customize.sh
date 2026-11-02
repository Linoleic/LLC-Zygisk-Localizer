SKIPUNZIP=0

# Request KernelSU dynamic hot-install without requiring full phone reboot
export MODULE_HOT_INSTALL_REQUEST=true

ui_print "- Installing LLC Zygisk Localizer (Development)..."
ui_print "- Architecture: arm64-v8a"
ui_print "- Mode: Runtime Zygisk Hook (Hot-Reload Enabled)"

set_perm_recursive "$MODPATH" 0 0 0755 0644
[ -f "$MODPATH/zygisk/arm64-v8a.so" ] && set_perm "$MODPATH/zygisk/arm64-v8a.so" 0 0 0755
[ -f "$MODPATH/bin/llc-manager" ] && set_perm "$MODPATH/bin/llc-manager" 0 0 0755
[ -f "$MODPATH/bin/llc-manager-native" ] && set_perm "$MODPATH/bin/llc-manager-native" 0 0 0755
[ -f "$MODPATH/action.sh" ] && set_perm "$MODPATH/action.sh" 0 0 0755
[ -f "$MODPATH/service.sh" ] && set_perm "$MODPATH/service.sh" 0 0 0755
[ -f "$MODPATH/post-fs-data.sh" ] && set_perm "$MODPATH/post-fs-data.sh" 0 0 0755

# Ensure persistent LLC staging directory exists
mkdir -p /data/local/tmp/llc
chmod 0777 /data/local/tmp/llc

# Synchronize latest libllc_core.so and configs to staging
cp -f "$MODPATH/libllc_core.so" "/data/local/tmp/llc/libllc_core.so"
chmod 0755 "/data/local/tmp/llc/libllc_core.so"
if [ -f "$MODPATH/config/title_overrides.json" ]; then
    cp -f "$MODPATH/config/title_overrides.json" "/data/local/tmp/llc/title_overrides.json"
    chmod 0644 "/data/local/tmp/llc/title_overrides.json"
fi

for app_dir in /data/data/com.ProjectMoon.LimbusCompany /data/user/0/com.ProjectMoon.LimbusCompany; do
    if [ -d "$app_dir" ]; then
        owner=$(stat -c %u:%g "$app_dir" 2>/dev/null)
        con=$(stat -c %C "$app_dir" 2>/dev/null)

        # 1. Sync libllc_core.so to cache
        if [ -d "$app_dir/cache" ]; then
            ui_print "- Syncing libllc_core.so to $app_dir/cache..."
            cp -f "$MODPATH/libllc_core.so" "$app_dir/cache/libllc_core.so"
            [ -n "$owner" ] && chown "$owner" "$app_dir/cache/libllc_core.so" 2>/dev/null
            [ -n "$con" ] && [ "$con" != "?" ] && chcon "$con" "$app_dir/cache/libllc_core.so" 2>/dev/null
            chmod 755 "$app_dir/cache/libllc_core.so"
        fi

        # 2. Backup or restore tmpchinesefont
        if [ -d "$app_dir/files" ]; then
            if [ -f "$app_dir/files/tmpchinesefont" ]; then
                if [ ! -f "/data/local/tmp/llc/tmpchinesefont" ]; then
                    ui_print "- Backing up font bundle to persistent storage..."
                    cp -f "$app_dir/files/tmpchinesefont" "/data/local/tmp/llc/tmpchinesefont"
                    chmod 0644 "/data/local/tmp/llc/tmpchinesefont"
                fi
            elif [ -f "/data/local/tmp/llc/tmpchinesefont" ]; then
                ui_print "- Restoring font bundle from persistent storage..."
                cp -f "/data/local/tmp/llc/tmpchinesefont" "$app_dir/files/tmpchinesefont"
                [ -n "$owner" ] && chown "$owner" "$app_dir/files/tmpchinesefont" 2>/dev/null
                [ -n "$con" ] && [ "$con" != "?" ] && chcon "$con" "$app_dir/files/tmpchinesefont" 2>/dev/null
                chmod 644 "$app_dir/files/tmpchinesefont"
            fi

            # 3. Synchronize title_overrides.json
            if [ -f "$MODPATH/config/title_overrides.json" ]; then
                for dest in "$app_dir/files/Lang/title_overrides.json" "$app_dir/files/Lang/LLC_zh-CN/title_overrides.json" "$app_dir/files/title_overrides.json"; do
                    if [ -d "$(dirname "$dest")" ]; then
                        cp -f "$MODPATH/config/title_overrides.json" "$dest"
                        [ -n "$owner" ] && chown "$owner" "$dest" 2>/dev/null
                        [ -n "$con" ] && [ "$con" != "?" ] && chcon "$con" "$dest" 2>/dev/null
                        chmod 0644 "$dest"
                    fi
                done
            fi
        fi
    fi
done
