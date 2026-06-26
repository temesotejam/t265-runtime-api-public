/*
 * T265 Latest State API
 *
 * Phase 7/8 status:
 * - Stable control-path API.
 * - Use this when the application needs the current Pose / Gyro / Accel /
 *   Fisheye metadata and does not need to process every historical sample.
 *
 * 責務:
 * - interrupt endpoint 0x83 IN reader thread の管理
 * - bulk endpoint 0x81 IN fisheye reader thread の管理
 * - Pose / Gyro / Accel / Fisheye metadata のlatest state保持
 * - reader stats の保持
 * - 利用者が最新値を取得するためのAPI提供
 */

#ifndef T265_LATEST_H
#define T265_LATEST_H

#include "t265_types.h"
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct t265_latest_state {
    int has_pose;
    int has_gyro;
    int has_accel;
    int has_fisheye0;
    int has_fisheye1;

    t265_pose_sample pose;
    t265_imu_sample gyro;
    t265_imu_sample accel;

    uint32_t fisheye0_frame_id;
    uint32_t fisheye1_frame_id;
    uint64_t fisheye0_timestamp_ns;
    uint64_t fisheye1_timestamp_ns;

    uint64_t pose_update_count;
    uint64_t gyro_update_count;
    uint64_t accel_update_count;
    uint64_t fisheye0_update_count;
    uint64_t fisheye1_update_count;
} t265_latest_state;

typedef struct t265_reader_stats {
    uint64_t interrupt_attempts;
    uint64_t interrupt_success;
    uint64_t interrupt_fail;
    uint64_t interrupt_max_fail_run;

    uint64_t fisheye_attempts;
    uint64_t fisheye_success;
    uint64_t fisheye_fail;
    uint64_t fisheye_max_fail_run;

    uint64_t pose_count;
    uint64_t gyro_count;
    uint64_t accel_count;
    uint64_t fisheye0_count;
    uint64_t fisheye1_count;
} t265_reader_stats;

/* Opaque struct to hide internal atomic types and implementation details */
typedef struct t265_reader_context t265_reader_context;

/* Allocate and initialize a reader context.
 *
 * Ownership:
 * - The supplied t265_runtime is not owned by the reader.
 * - Stop/destroy the reader before stopping/closing the runtime.
 */
t265_reader_context* t265_reader_create(t265_runtime *dev);

/* Start background threads */
int t265_reader_start(t265_reader_context *ctx);

/* Stop background threads */
int t265_reader_stop(t265_reader_context *ctx);

/* Stop threads and release reader resources.
 *
 * Phase 7 cleanup policy:
 * - Use t265_reader_free() for heap readers returned by t265_reader_create().
 * - Stack readers used by internal tools/probes are destroyed but not freed.
 */
void t265_reader_destroy(t265_reader_context *ctx);

/* Free a heap-allocated reader created by t265_reader_create().
 *
 * Do not call this for stack-allocated readers initialized with
 * t265_reader_init().
 */
void t265_reader_free(t265_reader_context *ctx);

/* Set optional callbacks.
 *
 * Callback rules:
 * - Callbacks execute on background reader threads.
 * - Keep callbacks fast and non-blocking.
 * - Do not printf, sleep, save files, or process images inside callbacks.
 * - Recommended use is to push lightweight samples into a multi-queue.
 * - Fisheye frame_data is not owned by the callback; copy it if it must live
 *   beyond the callback.
 */
int t265_reader_set_callbacks(
    t265_reader_context *ctx,
    void (*on_pose)(const t265_pose_sample *pose, void *user_data),
    void (*on_gyro)(const t265_imu_sample *gyro, void *user_data),
    void (*on_accel)(const t265_imu_sample *accel, void *user_data),
    void (*on_fisheye)(const t265_fisheye_frame *frame, void *user_data),
    void *user_data
);

/* Retrieve latest samples */
int t265_get_latest_pose(t265_reader_context *ctx, t265_pose_sample *pose);
int t265_get_latest_gyro(t265_reader_context *ctx, t265_imu_sample *gyro);
int t265_get_latest_accel(t265_reader_context *ctx, t265_imu_sample *accel);

/* Retrieve latest fisheye metadata */
int t265_get_latest_fisheye_info(
    t265_reader_context *ctx,
    uint32_t *fisheye0_frame_id,
    uint32_t *fisheye1_frame_id,
    uint64_t *fisheye0_timestamp_ns,
    uint64_t *fisheye1_timestamp_ns
);

/* Get internal stats */
int t265_get_reader_stats(t265_reader_context *ctx, t265_reader_stats *stats);

/* Availability checkers */
int t265_reader_has_pose(t265_reader_context *ctx);
int t265_reader_has_gyro(t265_reader_context *ctx);
int t265_reader_has_accel(t265_reader_context *ctx);
int t265_reader_has_fisheye0(t265_reader_context *ctx);
int t265_reader_has_fisheye1(t265_reader_context *ctx);

#ifdef __cplusplus
}
#endif

#endif // T265_LATEST_H
