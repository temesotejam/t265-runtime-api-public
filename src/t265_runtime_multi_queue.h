#ifndef T265_RUNTIME_MULTI_QUEUE_H
#define T265_RUNTIME_MULTI_QUEUE_H

#include "t265_runtime_queue.h"
#include "t265_internal.h"
#include "../include/t265_multi_queue.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define T265_IMAGE_BUFFER_COUNT 32
#define T265_IMAGE_SIZE (848 * 800)

struct t265_multi_queue {
    int heap_allocated;

    t265_sample_queue motion_queue;
    t265_sample_queue fisheye_meta_queue;

    /* Circular buffer for image data */
    pthread_mutex_t image_lock;
    int image_lock_initialized;
    uint8_t *image_buffers[T265_IMAGE_BUFFER_COUNT];
    int image_buffer_write_idx;

    uint64_t image_pushed;
    uint64_t image_popped;
    uint64_t image_dropped;
    uint64_t image_buffer_reused;
    /* Deprecated compatibility alias for image_buffer_reused. */
    uint64_t image_buffer_overwritten;
    uint64_t image_copy_fail;
    uint64_t image_bytes_copied;
};

int t265_multi_queue_init(t265_multi_queue *mq);
int t265_multi_queue_push(t265_multi_queue *mq, const t265_queue_sample *sample);

int t265_multi_queue_push_motion(t265_multi_queue *mq, const t265_queue_sample *sample);
int t265_multi_queue_push_fisheye_meta(t265_multi_queue *mq, const t265_queue_sample *sample);

int t265_multi_queue_pop_motion(t265_multi_queue *mq, t265_queue_sample *sample);
int t265_multi_queue_pop_fisheye_meta(t265_multi_queue *mq, t265_queue_sample *sample);

int t265_multi_queue_get_stats(t265_multi_queue *mq, t265_multi_queue_stats *stats);

int t265_multi_queue_motion_size(t265_multi_queue *mq);
int t265_multi_queue_fisheye_size(t265_multi_queue *mq);

#ifdef __cplusplus
}
#endif

#endif // T265_RUNTIME_MULTI_QUEUE_H
