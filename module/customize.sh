SKIPUNZIP=0

# Fallback definitions for non-Magisk/KernelSU environments (e.g. testing)
type ui_print >/dev/null 2>&1 || ui_print() { echo "$@"; }
type set_perm >/dev/null 2>&1 || set_perm() { :; }
type set_perm_recursive >/dev/null 2>&1 || set_perm_recursive() { :; }

# Request KernelSU dynamic hot-install without requiring full phone reboot
export MODULE_HOT_INSTALL_REQUEST=true

ROOT="${LLC_TEST_ROOT:-}"

ui_print "- Installing LLC Zygisk Localizer..."
ui_print "- Architecture: arm64-v8a"

# Detect whether this is a first-time installation or an update
PREV_MODDIR="$ROOT/data/adb/modules/llc_zygisk"
IS_FIRST_INSTALL=true
PREV_VERSION=""

if [ -d "$PREV_MODDIR" ] && [ "$MODPATH" != "$PREV_MODDIR" ] && [ -f "$PREV_MODDIR/module.prop" ]; then
    IS_FIRST_INSTALL=false
    PREV_VERSION=$(grep -E '^version=' "$PREV_MODDIR/module.prop" 2>/dev/null | head -n 1 | cut -d= -f2)
elif [ -f "$ROOT/data/local/tmp/llc/libllc_core.so" ] && [ -f "$PREV_MODDIR/module.prop" ]; then
    IS_FIRST_INSTALL=false
    PREV_VERSION=$(grep -E '^version=' "$PREV_MODDIR/module.prop" 2>/dev/null | head -n 1 | cut -d= -f2)
fi

if [ "$IS_FIRST_INSTALL" = "false" ]; then
    if [ -n "$PREV_VERSION" ]; then
        ui_print "- 检测到已安装版本: $PREV_VERSION"
    else
        ui_print "- 检测到已安装模块"
    fi
    ui_print "- 模式: 运行时 Zygisk 挂钩（热更新覆盖）"
else
    ui_print "- 未检测到已安装模块，准备首次安装"
    ui_print "- 模式: 运行时 Zygisk 挂钩（全新安装）"
fi

set_perm_recursive "$MODPATH" 0 0 0755 0644

# Manage boot_id:
# On first-time install, ensure .boot_id does NOT exist so reboot warning is displayed until reboot.
# On update/upgrade, preserve or record current boot session so hot reload does not falsely trigger reboot warning.
BOOT_ID_SRC="$ROOT/proc/sys/kernel/random/boot_id"
[ -f /proc/sys/kernel/random/boot_id ] && [ -z "$ROOT" ] && BOOT_ID_SRC="/proc/sys/kernel/random/boot_id"

if [ "$IS_FIRST_INSTALL" = "false" ]; then
    if [ -f "$PREV_MODDIR/.boot_id" ]; then
        cp -f "$PREV_MODDIR/.boot_id" "$MODPATH/.boot_id" 2>/dev/null
    elif [ -f "$BOOT_ID_SRC" ] && [ ! -f "$PREV_MODDIR/disable" ]; then
        cat "$BOOT_ID_SRC" > "$MODPATH/.boot_id" 2>/dev/null
    fi
    [ -f "$MODPATH/.boot_id" ] && chmod 0644 "$MODPATH/.boot_id" 2>/dev/null
else
    rm -f "$MODPATH/.boot_id" 2>/dev/null
fi

[ -f "$MODPATH/zygisk/arm64-v8a.so" ] && set_perm "$MODPATH/zygisk/arm64-v8a.so" 0 0 0755
[ -f "$MODPATH/bin/llc-manager" ] && set_perm "$MODPATH/bin/llc-manager" 0 0 0755
[ -f "$MODPATH/bin/llc-manager-native" ] && set_perm "$MODPATH/bin/llc-manager-native" 0 0 0755
[ -f "$MODPATH/action.sh" ] && set_perm "$MODPATH/action.sh" 0 0 0755
[ -f "$MODPATH/service.sh" ] && set_perm "$MODPATH/service.sh" 0 0 0755
[ -f "$MODPATH/post-fs-data.sh" ] && set_perm "$MODPATH/post-fs-data.sh" 0 0 0755
[ -f "$MODPATH/uninstall.sh" ] && set_perm "$MODPATH/uninstall.sh" 0 0 0755

# Ensure persistent LLC staging directory exists
mkdir -p "$ROOT/data/local/tmp/llc"
chmod 0777 "$ROOT/data/local/tmp/llc"

# Synchronize latest libllc_core.so and configs to staging
[ -f "$MODPATH/libllc_core.so" ] && cp -f "$MODPATH/libllc_core.so" "$ROOT/data/local/tmp/llc/libllc_core.so"
[ -f "$ROOT/data/local/tmp/llc/libllc_core.so" ] && chmod 0755 "$ROOT/data/local/tmp/llc/libllc_core.so"
if [ -f "$MODPATH/config/static_overrides.json" ]; then
    cp -f "$MODPATH/config/static_overrides.json" "$ROOT/data/local/tmp/llc/static_overrides.json"
    chmod 0644 "$ROOT/data/local/tmp/llc/static_overrides.json"
fi

for app_dir in "$ROOT/data/data/com.ProjectMoon.LimbusCompany" "$ROOT/data/user/0/com.ProjectMoon.LimbusCompany"; do
    if [ -d "$app_dir" ]; then
        owner=$(stat -c %u:%g "$app_dir" 2>/dev/null)
        con=$(stat -c %C "$app_dir" 2>/dev/null)

        # 1. Sync libllc_core.so to cache
        if [ -d "$app_dir/cache" ]; then
            ui_print "- Syncing libllc_core.so to $app_dir/cache..."
            cp -f "$MODPATH/libllc_core.so" "$app_dir/cache/libllc_core.so" 2>/dev/null
            [ -n "$owner" ] && chown "$owner" "$app_dir/cache/libllc_core.so" 2>/dev/null
            [ -n "$con" ] && [ "$con" != "?" ] && chcon "$con" "$app_dir/cache/libllc_core.so" 2>/dev/null
            [ -f "$app_dir/cache/libllc_core.so" ] && chmod 755 "$app_dir/cache/libllc_core.so"
        fi

        # 2. Backup or restore tmpchinesefont
        if [ -d "$app_dir/files" ]; then
            if [ -f "$app_dir/files/tmpchinesefont" ]; then
                if [ ! -f "$ROOT/data/local/tmp/llc/tmpchinesefont" ]; then
                    ui_print "- Backing up font bundle to persistent storage..."
                    cp -f "$app_dir/files/tmpchinesefont" "$ROOT/data/local/tmp/llc/tmpchinesefont"
                    chmod 0644 "$ROOT/data/local/tmp/llc/tmpchinesefont"
                fi
            elif [ -f "$ROOT/data/local/tmp/llc/tmpchinesefont" ]; then
                ui_print "- Restoring font bundle from persistent storage..."
                cp -f "$ROOT/data/local/tmp/llc/tmpchinesefont" "$app_dir/files/tmpchinesefont"
                [ -n "$owner" ] && chown "$owner" "$app_dir/files/tmpchinesefont" 2>/dev/null
                [ -n "$con" ] && [ "$con" != "?" ] && chcon "$con" "$app_dir/files/tmpchinesefont" 2>/dev/null
                chmod 644 "$app_dir/files/tmpchinesefont"
            fi

            # 3. Synchronize static_overrides.json
            if [ -f "$MODPATH/config/static_overrides.json" ]; then
                for dest in "$app_dir/files/Lang/static_overrides.json" "$app_dir/files/Lang/LLC_zh-CN/static_overrides.json" "$app_dir/files/static_overrides.json"; do
                    if [ -d "$(dirname "$dest")" ]; then
                        cp -f "$MODPATH/config/static_overrides.json" "$dest"
                        [ -n "$owner" ] && chown "$owner" "$dest" 2>/dev/null
                        [ -n "$con" ] && [ "$con" != "?" ] && chcon "$con" "$dest" 2>/dev/null
                        chmod 0644 "$dest"
                    fi
                done
            fi
        fi
    fi
done

ui_print ""
if [ "$IS_FIRST_INSTALL" = "false" ]; then
    ui_print "- 模块更新完成！"
    ui_print "- 提示：热更新已生效，无需重启手机。如需管理或更新汉化资源，请在模块 WebUI 中操作。"
else
    ui_print "- 首次安装完成！"
    ui_print "- 提示：请重启手机以使模块注入生效。开机后可在模块 WebUI 中下载字体与汉化包。"
fi
