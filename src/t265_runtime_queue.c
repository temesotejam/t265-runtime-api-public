#include "t265_runtime_queue.h"
#include <string.h>

int t265_queue_init(t265_sample_queue *queue)
{
    if (!queue) {
        return T265_ERR_INVALID_STATE;
    }

    memset(queue, 0, sizeof(*queue));

    if (pthread_mutex_init(&queue->lock, NULL) != 0) {
        return T265_ERR_USB;
    }
    if (pthread_cond_init(&queue->cond, NULL) != 0) {
        pthread_mutex_destroy(&queue->lock);
        return T265_ERR_USB;
    }

    queue->mutex_initialized = 1;
    queue->cond_initialized = 1;
    queue->next_sequence = 1;

    return T265_OK;
}

void t265_queue_destroy(t265_sample_queue *queue)
{
    if (!queue) {
        return;
    }

    if (queue->mutex_initialized) {
        pthread_mutex_destroy(&queue->lock);
    }
    if (queue->cond_initialized) {
        pthread_cond_destroy(&queue->cond);
    }

    memset(queue, 0, sizeof(*queue));
}

int t265_queue_push(t265_sample_queue *queue, const t265_queue_sample *sample)
{
    if (!queue || !sample) {
        return T265_ERR_INVALID_STATE;
    }
    if (!queue->mutex_initialized) {
        return T265_ERR_INVALID_STATE;
    }

    pthread_mutex_lock(&queue->lock);

    if (queue->count == T265_QUEUE_CAPACITY) {
        // Queue is full, drop the oldest sample
        queue->tail = (queue->tail + 1) % T265_QUEUE_CAPACITY;
        queue->count--;
        queue->dropped_count++;
    }

    queue->samples[queue->head] = *sample;
    queue->samples[queue->head].sequence = queue->next_sequence++;
    
    queue->head = (queue->head + 1) % T265_QUEUE_CAPACITY;
    queue->count++;
    queue->pushed_count++;

    pthread_cond_signal(&queue->cond);
    pthread_mutex_unlock(&queue->lock);

    return T265_OK;
}

int t265_queue_pop(t265_sample_queue *queue, t265_queue_sample *sample)
{
    if (!queue || !sample) {
        return T265_ERR_INVALID_STATE;
    }
    if (!queue->mutex_initialized) {
        return T265_ERR_INVALID_STATE;
    }

    pthread_mutex_lock(&queue->lock);

    if (queue->count == 0) {
        pthread_mutex_unlock(&queue->lock);
        return T265_ERR_QUEUE_EMPTY;
    }

    *sample = queue->samples[queue->tail];
    queue->tail = (queue->tail + 1) % T265_QUEUE_CAPACITY;
    queue->count--;
    queue->popped_count++;

    pthread_mutex_unlock(&queue->lock);

    return T265_OK;
}

int t265_queue_pop_blocking(t265_sample_queue *queue, t265_queue_sample *sample, int timeout_ms)
{
    struct timespec ts;
    int rc = 0;

    if (!queue || !sample || !queue->mutex_initialized || !queue->cond_initialized) {
        return T265_ERR_INVALID_STATE;
    }

    pthread_mutex_lock(&queue->lock);

    if (queue->count == 0) {
        if (timeout_ms < 0) {
            while (queue->count == 0) {
                pthread_cond_wait(&queue->cond, &queue->lock);
            }
        } else if (timeout_ms > 0) {
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += (time_t)(timeout_ms / 1000);
            ts.tv_nsec += (long)((timeout_ms % 1000) * 1000000);
            if (ts.tv_nsec >= 1000000000) {
                ts.tv_sec++;
                ts.tv_nsec -= 1000000000;
            }
            while (queue->count == 0 && rc == 0) {
                rc = pthread_cond_timedwait(&queue->cond, &queue->lock, &ts);
            }
        }
    }

    if (queue->count == 0) {
        pthread_mutex_unlock(&queue->lock);
        return timeout_ms > 0 ? T265_ERR_TIMEOUT : T265_ERR_QUEUE_EMPTY;
    }

    *sample = queue->samples[queue->tail];
    queue->tail = (queue->tail + 1) % T265_QUEUE_CAPACITY;
    queue->count--;
    queue->popped_count++;

    pthread_mutex_unlock(&queue->lock);

    return T265_OK;
}

int t265_queue_size(t265_sample_queue *queue)
{
    int size;

    if (!queue || !queue->mutex_initialized) {
        return T265_ERR_INVALID_STATE;
    }

    pthread_mutex_lock(&queue->lock);
    size = (int)queue->count;
    pthread_mutex_unlock(&queue->lock);

    return size;
}

int t265_queue_capacity(void)
{
    return T265_QUEUE_CAPACITY;
}

int t265_queue_get_stats(
    t265_sample_queue *queue,
    uint64_t *pushed_count,
    uint64_t *popped_count,
    uint64_t *dropped_count)
{
    if (!queue || !queue->mutex_initialized) {
        return T265_ERR_INVALID_STATE;
    }

    pthread_mutex_lock(&queue->lock);

    if (pushed_count) {
        *pushed_count = queue->pushed_count;
    }
    if (popped_count) {
        *popped_count = queue->popped_count;
    }
    if (dropped_count) {
        *dropped_count = queue->dropped_count;
    }

    pthread_mutex_unlock(&queue->lock);

    return T265_OK;
}
