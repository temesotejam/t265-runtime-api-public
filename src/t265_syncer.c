#include "t265_syncer.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#define T265_SYNCER_DEFAULT_THRESHOLD_NS 20000000ULL
#define T265_SYNCER_MOTION_HISTORY_CAPACITY 128

struct t265_syncer {
    t265_pose_sample latest_pose;
    t265_imu_sample latest_gyro;
    t265_imu_sample latest_accel;
    
    int has_pose;
    int has_gyro;
    int has_accel;

    t265_pose_sample pose_history[T265_SYNCER_MOTION_HISTORY_CAPACITY];
    t265_imu_sample gyro_history[T265_SYNCER_MOTION_HISTORY_CAPACITY];
    t265_imu_sample accel_history[T265_SYNCER_MOTION_HISTORY_CAPACITY];
    int pose_history_count;
    int gyro_history_count;
    int accel_history_count;
    int pose_history_next;
    int gyro_history_next;
    int accel_history_next;

    uint32_t fe0_id;
    uint32_t fe1_id;
    uint8_t *fe0_ptr;
    uint8_t *fe1_ptr;
    uint64_t fe_timestamp_ns;
    uint64_t fe1_timestamp_ns;

    uint64_t threshold_ns;
    t265_syncer_stats stats;
};

t265_syncer* t265_syncer_create(void) {
    t265_syncer *syncer = calloc(1, sizeof(t265_syncer));
    if (syncer) {
        syncer->threshold_ns = T265_SYNCER_DEFAULT_THRESHOLD_NS;
    }
    return syncer;
}

void t265_syncer_destroy(t265_syncer *syncer) {
    free(syncer);
}

static int64_t signed_delta_ns(uint64_t sample_ts, uint64_t base_ts) {
    uint64_t diff;
    if (sample_ts >= base_ts) {
        diff = sample_ts - base_ts;
        return diff > (uint64_t)INT64_MAX ? INT64_MAX : (int64_t)diff;
    }
    diff = base_ts - sample_ts;
    return diff > (uint64_t)INT64_MAX ? INT64_MIN : -(int64_t)diff;
}

static uint64_t abs_delta_ns(uint64_t sample_ts, uint64_t base_ts) {
    return sample_ts >= base_ts ? sample_ts - base_ts : base_ts - sample_ts;
}

static void push_pose_history(t265_syncer *syncer, const t265_pose_sample *pose) {
    syncer->pose_history[syncer->pose_history_next] = *pose;
    syncer->pose_history_next = (syncer->pose_history_next + 1) % T265_SYNCER_MOTION_HISTORY_CAPACITY;
    if (syncer->pose_history_count < T265_SYNCER_MOTION_HISTORY_CAPACITY) {
        syncer->pose_history_count++;
    }
}

static void push_gyro_history(t265_syncer *syncer, const t265_imu_sample *gyro) {
    syncer->gyro_history[syncer->gyro_history_next] = *gyro;
    syncer->gyro_history_next = (syncer->gyro_history_next + 1) % T265_SYNCER_MOTION_HISTORY_CAPACITY;
    if (syncer->gyro_history_count < T265_SYNCER_MOTION_HISTORY_CAPACITY) {
        syncer->gyro_history_count++;
    }
}

static void push_accel_history(t265_syncer *syncer, const t265_imu_sample *accel) {
    syncer->accel_history[syncer->accel_history_next] = *accel;
    syncer->accel_history_next = (syncer->accel_history_next + 1) % T265_SYNCER_MOTION_HISTORY_CAPACITY;
    if (syncer->accel_history_count < T265_SYNCER_MOTION_HISTORY_CAPACITY) {
        syncer->accel_history_count++;
    }
}

static int select_nearest_pose(const t265_syncer *syncer, uint64_t base_ts, t265_pose_sample *out) {
    uint64_t best_delta = 0;
    int best_index = -1;

    for (int i = 0; i < syncer->pose_history_count; ++i) {
        uint64_t delta = abs_delta_ns(syncer->pose_history[i].timestamp_ns, base_ts);
        if (best_index < 0 || delta < best_delta) {
            best_delta = delta;
            best_index = i;
        }
    }

    if (best_index < 0) return 0;
    *out = syncer->pose_history[best_index];
    return 1;
}

static int select_nearest_imu(const t265_imu_sample *history,
                              int history_count,
                              uint64_t base_ts,
                              t265_imu_sample *out) {
    uint64_t best_delta = 0;
    int best_index = -1;

    for (int i = 0; i < history_count; ++i) {
        uint64_t delta = abs_delta_ns(history[i].timestamp_ns, base_ts);
        if (best_index < 0 || delta < best_delta) {
            best_delta = delta;
            best_index = i;
        }
    }

    if (best_index < 0) return 0;
    *out = history[best_index];
    return 1;
}

static void update_delta_stats(uint64_t delta,
                               uint64_t *count,
                               uint64_t *min_value,
                               uint64_t *max_value,
                               uint64_t *sum_value) {
    if (*count == 0 || delta < *min_value) {
        *min_value = delta;
    }
    if (*count == 0 || delta > *max_value) {
        *max_value = delta;
    }
    *sum_value += delta;
    (*count)++;
}

static void update_frameset_stats(t265_syncer *syncer, t265_frameset *out) {
    t265_syncer_stats *stats = &syncer->stats;

    stats->frameset_emitted++;

    if (out->has_pose) {
        stats->frameset_with_pose++;
        out->pose_delta_ns = signed_delta_ns(out->pose.timestamp_ns, out->timestamp_ns);
        out->pose_delta_abs_ns = abs_delta_ns(out->pose.timestamp_ns, out->timestamp_ns);
        out->pose_within_threshold = out->pose_delta_abs_ns <= syncer->threshold_ns;
        update_delta_stats(out->pose_delta_abs_ns,
                           &stats->pose_delta_count,
                           &stats->pose_delta_abs_min_ns,
                           &stats->pose_delta_abs_max_ns,
                           &stats->pose_delta_abs_sum_ns);
        if (!out->pose_within_threshold) {
            stats->pose_out_of_threshold++;
        }
    } else {
        stats->frameset_without_pose++;
    }

    if (out->has_gyro) {
        stats->frameset_with_gyro++;
        out->gyro_delta_ns = signed_delta_ns(out->gyro.timestamp_ns, out->timestamp_ns);
        out->gyro_delta_abs_ns = abs_delta_ns(out->gyro.timestamp_ns, out->timestamp_ns);
        out->gyro_within_threshold = out->gyro_delta_abs_ns <= syncer->threshold_ns;
        update_delta_stats(out->gyro_delta_abs_ns,
                           &stats->gyro_delta_count,
                           &stats->gyro_delta_abs_min_ns,
                           &stats->gyro_delta_abs_max_ns,
                           &stats->gyro_delta_abs_sum_ns);
        if (!out->gyro_within_threshold) {
            stats->gyro_out_of_threshold++;
        }
    } else {
        stats->frameset_without_gyro++;
    }

    if (out->has_accel) {
        stats->frameset_with_accel++;
        out->accel_delta_ns = signed_delta_ns(out->accel.timestamp_ns, out->timestamp_ns);
        out->accel_delta_abs_ns = abs_delta_ns(out->accel.timestamp_ns, out->timestamp_ns);
        out->accel_within_threshold = out->accel_delta_abs_ns <= syncer->threshold_ns;
        update_delta_stats(out->accel_delta_abs_ns,
                           &stats->accel_delta_count,
                           &stats->accel_delta_abs_min_ns,
                           &stats->accel_delta_abs_max_ns,
                           &stats->accel_delta_abs_sum_ns);
        if (!out->accel_within_threshold) {
            stats->accel_out_of_threshold++;
        }
    } else {
        stats->frameset_without_accel++;
    }
}

int t265_syncer_process(t265_syncer *syncer, const t265_queue_sample *sample, t265_frameset *out) {
    if (!syncer || !sample || !out) return T265_ERR_INVALID_STATE;

    syncer->stats.samples_processed++;

    switch (sample->type) {
        case T265_QUEUE_SAMPLE_POSE:
            syncer->latest_pose = sample->data.pose;
            syncer->has_pose = 1;
            push_pose_history(syncer, &sample->data.pose);
            syncer->stats.motion_samples++;
            syncer->stats.pose_samples++;
            break;
        case T265_QUEUE_SAMPLE_GYRO:
            syncer->latest_gyro = sample->data.imu;
            syncer->has_gyro = 1;
            push_gyro_history(syncer, &sample->data.imu);
            syncer->stats.motion_samples++;
            syncer->stats.gyro_samples++;
            break;
        case T265_QUEUE_SAMPLE_ACCEL:
            syncer->latest_accel = sample->data.imu;
            syncer->has_accel = 1;
            push_accel_history(syncer, &sample->data.imu);
            syncer->stats.motion_samples++;
            syncer->stats.accel_samples++;
            break;
        case T265_QUEUE_SAMPLE_FISHEYE0:
            syncer->fe0_id = sample->data.fisheye.frame_id;
            syncer->fe0_ptr = sample->data.fisheye.frame_data;
            syncer->fe_timestamp_ns = sample->data.fisheye.timestamp_ns;
            syncer->stats.fisheye0_samples++;
            break;
        case T265_QUEUE_SAMPLE_FISHEYE1:
            syncer->fe1_id = sample->data.fisheye.frame_id;
            syncer->fe1_ptr = sample->data.fisheye.frame_data;
            syncer->fe1_timestamp_ns = sample->data.fisheye.timestamp_ns;
            syncer->stats.fisheye1_samples++;
            // If we have both FE0 and FE1 for the same frame ID, emit frameset
            if (syncer->fe0_id == syncer->fe1_id && syncer->fe0_ptr && syncer->fe1_ptr) {
                memset(out, 0, sizeof(*out));

                out->fisheye0_id = syncer->fe0_id;
                out->fisheye1_id = syncer->fe1_id;
                out->fisheye0_data = syncer->fe0_ptr;
                out->fisheye1_data = syncer->fe1_ptr;
                out->timestamp_ns = syncer->fe_timestamp_ns;
                out->has_fisheye0 = 1;
                out->has_fisheye1 = 1;
                out->threshold_ns = syncer->threshold_ns;

                out->has_pose = select_nearest_pose(syncer, out->timestamp_ns, &out->pose);
                out->has_gyro = select_nearest_imu(syncer->gyro_history,
                                                   syncer->gyro_history_count,
                                                   out->timestamp_ns,
                                                   &out->gyro);
                out->has_accel = select_nearest_imu(syncer->accel_history,
                                                    syncer->accel_history_count,
                                                    out->timestamp_ns,
                                                    &out->accel);

                syncer->stats.fisheye_pair_matched++;
                update_frameset_stats(syncer, out);
                
                // Clear pointers to avoid re-emitting
                syncer->fe0_ptr = NULL;
                syncer->fe1_ptr = NULL;
                return 1;
            }
            if (syncer->fe0_ptr) {
                syncer->stats.fisheye_pair_mismatch++;
            } else {
                syncer->stats.fisheye_pair_waiting++;
            }
            break;
        default:
            break;
    }
    return 0;
}

void t265_syncer_reset_stats(t265_syncer *syncer) {
    if (!syncer) return;
    memset(&syncer->stats, 0, sizeof(syncer->stats));
}

void t265_syncer_get_stats(const t265_syncer *syncer, t265_syncer_stats *stats) {
    if (!syncer || !stats) return;
    *stats = syncer->stats;
}

int t265_syncer_set_threshold_ns(t265_syncer *syncer, uint64_t threshold_ns) {
    if (!syncer) return T265_ERR_INVALID_STATE;
    syncer->threshold_ns = threshold_ns;
    return T265_OK;
}

uint64_t t265_syncer_get_threshold_ns(const t265_syncer *syncer) {
    if (!syncer) return 0;
    return syncer->threshold_ns;
}
