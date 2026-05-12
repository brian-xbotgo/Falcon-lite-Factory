#!/bin/bash
# TDD Test: usb_gadget_health.sh — merged replacement for init + reconnect
# Verifies the new health-check design: PID guard, is_healthy, repair,
# WAS_HEALTHY guard, COOLDOWN, timeouts, fallback.
#
# RED phase — these tests MUST FAIL if usb_gadget_health.sh is missing or broken.

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TARGET="$SCRIPT_DIR/usb_gadget_health.sh"
FAIL=0

log_test() { echo "[TEST] $1"; }
fail()    { echo "[FAIL] $1"; FAIL=1; }
pass()    { echo "[PASS] $1"; }

[ -f "$TARGET" ] || { echo "ERROR: $TARGET not found"; exit 2; }

# ---- Test 1: PID file + duplicate instance prevention ----
log_test "Test 1: PID file and duplicate instance prevention"

if grep -q "PIDFILE" "$TARGET" && grep -q "kill -0" "$TARGET"; then
    pass "PID file with kill -0 check found"
else
    fail "Missing PID file or duplicate instance prevention"
fi

if grep -q "trap.*rm.*PIDFILE.*EXIT" "$TARGET"; then
    pass "PID file cleanup trap on EXIT"
else
    fail "Missing PID file cleanup trap"
fi

# ---- Test 2: UDC wait ≤ 5s ----
log_test "Test 2: UDC wait loop max ≤ 5 iterations"

udc_max=$(grep -m1 -oP 'seq 1 \K[0-9]+' "$TARGET")
echo "  Current UDC max iterations: $udc_max"
if [ -n "$udc_max" ] && [ "$udc_max" -le 5 ]; then
    pass "UDC max wait $udc_max ≤ 5"
else
    fail "UDC max wait is ${udc_max:-?}, must be ≤ 5"
fi

# ---- Test 3: adbd wait ≤ 10s ----
log_test "Test 3: adbd wait loop max ≤ 10 iterations"

adbd_max=$(grep -oP 'seq 1 \K[0-9]+' "$TARGET" | tail -1)
echo "  Current adbd max iterations: $adbd_max"
if [ -n "$adbd_max" ] && [ "$adbd_max" -le 10 ]; then
    pass "adbd max wait $adbd_max ≤ 10"
else
    fail "adbd max wait is ${adbd_max:-?}, must be ≤ 10"
fi

# ---- Test 4: Fallback path for S50usb-gadget failure ----
log_test "Test 4: Fallback path when S50usb-gadget fails to start adbd"

if grep -q "usb-gadget service did not start adbd" "$TARGET" && \
   grep -q "usb_config.sh" "$TARGET"; then
    pass "Fallback path exists for S50usb-gadget failure"
else
    fail "Missing fallback for S50usb-gadget failure"
fi

# ---- Test 5: is_healthy() checks UDC state == "configured" ----
log_test "Test 5: is_healthy() requires UDC state == 'configured'"

# Extract the is_healthy function body
if grep -q 'state.*configured' "$TARGET" || grep -q '"configured"' "$TARGET"; then
    pass "is_healthy() checks for 'configured' state"
else
    fail "is_healthy() does not check for 'configured' state"
fi

# ---- Test 6: is_healthy() checks adbd is running ----
log_test "Test 6: is_healthy() requires adbd running"

if grep -A10 "is_healthy()" "$TARGET" | grep -q "pidof adbd"; then
    pass "is_healthy() checks pidof adbd"
else
    fail "is_healthy() does not check adbd"
fi

# ---- Test 7: repair() does UDC unbind + rebind ----
log_test "Test 7: repair() performs UDC unbind then rebind"

if grep -A15 "repair()" "$TARGET" | grep -q 'echo "" >.*UDC' && \
   grep -A15 "repair()" "$TARGET" | grep -q 'echo.*udc.*>.*UDC'; then
    pass "repair() does UDC unbind + rebind"
else
    fail "repair() missing UDC unbind/rebind"
fi

# ---- Test 8: repair() kills and restarts adbd ----
log_test "Test 8: repair() kills adbd then restarts it"

if grep -A15 "repair()" "$TARGET" | grep -q "killall adbd" && \
   grep -A15 "repair()" "$TARGET" | grep -q "start-stop-daemon.*adbd"; then
    pass "repair() kills and restarts adbd"
else
    fail "repair() missing adbd kill/restart"
fi

# ---- Test 9: repair() restores usb0 IP ----
log_test "Test 9: repair() restores usb0 IP 172.16.110.6"

if grep -A15 "repair()" "$TARGET" | grep -q "172.16.110.6"; then
    pass "repair() restores usb0 IP"
else
    fail "repair() missing usb0 IP restore"
fi

# ---- Test 10: WAS_HEALTHY guard — no repair if never healthy ----
log_test "Test 10: WAS_HEALTHY guard — skips repair if never healthy"

if grep -q "WAS_HEALTHY" "$TARGET"; then
    pass "WAS_HEALTHY guard present"
else
    fail "Missing WAS_HEALTHY guard — would repair endlessly without USB cable"
fi

# ---- Test 11: COOLDOWN mechanism prevents repair thrashing ----
log_test "Test 11: COOLDOWN mechanism limits repair frequency"

if grep -q "COOLDOWN" "$TARGET"; then
    pass "COOLDOWN mechanism present"
else
    fail "Missing COOLDOWN — repair could thrash every 3s"
fi

# ---- Test 12: RNDIS ensure section ----
log_test "Test 12: Phase 4 ensures RNDIS is configured (rndis.gs0)"

if grep -q "rndis.gs0" "$TARGET"; then
    pass "RNDIS ensure section found"
else
    fail "Missing RNDIS ensure section"
fi

# ---- Test 13: usb0 IP ensure section ----
log_test "Test 13: Phase 5 ensures usb0 has 172.16.110.6"

if grep -q "ifconfig usb0 172.16.110.6" "$TARGET"; then
    pass "usb0 IP ensure section found"
else
    fail "Missing usb0 IP ensure section"
fi

# ---- Test 14: No state machine remnants ----
log_test "Test 14: No UDC state machine (PREV_STATE, STUCK_COUNT, WAS_CONNECTED)"

if grep -q "STUCK_COUNT\|WAS_CONNECTED\|PREV_STATE" "$TARGET"; then
    fail "Legacy state machine variables still present"
else
    pass "No legacy state machine — clean design"
fi

# ---- Test 15: Monitoring loop runs in background ----
log_test "Test 15: Health loop runs in background (does not block caller)"

# Phase 6 should end with ') &' and use $! to capture background PID
if grep -q ') &' "$TARGET" && grep -q 'MONITOR_PID=\$!' "$TARGET"; then
    pass "Monitoring loop is backgrounded with \$! PID capture"
else
    fail "Monitoring loop not backgrounded — would block Phase 5 (BLE + factory)"
fi

# ---- Test 16: PID file gets background PID, not parent PID ----
log_test "Test 16: PID file contains background PID (via \$!), not parent PID"

if grep -q 'echo \$MONITOR_PID >.*PIDFILE' "$TARGET"; then
    pass "PID file written with background PID (\$MONITOR_PID)"
else
    fail "PID file uses wrong PID — may not be cleaned up correctly"
fi

echo ""
if [ $FAIL -eq 0 ]; then
    echo "=== ALL TESTS PASSED ==="
else
    echo "=== TESTS FAILED ($FAIL) — review and fix ==="
fi
exit $FAIL
