#ifndef T265_SYNCER_H
#define T265_SYNCER_H

#include "t265_types.h"
#include "t265_multi_queue.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * T265 Syncer API
 *
 * Phase 7/8 status:
 * - Experimental but usable timestamp association helper.
 * - This is a lightweight T265-specific helper, not a librealsense-compatible
 *   syncer.
 * - Default threshold is 20 ms. 15 ms is a strict candidate; 30 ms is relaxed.
 * - accepted/skipped stats are quality metrics and should be interpreted with
 *   the final USB/device state.
 */

typedef struct t265_frameset {
    t265_pose_sample pose;
    t265_imu_sample gyro;
    t265_imu_sample accel;
    
    uint32_t fisheye0_id;
    uint32_t fisheye1_id;
    uint64_t timestamp_ns;
    
    /*
     * Non-owning image pointers.
     *
     * These point at image payloads carried by queue samples. The syncer does
     * not own image payloads and does not extend their lifetime.
     *
     * With the current image queue path, these may point into a queue-owned
     * internal circular buffer. They become invalid after the source queue is
     * destroyed and their contents may be overwritten when the buffer slot is
     * reused. Copy image data before retaining a frameset, handing it to another
     * thread, saving it, or passing it to OpenCV.
     */
    uint8_t *fisheye0_data;
    uint8_t *fisheye1_data;
    
    int has_pose;
    int has_gyro;
    int has_accel;
    int has_fisheye0;
    int has_fisheye1;

    /*
     * Timestamp relationship to the frameset timestamp.
     *
     * Phase 7/8 policy:
     * - frameset timestamp is based on the Fisheye0/Fisheye1 pair timestamp.
     * - deltas compare selected Pose/Gyro/Accel timestamps against that base.
     * - the syncer keeps a small motion history and selects the nearest sample
     *   for each emitted frameset instead of blindly using the latest sample.
     * - threshold checks are advisory; out-of-threshold samples should be
     *   treated as WARN until real distributions are measured.
     */
    int64_t pose_delta_ns;
    int64_t gyro_delta_ns;
    int64_t accel_delta_ns;
    uint64_t pose_delta_abs_ns;
    uint64_t gyro_delta_abs_ns;
    uint64_t accel_delta_abs_ns;
    uint64_t threshold_ns;
    int pose_within_threshold;
    int gyro_within_threshold;
    int accel_within_threshold;
} t265_frameset;

typedef struct t265_syncer t265_syncer;

typedef struct t265_syncer_stats {
    uint64_t samples_processed;
    uint64_t motion_samples;
    uint64_t pose_samples;
    uint64_t gyro_samples;
    uint64_t accel_samples;
    uint64_t fisheye0_samples;
    uint64_t fisheye1_samples;

    uint64_t frameset_emitted;
    uint64_t frameset_with_pose;
    uint64_t frameset_with_gyro;
    uint64_t frameset_with_accel;
    uint64_t frameset_without_pose;
    uint64_t frameset_without_gyro;
    uint64_t frameset_without_accel;

    uint64_t fisheye_pair_matched;
    uint64_t fisheye_pair_mismatch;
    uint64_t fisheye_pair_waiting;

    uint64_t pose_delta_count;
    uint64_t gyro_delta_count;
    uint64_t accel_delta_count;

    uint64_t pose_delta_abs_min_ns;
    uint64_t pose_delta_abs_max_ns;
    uint64_t pose_delta_abs_sum_ns;

    uint64_t gyro_delta_abs_min_ns;
    uint64_t gyro_delta_abs_max_ns;
    uint64_t gyro_delta_abs_sum_ns;

    uint64_t accel_delta_abs_min_ns;
    uint64_t accel_delta_abs_max_ns;
    uint64_t accel_delta_abs_sum_ns;

    uint64_t pose_out_of_threshold;
    uint64_t gyro_out_of_threshold;
    uint64_t accel_out_of_threshold;
} t265_syncer_stats;

t265_syncer* t265_syncer_create(void);
void t265_syncer_destroy(t265_syncer *syncer);

/* Process a sample from the queue and return 1 if a new frameset is ready.
 *
 * Return convention:
 * - 1 means a new frameset is ready.
 * - 0 means no frameset is ready.
 * - negative values are error codes.
 */
int t265_syncer_process(t265_syncer *syncer, const t265_queue_sample *sample, t265_frameset *out_frameset);
void t265_syncer_reset_stats(t265_syncer *syncer);
void t265_syncer_get_stats(const t265_syncer *syncer, t265_syncer_stats *stats);
int t265_syncer_set_threshold_ns(t265_syncer *syncer, uint64_t threshold_ns);
uint64_t t265_syncer_get_threshold_ns(const t265_syncer *syncer);

#ifdef __cplusplus
}
#endif

#endif // T265_SYNCER_H
