/*
 * T265 Multi-Queue API Example
 *
 * Purpose:
 * - Demonstrate the log/analysis-oriented multi-queue flow.
 * - Split motion samples and Fisheye metadata into separate queues.
 * - Show queue stats and explicit PASS/FAIL output.
 *
 * Usage:
 *   ./example_multi_queue
 *
 * Expected result:
 *   EXAMPLE_MULTI_QUEUE_RESULT: PASS
 */

#include "t265.h"

#include <stdio.h>
#include <unistd.h>

#define EXAMPLE_MULTI_QUEUE_LOOP_COUNT 50
#define EXAMPLE_MULTI_QUEUE_SLEEP_US 100000

int main(void)
{
    t265_multi_queue *mq = NULL;
    t265_reader_context *reader = NULL;
    t265_multi_queue_stats stats;
    int motion_count = 0;
    int fisheye_meta_count = 0;
    int rc = 1;

    printf("--- T265 Multi-Queue API Example ---\n");

    mq = t265_multi_queue_create();
    if (!mq) {
        fprintf(stderr, "Failed to create multi-queue.\n");
        goto cleanup;
    }

    printf("Multi-queue created. Capacity: %d\n", t265_queue_capacity());

    reader = t265_create_simple_reader(NULL, 1, 1, 1, mq);
    if (!reader) {
        fprintf(stderr, "Failed to create reader.\n");
        goto cleanup;
    }

    for (int i = 0; i < EXAMPLE_MULTI_QUEUE_LOOP_COUNT; ++i) {
        t265_queue_sample sample;

        usleep(EXAMPLE_MULTI_QUEUE_SLEEP_US);

        while (t265_multi_queue_pop_motion(mq, &sample) == T265_OK) {
            ++motion_count;
        }

        while (t265_multi_queue_pop_fisheye_meta(mq, &sample) == T265_OK) {
            ++fisheye_meta_count;
        }
    }

    if (t265_multi_queue_get_stats(mq, &stats) != T265_OK) {
        fprintf(stderr, "Failed to read multi-queue stats.\n");
        goto cleanup;
    }

    printf("\nMulti-queue summary:\n");
    printf("  motion_count:       %d\n", motion_count);
    printf("  fisheye_meta_count: %d\n", fisheye_meta_count);
    printf("  motion_pushed:      %lu\n", stats.motion_pushed);
    printf("  motion_popped:      %lu\n", stats.motion_popped);
    printf("  motion_dropped:     %lu\n", stats.motion_dropped);
    printf("  fisheye_pushed:     %lu\n", stats.fisheye_pushed);
    printf("  fisheye_popped:     %lu\n", stats.fisheye_popped);
    printf("  fisheye_dropped:    %lu\n", stats.fisheye_dropped);

    if (motion_count > 0 &&
        fisheye_meta_count > 0 &&
        stats.motion_dropped == 0 &&
        stats.fisheye_dropped == 0) {
        printf("EXAMPLE_MULTI_QUEUE_RESULT: PASS\n");
        rc = 0;
    } else {
        printf("EXAMPLE_MULTI_QUEUE_RESULT: FAIL\n");
    }

cleanup:
    if (reader) {
        t265_destroy_simple_reader(reader);
    }
    if (mq) {
        t265_multi_queue_destroy(mq);
    }

    return rc;
}
