#!/bin/sh
set -u

INIT_SRC="${1:-/oem/usr/init.d/S90factory_fw}"
INIT_DST="${2:-/etc/init.d/S90factory_fw}"

if [ ! -f "$INIT_SRC" ]; then
    echo "missing init script: $INIT_SRC" >&2
    exit 1
fi

cp -f "$INIT_SRC" "$INIT_DST"
chmod 755 "$INIT_DST"
echo "installed $INIT_DST"
