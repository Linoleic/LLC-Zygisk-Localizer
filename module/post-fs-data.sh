#!/system/bin/sh
MODDIR=${0%/*}

# Record boot_id to identify that the module was loaded during early boot
if [ -f /proc/sys/kernel/random/boot_id ]; then
    cat /proc/sys/kernel/random/boot_id > "$MODDIR/.boot_id" 2>/dev/null
    chmod 0644 "$MODDIR/.boot_id" 2>/dev/null
fi

# Early stage synchronization of staging libllc_core.so
if [ -f "$MODDIR/libllc_core.so" ]; then
    mkdir -p /data/local/tmp/llc
    chmod 0777 /data/local/tmp/llc
    cp -f "$MODDIR/libllc_core.so" /data/local/tmp/llc/libllc_core.so
    chmod 0755 /data/local/tmp/llc/libllc_core.so
fi
