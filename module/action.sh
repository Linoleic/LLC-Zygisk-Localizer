#!/system/bin/sh

MODDIR=$(CDPATH= cd -- "${0%/*}" && pwd)
exec "$MODDIR/bin/llc-manager" check
