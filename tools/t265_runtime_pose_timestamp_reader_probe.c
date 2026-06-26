/*
 * T265 Pose timestamp reader probe
 *
 * Safer alternative to the low-level pose timestamp probe:
 * - Uses the existing two-thread latest-state reader path.
 * - Enables env-gated Pose raw uint64 candidate logging from t265_decode_pose().
 * - Compares latest Pose / Gyro / Accel / Fisheye timestamps in the main thread.
 */

#include "t265.h"
#include "../src/t265_runtime_threads.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>

#define POSE_TS_READER_LOOP_COUNT 50
#define POSE_TS_READER_SLEEP_US 100000

static int64_t delta_i64(uint64_t a, uint64_t b)
{
    if (a >= b) {
        uint64_t diff = a - b;
        return diff > (uint64_t)INT64_MAX ? INT64_MAX : (int64_t)diff;
    } else {
        uint64_t diff = b - a;
        return diff > (uint64_t)INT64_MAX ? INT64_MIN : -(int64_t)diff;
    }
}

static void print_delta(const char *label, uint64_t a, int has_a, uint64_t b, int has_b)
{
    if (has_a && has_b) {
        printf("    %-18s %lld\n", label, (long long)delta_i64(a, b));
    } else {
        printf("    %-18s unavailable\n", label);
    }
}

int main(void)
{
    t265_runtime *dev = NULL;
    t265_reader_context reader;
    t265_reader_stats stats;
    t265_pose_sample pose;
    t265_imu_sample gyro;
    t265_imu_sample accel;
    uint32_t fe0_id = 0;
    uint32_t fe1_id = 0;
    uint64_t fe0_ts = 0;
    uint64_t fe1_ts = 0;
    uint64_t prev_pose_ts = 0;
    uint64_t last_pose_delta = 0;
    int reader_initialized = 0;
    int reader_started = 0;
    int has_pose = 0;
    int has_gyro = 0;
    int has_accel = 0;
    int has_fe = 0;
    int rc = 1;

    memset(&reader, 0, sizeof(reader));
    memset(&stats, 0, sizeof(stats));
    memset(&pose, 0, sizeof(pose));
    memset(&gyro, 0, sizeof(gyro));
    memset(&accel, 0, sizeof(accel));

    putenv("T265_DEBUG_POSE_RAW_OFFSETS=1");
    putenv("T265_DEBUG_POSE_RAW_LIMIT=5");

    printf("Opening T265 device...\n");
    dev = t265_open();
    if (!dev) {
        goto cleanup;
    }

    printf("Configuring streams...\n");
    if (t265_configure_streams(dev, 1, 1, 1) != T265_OK) {
        fprintf(stderr, "Failed to configure streams.\n");
        goto cleanup;
    }

    printf("Starting streams...\n");
    if (t265_start(dev) != T265_OK) {
        fprintf(stderr, "Failed to start streams.\n");
        goto cleanup;
    }

    printf("Starting reader threads...\n");
    if (t265_reader_init(&reader, dev) != T265_OK) {
        fprintf(stderr, "Failed to initialize reader.\n");
        goto cleanup;
    }
    reader_initialized = 1;

    if (t265_reader_start(&reader) != T265_OK) {
        fprintf(stderr, "Failed to start reader.\n");
        goto cleanup;
    }
    reader_started = 1;

    for (int i = 0; i < POSE_TS_READER_LOOP_COUNT; ++i) {
        usleep(POSE_TS_READER_SLEEP_US);

        if (t265_get_latest_pose(&reader, &pose) == T265_OK) {
            if (has_pose && prev_pose_ts != 0) {
                last_pose_delta = pose.timestamp_ns >= prev_pose_ts ?
                    pose.timestamp_ns - prev_pose_ts : prev_pose_ts - pose.timestamp_ns;
            }
            prev_pose_ts = pose.timestamp_ns;
            has_pose = 1;
        }
        if (t265_get_latest_gyro(&reader, &gyro) == T265_OK) {
            has_gyro = 1;
        }
        if (t265_get_latest_accel(&reader, &accel) == T265_OK) {
            has_accel = 1;
        }
        if (t265_get_latest_fisheye_info(&reader, &fe0_id, &fe1_id, &fe0_ts, &fe1_ts) == T265_OK) {
            has_fe = 1;
        }
    }

    t265_get_reader_stats(&reader, &stats);

    printf("\nPose timestamp reader probe summary:\n");
    printf("  counts:\n");
    printf("    pose_count:       %lu\n", stats.pose_count);
    printf("    gyro_count:       %lu\n", stats.gyro_count);
    printf("    accel_count:      %lu\n", stats.accel_count);
    printf("    fisheye0_count:   %lu\n", stats.fisheye0_count);
    printf("    fisheye1_count:   %lu\n", stats.fisheye1_count);
    printf("    interrupt_fail:   %lu\n", stats.interrupt_fail);
    printf("    fisheye_fail:     %lu\n", stats.fisheye_fail);
    printf("\n");

    printf("  latest timestamps:\n");
    printf("    pose:             %llu\n", (unsigned long long)(has_pose ? pose.timestamp_ns : 0));
    printf("    gyro:             %llu\n", (unsigned long long)(has_gyro ? gyro.timestamp_ns : 0));
    printf("    accel:            %llu\n", (unsigned long long)(has_accel ? accel.timestamp_ns : 0));
    printf("    fisheye0:         %llu frame_id=%u\n", (unsigned long long)(has_fe ? fe0_ts : 0), fe0_id);
    printf("    fisheye1:         %llu frame_id=%u\n", (unsigned long long)(has_fe ? fe1_ts : 0), fe1_id);
    printf("\n");

    printf("  latest delta ns:\n");
    print_delta("pose - gyro:", has_pose ? pose.timestamp_ns : 0, has_pose, has_gyro ? gyro.timestamp_ns : 0, has_gyro);
    print_delta("pose - accel:", has_pose ? pose.timestamp_ns : 0, has_pose, has_accel ? accel.timestamp_ns : 0, has_accel);
    print_delta("pose - fe0:", has_pose ? pose.timestamp_ns : 0, has_pose, has_fe ? fe0_ts : 0, has_fe);
    print_delta("pose - fe1:", has_pose ? pose.timestamp_ns : 0, has_pose, has_fe ? fe1_ts : 0, has_fe);
    print_delta("gyro - fe0:", has_gyro ? gyro.timestamp_ns : 0, has_gyro, has_fe ? fe0_ts : 0, has_fe);
    print_delta("accel - fe0:", has_accel ? accel.timestamp_ns : 0, has_accel, has_fe ? fe0_ts : 0, has_fe);
    print_delta("fe1 - fe0:", has_fe ? fe1_ts : 0, has_fe, has_fe ? fe0_ts : 0, has_fe);
    printf("\n");

    printf("  pose details:\n");
    printf("    last_pose_delta:  %llu\n", (unsigned long long)last_pose_delta);
    printf("    xyz:              %.6f %.6f %.6f\n", pose.x, pose.y, pose.z);
    printf("    tracker_conf:     %u\n", pose.tracker_confidence);
    printf("    mapper_conf:      %u\n", pose.mapper_confidence);
    printf("    tracker_state:    %u\n", pose.tracker_state);
    printf("\n");

    printf("  raw pose candidates:\n");
    printf("    See stderr lines starting with T265_DEBUG_POSE_RAW_OFFSETS.\n");
    printf("\n");

    printf("  result:\n");
    if (has_pose && has_gyro && has_accel && has_fe) {
        printf("    POSE_TIMESTAMP_READER_PROBE_RESULT: PASS\n");
        rc = 0;
    } else {
        printf("    POSE_TIMESTAMP_READER_PROBE_RESULT: FAIL\n");
        rc = 1;
    }

cleanup:
    if (reader_started) {
        t265_reader_stop(&reader);
    }
    if (reader_initialized) {
        t265_reader_destroy(&reader);
    }
    if (dev) {
        t265_close(dev);
    }
    return rc;
}
