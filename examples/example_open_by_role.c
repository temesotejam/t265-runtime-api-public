/*
 * T265 Open By Role Example
 *
 * Usage:
 *   ./example_open_by_role roles.conf left
 *
 * roles.conf format:
 *   left=YOUR_LEFT_T265_SERIAL
 *   right=YOUR_RIGHT_T265_SERIAL
 */

#include "t265.h"

#include <stdio.h>

int main(int argc, char **argv)
{
    const char *role_file = argc > 1 ? argv[1] : "t265_roles.conf";
    const char *role = argc > 2 ? argv[2] : "left";
    t265_context *ctx = NULL;
    t265_runtime *dev = NULL;
    t265_device_info info;
    int loaded;
    int rc = 1;

    printf("--- T265 Open By Role Example ---\n");

    ctx = t265_context_create();
    if (!ctx) {
        fprintf(stderr, "Failed to create T265 context.\n");
        return 1;
    }

    loaded = t265_context_load_roles(ctx, role_file);
    if (loaded <= 0) {
        fprintf(stderr, "Failed to load role file: %s\n", role_file);
        goto cleanup;
    }

    dev = t265_context_open_device_by_role(ctx, role);
    if (!dev) {
        fprintf(stderr, "Failed to open role: %s\n", role);
        goto cleanup;
    }

    if (t265_get_device_info(dev, &info) != T265_OK) {
        fprintf(stderr, "Failed to read opened device info.\n");
        goto cleanup;
    }

    printf("role=%s opened %s\n", role, info.label);
    printf("EXAMPLE_OPEN_BY_ROLE_RESULT: PASS\n");
    rc = 0;

cleanup:
    if (dev) {
        t265_close(dev);
    }
    t265_context_destroy(ctx);
    return rc;
}
