#!/usr/bin/env bash
set -u

# Ensure T265 is visible as the runtime USB device without invoking
# RealSense SDK command-line tools.

BOOT_VID_PID="03e7:2150"
RUNTIME_VID_PID="8087:0b37"
ALT_RUNTIME_VID_PID="8087:0af3"

BOOT_CMD="${T265_BOOT_CMD:-./t265_boot_libusb}"
WAIT_SECONDS="${T265_RUNTIME_WAIT_SECONDS:-8}"
POLL_INTERVAL="${T265_RUNTIME_POLL_INTERVAL:-1}"
BOOT_RETRIES="${T265_BOOT_RETRIES:-3}"
BOOT_RETRY_SLEEP_SECONDS="${T265_BOOT_RETRY_SLEEP_SECONDS:-2}"

usage() {
    cat <<EOF
Usage: $0 [--check-only]

Environment:
  T265_BOOT_CMD                 Boot helper command (default: ./t265_boot_libusb)
  T265_RUNTIME_WAIT_SECONDS     Seconds to wait after boot attempt (default: 8)
  T265_RUNTIME_POLL_INTERVAL    Poll interval in seconds (default: 1)
  T265_BOOT_RETRIES             Boot helper attempts when boot mode is visible (default: 3)
  T265_BOOT_RETRY_SLEEP_SECONDS Seconds to wait between boot attempts (default: 2)
EOF
}

have_lsusbs_device() {
    local vid_pid="$1"
    lsusb 2>/dev/null | grep -q "$vid_pid"
}

count_lsusbs_device() {
    local vid_pid="$1"
    lsusb 2>/dev/null | grep -c "$vid_pid" || true
}

runtime_count() {
    local primary
    local alt
    primary=$(count_lsusbs_device "$RUNTIME_VID_PID")
    alt=$(count_lsusbs_device "$ALT_RUNTIME_VID_PID")
    echo $((primary + alt))
}

boot_count() {
    count_lsusbs_device "$BOOT_VID_PID"
}

print_candidates() {
    lsusb 2>/dev/null | grep -E "${BOOT_VID_PID}|${RUNTIME_VID_PID}|${ALT_RUNTIME_VID_PID}" || true
}

wait_for_runtime() {
    local expected_count="$1"
    local waited=0

    while [ "$waited" -lt "$WAIT_SECONDS" ]; do
        if [ "$(runtime_count)" -ge "$expected_count" ]; then
            return 0
        fi

        sleep "$POLL_INTERVAL"
        waited=$((waited + POLL_INTERVAL))
    done

    return 1
}

is_non_negative_int() {
    case "$1" in
        ''|*[!0-9]*)
            return 1
            ;;
        *)
            return 0
            ;;
    esac
}

normalize_settings() {
    if ! is_non_negative_int "$WAIT_SECONDS"; then
        WAIT_SECONDS=8
    fi

    if ! is_non_negative_int "$POLL_INTERVAL" || [ "$POLL_INTERVAL" -le 0 ]; then
        POLL_INTERVAL=1
    fi

    if ! is_non_negative_int "$BOOT_RETRIES" || [ "$BOOT_RETRIES" -le 0 ]; then
        BOOT_RETRIES=1
    fi

    if ! is_non_negative_int "$BOOT_RETRY_SLEEP_SECONDS"; then
        BOOT_RETRY_SLEEP_SECONDS=2
    fi
}

check_only=0
if [ "${1:-}" = "--help" ] || [ "${1:-}" = "-h" ]; then
    usage
    exit 0
elif [ "${1:-}" = "--check-only" ]; then
    check_only=1
elif [ "${1:-}" != "" ]; then
    usage >&2
    exit 2
fi

normalize_settings

echo "T265 runtime check"
echo "  runtime ids: ${RUNTIME_VID_PID}, ${ALT_RUNTIME_VID_PID}"
echo "  boot id:     ${BOOT_VID_PID}"
echo "  boot retries: ${BOOT_RETRIES}"
echo

initial_runtime_count=$(runtime_count)
initial_boot_count=$(boot_count)
expected_runtime_count=$((initial_runtime_count + initial_boot_count))
if [ "$expected_runtime_count" -le 0 ]; then
    expected_runtime_count=1
fi

echo "  initial runtime count: ${initial_runtime_count}"
echo "  initial boot count:    ${initial_boot_count}"
echo "  expected runtime count: ${expected_runtime_count}"
echo

echo "Current T265 candidates:"
print_candidates
echo

if [ "$initial_runtime_count" -ge "$expected_runtime_count" ]; then
    echo "T265 runtime devices are already available."
    exit 0
fi

if [ "$initial_boot_count" -le 0 ]; then
    echo "T265 was not found as runtime or boot device." >&2
    echo "Replug the device or check USB port/cable/power." >&2
    exit 1
fi

echo "T265 boot devices need runtime firmware."

if [ "$check_only" -eq 1 ]; then
    echo "Check-only mode: boot helper was not run."
    exit 3
fi

if [ ! -x "$BOOT_CMD" ]; then
    echo "Boot helper is not executable: $BOOT_CMD" >&2
    echo "Build it with:" >&2
    echo "  make t265_boot_libusb" >&2
    exit 4
fi

attempt=1
last_boot_rc=0

while [ "$attempt" -le "$BOOT_RETRIES" ]; do
    echo "Running boot helper: $BOOT_CMD (attempt ${attempt}/${BOOT_RETRIES})"
    "$BOOT_CMD"
    last_boot_rc=$?

    echo "Waiting for runtime device..."
    if wait_for_runtime "$expected_runtime_count"; then
        echo "T265 runtime devices are available:"
        print_candidates
        exit 0
    fi

    echo "Runtime device not visible after attempt ${attempt}."
    echo "Current T265 candidates:"
    print_candidates

    if [ "$last_boot_rc" -ne 0 ]; then
        echo "Boot helper exit code for attempt ${attempt}: ${last_boot_rc}" >&2
    fi

    if [ "$(runtime_count)" -ge "$expected_runtime_count" ]; then
        echo "T265 runtime devices became available after status check:"
        print_candidates
        exit 0
    fi

    if [ "$(boot_count)" -le 0 ]; then
        echo "Boot device is no longer visible after attempt ${attempt}." >&2
        echo "Replug the device or check USB port/cable/power." >&2
        exit 6
    fi

    if [ "$attempt" -lt "$BOOT_RETRIES" ]; then
        echo "Waiting ${BOOT_RETRY_SLEEP_SECONDS}s before retry..."
        sleep "$BOOT_RETRY_SLEEP_SECONDS"
    fi

    attempt=$((attempt + 1))
done

echo "Timed out waiting for T265 runtime device." >&2
if [ "$last_boot_rc" -ne 0 ]; then
    echo "Last boot helper exit code: ${last_boot_rc}" >&2
fi
echo "Current T265 candidates:" >&2
print_candidates >&2
exit 5
