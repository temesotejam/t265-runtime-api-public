#!/usr/bin/env sh
set -eu

URL="https://librealsense.intel.com/Releases/TM2/FW/target/0.2.0.951/target-0.2.0.951.mvcmd"
EXPECTED_SHA1="c3940ccbb0e3045603e4aceaa2d73427f96e24bc"
OUT="tools/t265_fw_target.bin"
TMP="${OUT}.tmp"

mkdir -p tools

if command -v curl >/dev/null 2>&1; then
    curl -L "$URL" -o "$TMP"
elif command -v wget >/dev/null 2>&1; then
    wget "$URL" -O "$TMP"
else
    echo "error: curl or wget is required to download T265 firmware" >&2
    exit 1
fi

if command -v sha1sum >/dev/null 2>&1; then
    ACTUAL_SHA1="$(sha1sum "$TMP" | awk '{print $1}')"
else
    echo "error: sha1sum is required to verify T265 firmware" >&2
    rm -f "$TMP"
    exit 1
fi

if [ "$ACTUAL_SHA1" != "$EXPECTED_SHA1" ]; then
    echo "error: firmware SHA1 mismatch" >&2
    echo "expected: $EXPECTED_SHA1" >&2
    echo "actual:   $ACTUAL_SHA1" >&2
    rm -f "$TMP"
    exit 1
fi

mv "$TMP" "$OUT"
echo "Downloaded $OUT"
echo "SHA1: $ACTUAL_SHA1"
