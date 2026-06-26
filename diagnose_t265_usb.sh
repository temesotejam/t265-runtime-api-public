#!/usr/bin/env bash
set -u

# diagnose_t265_usb.sh
# Check T265 USB status and save diagnostic logs before callback runs.

TIMESTAMP=$(date +"%Y%m%d_%H%M%S")
LOG_DIR="logs/t265_usb_diag_${TIMESTAMP}"
mkdir -p "$LOG_DIR"

echo "Collecting USB diagnostics into $LOG_DIR..."

lsusb > "$LOG_DIR/lsusb.txt"
lsusb -t > "$LOG_DIR/lsusb_tree.txt" 2>/dev/null || true
dmesg | tail -n 150 > "$LOG_DIR/dmesg_tail.txt" 2>&1 || true

STATUS="missing"
MESSAGE="T265 is not visible on USB bus."

if grep -q "8087:0b37" "$LOG_DIR/lsusb.txt"; then
    STATUS="runtime"
    MESSAGE="T265 runtime device detected."
elif grep -q "03e7:2150" "$LOG_DIR/lsusb.txt"; then
    STATUS="boot_or_movidius"
    MESSAGE="Movidius / boot mode detected. T265 may not have switched to runtime."
elif grep -q "8087:0af3" "$LOG_DIR/lsusb.txt"; then
    STATUS="possible_other_realsense_state"
    MESSAGE="Intel RealSense related device detected, but not expected T265 runtime 8087:0b37."
fi

SPEED_HINT="unknown: check lsusb_tree.txt manually"
if grep -q -E "5000M|10000M" "$LOG_DIR/lsusb_tree.txt"; then
    SPEED_HINT="5000M or 10000M appears in lsusb -t: USB3-class link likely available"
elif grep -q "480M" "$LOG_DIR/lsusb_tree.txt"; then
    SPEED_HINT="only 480M appears: device may be connected as USB2, Fisheye streaming may be unstable"
fi

cat <<EOF > "$LOG_DIR/usb_status.txt"
T265 USB status: $STATUS
message: $MESSAGE

USB speed hint:
- $SPEED_HINT
EOF

echo ""
cat "$LOG_DIR/usb_status.txt"
echo ""

echo "Diagnostic logs saved to: $LOG_DIR"
echo ""
echo "Next steps:"
echo "- If status is runtime, try:"
echo "  ./run_t265_combined_tests.sh --latest-only"
echo "  ./run_t265_combined_tests.sh --callback-only"
echo ""
echo "- If status is boot_or_movidius or missing:"
echo "  replug USB cable, try another USB3 port/cable, then rerun this script."
echo ""
echo "- If libusb_init fails even though runtime is visible:"
echo "  try closing other programs using USB/RealSense, replug the device, or reboot."
