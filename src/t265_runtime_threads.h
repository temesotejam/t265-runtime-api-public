/*
 * T265 Runtime Threads API
 *
 * 責務:
 * - interrupt endpoint 0x83 IN reader thread の管理
 * - bulk endpoint 0x81 IN fisheye reader thread の管理
 * - Pose / Gyro / Accel / Fisheye metadata のlatest state保持
 * - reader stats の保持
 * - 利用者が最新値を取得するためのAPI提供
 *
 * 担当しないこと:
 * - USB open / close
 * - command送受信
 * - stream configuration
 * - DEV_START / DEV_STOP
 * - Fisheye画像本体の保持
 * - queue API
 */

#ifndef T265_RUNTIME_THREADS_H
#define T265_RUNTIME_THREADS_H

#include "t265_internal.h"
#include <pthread.h>
#include <stdatomic.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "../include/t265_latest.h"
#include "../include/t265_multi_queue.h"

typedef struct t265_reader_context {
    t265_runtime *dev;

    pthread_t interrupt_thread;
    pthread_t fisheye_thread;

    pthread_mutex_t lock;
    atomic_int running;
    int threads_started;
    int mutex_initialized;
    int heap_allocated;
    int owns_runtime;
    int runtime_started_by_reader;

    t265_latest_state latest;
    t265_reader_stats stats;

    t265_multi_queue *mq;

    void (*on_pose)(const t265_pose_sample *pose, void *user_data);
    void (*on_gyro)(const t265_imu_sample *gyro, void *user_data);
    void (*on_accel)(const t265_imu_sample *accel, void *user_data);
    void (*on_fisheye)(const t265_fisheye_frame *frame, void *user_data);
    void *callback_user_data;
} t265_reader_context;

int t265_reader_init(t265_reader_context *ctx, t265_runtime *dev);
int t265_reader_set_multi_queue(t265_reader_context *ctx, t265_multi_queue *mq);
#ifdef __cplusplus
}
#endif

#endif // T265_RUNTIME_THREADS_H
