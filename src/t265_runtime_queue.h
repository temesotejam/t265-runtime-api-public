/*
 * T265 Runtime Queue API
 *
 * 責務:
 * - Pose / Gyro / Accel / Fisheye metadata sample のqueue保持
 * - nonblocking push / pop
 * - overflow時のdrop count管理
 * - queue stats の提供
 *
 * 担当しないこと:
 * - USB通信
 * - T265 command送受信
 * - stream configuration
 * - reader thread管理
 * - callback登録
 * - Fisheye画像本体の保持
 * - file logger
 * - OpenCV連携
 */

#ifndef T265_RUNTIME_QUEUE_H
#define T265_RUNTIME_QUEUE_H

#include <stdint.h>
#include <pthread.h>
#include "t265_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

#include "../include/t265_multi_queue.h"

#ifndef T265_QUEUE_CAPACITY
#define T265_QUEUE_CAPACITY 1024
#endif

typedef struct t265_sample_queue {
    pthread_mutex_t lock;
    pthread_cond_t cond;
    int mutex_initialized;
    int cond_initialized;

    t265_queue_sample samples[T265_QUEUE_CAPACITY];

    uint32_t head;
    uint32_t tail;
    uint32_t count;

    uint64_t next_sequence;

    uint64_t pushed_count;
    uint64_t popped_count;
    uint64_t dropped_count;
} t265_sample_queue;

int t265_queue_init(t265_sample_queue *queue);
void t265_queue_destroy(t265_sample_queue *queue);

int t265_queue_push(t265_sample_queue *queue, const t265_queue_sample *sample);
int t265_queue_pop(t265_sample_queue *queue, t265_queue_sample *sample);
int t265_queue_pop_blocking(t265_sample_queue *queue, t265_queue_sample *sample, int timeout_ms);

int t265_queue_size(t265_sample_queue *queue);
int t265_queue_capacity(void);

int t265_queue_get_stats(
    t265_sample_queue *queue,
    uint64_t *pushed_count,
    uint64_t *popped_count,
    uint64_t *dropped_count
);

#ifdef __cplusplus
}
#endif

#endif // T265_RUNTIME_QUEUE_H
