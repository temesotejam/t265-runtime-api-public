/*
 * Minimal T265 Pose Demo
 *
 * Purpose:
 * - Open one runtime T265 device.
 * - Start pose, gyro, accel, and fisheye metadata streams.
 * - Print the latest pose at about 10 Hz.
 * - Save no images or CSV files.
 *
 * Usage:
 *   ./demo_minimal_pose
 *   ./demo_minimal_pose 10
 *
 * Expected result:
 *   DEMO_MINIMAL_POSE_RESULT: PASS
 */

#include "t265.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define DEMO_DEFAULT_DURATION_SEC 5
#define DEMO_POLL_HZ 10
#define DEMO_SLEEP_US (1000000 / DEMO_POLL_HZ)
#define DEMO_MAX_DURATION_SEC 3600

static int parse_duration_sec(int argc, char **argv)
{
    char *end = NULL;
    long value;

    if (argc < 2) {
        return DEMO_DEFAULT_DURATION_SEC;
    }

    value = strtol(argv[1], &end, 10);
    if (!end || *end != '\0' || value <= 0 || value > DEMO_MAX_DURATION_SEC) {
        fprintf(stderr, "Usage: %s [duration_sec]\n", argv[0]);
        fprintf(stderr, "duration_sec must be 1..%d\n", DEMO_MAX_DURATION_SEC);
        return -1;
    }

    return (int)value;
}

int main(int argc, char **argv)
{
    t265_runtime *dev = NULL;
    t265_reader_context *reader = NULL;
    int duration_sec = parse_duration_sec(argc, argv);
    int loop_count;
    int pose_seen = 0;
    int gyro_seen = 0;
    int accel_seen = 0;
    int fisheye_seen = 0;
    int rc = 1;

    if (duration_sec <= 0) {
        printf("DEMO_MINIMAL_POSE_RESULT: FAIL\n");
        return 1;
    }

    loop_count = duration_sec * DEMO_POLL_HZ;

    printf("--- Minimal T265 Pose Demo ---\n");
    printf("duration_sec: %d\n", duration_sec);

    dev = t265_open();
    if (!dev) {
        fprintf(stderr, "Failed to open a runtime T265 device.\n");
        goto cleanup;
    }

    if (t265_configure_streams(dev, 1, 1, 1) != T265_OK) {
        fprintf(stderr, "Failed to configure pose/imu/fisheye streams.\n");
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

    for (int i = 0; i < loop_count; ++i) {
        t265_pose_sample pose;
        t265_imu_sample gyro;
        t265_imu_sample accel;
        uint32_t fe0_id = 0;
        uint32_t fe1_id = 0;
        uint64_t fe0_ts = 0;
        uint64_t fe1_ts = 0;

        if (t265_get_latest_pose(reader, &pose) == T265_OK) {
            pose_seen = 1;
            printf(
                "pose t=%" PRIu64 "ns x=%.3f y=%.3f z=%.3f q=(%.3f %.3f %.3f %.3f)\n",
                pose.timestamp_ns,
                pose.x,
                pose.y,
                pose.z,
                pose.quat_i,
                pose.quat_j,
                pose.quat_k,
                pose.quat_r
            );
        } else {
            printf("pose: waiting\n");
        }

        if (t265_get_latest_gyro(reader, &gyro) == T265_OK) {
            gyro_seen = 1;
        }

        if (t265_get_latest_accel(reader, &accel) == T265_OK) {
            accel_seen = 1;
        }

        if (t265_get_latest_fisheye_info(reader, &fe0_id, &fe1_id, &fe0_ts, &fe1_ts) == T265_OK) {
            fisheye_seen = 1;
        }

        usleep(DEMO_SLEEP_US);
    }

    {
        t265_reader_stats stats;
        if (t265_get_reader_stats(reader, &stats) == T265_OK) {
            printf("\nCounts:\n");
            printf("  pose_count:     %" PRIu64 "\n", stats.pose_count);
            printf("  gyro_count:     %" PRIu64 "\n", stats.gyro_count);
            printf("  accel_count:    %" PRIu64 "\n", stats.accel_count);
            printf("  fisheye0_count: %" PRIu64 "\n", stats.fisheye0_count);
            printf("  fisheye1_count: %" PRIu64 "\n", stats.fisheye1_count);
        }
    }

    printf("\nAvailability:\n");
    printf("  pose:    %s\n", pose_seen ? "yes" : "no");
    printf("  gyro:    %s\n", gyro_seen ? "yes" : "no");
    printf("  accel:   %s\n", accel_seen ? "yes" : "no");
    printf("  fisheye: %s\n", fisheye_seen ? "yes" : "no");

    if (pose_seen && gyro_seen && accel_seen) {
        printf("DEMO_MINIMAL_POSE_RESULT: PASS\n");
        rc = 0;
    } else {
        printf("DEMO_MINIMAL_POSE_RESULT: FAIL\n");
    }

cleanup:
    if (reader) {
        t265_reader_free(reader);
    }
    if (dev) {
        t265_stop(dev);
        t265_close(dev);
    }

    if (rc != 0) {
        printf("DEMO_MINIMAL_POSE_RESULT: FAIL\n");
    }

    return rc;
}
