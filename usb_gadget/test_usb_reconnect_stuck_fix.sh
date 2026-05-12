#!/bin/bash
# TDD Test: usb_reconnect.sh stuck-detection fix
# Bug 1: line 131 STUCK_COUNT=0 resets counter on EVERY state transition
#         → when state oscillates (powered↔default↔not_attached), counter never accumulates
# Bug 2: No duplicate instance prevention → multiple monitors running simultaneously
# RED phase — these tests MUST FAIL with current usb_reconnect.sh.

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TARGET="$SCRIPT_DIR/usb_reconnect.sh"
FAIL=0

log_test() { echo "[TEST] $1"; }
fail()    { echo "[FAIL] $1"; FAIL=1; }
pass()    { echo "[PASS] $1"; }

[ -f "$TARGET" ] || { echo "ERROR: $TARGET not found"; exit 2; }

# ---- Test 1: STUCK_COUNT must NOT be reset in state-transition detection block ----
log_test "Test 1: STUCK_COUNT is NOT reset on every state transition (line 131 bug)"

# Extract the state-transition detection block (the if [ STATE != PREV_STATE ] block)
awk '/if \[ \"\$STATE\" != \"\$PREV_STATE\" \]/,/fi/' "$TARGET" > /tmp/transition_block.txt

echo "--- Current transition-detection block: ---"
cat /tmp/transition_block.txt
echo "--- End ---"

if grep -q "STUCK_COUNT=0" /tmp/transition_block.txt; then
    fail "STUCK_COUNT=0 found in transition block — counter resets on EVERY state change, oscillation prevents accumulation"
else
    pass "STUCK_COUNT is NOT reset in transition block"
fi

# ---- Test 2: STUCK_COUNT resets only in configured/addressed and not_attached ----
log_test "Test 2: STUCK_COUNT only resets in 'configured'/'addressed' and 'not_attached'"

# Count STUCK_COUNT=0 occurrences — should only be in configured case and not_attached case
stuck_resets=$(grep -n "STUCK_COUNT=0" "$TARGET")

echo "--- All STUCK_COUNT=0 locations: ---"
echo "$stuck_resets"
echo "--- End ---"

# Count how many STUCK_COUNT=0 there are (excluding the initialization at the top)
reset_count=$(echo "$stuck_resets" | grep -c "STUCK_COUNT=0" || true)

# For each STUCK_COUNT=0, check its context
echo "$stuck_resets" | while read line; do
    linenum=$(echo "$line" | cut -d: -f1)
    # Get surrounding context
    context=$(sed -n "$((linenum-2)),$((linenum+2))p" "$TARGET")
    echo "  Line $linenum context:"
    echo "$context" | sed 's/^/    /'
done

# We want exactly 2 STUCK_COUNT=0 resets:
# - One in configured|addressed (just before/after setting WAS_CONNECTED)
# - One in "not attached"|"none"|"" case
# The initialization at the top (STUCK_COUNT=0) is fine.
# The buggy one is in the transition block.

# Check that the transition block does NOT have STUCK_COUNT=0
trans_block_start=$(grep -n 'if \[ "\$STATE" != "\$PREV_STATE" \]' "$TARGET" | head -1 | cut -d: -f1)
trans_block_end=$(tail -n +"$trans_block_start" "$TARGET" | grep -n "^[[:space:]]*fi" | head -1 | cut -d: -f1)
trans_block_end=$((trans_block_start + trans_block_end - 1))

has_reset_in_trans=$(sed -n "${trans_block_start},${trans_block_end}p" "$TARGET" | grep -c "STUCK_COUNT=0" || true)
if [ "$has_reset_in_trans" -gt 0 ]; then
    fail "STUCK_COUNT=0 found in transition block (lines $trans_block_start-$trans_block_end)"
else
    pass "No STUCK_COUNT=0 in transition block — counter survives oscillations"
fi

# ---- Test 3: Duplicate instance prevention ----
log_test "Test 3: Script prevents duplicate instances (PID file or lock)"

if grep -q "PIDFILE\|pid_file\|pidfile\|\.pid\|flock\|lock\|RUNNING\|already running\|mkdir.*lock" "$TARGET"; then
    pass "Duplicate instance prevention found (PID file, lock, or similar)"
else
    fail "No duplicate instance prevention — multiple monitors can run simultaneously"
fi

echo ""
if [ $FAIL -eq 0 ]; then
    echo "=== ALL TESTS PASSED ==="
else
    echo "=== TESTS FAILED ($FAIL) — this is expected in RED phase ==="
fi
exit $FAIL
