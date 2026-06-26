/*
 * T265 Open By Serial Example
 *
 * Purpose:
 * - Demonstrate stable per-device selection using USB serial number.
 */

#include "t265.h"

#include <stdio.h>

int main(void)
{
    t265_context *ctx = NULL;
    t265_device_info info;
    t265_runtime *dev = NULL;
    int count;
    int rc = 1;

    printf("--- T265 Open By Serial Example ---\n");

    ctx = t265_context_create();
    if (!ctx) {
        fprintf(stderr, "Failed to create T265 context.\n");
        return 1;
    }

    count = t265_context_refresh_devices(ctx);
    if (count <= 0) {
        fprintf(stderr, "No runtime T265 devices found.\n");
        goto cleanup;
    }

    if (t265_context_get_device_info(ctx, 0, &info) != T265_OK) {
        fprintf(stderr, "Failed to get first device info.\n");
        goto cleanup;
    }

    if (info.serial[0] == '\0') {
        fprintf(stderr, "First T265 did not report a serial number.\n");
        goto cleanup;
    }

    printf("opening serial=%s\n", info.serial);
    dev = t265_context_open_device_by_serial(ctx, info.serial);
    if (!dev) {
        fprintf(stderr, "Failed to open T265 by serial.\n");
        goto cleanup;
    }

    if (t265_get_device_info(dev, &info) != T265_OK) {
        fprintf(stderr, "Failed to read opened device info.\n");
        goto cleanup;
    }

    printf("opened %s\n", info.label);
    printf("EXAMPLE_OPEN_BY_SERIAL_RESULT: PASS\n");
    rc = 0;

cleanup:
    if (dev) {
        t265_close(dev);
    }
    t265_context_destroy(ctx);
    return rc;
}
