#!/bin/bash
# TDD Test: usb_reconnect.sh restart_adbd() must KILL existing adbd before restart
# Bug: Current restart_adbd only starts adbd if NOT running. After UDC rebind,
# the old adbd FFS fd is stale — adbd MUST be killed and restarted.
#
# RED phase — this test MUST FAIL with current usb_reconnect.sh.

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
RECONNECT_SCRIPT="$SCRIPT_DIR/usb_reconnect.sh"
FAIL=0

log_test() { echo "[TEST] $1"; }
fail()    { echo "[FAIL] $1"; FAIL=1; }
pass()    { echo "[PASS] $1"; }

# ---- Test 1: restart_adbd kills existing adbd before starting ----
log_test "Test 1: restart_adbd MUST kill existing adbd before starting new one"

grep -q 'restart_adbd()' "$RECONNECT_SCRIPT" || { fail "restart_adbd() not found"; }

# Extract the body of restart_adbd() from the script
# Count "killall adbd" occurrences inside restart_adbd function
awk '/^restart_adbd\(\)/,/^}/' "$RECONNECT_SCRIPT" > /tmp/restart_adbd_func.txt

echo "--- Current restart_adbd() body: ---"
cat /tmp/restart_adbd_func.txt
echo "--- End ---"

if grep -q "killall adbd" /tmp/restart_adbd_func.txt; then
    pass "restart_adbd() contains killall adbd"
else
    fail "restart_adbd() does NOT kill existing adbd — must kill before restart"
fi

# ---- Test 2: restart_adbd unconditionally kills then starts ----
log_test "Test 2: restart_adbd must ALWAYS kill adbd, not conditionally"

if grep -q "pidof adbd" /tmp/restart_adbd_func.txt && \
   grep -q "start-stop-daemon.*adbd" /tmp/restart_adbd_func.txt; then
    # Check pattern: currently it's "if ! pidof adbd; then start-stop-daemon ... adbd"
    # The bug: it only starts adbd if NOT running, and never kills
    if grep -q "killall adbd" /tmp/restart_adbd_func.txt; then
        # Has killall — check it's BEFORE start-stop-daemon, not conditional on ! pidof
        kill_line=$(grep -n "killall adbd" /tmp/restart_adbd_func.txt | head -1 | cut -d: -f1)
        start_line=$(grep -n "start-stop-daemon.*adbd" /tmp/restart_adbd_func.txt | head -1 | cut -d: -f1)
        if [ "$kill_line" -lt "$start_line" ]; then
            pass "killall adbd occurs before start-stop-daemon"
        else
            fail "killall adbd must come BEFORE start-stop-daemon start"
        fi
    fi
else
    fail "restart_adbd() missing expected adbd lifecycle commands"
fi

# ---- Test 3: WAS_CONNECTED stuck-detection works on FIRST connect attempt ----
log_test "Test 3: Stuck detection must fire even when WAS_CONNECTED=0"
log_test "         (current: requires WAS_CONNECTED=1, skipping first connect)"

# Extract the "powered" case from the monitoring loop
awk '/powered\)/,/;;/' "$RECONNECT_SCRIPT" | head -20 > /tmp/powered_case.txt

echo "--- Current 'powered' case: ---"
cat /tmp/powered_case.txt
echo "--- End ---"

if grep -q 'WAS_CONNECTED.*-eq 1' /tmp/powered_case.txt; then
    fail "'powered' stuck-detection gated on WAS_CONNECTED==1 — first connect never triggers fix"
else
    pass "'powered' stuck-detection works regardless of WAS_CONNECTED"
fi

# ---- Test 4: "default" case also works on first connect ----
log_test "Test 4: 'default' stuck-detection must also fire when WAS_CONNECTED=0"

awk '/default\)/,/;;/' "$RECONNECT_SCRIPT" | head -20 > /tmp/default_case.txt

if grep -q 'WAS_CONNECTED.*-eq 1' /tmp/default_case.txt; then
    fail "'default' stuck-detection gated on WAS_CONNECTED==1 — first connect never triggers fix"
else
    pass "'default' stuck-detection works regardless of WAS_CONNECTED"
fi

# ---- Summary ----
echo ""
if [ $FAIL -eq 0 ]; then
    echo "=== ALL TESTS PASSED ==="
else
    echo "=== TESTS FAILED ($FAIL) — this is expected in RED phase ==="
fi
exit $FAIL
