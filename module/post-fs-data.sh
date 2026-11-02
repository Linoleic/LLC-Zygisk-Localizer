#!/system/bin/sh
MODDIR=${0%/*}

# Early stage synchronization of staging libllc_core.so
if [ -f "$MODDIR/libllc_core.so" ]; then
    mkdir -p /data/local/tmp/llc
    chmod 0777 /data/local/tmp/llc
    cp -f "$MODDIR/libllc_core.so" /data/local/tmp/llc/libllc_core.so
    chmod 0755 /data/local/tmp/llc/libllc_core.so
fi
