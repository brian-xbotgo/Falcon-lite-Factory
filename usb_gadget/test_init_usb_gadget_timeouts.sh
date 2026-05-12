#!/bin/bash
# TDD Test: init_usb_gadget.sh wait timeouts
# Bug: UDC wait is 15s (should be 5s), adbd wait is 20s (should be 10s)
# These were arbitrary "big enough" values with no timing justification.
#
# RED phase — this test MUST FAIL with current init_usb_gadget.sh.

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TARGET="$SCRIPT_DIR/init_usb_gadget.sh"
FAIL=0

log_test() { echo "[TEST] $1"; }
fail()    { echo "[FAIL] $1"; FAIL=1; }
pass()    { echo "[PASS] $1"; }

[ -f "$TARGET" ] || { echo "ERROR: $TARGET not found"; exit 2; }

echo "=== Script: init_usb_gadget.sh ==="

# ---- Test 1: UDC max wait ≤ 5s ----
log_test "Test 1: UDC wait loop max ≤ 5s (currently 15s)"

# Extract the UDC wait loop: "for i in $(seq 1 N)" before "udc_dev="
udc_max=$(grep -A2 "Wait for UDC" "$TARGET" | grep -oP 'seq 1 \K[0-9]+' || \
          sed -n '/Wait for UDC/,/done/p' "$TARGET" | grep -oP 'seq 1 \K[0-9]+')

if [ -z "$udc_max" ]; then
    # Alternative: find first "seq 1 N" in the file — that's the UDC loop
    udc_max=$(grep -m1 -oP 'seq 1 \K[0-9]+' "$TARGET")
fi

echo "  Current UDC max iterations: $udc_max"
if [ -n "$udc_max" ] && [ "$udc_max" -le 5 ]; then
    pass "UDC max wait $udc_max ≤ 5"
else
    fail "UDC max wait is ${udc_max:-?}, must be ≤ 5 (was 15)"
fi

# ---- Test 2: adbd max wait ≤ 10s ----
log_test "Test 2: adbd wait loop max ≤ 10s (currently 20s)"

# Extract the adbd wait loop: second "seq 1 N" in the file
adbd_max=$(grep -oP 'seq 1 \K[0-9]+' "$TARGET" | tail -1)

echo "  Current adbd max iterations: $adbd_max"
if [ -n "$adbd_max" ] && [ "$adbd_max" -le 10 ]; then
    pass "adbd max wait $adbd_max ≤ 10"
else
    fail "adbd max wait is ${adbd_max:-?}, must be ≤ 10 (was 20)"
fi

# ---- Test 3: Exit messages reference correct timeouts ----
log_test "Test 3: Error messages match actual timeout values"

udc_msg=$(grep "No UDC after" "$TARGET")
adbd_msg=$(grep "usb-gadget service did not start" "$TARGET")

echo "  UDC error message: $udc_msg"
if echo "$udc_msg" | grep -q "after ${udc_max}s"; then
    pass "UDC error message matches actual timeout (${udc_max}s)"
else
    fail "UDC error message does not reference correct timeout"
fi

echo ""
if [ $FAIL -eq 0 ]; then
    echo "=== ALL TESTS PASSED ==="
else
    echo "=== TESTS FAILED ($FAIL) — this is expected in RED phase ==="
fi
exit $FAIL
