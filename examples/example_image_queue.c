/*
 * T265 Image Queue API Example
 *
 * Purpose:
 * - Demonstrate the Phase 3 Fisheye image payload path.
 * - Convert a queued Fisheye payload sample into t265_image_view.
 * - Deep-copy one image into t265_owned_image before saving it as PGM.
 * - Show image payload stats separately from metadata queue stats.
 *
 * Usage:
 *   ./example_image_queue
 *
 * Expected result:
 *   EXAMPLE_IMAGE_QUEUE_RESULT: PASS
 */

#include "t265.h"

#include <stdio.h>
#include <unistd.h>

#define EXAMPLE_IMAGE_QUEUE_TARGET_FRAMES 30
#define EXAMPLE_IMAGE_QUEUE_LOOP_LIMIT 80
#define EXAMPLE_IMAGE_QUEUE_SLEEP_US 100000

int main(void)
{
    t265_multi_queue *mq = NULL;
    t265_reader_context *reader = NULL;
    t265_multi_queue_stats queue_stats;
    t265_image_queue_stats image_stats;
    int image_count = 0;
    int saved_count = 0;
    int rc = 1;

    printf("--- T265 Image Queue API Example ---\n");

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

    for (int i = 0; i < EXAMPLE_IMAGE_QUEUE_LOOP_LIMIT && image_count < EXAMPLE_IMAGE_QUEUE_TARGET_FRAMES; ++i) {
        t265_queue_sample sample;

        usleep(EXAMPLE_IMAGE_QUEUE_SLEEP_US);

        while (image_count < EXAMPLE_IMAGE_QUEUE_TARGET_FRAMES &&
               t265_multi_queue_pop_fisheye_meta(mq, &sample) == T265_OK) {
            t265_image_view view;

            if (t265_image_view_from_queue_sample(&sample, &view) != T265_OK) {
                continue;
            }

            ++image_count;

            if (saved_count == 0) {
                t265_owned_image owned;
                char path[96];

                if (t265_owned_image_copy_from_view(&view, &owned) != T265_OK) {
                    fprintf(stderr, "Failed to copy image payload.\n");
                    goto cleanup;
                }

                snprintf(path, sizeof(path), "example_image_queue_fe%d_%u.pgm",
                         owned.sensor_id == T265_SENSOR_ID_FISHEYE0 ? 0 : 1,
                         owned.frame_id);

                if (t265_write_pgm_from_owned_image(path, &owned) == T265_OK) {
                    ++saved_count;
                    printf("Saved %s\n", path);
                } else {
                    fprintf(stderr, "Failed to write PGM.\n");
                }

                t265_owned_image_destroy(&owned);
            }
        }
    }

    if (t265_multi_queue_get_stats(mq, &queue_stats) != T265_OK) {
        fprintf(stderr, "Failed to read queue stats.\n");
        goto cleanup;
    }
    if (t265_multi_queue_get_image_stats(mq, &image_stats) != T265_OK) {
        fprintf(stderr, "Failed to read image stats.\n");
        goto cleanup;
    }

    printf("\nImage queue summary:\n");
    printf("  image_count:              %d\n", image_count);
    printf("  saved_count:              %d\n", saved_count);
    printf("  fisheye_pushed:           %lu\n", queue_stats.fisheye_pushed);
    printf("  fisheye_popped:           %lu\n", queue_stats.fisheye_popped);
    printf("  fisheye_dropped:          %lu\n", queue_stats.fisheye_dropped);
    printf("  image_pushed:             %lu\n", image_stats.image_pushed);
    printf("  image_popped:             %lu\n", image_stats.image_popped);
    printf("  image_dropped:            %lu\n", image_stats.image_dropped);
    printf("  image_buffer_reused:      %lu\n", image_stats.image_buffer_reused);
    printf("  image_buffer_overwritten: %lu\n", image_stats.image_buffer_overwritten);
    printf("  image_copy_fail:          %lu\n", image_stats.image_copy_fail);
    printf("  image_bytes_copied:       %lu\n", image_stats.image_bytes_copied);
    printf("  image_remaining:          %d\n", image_stats.image_remaining);

    if (image_count > 0 &&
        saved_count > 0 &&
        image_stats.image_pushed > 0 &&
        image_stats.image_popped > 0 &&
        image_stats.image_copy_fail == 0) {
        printf("EXAMPLE_IMAGE_QUEUE_RESULT: PASS\n");
        rc = 0;
    } else {
        printf("EXAMPLE_IMAGE_QUEUE_RESULT: FAIL\n");
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
