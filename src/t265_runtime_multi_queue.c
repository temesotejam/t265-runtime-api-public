#include "t265_runtime_multi_queue.h"
#include <string.h>
#include <stdlib.h>

static void t265_multi_queue_note_image_copy_fail(t265_multi_queue *mq)
{
    if (!mq) {
        return;
    }
    if (mq->image_lock_initialized) {
        pthread_mutex_lock(&mq->image_lock);
        mq->image_copy_fail++;
        pthread_mutex_unlock(&mq->image_lock);
    } else {
        mq->image_copy_fail++;
    }
}

static void t265_multi_queue_note_image_pop(t265_multi_queue *mq, const t265_queue_sample *sample)
{
    if (!mq || !sample || !sample->data.fisheye.frame_data) {
        return;
    }
    if (mq->image_lock_initialized) {
        pthread_mutex_lock(&mq->image_lock);
        mq->image_popped++;
        pthread_mutex_unlock(&mq->image_lock);
    } else {
        mq->image_popped++;
    }
}

int t265_multi_queue_init(t265_multi_queue *mq)
{
    if (!mq) {
        return T265_ERR_INVALID_STATE;
    }

    memset(mq, 0, sizeof(*mq));

    if (t265_queue_init(&mq->motion_queue) != T265_OK) {
        return T265_ERR_USB;
    }

    if (t265_queue_init(&mq->fisheye_meta_queue) != T265_OK) {
        t265_queue_destroy(&mq->motion_queue);
        return T265_ERR_USB;
    }

    if (pthread_mutex_init(&mq->image_lock, NULL) != 0) {
        t265_queue_destroy(&mq->motion_queue);
        t265_queue_destroy(&mq->fisheye_meta_queue);
        return T265_ERR_USB;
    }
    mq->image_lock_initialized = 1;

    for (int i = 0; i < T265_IMAGE_BUFFER_COUNT; i++) {
        mq->image_buffers[i] = malloc(T265_IMAGE_SIZE);
        if (!mq->image_buffers[i]) {
            t265_multi_queue_destroy(mq); // Use destroy to cleanup already allocated ones
            return T265_ERR_USB;
        }
    }
    mq->image_buffer_write_idx = 0;

    return T265_OK;
}

t265_multi_queue* t265_multi_queue_create(void)
{
    t265_multi_queue *mq = calloc(1, sizeof(t265_multi_queue));
    if (!mq) return NULL;
    if (t265_multi_queue_init(mq) != T265_OK) {
        free(mq);
        return NULL;
    }
    mq->heap_allocated = 1;
    return mq;
}

void t265_multi_queue_destroy(t265_multi_queue *mq)
{
    int heap_allocated;

    if (!mq) {
        return;
    }

    heap_allocated = mq->heap_allocated;

    t265_queue_destroy(&mq->motion_queue);
    t265_queue_destroy(&mq->fisheye_meta_queue);

    if (mq->image_lock_initialized) {
        pthread_mutex_destroy(&mq->image_lock);
    }

    for (int i = 0; i < T265_IMAGE_BUFFER_COUNT; i++) {
        if (mq->image_buffers[i]) {
            free(mq->image_buffers[i]);
            mq->image_buffers[i] = NULL;
        }
    }

    if (heap_allocated) {
        free(mq);
    }
}

int t265_multi_queue_push(t265_multi_queue *mq, const t265_queue_sample *sample)
{
    if (!mq || !sample) {
        return T265_ERR_INVALID_STATE;
    }

    switch (sample->type) {
        case T265_QUEUE_SAMPLE_POSE:
        case T265_QUEUE_SAMPLE_GYRO:
        case T265_QUEUE_SAMPLE_ACCEL:
            return t265_multi_queue_push_motion(mq, sample);
        case T265_QUEUE_SAMPLE_FISHEYE0:
        case T265_QUEUE_SAMPLE_FISHEYE1:
            return t265_multi_queue_push_fisheye_meta(mq, sample);
        default:
            return T265_ERR_INVALID_STATE;
    }
}

int t265_multi_queue_push_motion(t265_multi_queue *mq, const t265_queue_sample *sample)
{
    if (!mq || !sample) {
        return T265_ERR_INVALID_STATE;
    }
    
    switch (sample->type) {
        case T265_QUEUE_SAMPLE_POSE:
        case T265_QUEUE_SAMPLE_GYRO:
        case T265_QUEUE_SAMPLE_ACCEL:
            return t265_queue_push(&mq->motion_queue, sample);
        default:
            return T265_ERR_INVALID_STATE;
    }
}

int t265_multi_queue_push_fisheye_meta(t265_multi_queue *mq, const t265_queue_sample *sample)
{
    if (!mq || !sample) {
        return T265_ERR_INVALID_STATE;
    }

    switch (sample->type) {
        case T265_QUEUE_SAMPLE_FISHEYE0:
        case T265_QUEUE_SAMPLE_FISHEYE1:
            return t265_queue_push(&mq->fisheye_meta_queue, sample);
        default:
            return T265_ERR_INVALID_STATE;
    }
}

int t265_multi_queue_push_fisheye_frame(t265_multi_queue *mq, t265_queue_sample *sample, const uint8_t *frame_data)
{
    if (!mq || !sample || !frame_data) {
        t265_multi_queue_note_image_copy_fail(mq);
        return T265_ERR_INVALID_STATE;
    }
    if (sample->type != T265_QUEUE_SAMPLE_FISHEYE0 &&
        sample->type != T265_QUEUE_SAMPLE_FISHEYE1) {
        t265_multi_queue_note_image_copy_fail(mq);
        return T265_ERR_INVALID_STATE;
    }
    if (sample->data.fisheye.frame_length == 0 ||
        sample->data.fisheye.frame_length > T265_IMAGE_SIZE) {
        t265_multi_queue_note_image_copy_fail(mq);
        return T265_ERR_INVALID_STATE;
    }
    if (!mq->image_lock_initialized) {
        t265_multi_queue_note_image_copy_fail(mq);
        return T265_ERR_INVALID_STATE;
    }

    pthread_mutex_lock(&mq->image_lock);
    
    // Use the next available buffer
    uint8_t *buffer = mq->image_buffers[mq->image_buffer_write_idx];
    if (!buffer) {
        mq->image_copy_fail++;
        pthread_mutex_unlock(&mq->image_lock);
        return T265_ERR_INVALID_STATE;
    }
    if (mq->image_pushed + mq->image_dropped >= T265_IMAGE_BUFFER_COUNT) {
        mq->image_buffer_reused++;
        mq->image_buffer_overwritten = mq->image_buffer_reused;
    }
    memcpy(buffer, frame_data, sample->data.fisheye.frame_length);
    mq->image_bytes_copied += sample->data.fisheye.frame_length;
    
    // Attach the buffer to the sample
    sample->data.fisheye.frame_data = buffer;
    
    // Advance index
    mq->image_buffer_write_idx = (mq->image_buffer_write_idx + 1) % T265_IMAGE_BUFFER_COUNT;
    
    pthread_mutex_unlock(&mq->image_lock);

    int rc = t265_multi_queue_push_fisheye_meta(mq, sample);
    pthread_mutex_lock(&mq->image_lock);
    if (rc == T265_OK) {
        mq->image_pushed++;
    } else {
        mq->image_dropped++;
    }
    pthread_mutex_unlock(&mq->image_lock);

    return rc;
}

int t265_multi_queue_pop_motion(t265_multi_queue *mq, t265_queue_sample *sample)
{
    if (!mq || !sample) {
        return T265_ERR_INVALID_STATE;
    }
    return t265_queue_pop(&mq->motion_queue, sample);
}

int t265_multi_queue_pop_fisheye_meta(t265_multi_queue *mq, t265_queue_sample *sample)
{
    int rc;
    if (!mq || !sample) {
        return T265_ERR_INVALID_STATE;
    }
    rc = t265_queue_pop(&mq->fisheye_meta_queue, sample);
    if (rc == T265_OK) {
        t265_multi_queue_note_image_pop(mq, sample);
    }
    return rc;
}

int t265_multi_queue_pop_motion_blocking(t265_multi_queue *mq, t265_queue_sample *sample, int timeout_ms)
{
    if (!mq || !sample) {
        return T265_ERR_INVALID_STATE;
    }
    return t265_queue_pop_blocking(&mq->motion_queue, sample, timeout_ms);
}

int t265_multi_queue_pop_fisheye_meta_blocking(t265_multi_queue *mq, t265_queue_sample *sample, int timeout_ms)
{
    int rc;
    if (!mq || !sample) {
        return T265_ERR_INVALID_STATE;
    }
    rc = t265_queue_pop_blocking(&mq->fisheye_meta_queue, sample, timeout_ms);
    if (rc == T265_OK) {
        t265_multi_queue_note_image_pop(mq, sample);
    }
    return rc;
}

int t265_multi_queue_get_stats(t265_multi_queue *mq, t265_multi_queue_stats *stats)
{
    if (!mq || !stats) {
        return T265_ERR_INVALID_STATE;
    }

    memset(stats, 0, sizeof(*stats));

    t265_queue_get_stats(&mq->motion_queue, &stats->motion_pushed, &stats->motion_popped, &stats->motion_dropped);
    stats->motion_remaining = t265_queue_size(&mq->motion_queue);

    t265_queue_get_stats(&mq->fisheye_meta_queue, &stats->fisheye_pushed, &stats->fisheye_popped, &stats->fisheye_dropped);
    stats->fisheye_remaining = t265_queue_size(&mq->fisheye_meta_queue);

    return T265_OK;
}

int t265_multi_queue_get_image_stats(t265_multi_queue *mq, t265_image_queue_stats *stats)
{
    uint64_t remaining;

    if (!mq || !stats) {
        return T265_ERR_INVALID_STATE;
    }

    memset(stats, 0, sizeof(*stats));

    if (mq->image_lock_initialized) {
        pthread_mutex_lock(&mq->image_lock);
    }

    stats->image_pushed = mq->image_pushed;
    stats->image_popped = mq->image_popped;
    stats->image_dropped = mq->image_dropped;
    stats->image_buffer_reused = mq->image_buffer_reused;
    stats->image_buffer_overwritten = mq->image_buffer_overwritten;
    stats->image_copy_fail = mq->image_copy_fail;
    stats->image_bytes_copied = mq->image_bytes_copied;
    stats->image_buffer_count = T265_IMAGE_BUFFER_COUNT;
    stats->image_buffer_size = T265_IMAGE_SIZE;

    remaining = 0;
    if (mq->image_pushed > mq->image_popped) {
        remaining = mq->image_pushed - mq->image_popped;
    }
    stats->image_remaining = (remaining > (uint64_t)T265_IMAGE_BUFFER_COUNT) ?
        T265_IMAGE_BUFFER_COUNT : (int)remaining;

    if (mq->image_lock_initialized) {
        pthread_mutex_unlock(&mq->image_lock);
    }

    return T265_OK;
}

int t265_multi_queue_motion_size(t265_multi_queue *mq)
{
    if (!mq) {
        return T265_ERR_INVALID_STATE;
    }
    return t265_queue_size(&mq->motion_queue);
}

int t265_multi_queue_fisheye_size(t265_multi_queue *mq)
{
    if (!mq) {
        return T265_ERR_INVALID_STATE;
    }
    return t265_queue_size(&mq->fisheye_meta_queue);
}
