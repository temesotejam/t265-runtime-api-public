#ifndef T265_IMAGE_H
#define T265_IMAGE_H

#include "t265_multi_queue.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * T265 Image API
 *
 * Phase 7/8 status:
 * - Experimental but usable image payload helpers.
 * - t265_image_view is non-owning.
 * - t265_owned_image is the safe deep-copy form for retention, saving,
 *   worker-thread handoff, or OpenCV use.
 * - Do not perform image processing or file I/O inside callbacks.
 */

/*
 * Non-owning image view.
 *
 * This is a lightweight view over image bytes carried by a queue sample.
 * It does not own data and does not extend the lifetime of the source buffer.
 * Copy to t265_owned_image if the image must be retained, saved later, handed
 * to another thread, or passed to a library that may outlive the queue sample.
 */
typedef struct t265_image_view {
    uint8_t sensor_id;
    uint32_t frame_id;
    uint64_t timestamp_ns;
    uint32_t width;
    uint32_t height;
    uint32_t frame_length;
    const uint8_t *data;
} t265_image_view;

/* Owned deep-copy image. Destroy with t265_owned_image_destroy(). */
typedef struct t265_owned_image {
    uint8_t sensor_id;
    uint32_t frame_id;
    uint64_t timestamp_ns;
    uint32_t width;
    uint32_t height;
    uint32_t frame_length;
    uint8_t *data;
} t265_owned_image;

/* Build a non-owning image view from a Fisheye queue sample with payload. */
int t265_image_view_from_queue_sample(
    const t265_queue_sample *sample,
    t265_image_view *out_view
);

/* Deep-copy a view into an owned image. */
int t265_owned_image_copy_from_view(
    const t265_image_view *view,
    t265_owned_image *out_image
);

/* Convenience helper: build a view from a sample, then deep-copy it. */
int t265_owned_image_copy_from_queue_sample(
    const t265_queue_sample *sample,
    t265_owned_image *out_image
);

/* Release owned image memory and reset the struct to zero. */
void t265_owned_image_destroy(t265_owned_image *image);

/* Optional PGM utilities. Call after queue pop or from a worker, not callback. */
int t265_write_pgm_from_view(const char *path, const t265_image_view *view);
int t265_write_pgm_from_owned_image(const char *path, const t265_owned_image *image);

#ifdef __cplusplus
}
#endif

#endif // T265_IMAGE_H
