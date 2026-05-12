#!/bin/bash
# USB gadget health monitor — ensures ADB + RNDIS stay functional
# Replaces init_usb_gadget.sh (orchestrator) + usb_reconnect.sh (state machine)
# with a single health-check loop.
#
# Design:
#   Phases 1-5: wait for UDC + adbd, ensure RNDIS + usb0 IP (one-time setup)
#   Phase 6:    every 3s, check "is the gadget working?" — if not, repair
#
# The "repair" is a UDC rebind + adbd restart + usb0 IP restore.
# This handles all DWC3 failure modes (ep0 errors, stuck states, oscillation)
# because the check is "does it work?" not "what state are we in?"

GADGET=/sys/kernel/config/usb_gadget/rockchip
FUNC=$GADGET/functions
CFG=$GADGET/configs/b.1
PIDFILE=/var/run/usb_gadget_health.pid
TARGET_DIR="${TARGET_DIR:-/oem}"

log() { echo "[usb-health] $1" > /dev/kmsg; }

# ---- Duplicate instance prevention ----
if [ -f "$PIDFILE" ]; then
    old_pid=$(cat "$PIDFILE" 2>/dev/null)
    if [ -n "$old_pid" ] && kill -0 "$old_pid" 2>/dev/null; then
        log "Another instance is already running (PID $old_pid), exiting"
        exit 0
    fi
fi

find_udc() { ls /sys/class/udc/ 2>/dev/null | head -1; }

# ---- Phase 1: Wait for UDC ----
log "Waiting for UDC..."
for i in $(seq 1 5); do
    udc_dev=$(find_udc)
    [ -n "$udc_dev" ] && break
    sleep 1
done

if [ -z "$udc_dev" ]; then
    log "No UDC after 5s, ADB unavailable"
    exit 0
fi
log "UDC $udc_dev present"

# ---- Phase 2: Wait for adbd (started by S50usb-gadget) ----
log "Waiting for adbd..."
for i in $(seq 1 10); do
    if pidof adbd >/dev/null 2>&1; then
        log "adbd started by usb-gadget service"
        break
    fi
    sleep 1
done

# ---- Fallback: S50usb-gadget completely failed ----
if ! pidof adbd >/dev/null 2>&1; then
    log "usb-gadget service did not start adbd, trying manual fallback..."
    if [ -x $TARGET_DIR/usr/scripts/usb_config.sh ]; then
        if [ -e "$GADGET/UDC" ]; then
            echo "" > "$GADGET/UDC" 2>/dev/null
        fi
        umount /dev/usb-ffs/adb 2>/dev/null
        umount /sys/kernel/config 2>/dev/null
        rm -rf "$GADGET" 2>/dev/null
        rm -rf /dev/usb-ffs 2>/dev/null
        $TARGET_DIR/usr/scripts/usb_config.sh rndis >> /dev/kmsg 2>&1 &
        sleep 3
    else
        log "usb_config.sh not found, ADB unavailable"
        exit 0
    fi
fi

# ---- Phase 3: Ensure UDC is bound ----
if [ -e "$GADGET/UDC" ]; then
    bound_udc=$(cat "$GADGET/UDC" 2>/dev/null)
    if [ -z "$bound_udc" ]; then
        log "Gadget not bound to UDC, binding $udc_dev..."
        echo "$udc_dev" > "$GADGET/UDC" 2>/dev/null || log "Failed to bind $udc_dev"
    fi
fi

# ---- Phase 4: Ensure RNDIS is configured ----
if [ ! -e "$FUNC/rndis.gs0" ] && [ -d "$GADGET" ]; then
    log "Adding RNDIS function..."
    mkdir -p "$FUNC/rndis.gs0"
    ln -s "$FUNC/rndis.gs0" "$CFG/f2" 2>/dev/null
    echo 0x0006 > "$GADGET/idProduct"
    log "RNDIS added (PID=0x0006)"
    # Rebind to apply new function
    echo "" > "$GADGET/UDC" 2>/dev/null
    sleep 1
    echo "$udc_dev" > "$GADGET/UDC" 2>/dev/null
    killall adbd 2>/dev/null
    sleep 0.5
    start-stop-daemon --start --quiet --background --exec /usr/bin/adbd
    log "adbd restarted after RNDIS rebind"
fi

# ---- Phase 5: Ensure usb0 IP ----
sleep 2
ifconfig usb0 172.16.110.6 2>/dev/null && \
    ifconfig usb0 up 2>/dev/null && \
    log "usb0 configured: 172.16.110.6" || \
    log "usb0 configuration failed"

# ---- Health check helpers ----
is_healthy() {
    local udc=$(find_udc)
    [ -z "$udc" ] && return 1
    [ "$(cat /sys/class/udc/$udc/state 2>/dev/null)" = "configured" ] || return 1
    pidof adbd >/dev/null 2>&1 || return 1
    return 0
}

repair() {
    log "Repairing USB gadget"
    local udc=$(find_udc)
    [ -z "$udc" ] && { log "No UDC, cannot repair"; return 1; }

    echo "" > "$GADGET/UDC" 2>/dev/null
    sleep 0.3
    echo "$udc" > "$GADGET/UDC" 2>/dev/null

    killall adbd 2>/dev/null
    sleep 0.5
    start-stop-daemon --start --quiet --background --exec /usr/bin/adbd

    ifconfig usb0 172.16.110.6 2>/dev/null
    ifconfig usb0 up 2>/dev/null
    log "Repair complete"
}

# ---- Phase 6: Health monitoring (background) ----
# Runs in background so the caller (Dragonfly_lunch) can continue
# to Phase 5 (BLE + factory test) without being blocked.
(
    trap 'rm -f "$PIDFILE"' EXIT

    WAS_HEALTHY=0
    COOLDOWN=0

    if ! is_healthy; then
        log "Initial setup unhealthy, attempting repair"
        repair
        COOLDOWN=5
    fi

    while true; do
        if is_healthy; then
            WAS_HEALTHY=1
            COOLDOWN=0
        elif [ "$WAS_HEALTHY" -eq 1 ] && [ "$COOLDOWN" -le 0 ]; then
            log "Gadget unhealthy, triggering repair"
            repair
            COOLDOWN=5
        fi
        [ "$COOLDOWN" -gt 0 ] && COOLDOWN=$((COOLDOWN - 1))
        sleep 3
    done
) &
MONITOR_PID=$!
echo $MONITOR_PID > "$PIDFILE"
log "Health monitor started in background (PID $MONITOR_PID)"
