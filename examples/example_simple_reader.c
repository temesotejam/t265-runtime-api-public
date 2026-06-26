/*
 * T265 Simple Reader Example
 *
 * Purpose:
 * - Demonstrate the high-level simple reader factory.
 * - Hide the low-level open/configure/start sequence from the caller.
 * - Read latest Pose and pop Fisheye metadata from a multi-queue.
 *
 * Usage:
 *   ./example_simple_reader
 *
 * Expected result:
 *   EXAMPLE_SIMPLE_READER_RESULT: PASS
 */

#include "t265.h"

#include <stdio.h>
#include <unistd.h>

#define EXAMPLE_SIMPLE_READER_LOOP_COUNT 50
#define EXAMPLE_SIMPLE_READER_SLEEP_US 100000

int main(void)
{
    t265_multi_queue *mq = NULL;
    t265_reader_context *reader = NULL;
    int pose_count = 0;
    int fisheye_meta_count = 0;
    int rc = 1;

    printf("--- T265 Simple Reader Example ---\n");

    mq = t265_multi_queue_create();
    if (!mq) {
        fprintf(stderr, "Failed to create multi-queue.\n");
        goto cleanup;
    }

    /*
     * Passing NULL asks the factory to create and own the low-level runtime.
     * Use t265_destroy_simple_reader() for the matching cleanup path.
     */
    reader = t265_create_simple_reader(NULL, 1, 1, 1, mq);
    if (!reader) {
        fprintf(stderr, "Failed to create reader.\n");
        goto cleanup;
    }

    printf("Reader started. Polling for 5 seconds...\n");

    for (int i = 0; i < EXAMPLE_SIMPLE_READER_LOOP_COUNT; ++i) {
        t265_pose_sample pose;
        t265_queue_sample sample;

        if (t265_get_latest_pose(reader, &pose) == T265_OK) {
            ++pose_count;
            printf("Pose: %.3f %.3f %.3f\n", pose.x, pose.y, pose.z);
        }

        while (t265_multi_queue_pop_fisheye_meta(mq, &sample) == T265_OK) {
            ++fisheye_meta_count;
        }

        usleep(EXAMPLE_SIMPLE_READER_SLEEP_US);
    }

    printf("\nSimple reader summary:\n");
    printf("  pose_count:         %d\n", pose_count);
    printf("  fisheye_meta_count: %d\n", fisheye_meta_count);

    if (pose_count > 0 && fisheye_meta_count > 0) {
        printf("EXAMPLE_SIMPLE_READER_RESULT: PASS\n");
        rc = 0;
    } else {
        printf("EXAMPLE_SIMPLE_READER_RESULT: FAIL\n");
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
