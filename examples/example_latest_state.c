/*
 * T265 Latest State API Example
 *
 * Purpose:
 * - Demonstrate the control-oriented latest state API.
 * - Read the current Pose / Gyro / Accel / Fisheye metadata.
 * - Avoid processing every historical sample.
 *
 * Usage:
 *   ./example_latest_state
 *
 * Expected result:
 *   EXAMPLE_LATEST_STATE_RESULT: PASS
 */

#include "t265.h"

#include <stdio.h>
#include <unistd.h>

#define EXAMPLE_LATEST_STATE_LOOP_COUNT 50
#define EXAMPLE_LATEST_STATE_SLEEP_US 100000

int main(void)
{
    t265_runtime *dev = NULL;
    t265_reader_context *reader = NULL;
    int rc = 1;

    int pose_seen = 0;
    int gyro_seen = 0;
    int accel_seen = 0;
    int fisheye_seen = 0;

    printf("--- T265 Latest State API Example ---\n");

    dev = t265_open();
    if (!dev) {
        fprintf(stderr, "Failed to open T265 device.\n");
        goto cleanup;
    }

    if (t265_configure_streams(dev, 1, 1, 1) != T265_OK) {
        fprintf(stderr, "Failed to configure T265 streams.\n");
        goto cleanup;
    }

    if (t265_start(dev) != T265_OK) {
        fprintf(stderr, "Failed to start T265 streams.\n");
        goto cleanup;
    }

    reader = t265_reader_create(dev);
    if (!reader) {
        fprintf(stderr, "Failed to create reader.\n");
        goto cleanup;
    }

    if (t265_reader_start(reader) != T265_OK) {
        fprintf(stderr, "Failed to start reader.\n");
        goto cleanup;
    }

    for (int i = 0; i < EXAMPLE_LATEST_STATE_LOOP_COUNT; ++i) {
        t265_pose_sample pose;
        t265_imu_sample gyro;
        t265_imu_sample accel;
        uint32_t fe0_id = 0;
        uint32_t fe1_id = 0;
        uint64_t fe0_ts = 0;
        uint64_t fe1_ts = 0;

        usleep(EXAMPLE_LATEST_STATE_SLEEP_US);

        if (t265_get_latest_pose(reader, &pose) == T265_OK) {
            pose_seen = 1;
            printf("pose: yes x=%.3f y=%.3f z=%.3f\n", pose.x, pose.y, pose.z);
        }

        if (t265_get_latest_gyro(reader, &gyro) == T265_OK) {
            gyro_seen = 1;
        }

        if (t265_get_latest_accel(reader, &accel) == T265_OK) {
            accel_seen = 1;
        }

        if (t265_get_latest_fisheye_info(reader, &fe0_id, &fe1_id, &fe0_ts, &fe1_ts) == T265_OK) {
            fisheye_seen = 1;
            printf("fisheye metadata: fe0=%u fe1=%u\n", fe0_id, fe1_id);
        }
    }

    printf("\nLatest state availability:\n");
    printf("  pose:    %s\n", pose_seen ? "yes" : "no");
    printf("  gyro:    %s\n", gyro_seen ? "yes" : "no");
    printf("  accel:   %s\n", accel_seen ? "yes" : "no");
    printf("  fisheye: %s\n", fisheye_seen ? "yes" : "no");

    if (pose_seen && gyro_seen && accel_seen && fisheye_seen) {
        printf("EXAMPLE_LATEST_STATE_RESULT: PASS\n");
        rc = 0;
    } else {
        printf("EXAMPLE_LATEST_STATE_RESULT: FAIL\n");
    }

cleanup:
    if (reader) {
        t265_reader_free(reader);
    }
    if (dev) {
        t265_stop(dev);
        t265_close(dev);
    }

    return rc;
}
