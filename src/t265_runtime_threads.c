#include "t265_runtime_threads.h"
#include <string.h>
#include <pthread.h>
#include <stdlib.h>

// Timeout settings. In the future these could be configurable.
#define T265_READER_INTERRUPT_TIMEOUT_MS 10
#define T265_READER_FISHEYE_TIMEOUT_MS   100

static void *t265_interrupt_reader_main(void *arg) {
    t265_reader_context *ctx = (t265_reader_context *)arg;
    uint64_t consec_fail = 0;

    while (atomic_load(&ctx->running)) {
        t265_interrupt_sample sample;
        int status = t265_read_next_interrupt_sample(ctx->dev, &sample, T265_READER_INTERRUPT_TIMEOUT_MS);

        pthread_mutex_lock(&ctx->lock);
        ctx->stats.interrupt_attempts++;

        if (status == T265_OK) {
            ctx->stats.interrupt_success++;
            consec_fail = 0;

            if (sample.type == T265_SAMPLE_POSE) {
                void (*on_pose)(const t265_pose_sample *, void *) = NULL;
                void *user_data = NULL;
                t265_pose_sample pose_copy = sample.data.pose;
                ctx->latest.pose = sample.data.pose;
                ctx->latest.has_pose = 1;
                ctx->latest.pose_update_count++;
                ctx->stats.pose_count++;
                on_pose = ctx->on_pose;
                user_data = ctx->callback_user_data;
                pthread_mutex_unlock(&ctx->lock);
                if (on_pose) on_pose(&pose_copy, user_data);
                
                if (ctx->mq) {
                    t265_queue_sample qs;
                    memset(&qs, 0, sizeof(qs));
                    qs.type = T265_QUEUE_SAMPLE_POSE;
                    qs.timestamp_ns = pose_copy.timestamp_ns;
                    qs.data.pose = pose_copy;
                    t265_multi_queue_push_motion(ctx->mq, &qs);
                }
                continue;
            } else if (sample.type == T265_SAMPLE_GYRO) {
                void (*on_gyro)(const t265_imu_sample *, void *) = NULL;
                void *user_data = NULL;
                t265_imu_sample imu_copy = sample.data.imu;
                ctx->latest.gyro = sample.data.imu;
                ctx->latest.has_gyro = 1;
                ctx->latest.gyro_update_count++;
                ctx->stats.gyro_count++;
                on_gyro = ctx->on_gyro;
                user_data = ctx->callback_user_data;
                pthread_mutex_unlock(&ctx->lock);
                if (on_gyro) on_gyro(&imu_copy, user_data);
                
                if (ctx->mq) {
                    t265_queue_sample qs;
                    memset(&qs, 0, sizeof(qs));
                    qs.type = T265_QUEUE_SAMPLE_GYRO;
                    qs.timestamp_ns = imu_copy.timestamp_ns;
                    qs.data.imu = imu_copy;
                    t265_multi_queue_push_motion(ctx->mq, &qs);
                }
                continue;
            } else if (sample.type == T265_SAMPLE_ACCEL) {
                void (*on_accel)(const t265_imu_sample *, void *) = NULL;
                void *user_data = NULL;
                t265_imu_sample imu_copy = sample.data.imu;
                ctx->latest.accel = sample.data.imu;
                ctx->latest.has_accel = 1;
                ctx->latest.accel_update_count++;
                ctx->stats.accel_count++;
                on_accel = ctx->on_accel;
                user_data = ctx->callback_user_data;
                pthread_mutex_unlock(&ctx->lock);
                if (on_accel) on_accel(&imu_copy, user_data);
                
                if (ctx->mq) {
                    t265_queue_sample qs;
                    memset(&qs, 0, sizeof(qs));
                    qs.type = T265_QUEUE_SAMPLE_ACCEL;
                    qs.timestamp_ns = imu_copy.timestamp_ns;
                    qs.data.imu = imu_copy;
                    t265_multi_queue_push_motion(ctx->mq, &qs);
                }
                continue;
            }
        } else {
            ctx->stats.interrupt_fail++;
            consec_fail++;
            if (consec_fail > ctx->stats.interrupt_max_fail_run) {
                ctx->stats.interrupt_max_fail_run = consec_fail;
            }
        }
        pthread_mutex_unlock(&ctx->lock);
    }
    return NULL;
}

static void *t265_fisheye_reader_main(void *arg) {
    t265_reader_context *ctx = (t265_reader_context *)arg;
    uint64_t consec_fail = 0;

    while (atomic_load(&ctx->running)) {
        t265_fisheye_frame frame;
        int status = t265_read_next_fisheye_frame(ctx->dev, &frame, T265_READER_FISHEYE_TIMEOUT_MS);

        pthread_mutex_lock(&ctx->lock);
        ctx->stats.fisheye_attempts++;

        if (status == T265_OK) {
            void (*on_fisheye)(const t265_fisheye_frame *, void *) = NULL;
            void *user_data = NULL;
            t265_fisheye_frame frame_copy = frame;
            ctx->stats.fisheye_success++;
            consec_fail = 0;

            if (frame.sensor_id == T265_SENSOR_ID_FISHEYE0) {
                ctx->latest.has_fisheye0 = 1;
                ctx->latest.fisheye0_frame_id = frame.frame_id;
                ctx->latest.fisheye0_timestamp_ns = frame.timestamp_ns;
                ctx->latest.fisheye0_update_count++;
                ctx->stats.fisheye0_count++;
            } else if (frame.sensor_id == T265_SENSOR_ID_FISHEYE1) {
                ctx->latest.has_fisheye1 = 1;
                ctx->latest.fisheye1_frame_id = frame.frame_id;
                ctx->latest.fisheye1_timestamp_ns = frame.timestamp_ns;
                ctx->latest.fisheye1_update_count++;
                ctx->stats.fisheye1_count++;
            }
            on_fisheye = ctx->on_fisheye;
            user_data = ctx->callback_user_data;
            pthread_mutex_unlock(&ctx->lock);
            if (on_fisheye) on_fisheye(&frame_copy, user_data);
            
            if (ctx->mq) {
                t265_queue_sample qs;
                memset(&qs, 0, sizeof(qs));
                qs.type = (frame_copy.sensor_id == T265_SENSOR_ID_FISHEYE0) ? T265_QUEUE_SAMPLE_FISHEYE0 : T265_QUEUE_SAMPLE_FISHEYE1;
                qs.timestamp_ns = frame_copy.timestamp_ns;
                qs.data.fisheye.sensor_id = frame_copy.sensor_id;
                qs.data.fisheye.frame_id = frame_copy.frame_id;
                qs.data.fisheye.timestamp_ns = frame_copy.timestamp_ns;
                qs.data.fisheye.width = frame_copy.width;
                qs.data.fisheye.height = frame_copy.height;
                qs.data.fisheye.frame_length = frame_copy.frame_length;
                
                // Use push_fisheye_frame to copy image data into the queue's circular buffer
                t265_multi_queue_push_fisheye_frame(ctx->mq, &qs, frame_copy.frame_data);
            }
            continue;
        } else {
            ctx->stats.fisheye_fail++;
            consec_fail++;
            if (consec_fail > ctx->stats.fisheye_max_fail_run) {
                ctx->stats.fisheye_max_fail_run = consec_fail;
            }
        }
        pthread_mutex_unlock(&ctx->lock);
    }
    return NULL;
}

int t265_reader_init(t265_reader_context *ctx, t265_runtime *dev) {
    if (!ctx || !dev) return T265_ERR_INVALID_STATE;

    memset(ctx, 0, sizeof(*ctx));
    ctx->dev = dev;

    if (pthread_mutex_init(&ctx->lock, NULL) != 0) {
        return T265_ERR_USB;
    }
    ctx->mutex_initialized = 1;
    atomic_store(&ctx->running, 0);
    ctx->threads_started = 0;

    return T265_OK;
}

t265_reader_context* t265_reader_create(t265_runtime *dev) {
    t265_reader_context *ctx = calloc(1, sizeof(t265_reader_context));
    if (!ctx) return NULL;
    if (t265_reader_init(ctx, dev) != T265_OK) {
        free(ctx);
        return NULL;
    }
    ctx->heap_allocated = 1;
    return ctx;
}

int t265_reader_set_callbacks(
    t265_reader_context *ctx,
    void (*on_pose)(const t265_pose_sample *pose, void *user_data),
    void (*on_gyro)(const t265_imu_sample *gyro, void *user_data),
    void (*on_accel)(const t265_imu_sample *accel, void *user_data),
    void (*on_fisheye)(const t265_fisheye_frame *frame, void *user_data),
    void *user_data
) {
    if (!ctx || !ctx->mutex_initialized) return T265_ERR_INVALID_STATE;
    pthread_mutex_lock(&ctx->lock);
    ctx->on_pose = on_pose;
    ctx->on_gyro = on_gyro;
    ctx->on_accel = on_accel;
    ctx->on_fisheye = on_fisheye;
    ctx->callback_user_data = user_data;
    pthread_mutex_unlock(&ctx->lock);
    return T265_OK;
}

int t265_reader_set_multi_queue(t265_reader_context *ctx, t265_multi_queue *mq) {
    if (!ctx || !ctx->mutex_initialized) return T265_ERR_INVALID_STATE;
    pthread_mutex_lock(&ctx->lock);
    ctx->mq = mq;
    pthread_mutex_unlock(&ctx->lock);
    return T265_OK;
}

int t265_reader_start(t265_reader_context *ctx) {
    if (!ctx || !ctx->dev) return T265_ERR_INVALID_STATE;
    if (!ctx->mutex_initialized) return T265_ERR_INVALID_STATE;
    if (ctx->threads_started) return T265_ERR_INVALID_STATE;

    atomic_store(&ctx->running, 1);

    if (pthread_create(&ctx->interrupt_thread, NULL, t265_interrupt_reader_main, ctx) != 0) {
        atomic_store(&ctx->running, 0);
        return T265_ERR_USB;
    }

    if (pthread_create(&ctx->fisheye_thread, NULL, t265_fisheye_reader_main, ctx) != 0) {
        atomic_store(&ctx->running, 0);
        pthread_join(ctx->interrupt_thread, NULL);
        return T265_ERR_USB;
    }

    ctx->threads_started = 1;
    return T265_OK;
}

int t265_reader_stop(t265_reader_context *ctx) {
    pthread_t self_id;
    if (!ctx) return T265_ERR_INVALID_STATE;
    if (!ctx->threads_started) return T265_OK;

    atomic_store(&ctx->running, 0);
    self_id = pthread_self();

    if (!pthread_equal(self_id, ctx->interrupt_thread)) {
        pthread_join(ctx->interrupt_thread, NULL);
    }
    if (!pthread_equal(self_id, ctx->fisheye_thread)) {
        pthread_join(ctx->fisheye_thread, NULL);
    }

    ctx->threads_started = 0;
    return T265_OK;
}

void t265_reader_destroy(t265_reader_context *ctx) {
    if (!ctx) return;
    if (ctx->threads_started) {
        t265_reader_stop(ctx);
    }
    if (ctx->mutex_initialized) {
        pthread_mutex_destroy(&ctx->lock);
        ctx->mutex_initialized = 0;
    }
    memset(ctx, 0, sizeof(*ctx));
}

void t265_reader_free(t265_reader_context *ctx) {
    int heap_allocated;

    if (!ctx) return;
    heap_allocated = ctx->heap_allocated;
    t265_reader_destroy(ctx);
    if (heap_allocated) {
        free(ctx);
    }
}

int t265_get_latest_pose(t265_reader_context *ctx, t265_pose_sample *pose) {
    if (!ctx || !pose || !ctx->mutex_initialized) return T265_ERR_INVALID_STATE;
    
    pthread_mutex_lock(&ctx->lock);
    if (!ctx->latest.has_pose) {
        pthread_mutex_unlock(&ctx->lock);
        return T265_ERR_NOT_FOUND;
    }
    *pose = ctx->latest.pose;
    pthread_mutex_unlock(&ctx->lock);
    
    return T265_OK;
}

int t265_get_latest_gyro(t265_reader_context *ctx, t265_imu_sample *gyro) {
    if (!ctx || !gyro || !ctx->mutex_initialized) return T265_ERR_INVALID_STATE;
    
    pthread_mutex_lock(&ctx->lock);
    if (!ctx->latest.has_gyro) {
        pthread_mutex_unlock(&ctx->lock);
        return T265_ERR_NOT_FOUND;
    }
    *gyro = ctx->latest.gyro;
    pthread_mutex_unlock(&ctx->lock);
    
    return T265_OK;
}

int t265_get_latest_accel(t265_reader_context *ctx, t265_imu_sample *accel) {
    if (!ctx || !accel || !ctx->mutex_initialized) return T265_ERR_INVALID_STATE;
    
    pthread_mutex_lock(&ctx->lock);
    if (!ctx->latest.has_accel) {
        pthread_mutex_unlock(&ctx->lock);
        return T265_ERR_NOT_FOUND;
    }
    *accel = ctx->latest.accel;
    pthread_mutex_unlock(&ctx->lock);
    
    return T265_OK;
}

int t265_get_latest_fisheye_info(
    t265_reader_context *ctx,
    uint32_t *fisheye0_frame_id,
    uint32_t *fisheye1_frame_id,
    uint64_t *fisheye0_timestamp_ns,
    uint64_t *fisheye1_timestamp_ns
) {
    if (!ctx || !fisheye0_frame_id || !fisheye1_frame_id || 
        !fisheye0_timestamp_ns || !fisheye1_timestamp_ns || !ctx->mutex_initialized) return T265_ERR_INVALID_STATE;
        
    pthread_mutex_lock(&ctx->lock);
    if (!ctx->latest.has_fisheye0 || !ctx->latest.has_fisheye1) {
        pthread_mutex_unlock(&ctx->lock);
        return T265_ERR_NOT_FOUND;
    }
    
    *fisheye0_frame_id = ctx->latest.fisheye0_frame_id;
    *fisheye1_frame_id = ctx->latest.fisheye1_frame_id;
    *fisheye0_timestamp_ns = ctx->latest.fisheye0_timestamp_ns;
    *fisheye1_timestamp_ns = ctx->latest.fisheye1_timestamp_ns;
    pthread_mutex_unlock(&ctx->lock);
    
    return T265_OK;
}

int t265_get_reader_stats(t265_reader_context *ctx, t265_reader_stats *stats) {
    if (!ctx || !stats || !ctx->mutex_initialized) return T265_ERR_INVALID_STATE;
    
    pthread_mutex_lock(&ctx->lock);
    *stats = ctx->stats;
    pthread_mutex_unlock(&ctx->lock);
    
    return T265_OK;
}

int t265_reader_has_pose(t265_reader_context *ctx) {
    if (!ctx || !ctx->mutex_initialized) return T265_ERR_INVALID_STATE;
    pthread_mutex_lock(&ctx->lock);
    int has = ctx->latest.has_pose;
    pthread_mutex_unlock(&ctx->lock);
    return has ? 1 : 0;
}

int t265_reader_has_gyro(t265_reader_context *ctx) {
    if (!ctx || !ctx->mutex_initialized) return T265_ERR_INVALID_STATE;
    pthread_mutex_lock(&ctx->lock);
    int has = ctx->latest.has_gyro;
    pthread_mutex_unlock(&ctx->lock);
    return has ? 1 : 0;
}

int t265_reader_has_accel(t265_reader_context *ctx) {
    if (!ctx || !ctx->mutex_initialized) return T265_ERR_INVALID_STATE;
    pthread_mutex_lock(&ctx->lock);
    int has = ctx->latest.has_accel;
    pthread_mutex_unlock(&ctx->lock);
    return has ? 1 : 0;
}

int t265_reader_has_fisheye0(t265_reader_context *ctx) {
    if (!ctx || !ctx->mutex_initialized) return T265_ERR_INVALID_STATE;
    pthread_mutex_lock(&ctx->lock);
    int has = ctx->latest.has_fisheye0;
    pthread_mutex_unlock(&ctx->lock);
    return has ? 1 : 0;
}

int t265_reader_has_fisheye1(t265_reader_context *ctx) {
    if (!ctx || !ctx->mutex_initialized) return T265_ERR_INVALID_STATE;
    pthread_mutex_lock(&ctx->lock);
    int has = ctx->latest.has_fisheye1;
    pthread_mutex_unlock(&ctx->lock);
    return has ? 1 : 0;
}
