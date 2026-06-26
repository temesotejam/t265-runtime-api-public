/*
 * T265 Device List Example
 *
 * Purpose:
 * - Show the multi-device context API.
 * - Print connected runtime-mode T265 devices.
 */

#include "t265.h"

#include <stdio.h>

int main(void)
{
    t265_context *ctx = NULL;
    int count;

    printf("--- T265 Device List Example ---\n");

    ctx = t265_context_create();
    if (!ctx) {
        fprintf(stderr, "Failed to create T265 context.\n");
        return 1;
    }

    count = t265_context_refresh_devices(ctx);
    if (count < 0) {
        fprintf(stderr, "Failed to refresh T265 device list.\n");
        t265_context_destroy(ctx);
        return 1;
    }

    printf("runtime devices: %d\n", count);
    for (int i = 0; i < count; ++i) {
        t265_device_info info;
        if (t265_context_get_device_info(ctx, i, &info) == T265_OK) {
            printf("  [%d] vid=%04x pid=%04x serial=%s bus=%u address=%u label=%s\n",
                   info.index, info.vid, info.pid,
                   info.serial[0] ? info.serial : "<none>",
                   (unsigned int)info.bus_number,
                   (unsigned int)info.device_address,
                   info.label);
        }
    }

    t265_context_destroy(ctx);
    return 0;
}
