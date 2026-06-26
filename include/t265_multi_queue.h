/*
 * T265 Multi Queue API
 *
 * Phase 7/8 status:
 * - Metadata motion/fisheye queues are stable logging / analysis candidates.
 * - Image payload queue is experimental but usable with ownership care.
 * - Blocking pop remains experimental until worker-thread API policy is final.
 *
 * 責務:
 * - Pose / Gyro / Accel / Fisheye metadata sample の queue 保持
 * - motion_queue と fisheye_meta_queue の分離管理
 * - nonblocking push / pop
 * - overflow時の drop count 管理
 * - ログ保存や時系列解析用途
 */

#ifndef T265_MULTI_QUEUE_H
#define T265_MULTI_QUEUE_H

#include "t265_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum t265_queue_sample_type {
    T265_QUEUE_SAMPLE_UNKNOWN = 0,
    T265_QUEUE_SAMPLE_POSE,
    T265_QUEUE_SAMPLE_GYRO,
    T265_QUEUE_SAMPLE_ACCEL,
    T265_QUEUE_SAMPLE_FISHEYE0,
    T265_QUEUE_SAMPLE_FISHEYE1
} t265_queue_sample_type;

typedef struct t265_queue_sample {
    t265_queue_sample_type type;
    uint64_t sequence;
    uint64_t timestamp_ns;

    union {
        t265_pose_sample pose;
        t265_imu_sample imu;

        struct {
            uint8_t sensor_id;
            uint32_t frame_id;
            uint64_t timestamp_ns;
            uint32_t width;
            uint32_t height;
            uint32_t frame_length;
            /*
             * Optional image payload pointer.
             *
             * Ownership:
             * - Metadata-only fisheye samples leave this NULL.
             * - t265_multi_queue_push_fisheye_frame() stores a borrowed pointer
             *   to a queue-owned internal circular image buffer.
             * - The pointer becomes invalid after t265_multi_queue_destroy().
             * - The pointed-to contents may be overwritten when the same
             *   internal buffer slot is reused.
             * - Copy frame_length bytes immediately after pop if the image must
             *   be retained or processed asynchronously.
             */
            uint8_t *frame_data;
        } fisheye;
    } data;
} t265_queue_sample;

typedef struct t265_multi_queue_stats {
    uint64_t motion_pushed;
    uint64_t motion_popped;
    uint64_t motion_dropped;
    int motion_remaining;

    uint64_t fisheye_pushed;
    uint64_t fisheye_popped;
    uint64_t fisheye_dropped;
    int fisheye_remaining;
} t265_multi_queue_stats;

typedef struct t265_image_queue_stats {
    uint64_t image_pushed;
    uint64_t image_popped;
    uint64_t image_dropped;
    /*
     * Counts circular image buffer slot reuse. This does not necessarily mean
     * a sample was dropped; check image_dropped/image_copy_fail for that.
     */
    uint64_t image_buffer_reused;
    /* Deprecated compatibility alias for image_buffer_reused. */
    uint64_t image_buffer_overwritten;
    uint64_t image_copy_fail;
    uint64_t image_bytes_copied;
    int image_buffer_count;
    int image_buffer_size;
    int image_remaining;
} t265_image_queue_stats;

/* Opaque struct to hide the queue arrays and mutexes */
typedef struct t265_multi_queue t265_multi_queue;

/*
 * Heap multi-queue lifecycle.
 *
 * t265_multi_queue_create() returns a heap-owned queue.
 * Release it with t265_multi_queue_destroy(); do not call free() yourself.
 *
 * Internal tools may still use t265_multi_queue_init() on stack storage from
 * src/t265_runtime_multi_queue.h. For stack queues, destroy only releases the
 * internal queue/image buffers and does not free the stack object.
 */
t265_multi_queue* t265_multi_queue_create(void);
void t265_multi_queue_destroy(t265_multi_queue *mq);

/* Push lightweight motion samples: Pose / Gyro / Accel. */
int t265_multi_queue_push_motion(t265_multi_queue *mq, const t265_queue_sample *sample);

/* Push Fisheye metadata only. Do not include raw image payload here. */
int t265_multi_queue_push_fisheye_meta(t265_multi_queue *mq, const t265_queue_sample *sample);

/* Experimental image payload push.
 *
 * Current Phase 7/8 ownership contract:
 * - frame_length must be greater than zero and fit in the internal image buffer.
 * - The input frame_data is copied into a queue-owned internal circular buffer
 *   using sample->data.fisheye.frame_length bytes.
 * - sample->data.fisheye.frame_data is replaced with a borrowed pointer to that
 *   internal buffer.
 * - The borrowed pointer is valid only while the queue exists and until that
 *   internal slot is reused.
 * - Long-running processing, worker threads, file saving, or OpenCV handoff
 *   should first deep-copy the image after pop.
 *
 * Prefer t265_multi_queue_push_fisheye_meta() for stable metadata-only logging.
 */
int t265_multi_queue_push_fisheye_frame(t265_multi_queue *mq, t265_queue_sample *sample, const uint8_t *frame_data);

/* Non-blocking pop APIs. Return T265_ERR_QUEUE_EMPTY when no sample is ready. */
int t265_multi_queue_pop_motion(t265_multi_queue *mq, t265_queue_sample *sample);
int t265_multi_queue_pop_fisheye_meta(t265_multi_queue *mq, t265_queue_sample *sample);

/* Blocking pop APIs are experimental worker-thread candidates. */
int t265_multi_queue_pop_motion_blocking(t265_multi_queue *mq, t265_queue_sample *sample, int timeout_ms);
int t265_multi_queue_pop_fisheye_meta_blocking(t265_multi_queue *mq, t265_queue_sample *sample, int timeout_ms);

/* Stats expose pushed/popped/dropped/remaining counts per queue. */
int t265_multi_queue_get_stats(t265_multi_queue *mq, t265_multi_queue_stats *stats);

/* Image payload stats for the experimental fisheye frame path. */
int t265_multi_queue_get_image_stats(t265_multi_queue *mq, t265_image_queue_stats *stats);

int t265_multi_queue_motion_size(t265_multi_queue *mq);
int t265_multi_queue_fisheye_size(t265_multi_queue *mq);

int t265_queue_capacity(void);

#ifdef __cplusplus
}
#endif

#endif // T265_MULTI_QUEUE_H
