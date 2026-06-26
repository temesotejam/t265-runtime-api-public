#!/usr/bin/env bash
set -u

RULE_SRC="${1:-99-t265-runtime.rules}"
RULE_DST="/etc/udev/rules.d/99-t265-runtime.rules"

if [ ! -f "$RULE_SRC" ]; then
    echo "Rule file not found: $RULE_SRC" >&2
    exit 1
fi

if [ "$(id -u)" -ne 0 ]; then
    echo "This installer writes to /etc/udev/rules.d and must be run once with sudo:" >&2
    echo "  sudo $0" >&2
    exit 2
fi

cp "$RULE_SRC" "$RULE_DST"
udevadm control --reload-rules
udevadm trigger

echo "Installed: $RULE_DST"
echo "Next steps:"
echo "  1. Replug the T265, or run: sudo udevadm trigger"
echo "  2. Check: lsusb"
echo "  3. Try without sudo: ./ensure_t265_runtime.sh"
