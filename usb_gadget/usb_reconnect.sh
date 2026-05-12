#!/bin/bash
# USB gadget hotplug handler - monitors UDC state and re-enumerates on reconnect
# Fixes "device not recognized" after USB cable unplug/replug
#
# Background daemon: polls /sys/class/udc/<udc>/state
# When USB cable is unplugged and replugged, the DWC3 controller may not
# properly re-enumerate. This script detects the state transition and forces
# a UDC rebind to fix it.

GADGET=/sys/kernel/config/usb_gadget/rockchip
LOGFILE=/tmp/usb_reconnect.log
MAX_LOG_SIZE=8192
PIDFILE=/var/run/usb_reconnect.pid

log_msg() {
    # Rotate log if too large
    [ -f "$LOGFILE" ] && [ $(stat -c%s "$LOGFILE" 2>/dev/null || echo 0) -gt $MAX_LOG_SIZE ] && \
        mv "$LOGFILE" "${LOGFILE}.old" 2>/dev/null
    echo "[$(date '+%H:%M:%S')] $1" >> "$LOGFILE"
    echo "[usb-reconnect] $1" > /dev/kmsg
}

# Find UDC device
find_udc() {
    ls /sys/class/udc/ 2>/dev/null | head -1
}

# Get current UDC state
get_udc_state() {
    local udc=$(find_udc)
    [ -n "$udc" ] && cat /sys/class/udc/$udc/state 2>/dev/null || echo "none"
}

# Check if gadget configfs exists
gadget_exists() {
    [ -d "$GADGET" ] && [ -e "$GADGET/UDC" ]
}

# Check if USB cable is physically connected (VBUS present)
# On DWC3, "not attached" means no cable, other states mean cable present
cable_connected() {
    local state=$(get_udc_state)
    [ "$state" != "not attached" ] && [ "$state" != "none" ] && [ -n "$state" ]
}

# Force UDC rebind to trigger re-enumeration
force_rebind() {
    local udc=$(find_udc)
    [ -z "$udc" ] && {
        log_msg "No UDC device found"
        return 1
    }

    log_msg "Force rebind UDC: $udc"

    # Unbind - tells DWC3 to disconnect from host
    echo "" > "$GADGET/UDC" 2>/dev/null
    sleep 0.3

    # Rebind - DWC3 re-reads configfs and re-enumerates
    echo "$udc" > "$GADGET/UDC" 2>/dev/null
    local rc=$?

    if [ $rc -eq 0 ]; then
        log_msg "UDC rebound successfully"
        return 0
    else
        # Retry with longer delay
        log_msg "UDC rebind failed (rc=$rc), retrying..."
        sleep 1
        echo "" > "$GADGET/UDC" 2>/dev/null
        sleep 0.5
        echo "$udc" > "$GADGET/UDC" 2>/dev/null
        if [ $? -eq 0 ]; then
            log_msg "UDC rebound on retry"
            return 0
        else
            log_msg "UDC rebind failed after retry"
            return 1
        fi
    fi
}

# Restart adbd — always kill and restart
# After UDC rebind, the old adbd process holds stale FFS file descriptors.
# adbd MUST be killed and restarted for the host to re-enumerate properly.
restart_adbd() {
    log_msg "Restarting adbd"
    killall adbd 2>/dev/null
    sleep 0.5
    start-stop-daemon --start --quiet --background --exec /usr/bin/adbd
    sleep 0.5
    if pidof adbd > /dev/null 2>&1; then
        log_msg "adbd restarted successfully"
    else
        log_msg "adbd restart failed"
    fi
}

# Full reconnect sequence: rebind + restart adbd + restore usb0 IP
do_reconnect() {
    log_msg "=== Starting reconnect sequence ==="

    force_rebind
    sleep 1
    restart_adbd

    # Restore usb0 IP for RNDIS
    if ifconfig usb0 >/dev/null 2>&1; then
        ifconfig usb0 172.16.110.6 2>/dev/null
        ifconfig usb0 up 2>/dev/null
        log_msg "usb0 IP restored"
    fi

    log_msg "=== Reconnect sequence complete ==="
}

# Prevent duplicate instances
if [ -f "$PIDFILE" ]; then
    old_pid=$(cat "$PIDFILE" 2>/dev/null)
    if [ -n "$old_pid" ] && kill -0 "$old_pid" 2>/dev/null; then
        echo "[usb-reconnect] Another instance is already running (PID $old_pid), exiting" > /dev/kmsg
        exit 0
    fi
fi
echo $$ > "$PIDFILE"
trap 'rm -f "$PIDFILE"' EXIT

# ---- Main monitoring loop ----
log_msg "USB reconnect monitor started (PID $$)"

PREV_STATE=""
WAS_CONNECTED=0
STUCK_COUNT=0

while true; do
    STATE=$(get_udc_state)

    # Log state transitions
    if [ "$STATE" != "$PREV_STATE" ]; then
        log_msg "UDC state: '${PREV_STATE}' -> '${STATE}'"
        PREV_STATE="$STATE"
    fi

    case "$STATE" in
        configured|addressed)
            if [ "$WAS_CONNECTED" -eq 0 ]; then
                log_msg "USB connected and configured"
                WAS_CONNECTED=1
            fi
            STUCK_COUNT=0
            ;;
        default|powered)
            # Cable connected but host hasn't enumerated (or enumeration failed)
            # STUCK_COUNT accumulates across 'default' and 'powered' oscillations
            # because the transition-detection block no longer resets it
            STUCK_COUNT=$((STUCK_COUNT + 1))
            if [ "$STUCK_COUNT" -ge 5 ]; then
                log_msg "UDC stuck in '${STATE}' for ${STUCK_COUNT}s, forcing rebind"
                do_reconnect
                STUCK_COUNT=0
            fi
            ;;
        "not attached"|"none"|"")
            if [ "$WAS_CONNECTED" -eq 1 ]; then
                log_msg "USB cable unplugged"
                WAS_CONNECTED=0
            fi
            STUCK_COUNT=0
            ;;
        *)
            log_msg "UDC in unexpected state: '$STATE'"
            ;;
    esac

    sleep 1
done
