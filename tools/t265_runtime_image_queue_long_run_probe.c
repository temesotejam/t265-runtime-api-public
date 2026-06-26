/*
 * T265 image queue long-run probe
 *
 * Purpose:
 * - Keep one reader process running for a requested duration.
 * - Pop image payload samples continuously from the fisheye queue.
 * - Record image queue drop / overwrite / copy-fail stats.
 * - Keep heavy work out of callbacks; image saving happens after queue pop.
 */

#include "t265.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <stdatomic.h>

#define DEFAULT_DURATION_SEC 60
#define DEFAULT_POLL_US 10000
#define FINAL_DRAIN_LIMIT 1024

static atomic_int g_stop_requested;

static void handle_signal(int signo)
{
    (void)signo;
    atomic_store(&g_stop_requested, 1);
}

static uint64_t monotonic_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

static int parse_positive_int(const char *text, int fallback)
{
    char *end = NULL;
    long value;

    if (!text || !*text) {
        return fallback;
    }

    value = strtol(text, &end, 10);
    if (!end || *end != '\0' || value <= 0 || value > 86400) {
        return fallback;
    }

    return (int)value;
}

static void usage(const char *argv0)
{
    printf("Usage: %s [--duration-sec N] [--save-every-sec N] [--no-save]\n", argv0);
    printf("Defaults: --duration-sec %d, save first frame only\n", DEFAULT_DURATION_SEC);
}

int main(int argc, char **argv)
{
    t265_multi_queue *mq = NULL;
    t265_reader_context *reader = NULL;
    t265_multi_queue_stats queue_stats;
    t265_image_queue_stats image_stats;
    t265_reader_stats reader_stats;
    int duration_sec = DEFAULT_DURATION_SEC;
    int save_every_sec = 0;
    int save_enabled = 1;
    uint64_t start_ms;
    uint64_t end_ms;
    uint64_t next_save_ms = 0;
    uint64_t image_count = 0;
    uint64_t image_view_fail = 0;
    uint64_t saved_count = 0;
    uint64_t fisheye0_count = 0;
    uint64_t fisheye1_count = 0;
    uint64_t motion_popped = 0;
    int final_drained_fisheye = 0;
    int final_drained_motion = 0;
    int rc = 1;
    const char *result = "FAIL";

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--duration-sec") == 0) {
            if (i + 1 >= argc) {
                usage(argv[0]);
                return 2;
            }
            duration_sec = parse_positive_int(argv[++i], duration_sec);
        } else if (strcmp(argv[i], "--save-every-sec") == 0) {
            if (i + 1 >= argc) {
                usage(argv[0]);
                return 2;
            }
            save_every_sec = parse_positive_int(argv[++i], 0);
            save_enabled = 1;
        } else if (strcmp(argv[i], "--no-save") == 0) {
            save_enabled = 0;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(argv[0]);
            return 0;
        } else {
            usage(argv[0]);
            return 2;
        }
    }

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    atomic_store(&g_stop_requested, 0);

    printf("--- T265 Image Queue Long-run Probe ---\n");
    printf("duration_sec: %d\n", duration_sec);
    printf("save_enabled: %s\n", save_enabled ? "yes" : "no");
    printf("save_every_sec: %d\n", save_every_sec);

    mq = t265_multi_queue_create();
    if (!mq) {
        fprintf(stderr, "Failed to create multi-queue.\n");
        goto cleanup;
    }

    reader = t265_create_simple_reader(NULL, 1, 1, 1, mq);
    if (!reader) {
        fprintf(stderr, "Failed to create reader.\n");
        goto cleanup;
    }

    start_ms = monotonic_ms();
    end_ms = start_ms + (uint64_t)duration_sec * 1000u;
    next_save_ms = start_ms;

    while (!atomic_load(&g_stop_requested) && monotonic_ms() < end_ms) {
        int did_work = 0;
        t265_queue_sample sample;

        while (t265_multi_queue_pop_motion(mq, &sample) == T265_OK) {
            ++motion_popped;
            did_work = 1;
        }

        while (t265_multi_queue_pop_fisheye_meta(mq, &sample) == T265_OK) {
            t265_image_view view;
            did_work = 1;

            if (t265_image_view_from_queue_sample(&sample, &view) != T265_OK) {
                ++image_view_fail;
                continue;
            }

            ++image_count;
            if (view.sensor_id == T265_SENSOR_ID_FISHEYE0) {
                ++fisheye0_count;
            } else if (view.sensor_id == T265_SENSOR_ID_FISHEYE1) {
                ++fisheye1_count;
            }

            if (save_enabled) {
                uint64_t now = monotonic_ms();
                int should_save = 0;

                if (saved_count == 0) {
                    should_save = 1;
                } else if (save_every_sec > 0 && now >= next_save_ms) {
                    should_save = 1;
                }

                if (should_save) {
                    t265_owned_image owned;
                    char path[128];

                    if (t265_owned_image_copy_from_view(&view, &owned) == T265_OK) {
                        snprintf(path, sizeof(path), "long_run_fe%d_%u.pgm",
                                 owned.sensor_id == T265_SENSOR_ID_FISHEYE0 ? 0 : 1,
                                 owned.frame_id);
                        if (t265_write_pgm_from_owned_image(path, &owned) == T265_OK) {
                            ++saved_count;
                        }
                        t265_owned_image_destroy(&owned);
                    }
                    if (save_every_sec > 0) {
                        next_save_ms = now + (uint64_t)save_every_sec * 1000u;
                    } else {
                        next_save_ms = end_ms + 1u;
                    }
                }
            }
        }

        if (!did_work) {
            usleep(DEFAULT_POLL_US);
        }
    }

    if (reader) {
        t265_reader_stop(reader);
    }

    while (final_drained_motion < FINAL_DRAIN_LIMIT &&
           t265_multi_queue_pop_motion(mq, &(t265_queue_sample){0}) == T265_OK) {
        ++final_drained_motion;
    }

    while (final_drained_fisheye < FINAL_DRAIN_LIMIT) {
        t265_queue_sample sample;
        if (t265_multi_queue_pop_fisheye_meta(mq, &sample) != T265_OK) {
            break;
        }
        ++final_drained_fisheye;
    }

    memset(&queue_stats, 0, sizeof(queue_stats));
    memset(&image_stats, 0, sizeof(image_stats));
    memset(&reader_stats, 0, sizeof(reader_stats));
    t265_multi_queue_get_stats(mq, &queue_stats);
    t265_multi_queue_get_image_stats(mq, &image_stats);
    if (reader) {
        t265_get_reader_stats(reader, &reader_stats);
    }

    if (!atomic_load(&g_stop_requested) &&
        image_count > 0 &&
        image_stats.image_copy_fail == 0 &&
        image_stats.image_dropped == 0 &&
        queue_stats.fisheye_dropped == 0) {
        if (image_stats.image_buffer_reused > 0 || image_stats.image_remaining > 0) {
            result = "WARN";
        } else {
            result = "PASS";
        }
        rc = 0;
    }

    printf("\nImage queue long-run summary:\n");
    printf("  duration_sec:             %d\n", duration_sec);
    printf("  stop_requested:           %s\n", atomic_load(&g_stop_requested) ? "yes" : "no");
    printf("  image_count:              %lu\n", image_count);
    printf("  fisheye0_images:          %lu\n", fisheye0_count);
    printf("  fisheye1_images:          %lu\n", fisheye1_count);
    printf("  image_view_fail:          %lu\n", image_view_fail);
    printf("  saved_count:              %lu\n", saved_count);
    printf("  motion_popped:            %lu\n", motion_popped);
    printf("  final_drained_motion:     %d\n", final_drained_motion);
    printf("  final_drained_fisheye:    %d\n", final_drained_fisheye);

    printf("\nReader stats:\n");
    printf("  pose_count:               %lu\n", reader_stats.pose_count);
    printf("  gyro_count:               %lu\n", reader_stats.gyro_count);
    printf("  accel_count:              %lu\n", reader_stats.accel_count);
    printf("  fisheye0_count:           %lu\n", reader_stats.fisheye0_count);
    printf("  fisheye1_count:           %lu\n", reader_stats.fisheye1_count);
    printf("  interrupt_fail:           %lu\n", reader_stats.interrupt_fail);
    printf("  fisheye_fail:             %lu\n", reader_stats.fisheye_fail);

    printf("\nQueue stats:\n");
    printf("  motion_pushed:            %lu\n", queue_stats.motion_pushed);
    printf("  motion_popped:            %lu\n", queue_stats.motion_popped);
    printf("  motion_dropped:           %lu\n", queue_stats.motion_dropped);
    printf("  motion_remaining:         %d\n", queue_stats.motion_remaining);
    printf("  fisheye_pushed:           %lu\n", queue_stats.fisheye_pushed);
    printf("  fisheye_popped:           %lu\n", queue_stats.fisheye_popped);
    printf("  fisheye_dropped:          %lu\n", queue_stats.fisheye_dropped);
    printf("  fisheye_remaining:        %d\n", queue_stats.fisheye_remaining);

    printf("\nImage queue stats:\n");
    printf("  image_pushed:             %lu\n", image_stats.image_pushed);
    printf("  image_popped:             %lu\n", image_stats.image_popped);
    printf("  image_dropped:            %lu\n", image_stats.image_dropped);
    printf("  image_buffer_reused:      %lu\n", image_stats.image_buffer_reused);
    printf("  image_buffer_overwritten: %lu\n", image_stats.image_buffer_overwritten);
    printf("  image_copy_fail:          %lu\n", image_stats.image_copy_fail);
    printf("  image_bytes_copied:       %lu\n", image_stats.image_bytes_copied);
    printf("  image_remaining:          %d\n", image_stats.image_remaining);

    printf("\nResult:\n");
    printf("  IMAGE_QUEUE_LONG_RUN_RESULT: %s\n", result);

cleanup:
    if (reader) {
        t265_destroy_simple_reader(reader);
    }
    if (mq) {
        t265_multi_queue_destroy(mq);
    }

    return rc;
}
