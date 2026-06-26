#include "t265.h"
#include "t265_syncer.h"
#include "../src/t265_internal.h"
#include "../src/t265_runtime_threads.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>

static void update_delta_stat(uint64_t delta,
                              uint64_t *count,
                              uint64_t *min_value,
                              uint64_t *max_value,
                              uint64_t *sum_value) {
    if (*count == 0 || delta < *min_value) {
        *min_value = delta;
    }
    if (*count == 0 || delta > *max_value) {
        *max_value = delta;
    }
    *sum_value += delta;
    (*count)++;
}

static int parse_sync_threshold_env(uint64_t *threshold_ns, const char **threshold_source) {
    const char *env = getenv("SYNC_THRESHOLD_NS");
    char *end = NULL;
    unsigned long long value;

    if (!env || env[0] == '\0') {
        *threshold_source = "default";
        return 0;
    }

    errno = 0;
    value = strtoull(env, &end, 10);
    if (errno != 0 || end == env || *end != '\0' || value == 0) {
        *threshold_source = "env_invalid_default";
        return 0;
    }

    *threshold_ns = (uint64_t)value;
    *threshold_source = "env";
    return 1;
}

int main() {
    t265_runtime *dev = NULL;
    t265_reader_context reader;
    t265_multi_queue *mq = t265_multi_queue_create();
    t265_syncer *syncer = t265_syncer_create();
    t265_syncer_stats sync_stats;
    t265_multi_queue_stats queue_stats;
    int reader_initialized = 0;
    int reader_started = 0;
    int frameset_count = 0;
    int skipped_incomplete_framesets = 0;
    int skipped_out_of_threshold_framesets = 0;
    int empty_waits = 0;
    int result_fail = 0;
    int result_warn = 0;
    int exit_code = 1;
    uint64_t accepted_pose_delta_count = 0;
    uint64_t accepted_gyro_delta_count = 0;
    uint64_t accepted_accel_delta_count = 0;
    uint64_t accepted_pose_delta_min_ns = 0;
    uint64_t accepted_pose_delta_max_ns = 0;
    uint64_t accepted_pose_delta_sum_ns = 0;
    uint64_t accepted_gyro_delta_min_ns = 0;
    uint64_t accepted_gyro_delta_max_ns = 0;
    uint64_t accepted_gyro_delta_sum_ns = 0;
    uint64_t accepted_accel_delta_min_ns = 0;
    uint64_t accepted_accel_delta_max_ns = 0;
    uint64_t accepted_accel_delta_sum_ns = 0;
    uint64_t threshold_ns = 0;
    const char *threshold_source = "default";

    memset(&reader, 0, sizeof(reader));

    if (!mq || !syncer) {
        fprintf(stderr, "Failed to allocate queue or syncer.\n");
        goto cleanup;
    }

    threshold_ns = t265_syncer_get_threshold_ns(syncer);
    if (parse_sync_threshold_env(&threshold_ns, &threshold_source)) {
        if (t265_syncer_set_threshold_ns(syncer, threshold_ns) != T265_OK) {
            fprintf(stderr, "Failed to set sync threshold.\n");
            goto cleanup;
        }
    }
    
    printf("Opening T265 device...\n");
    if ((dev = t265_open()) == NULL) goto cleanup;
    
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
    
    printf("Initializing reader and syncer...\n");
    if (t265_reader_init(&reader, dev) != T265_OK) {
        fprintf(stderr, "Failed to initialize reader.\n");
        goto cleanup;
    }
    reader_initialized = 1;
    t265_reader_set_multi_queue(&reader, mq);
    if (t265_reader_start(&reader) != T265_OK) {
        fprintf(stderr, "Failed to start reader.\n");
        goto cleanup;
    }
    reader_started = 1;
    
    printf("Waiting for 10 synchronized framesets...\n");
    while (frameset_count < 10 && empty_waits < 30) {
        t265_queue_sample sample;
        int got_sample = 0;

        while (t265_multi_queue_pop_motion(mq, &sample) == T265_OK) {
            t265_frameset fs;
            got_sample = 1;
            t265_syncer_process(syncer, &sample, &fs);
        }

        if (t265_multi_queue_pop_fisheye_meta_blocking(mq, &sample, 1000) == T265_OK) {
            t265_frameset fs;
            t265_queue_sample fisheye_sample = sample;
            got_sample = 1;

            /*
             * Motion samples can arrive while the blocking fisheye pop waits.
             * Feed them first so the syncer can choose the sample nearest to
             * this fisheye timestamp instead of reusing an older Pose.
             */
            while (t265_multi_queue_pop_motion(mq, &sample) == T265_OK) {
                t265_frameset ignored_fs;
                t265_syncer_process(syncer, &sample, &ignored_fs);
            }

            if (t265_syncer_process(syncer, &fisheye_sample, &fs)) {
                if (!fs.has_pose || !fs.has_gyro || !fs.has_accel) {
                    printf("Skipping incomplete frameset: ID=%u TS=%lu Pose=%s Gyro=%s Accel=%s\n",
                           fs.fisheye0_id, fs.timestamp_ns,
                           fs.has_pose ? "YES" : "no",
                           fs.has_gyro ? "YES" : "no",
                           fs.has_accel ? "YES" : "no");
                    skipped_incomplete_framesets++;
                    continue;
                }

                if (!fs.pose_within_threshold ||
                    !fs.gyro_within_threshold ||
                    !fs.accel_within_threshold) {
                    printf("Skipping out-of-threshold frameset: ID=%u TS=%lu "
                           "PoseDeltaAbs=%lu GyroDeltaAbs=%lu AccelDeltaAbs=%lu\n",
                           fs.fisheye0_id, fs.timestamp_ns,
                           fs.pose_delta_abs_ns,
                           fs.gyro_delta_abs_ns,
                           fs.accel_delta_abs_ns);
                    skipped_out_of_threshold_framesets++;
                    continue;
                }

                printf("Frameset #%d: ID=%u TS=%lu Pose=%s Gyro=%s Accel=%s "
                       "PoseDeltaAbs=%lu GyroDeltaAbs=%lu AccelDeltaAbs=%lu\n",
                       frameset_count, fs.fisheye0_id, fs.timestamp_ns,
                       fs.has_pose ? "YES" : "no",
                       fs.has_gyro ? "YES" : "no",
                       fs.has_accel ? "YES" : "no",
                       fs.pose_delta_abs_ns,
                       fs.gyro_delta_abs_ns,
                       fs.accel_delta_abs_ns);
                update_delta_stat(fs.pose_delta_abs_ns,
                                  &accepted_pose_delta_count,
                                  &accepted_pose_delta_min_ns,
                                  &accepted_pose_delta_max_ns,
                                  &accepted_pose_delta_sum_ns);
                update_delta_stat(fs.gyro_delta_abs_ns,
                                  &accepted_gyro_delta_count,
                                  &accepted_gyro_delta_min_ns,
                                  &accepted_gyro_delta_max_ns,
                                  &accepted_gyro_delta_sum_ns);
                update_delta_stat(fs.accel_delta_abs_ns,
                                  &accepted_accel_delta_count,
                                  &accepted_accel_delta_min_ns,
                                  &accepted_accel_delta_max_ns,
                                  &accepted_accel_delta_sum_ns);
                frameset_count++;
            }
        }
        
        while (t265_multi_queue_pop_motion(mq, &sample) == T265_OK) {
            t265_frameset fs;
            got_sample = 1;
            t265_syncer_process(syncer, &sample, &fs);
        }

        if (got_sample) {
            empty_waits = 0;
        } else {
            empty_waits++;
        }
    }
    
    printf("Closing...\n");
    if (reader_started) {
        t265_reader_stop(&reader);
        reader_started = 0;
    }
    if (reader_initialized) {
        t265_reader_destroy(&reader);
        reader_initialized = 0;
    }
    if (dev) {
        t265_stop(dev);
    }

    memset(&sync_stats, 0, sizeof(sync_stats));
    memset(&queue_stats, 0, sizeof(queue_stats));
    t265_syncer_get_stats(syncer, &sync_stats);
    t265_multi_queue_get_stats(mq, &queue_stats);

    if (frameset_count == 0 ||
        sync_stats.frameset_with_pose == 0 ||
        sync_stats.frameset_with_gyro == 0 ||
        sync_stats.frameset_with_accel == 0) {
        result_fail = 1;
    }

    if (!result_fail &&
        (queue_stats.motion_dropped > 0 ||
         queue_stats.fisheye_dropped > 0)) {
        result_warn = 1;
    }
    
    printf("\nSync probe summary:\n");
    printf("  frameset_count:              %d\n", frameset_count);
    printf("  skipped_incomplete_framesets:%d\n", skipped_incomplete_framesets);
    printf("  skipped_out_of_threshold:    %d\n", skipped_out_of_threshold_framesets);
    printf("  threshold_ns:                %lu\n", t265_syncer_get_threshold_ns(syncer));
    printf("  threshold_source:            %s\n", threshold_source);
    printf("\n");
    printf("  samples:\n");
    printf("    processed:                 %lu\n", sync_stats.samples_processed);
    printf("    motion:                    %lu\n", sync_stats.motion_samples);
    printf("    pose:                      %lu\n", sync_stats.pose_samples);
    printf("    gyro:                      %lu\n", sync_stats.gyro_samples);
    printf("    accel:                     %lu\n", sync_stats.accel_samples);
    printf("    fisheye0:                  %lu\n", sync_stats.fisheye0_samples);
    printf("    fisheye1:                  %lu\n", sync_stats.fisheye1_samples);
    printf("\n");
    printf("  framesets:\n");
    printf("    emitted:                   %lu\n", sync_stats.frameset_emitted);
    printf("    with_pose:                 %lu\n", sync_stats.frameset_with_pose);
    printf("    with_gyro:                 %lu\n", sync_stats.frameset_with_gyro);
    printf("    with_accel:                %lu\n", sync_stats.frameset_with_accel);
    printf("    without_pose:              %lu\n", sync_stats.frameset_without_pose);
    printf("    without_gyro:              %lu\n", sync_stats.frameset_without_gyro);
    printf("    without_accel:             %lu\n", sync_stats.frameset_without_accel);
    printf("\n");
    printf("  fisheye pairs:\n");
    printf("    matched:                   %lu\n", sync_stats.fisheye_pair_matched);
    printf("    mismatch:                  %lu\n", sync_stats.fisheye_pair_mismatch);
    printf("    waiting:                   %lu\n", sync_stats.fisheye_pair_waiting);
    printf("\n");
    printf("  timestamp delta abs ns:\n");
    printf("    pose min:                  %lu\n", sync_stats.pose_delta_abs_min_ns);
    printf("    pose max:                  %lu\n", sync_stats.pose_delta_abs_max_ns);
    printf("    pose avg:                  %lu\n", sync_stats.pose_delta_count ? sync_stats.pose_delta_abs_sum_ns / sync_stats.pose_delta_count : 0);
    printf("    gyro min:                  %lu\n", sync_stats.gyro_delta_abs_min_ns);
    printf("    gyro max:                  %lu\n", sync_stats.gyro_delta_abs_max_ns);
    printf("    gyro avg:                  %lu\n", sync_stats.gyro_delta_count ? sync_stats.gyro_delta_abs_sum_ns / sync_stats.gyro_delta_count : 0);
    printf("    accel min:                 %lu\n", sync_stats.accel_delta_abs_min_ns);
    printf("    accel max:                 %lu\n", sync_stats.accel_delta_abs_max_ns);
    printf("    accel avg:                 %lu\n", sync_stats.accel_delta_count ? sync_stats.accel_delta_abs_sum_ns / sync_stats.accel_delta_count : 0);
    printf("\n");
    printf("  accepted timestamp delta abs ns:\n");
    printf("    accepted_framesets:        %d\n", frameset_count);
    printf("    accepted_pose_min_ns:      %lu\n", accepted_pose_delta_min_ns);
    printf("    accepted_pose_max_ns:      %lu\n", accepted_pose_delta_max_ns);
    printf("    accepted_pose_avg_ns:      %lu\n", accepted_pose_delta_count ? accepted_pose_delta_sum_ns / accepted_pose_delta_count : 0);
    printf("    accepted_gyro_min_ns:      %lu\n", accepted_gyro_delta_min_ns);
    printf("    accepted_gyro_max_ns:      %lu\n", accepted_gyro_delta_max_ns);
    printf("    accepted_gyro_avg_ns:      %lu\n", accepted_gyro_delta_count ? accepted_gyro_delta_sum_ns / accepted_gyro_delta_count : 0);
    printf("    accepted_accel_min_ns:     %lu\n", accepted_accel_delta_min_ns);
    printf("    accepted_accel_max_ns:     %lu\n", accepted_accel_delta_max_ns);
    printf("    accepted_accel_avg_ns:     %lu\n", accepted_accel_delta_count ? accepted_accel_delta_sum_ns / accepted_accel_delta_count : 0);
    printf("\n");
    printf("  warm-up policy:\n");
    printf("    skipped framesets are reported for visibility but do not make the probe WARN/FAIL\n");
    printf("    accepted framesets must satisfy the threshold checks\n");
    printf("\n");
    printf("  threshold outliers:\n");
    printf("    pose:                      %lu\n", sync_stats.pose_out_of_threshold);
    printf("    gyro:                      %lu\n", sync_stats.gyro_out_of_threshold);
    printf("    accel:                     %lu\n", sync_stats.accel_out_of_threshold);
    printf("\n");
    printf("  queue stats:\n");
    printf("    motion_dropped:            %lu\n", queue_stats.motion_dropped);
    printf("    motion_remaining:          %d\n", queue_stats.motion_remaining);
    printf("    fisheye_dropped:           %lu\n", queue_stats.fisheye_dropped);
    printf("    fisheye_remaining:         %d\n", queue_stats.fisheye_remaining);
    printf("\n");
    printf("  result:\n");
    printf("    SYNC_PROBE_RESULT: %s\n", result_fail ? "FAIL" : (result_warn ? "WARN" : "PASS"));
    printf("\n");

    exit_code = result_fail ? 1 : 0;

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
    if (syncer) {
        t265_syncer_destroy(syncer);
    }
    if (mq) {
        t265_multi_queue_destroy(mq);
    }

    if (exit_code == 0) {
        printf("Done. Received %d synchronized framesets.\n", frameset_count);
    }
    return exit_code;
}
