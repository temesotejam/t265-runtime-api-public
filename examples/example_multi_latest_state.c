/*
 * T265 Multi Latest State Example
 *
 * Purpose:
 * - Open two runtime-mode T265 devices at the same time.
 * - Read latest Pose / Gyro / Accel / Fisheye metadata from both devices.
 */

#include "t265.h"

#include <stdio.h>
#include <unistd.h>

#define MULTI_LOOP_COUNT 50
#define MULTI_SLEEP_US 100000
#define MULTI_DEVICE_COUNT 2

typedef struct example_device {
    t265_runtime *dev;
    t265_reader_context *reader;
    t265_device_info info;
    int pose_seen;
    int gyro_seen;
    int accel_seen;
    int fisheye_seen;
} example_device;

static int start_device(t265_context *ctx, int index, example_device *slot)
{
    slot->dev = t265_context_open_device(ctx, index);
    if (!slot->dev) {
        fprintf(stderr, "Failed to open T265 device index %d.\n", index);
        return 1;
    }

    if (t265_get_device_info(slot->dev, &slot->info) != T265_OK) {
        fprintf(stderr, "Failed to get T265 device info for index %d.\n", index);
        return 1;
    }

    if (t265_configure_streams(slot->dev, 1, 1, 1) != T265_OK) {
        fprintf(stderr, "Failed to configure %s.\n", slot->info.label);
        return 1;
    }

    if (t265_start(slot->dev) != T265_OK) {
        fprintf(stderr, "Failed to start %s.\n", slot->info.label);
        return 1;
    }

    slot->reader = t265_reader_create(slot->dev);
    if (!slot->reader) {
        fprintf(stderr, "Failed to create reader for %s.\n", slot->info.label);
        return 1;
    }

    if (t265_reader_start(slot->reader) != T265_OK) {
        fprintf(stderr, "Failed to start reader for %s.\n", slot->info.label);
        return 1;
    }

    return 0;
}

static void stop_device(example_device *slot)
{
    if (slot->reader) {
        t265_reader_free(slot->reader);
        slot->reader = NULL;
    }
    if (slot->dev) {
        t265_stop(slot->dev);
        t265_close(slot->dev);
        slot->dev = NULL;
    }
}

int main(void)
{
    t265_context *ctx = NULL;
    example_device devices[MULTI_DEVICE_COUNT] = {0};
    int count;
    int rc = 1;

    printf("--- T265 Multi Latest State Example ---\n");

    ctx = t265_context_create();
    if (!ctx) {
        fprintf(stderr, "Failed to create T265 context.\n");
        return 1;
    }

    count = t265_context_refresh_devices(ctx);
    if (count < MULTI_DEVICE_COUNT) {
        fprintf(stderr, "Need %d runtime T265 devices, found %d.\n",
                MULTI_DEVICE_COUNT, count < 0 ? 0 : count);
        goto cleanup;
    }

    for (int i = 0; i < MULTI_DEVICE_COUNT; ++i) {
        if (start_device(ctx, i, &devices[i]) != 0) {
            goto cleanup;
        }
        printf("started %s\n", devices[i].info.label);
    }

    for (int loop = 0; loop < MULTI_LOOP_COUNT; ++loop) {
        usleep(MULTI_SLEEP_US);

        for (int i = 0; i < MULTI_DEVICE_COUNT; ++i) {
            t265_pose_sample pose;
            t265_imu_sample gyro;
            t265_imu_sample accel;
            uint32_t fe0_id = 0;
            uint32_t fe1_id = 0;
            uint64_t fe0_ts = 0;
            uint64_t fe1_ts = 0;

            if (t265_get_latest_pose(devices[i].reader, &pose) == T265_OK) {
                devices[i].pose_seen = 1;
                printf("%s pose: x=%.3f y=%.3f z=%.3f\n",
                       devices[i].info.label, pose.x, pose.y, pose.z);
            }
            if (t265_get_latest_gyro(devices[i].reader, &gyro) == T265_OK) {
                devices[i].gyro_seen = 1;
            }
            if (t265_get_latest_accel(devices[i].reader, &accel) == T265_OK) {
                devices[i].accel_seen = 1;
            }
            if (t265_get_latest_fisheye_info(devices[i].reader, &fe0_id, &fe1_id,
                                             &fe0_ts, &fe1_ts) == T265_OK) {
                devices[i].fisheye_seen = 1;
            }
        }
    }

    rc = 0;
    printf("\nLatest state availability:\n");
    for (int i = 0; i < MULTI_DEVICE_COUNT; ++i) {
        printf("  %s pose=%s gyro=%s accel=%s fisheye=%s\n",
               devices[i].info.label,
               devices[i].pose_seen ? "yes" : "no",
               devices[i].gyro_seen ? "yes" : "no",
               devices[i].accel_seen ? "yes" : "no",
               devices[i].fisheye_seen ? "yes" : "no");
        if (!devices[i].pose_seen || !devices[i].gyro_seen ||
            !devices[i].accel_seen || !devices[i].fisheye_seen) {
            rc = 1;
        }
    }

    printf("EXAMPLE_MULTI_LATEST_STATE_RESULT: %s\n", rc == 0 ? "PASS" : "FAIL");

cleanup:
    for (int i = MULTI_DEVICE_COUNT - 1; i >= 0; --i) {
        stop_device(&devices[i]);
    }
    t265_context_destroy(ctx);
    return rc;
}
