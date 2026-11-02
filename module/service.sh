#!/system/bin/sh
MODDIR=${0%/*}

# Wait until boot completes
while [ "$(getprop sys.boot_completed)" != "1" ]; do
    sleep 2
done

mkdir -p /data/local/tmp/llc
chmod 0777 /data/local/tmp/llc

if [ -f "$MODDIR/libllc_core.so" ]; then
    cp -f "$MODDIR/libllc_core.so" /data/local/tmp/llc/libllc_core.so
    chmod 0755 /data/local/tmp/llc/libllc_core.so
fi

if [ -f "$MODDIR/config/title_overrides.json" ]; then
    cp -f "$MODDIR/config/title_overrides.json" /data/local/tmp/llc/title_overrides.json
    chmod 0644 /data/local/tmp/llc/title_overrides.json
fi

sync_assets() {
    for app_dir in /data/data/com.ProjectMoon.LimbusCompany /data/user/0/com.ProjectMoon.LimbusCompany; do
        if [ -d "$app_dir" ]; then
            owner=$(stat -c %u:%g "$app_dir" 2>/dev/null)
            con=$(stat -c %C "$app_dir" 2>/dev/null)

            # Sync libllc_core.so to cache
            if [ -d "$app_dir/cache" ]; then
                if [ ! -f "$app_dir/cache/libllc_core.so" ] || [ "$MODDIR/libllc_core.so" -nt "$app_dir/cache/libllc_core.so" ]; then
                    cp -f "$MODDIR/libllc_core.so" "$app_dir/cache/libllc_core.so"
                    [ -n "$owner" ] && chown "$owner" "$app_dir/cache/libllc_core.so" 2>/dev/null
                    [ -n "$con" ] && [ "$con" != "?" ] && chcon "$con" "$app_dir/cache/libllc_core.so" 2>/dev/null
                    chmod 0755 "$app_dir/cache/libllc_core.so"
                fi
            fi

            # Auto-restore tmpchinesefont if missing
            if [ -d "$app_dir/files" ]; then
                if [ ! -f "$app_dir/files/tmpchinesefont" ]; then
                    for src in /data/local/tmp/llc/tmpchinesefont /sdcard/Download/tmpchinesefont /sdcard/Android/data/com.ProjectMoon.LimbusCompany/files/tmpchinesefont; do
                        if [ -f "$src" ]; then
                            cp -f "$src" "$app_dir/files/tmpchinesefont"
                            [ -n "$owner" ] && chown "$owner" "$app_dir/files/tmpchinesefont" 2>/dev/null
                            [ -n "$con" ] && [ "$con" != "?" ] && chcon "$con" "$app_dir/files/tmpchinesefont" 2>/dev/null
                            chmod 0644 "$app_dir/files/tmpchinesefont"
                            break
                        fi
                    done
                elif [ ! -f /data/local/tmp/llc/tmpchinesefont ]; then
                    cp -f "$app_dir/files/tmpchinesefont" /data/local/tmp/llc/tmpchinesefont
                    chmod 0644 /data/local/tmp/llc/tmpchinesefont
                fi
            fi

            # Sync title_overrides.json
            if [ -f "$MODDIR/config/title_overrides.json" ]; then
                for dest in "$app_dir/files/Lang/title_overrides.json" "$app_dir/files/Lang/LLC_zh-CN/title_overrides.json" "$app_dir/files/title_overrides.json"; do
                    if [ -d "$(dirname "$dest")" ]; then
                        if [ ! -f "$dest" ] || [ "$MODDIR/config/title_overrides.json" -nt "$dest" ]; then
                            cp -f "$MODDIR/config/title_overrides.json" "$dest"
                            [ -n "$owner" ] && chown "$owner" "$dest" 2>/dev/null
                            [ -n "$con" ] && [ "$con" != "?" ] && chcon "$con" "$dest" 2>/dev/null
                            chmod 0644 "$dest"
                        fi
                    fi
                done
            fi
        fi
    done
}

# Initial synchronization on boot
sync_assets

# Periodic watchdog in background to handle game data wipes/re-downloads
(
    while true; do
        sleep 15
        sync_assets
    done
) &
