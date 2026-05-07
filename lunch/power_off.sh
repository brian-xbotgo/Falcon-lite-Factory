#!/bin/bash

PARENT_PID=${PPID}
PARENT_NAME=$(cat /proc/${PARENT_PID}/cmdline | tr '\0' ' ')

if [ "${PARENT_PID}" -ne 1 ]; then
    G_PARENT_PID=$(cat /proc/${PARENT_PID}/status | grep PPid | awk '{print $2}')
    G_PARENT_NAME=$(cat /proc/${G_PARENT_PID}/cmdline | tr '\0' ' ')
else
    G_PARENT_PID=1
    G_PARENT_NAME="init"
fi

if [ "$1" = "r" ]; then
    echo "call reboot from ${G_PARENT_NAME} > ${PARENT_NAME}" > /dev/kmsg
else
    echo "call poweroff from ${G_PARENT_NAME} > ${PARENT_NAME}" > /dev/kmsg
fi

safe_kill()
{
    local pid
    pid=$(ps | grep -w "$1" | grep -v grep | awk '{print $1}')
    [ -n "$pid" ] && kill -9 $pid
    echo "safe_kill ${1} pid:$pid" > /dev/kmsg
}

safe_kill wifi_manager
safe_kill multi_media
safe_kill file_mng
safe_kill normal_lvgl_app
safe_kill charge_lvgl_app
safe_kill misc_app
safe_kill prod_test

sync

online=$(cat /sys/class/power_supply/sc89890-charger/online 2>/dev/null)

if [ "$1" = "r" ] || [ "$online" -eq 1 ] ; then
    reboot
else
   poweroff
fi
