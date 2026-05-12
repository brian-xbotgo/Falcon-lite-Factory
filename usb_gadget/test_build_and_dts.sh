#!/bin/bash
# TDD Test: build_factory.sh must create .skip_fsck AFTER ./build.sh
# Bug: .skip_fsck is created before build.sh, so Buildroot wipe-and-rebuild
# removes it. fsck+resize2fs runs every boot wasting ~1.5s.
#
# RED phase — this test MUST FAIL with current build_factory.sh.

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_SCRIPT="$SCRIPT_DIR/../build_factory.sh"
FAIL=0

log_test() { echo "[TEST] $1"; }
fail()    { echo "[FAIL] $1"; FAIL=1; }
pass()    { echo "[PASS] $1"; }

[ -f "$BUILD_SCRIPT" ] || BUILD_SCRIPT="/home/gdh/FACTORY_GIT/build_factory.sh"

log_test "Test A1: .skip_fsck is created AFTER ./build.sh, not before"

# Find all places where .skip_fsck is touched
echo "--- All .skip_fsck references in build_factory.sh: ---"
grep -n "skip_fsck\|\.skip_fsck" "$BUILD_SCRIPT" || echo "(none found)"
echo "--- End ---"

# Find the line number of "./build.sh" calls (exclude comments)
build_sh_lines=$(grep -n "\./build\.sh" "$BUILD_SCRIPT" | grep -v "^[0-9]*:[[:space:]]*#" | cut -d: -f1)
echo "--- ./build.sh calls at lines: $build_sh_lines ---"

# Find the line number of .skip_fsck creation
skip_fsck_line=$(grep -n "skip_fsck" "$BUILD_SCRIPT" | grep -v "^[0-9]*:[[:space:]]*#" | head -1 | cut -d: -f1)

if [ -z "$skip_fsck_line" ]; then
    fail "No .skip_fsck creation found in build_factory.sh"
else
    last_build_line=$(echo "$build_sh_lines" | tail -1)
    if [ "$skip_fsck_line" -gt "$last_build_line" ]; then
        pass ".skip_fsck created (line $skip_fsck_line) AFTER last ./build.sh (line $last_build_line)"
    else
        fail ".skip_fsck created on line $skip_fsck_line BEFORE last ./build.sh on line $last_build_line — will be wiped by Buildroot rebuild"
    fi
fi

log_test "Test B1: backlight node has power-supply property"
# Check the primary backlight node in rv1126b-evb.dtsi
DTS_FILE="/home/gdh/FACTORY_GIT/sdk_patch/kernel/arch/arm64/boot/dts/rockchip/rv1126b-evb.dtsi"
[ -f "$DTS_FILE" ] || DTS_FILE=$(find /home/gdh/FACTORY_GIT -name "rv1126b-evb.dtsi" -path "*/dts/*" 2>/dev/null | head -1)

if [ -z "$DTS_FILE" ] || [ ! -f "$DTS_FILE" ]; then
    log_test "  (DTS file not found, skipping DTS test)"
else
    # Extract the backlight node content
    awk '/^[[:space:]]*backlight: backlight/,/^[[:space:]]*};/' "$DTS_FILE" > /tmp/backlight_node.txt
    echo "--- Backlight node in $DTS_FILE: ---"
    cat /tmp/backlight_node.txt
    echo "--- End ---"

    if grep -q "power-supply" /tmp/backlight_node.txt; then
        pass "backlight node has power-supply property"
    else
        fail "backlight node MISSING power-supply — causes 'supply power not found, using dummy regulator' spam"
    fi
fi

echo ""
if [ $FAIL -eq 0 ]; then
    echo "=== ALL TESTS PASSED ==="
else
    echo "=== TESTS FAILED ($FAIL) — this is expected in RED phase ==="
fi
exit $FAIL
