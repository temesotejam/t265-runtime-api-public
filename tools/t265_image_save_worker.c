/*
 * T265 image save worker utility
 *
 * Purpose:
 * - Keep one reader process running for a requested duration.
 * - Pop image payload samples continuously from the fisheye queue.
 * - Keep PGM write I/O out of the main pop path by using a save worker thread.
 * - Drop save requests, not sensor samples, if the save worker falls behind.
 * - Provide a small optional CLI utility, not a public API surface.
 */

#include "t265.h"

#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdint.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define DEFAULT_DURATION_SEC 60
#define DEFAULT_POLL_US 10000
#define FINAL_DRAIN_LIMIT 1024
#define IMAGE_SAVE_WORKER_QUEUE_CAPACITY 16
#define IMAGE_SAVE_WORKER_MAX_IMAGE_BYTES (848u * 800u)
#define DEFAULT_OUTPUT_DIR "t265_images"
#define DEFAULT_PREFIX "t265_worker"

static atomic_int g_stop_requested;

static void handle_signal(int signo)
{
    (void)signo;
    atomic_store(&g_stop_requested, 1);
}

static uint64_t monotonic_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

static int parse_positive_int(const char *text, int fallback)
{
    char *end = NULL;
    long value;

    if (!text || !*text) {
        return fallback;
    }

    value = strtol(text, &end, 10);
    if (!end || *end != '\0' || value <= 0 || value > 86400) {
        return fallback;
    }

    return (int)value;
}

static void usage(const char *argv0)
{
    printf("Usage: %s [--duration-sec N] [--save-every-sec N] [--output-dir DIR] [--prefix NAME]\n", argv0);
    printf("Defaults: --duration-sec %d, --save-every-sec 1, --output-dir %s, --prefix %s\n",
           DEFAULT_DURATION_SEC, DEFAULT_OUTPUT_DIR, DEFAULT_PREFIX);
}

static int ensure_output_dir(const char *path)
{
    struct stat st;

    if (!path || !*path) {
        return -1;
    }

    if (stat(path, &st) == 0) {
        return S_ISDIR(st.st_mode) ? 0 : -1;
    }

    if (errno != ENOENT) {
        return -1;
    }

    return mkdir(path, 0775);
}

typedef struct image_save_request {
    uint8_t sensor_id;
    uint32_t frame_id;
    uint64_t timestamp_ns;
    uint32_t width;
    uint32_t height;
    uint32_t frame_length;
    uint64_t save_index;
    uint8_t data[IMAGE_SAVE_WORKER_MAX_IMAGE_BYTES];
} image_save_request;

typedef struct image_save_worker_context {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    pthread_t thread;

    image_save_request requests[IMAGE_SAVE_WORKER_QUEUE_CAPACITY];
    int head;
    int tail;
    int count;
    int stop_requested;
    int worker_started;
    int worker_stopped;

    uint64_t save_requested;
    uint64_t save_enqueued;
    uint64_t save_completed;
    uint64_t save_request_dropped;
    uint64_t save_worker_errors;

    char output_dir[256];
    char prefix[64];
} image_save_worker_context;

static image_save_worker_context g_save_worker;

static void image_save_worker_init(image_save_worker_context *ctx)
{
    memset(ctx, 0, sizeof(*ctx));
    snprintf(ctx->output_dir, sizeof(ctx->output_dir), "%s", DEFAULT_OUTPUT_DIR);
    snprintf(ctx->prefix, sizeof(ctx->prefix), "%s", DEFAULT_PREFIX);
    pthread_mutex_init(&ctx->mutex, NULL);
    pthread_cond_init(&ctx->cond, NULL);
}

static void image_save_worker_destroy(image_save_worker_context *ctx)
{
    pthread_cond_destroy(&ctx->cond);
    pthread_mutex_destroy(&ctx->mutex);
}

static int image_save_worker_remaining(image_save_worker_context *ctx)
{
    int remaining;

    pthread_mutex_lock(&ctx->mutex);
    remaining = ctx->count;
    pthread_mutex_unlock(&ctx->mutex);

    return remaining;
}

static int image_save_worker_push(image_save_worker_context *ctx,
                                  const t265_image_view *view,
                                  uint64_t save_index)
{
    image_save_request *req;

    pthread_mutex_lock(&ctx->mutex);
    ctx->save_requested++;

    if (!view || !view->data || view->frame_length == 0 ||
        view->frame_length > IMAGE_SAVE_WORKER_MAX_IMAGE_BYTES ||
        view->width == 0 || view->height == 0 ||
        ctx->count >= IMAGE_SAVE_WORKER_QUEUE_CAPACITY) {
        ctx->save_request_dropped++;
        pthread_mutex_unlock(&ctx->mutex);
        return T265_ERR_QUEUE_FULL;
    }

    req = &ctx->requests[ctx->tail];
    memset(req, 0, sizeof(*req));
    req->sensor_id = view->sensor_id;
    req->frame_id = view->frame_id;
    req->timestamp_ns = view->timestamp_ns;
    req->width = view->width;
    req->height = view->height;
    req->frame_length = view->frame_length;
    req->save_index = save_index;
    memcpy(req->data, view->data, view->frame_length);

    ctx->tail = (ctx->tail + 1) % IMAGE_SAVE_WORKER_QUEUE_CAPACITY;
    ctx->count++;
    ctx->save_enqueued++;
    pthread_cond_signal(&ctx->cond);
    pthread_mutex_unlock(&ctx->mutex);

    return T265_OK;
}

static int image_save_worker_pop(image_save_worker_context *ctx,
                                 image_save_request *out_req)
{
    pthread_mutex_lock(&ctx->mutex);
    while (ctx->count == 0 && !ctx->stop_requested) {
        pthread_cond_wait(&ctx->cond, &ctx->mutex);
    }

    if (ctx->count == 0 && ctx->stop_requested) {
        pthread_mutex_unlock(&ctx->mutex);
        return T265_ERR_QUEUE_EMPTY;
    }

    *out_req = ctx->requests[ctx->head];
    ctx->head = (ctx->head + 1) % IMAGE_SAVE_WORKER_QUEUE_CAPACITY;
    ctx->count--;
    pthread_mutex_unlock(&ctx->mutex);

    return T265_OK;
}

static void image_save_worker_note_completed(image_save_worker_context *ctx)
{
    pthread_mutex_lock(&ctx->mutex);
    ctx->save_completed++;
    pthread_mutex_unlock(&ctx->mutex);
}

static void image_save_worker_note_error(image_save_worker_context *ctx)
{
    pthread_mutex_lock(&ctx->mutex);
    ctx->save_worker_errors++;
    pthread_mutex_unlock(&ctx->mutex);
}

static void *image_save_worker_main(void *arg)
{
    image_save_worker_context *ctx = (image_save_worker_context *)arg;
    image_save_request req;

    for (;;) {
        if (image_save_worker_pop(ctx, &req) != T265_OK) {
            break;
        }

        t265_image_view view;
        char path[512];

        memset(&view, 0, sizeof(view));
        view.sensor_id = req.sensor_id;
        view.frame_id = req.frame_id;
        view.timestamp_ns = req.timestamp_ns;
        view.width = req.width;
        view.height = req.height;
        view.frame_length = req.frame_length;
        view.data = req.data;

        snprintf(path, sizeof(path), "%s/%s_fe%d_%lu_%u.pgm",
                 ctx->output_dir,
                 ctx->prefix,
                 req.sensor_id == T265_SENSOR_ID_FISHEYE0 ? 0 : 1,
                 (unsigned long)req.timestamp_ns,
                 req.frame_id);

        if (t265_write_pgm_from_view(path, &view) == T265_OK) {
            image_save_worker_note_completed(ctx);
        } else {
            image_save_worker_note_error(ctx);
        }
    }

    pthread_mutex_lock(&ctx->mutex);
    ctx->worker_stopped = 1;
    pthread_mutex_unlock(&ctx->mutex);

    return NULL;
}

static int image_save_worker_start(image_save_worker_context *ctx)
{
    if (pthread_create(&ctx->thread, NULL, image_save_worker_main, ctx) != 0) {
        return -1;
    }

    pthread_mutex_lock(&ctx->mutex);
    ctx->worker_started = 1;
    pthread_mutex_unlock(&ctx->mutex);

    return T265_OK;
}

static void image_save_worker_stop(image_save_worker_context *ctx)
{
    int should_join = 0;

    pthread_mutex_lock(&ctx->mutex);
    if (ctx->worker_started) {
        should_join = 1;
    }
    ctx->stop_requested = 1;
    pthread_cond_broadcast(&ctx->cond);
    pthread_mutex_unlock(&ctx->mutex);

    if (should_join) {
        pthread_join(ctx->thread, NULL);
    }
}

int main(int argc, char **argv)
{
    t265_multi_queue *mq = NULL;
    t265_reader_context *reader = NULL;
    t265_multi_queue_stats queue_stats;
    t265_image_queue_stats image_stats;
    t265_reader_stats reader_stats;
    image_save_worker_context *save_worker = &g_save_worker;
    int duration_sec = DEFAULT_DURATION_SEC;
    int save_every_sec = 1;
    int save_enabled = 1;
    int output_dir_ready = 0;
    char output_dir[256] = DEFAULT_OUTPUT_DIR;
    char prefix[64] = DEFAULT_PREFIX;
    uint64_t start_ms;
    uint64_t end_ms;
    uint64_t next_save_ms = 0;
    uint64_t image_count = 0;
    uint64_t image_view_fail = 0;
    uint64_t save_candidate_count = 0;
    uint64_t fisheye0_count = 0;
    uint64_t fisheye1_count = 0;
    uint64_t motion_popped = 0;
    int final_drained_fisheye = 0;
    int final_drained_motion = 0;
    int save_queue_remaining = 0;
    int rc = 1;
    const char *result = "FAIL";

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--duration-sec") == 0) {
            if (i + 1 >= argc) {
                usage(argv[0]);
                return 2;
            }
            duration_sec = parse_positive_int(argv[++i], duration_sec);
        } else if (strcmp(argv[i], "--save-every-sec") == 0) {
            if (i + 1 >= argc) {
                usage(argv[0]);
                return 2;
            }
            save_every_sec = parse_positive_int(argv[++i], 0);
            save_enabled = 1;
        } else if (strcmp(argv[i], "--output-dir") == 0) {
            if (i + 1 >= argc) {
                usage(argv[0]);
                return 2;
            }
            snprintf(output_dir, sizeof(output_dir), "%s", argv[++i]);
        } else if (strcmp(argv[i], "--prefix") == 0) {
            if (i + 1 >= argc) {
                usage(argv[0]);
                return 2;
            }
            snprintf(prefix, sizeof(prefix), "%s", argv[++i]);
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(argv[0]);
            return 0;
        } else {
            usage(argv[0]);
            return 2;
        }
    }

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    atomic_store(&g_stop_requested, 0);

    image_save_worker_init(save_worker);
    snprintf(save_worker->output_dir, sizeof(save_worker->output_dir), "%s", output_dir);
    snprintf(save_worker->prefix, sizeof(save_worker->prefix), "%s", prefix);

    printf("--- T265 Image Save Worker Utility ---\n");
    printf("duration_sec: %d\n", duration_sec);
    printf("save_enabled: %s\n", save_enabled ? "yes" : "no");
    printf("save_every_sec: %d\n", save_every_sec);
    printf("output_dir: %s\n", save_worker->output_dir);
    printf("prefix: %s\n", save_worker->prefix);
    printf("save_queue_capacity: %d\n", IMAGE_SAVE_WORKER_QUEUE_CAPACITY);

    if (ensure_output_dir(save_worker->output_dir) != 0) {
        fprintf(stderr, "Failed to create or use output directory: %s\n", save_worker->output_dir);
        goto cleanup;
    }
    output_dir_ready = 1;

    if (save_enabled && image_save_worker_start(save_worker) != T265_OK) {
        fprintf(stderr, "Failed to start image save worker.\n");
        goto cleanup;
    }

    mq = t265_multi_queue_create();
    if (!mq) {
        fprintf(stderr, "Failed to create multi-queue.\n");
        goto cleanup;
    }

    reader = t265_create_simple_reader(NULL, 1, 1, 1, mq);
    if (!reader) {
        fprintf(stderr, "Failed to create reader.\n");
        goto cleanup;
    }

    start_ms = monotonic_ms();
    end_ms = start_ms + (uint64_t)duration_sec * 1000u;
    next_save_ms = start_ms;

    while (!atomic_load(&g_stop_requested) && monotonic_ms() < end_ms) {
        int did_work = 0;
        t265_queue_sample sample;

        while (t265_multi_queue_pop_motion(mq, &sample) == T265_OK) {
            ++motion_popped;
            did_work = 1;
        }

        while (t265_multi_queue_pop_fisheye_meta(mq, &sample) == T265_OK) {
            t265_image_view view;
            did_work = 1;

            if (t265_image_view_from_queue_sample(&sample, &view) != T265_OK) {
                ++image_view_fail;
                continue;
            }

            ++image_count;
            if (view.sensor_id == T265_SENSOR_ID_FISHEYE0) {
                ++fisheye0_count;
            } else if (view.sensor_id == T265_SENSOR_ID_FISHEYE1) {
                ++fisheye1_count;
            }

            if (save_enabled) {
                uint64_t now = monotonic_ms();
                int should_save = 0;

                if (save_candidate_count == 0) {
                    should_save = 1;
                } else if (save_every_sec > 0 && now >= next_save_ms) {
                    should_save = 1;
                }

                if (should_save) {
                    ++save_candidate_count;
                    (void)image_save_worker_push(save_worker, &view, save_candidate_count);
                    if (save_every_sec > 0) {
                        next_save_ms = now + (uint64_t)save_every_sec * 1000u;
                    } else {
                        next_save_ms = end_ms + 1u;
                    }
                }
            }
        }

        if (!did_work) {
            usleep(DEFAULT_POLL_US);
        }
    }

    if (reader) {
        t265_reader_stop(reader);
    }

    while (final_drained_motion < FINAL_DRAIN_LIMIT &&
           t265_multi_queue_pop_motion(mq, &(t265_queue_sample){0}) == T265_OK) {
        ++final_drained_motion;
    }

    while (final_drained_fisheye < FINAL_DRAIN_LIMIT) {
        t265_queue_sample sample;
        if (t265_multi_queue_pop_fisheye_meta(mq, &sample) != T265_OK) {
            break;
        }
        ++final_drained_fisheye;
    }

    image_save_worker_stop(save_worker);
    save_queue_remaining = image_save_worker_remaining(save_worker);

    memset(&queue_stats, 0, sizeof(queue_stats));
    memset(&image_stats, 0, sizeof(image_stats));
    memset(&reader_stats, 0, sizeof(reader_stats));
    t265_multi_queue_get_stats(mq, &queue_stats);
    t265_multi_queue_get_image_stats(mq, &image_stats);
    if (reader) {
        t265_get_reader_stats(reader, &reader_stats);
    }

    if (!atomic_load(&g_stop_requested) &&
        image_count > 0 &&
        image_stats.image_copy_fail == 0 &&
        image_stats.image_dropped == 0 &&
        queue_stats.fisheye_dropped == 0 &&
        queue_stats.motion_dropped == 0 &&
        save_worker->save_worker_errors == 0 &&
        (!save_enabled || (save_worker->worker_started && save_worker->worker_stopped))) {
        if (image_stats.image_buffer_reused > 0 ||
            image_stats.image_remaining > 0 ||
            save_worker->save_request_dropped > 0 ||
            save_queue_remaining > 0) {
            result = "WARN";
        } else {
            result = "PASS";
        }
        rc = 0;
    }

    printf("\nT265 image save worker summary:\n");
    printf("  duration_sec:             %d\n", duration_sec);
    printf("  output_dir:               %s\n", save_worker->output_dir);
    printf("  prefix:                   %s\n", save_worker->prefix);
    printf("  stop_requested:           %s\n", atomic_load(&g_stop_requested) ? "yes" : "no");
    printf("  save_enabled:             %s\n", save_enabled ? "yes" : "no");
    printf("  save_every_sec:           %d\n", save_every_sec);
    printf("  save_queue_capacity:      %d\n", IMAGE_SAVE_WORKER_QUEUE_CAPACITY);

    printf("\nImage stats:\n");
    printf("  image_count:              %lu\n", image_count);
    printf("  fisheye0_images:          %lu\n", fisheye0_count);
    printf("  fisheye1_images:          %lu\n", fisheye1_count);
    printf("  image_view_fail:          %lu\n", image_view_fail);
    printf("  final_drained_motion:     %d\n", final_drained_motion);
    printf("  final_drained_fisheye:    %d\n", final_drained_fisheye);

    printf("\nReader stats:\n");
    printf("  pose_count:               %lu\n", reader_stats.pose_count);
    printf("  gyro_count:               %lu\n", reader_stats.gyro_count);
    printf("  accel_count:              %lu\n", reader_stats.accel_count);
    printf("  fisheye0_count:           %lu\n", reader_stats.fisheye0_count);
    printf("  fisheye1_count:           %lu\n", reader_stats.fisheye1_count);
    printf("  interrupt_fail:           %lu\n", reader_stats.interrupt_fail);
    printf("  fisheye_fail:             %lu\n", reader_stats.fisheye_fail);

    printf("\nQueue stats:\n");
    printf("  motion_pushed:            %lu\n", queue_stats.motion_pushed);
    printf("  motion_popped:            %lu\n", queue_stats.motion_popped);
    printf("  motion_dropped:           %lu\n", queue_stats.motion_dropped);
    printf("  motion_remaining:         %d\n", queue_stats.motion_remaining);
    printf("  fisheye_pushed:           %lu\n", queue_stats.fisheye_pushed);
    printf("  fisheye_popped:           %lu\n", queue_stats.fisheye_popped);
    printf("  fisheye_dropped:          %lu\n", queue_stats.fisheye_dropped);
    printf("  fisheye_remaining:        %d\n", queue_stats.fisheye_remaining);

    printf("\nImage queue stats:\n");
    printf("  image_pushed:             %lu\n", image_stats.image_pushed);
    printf("  image_popped:             %lu\n", image_stats.image_popped);
    printf("  image_dropped:            %lu\n", image_stats.image_dropped);
    printf("  image_buffer_reused:      %lu\n", image_stats.image_buffer_reused);
    printf("  image_buffer_overwritten: %lu\n", image_stats.image_buffer_overwritten);
    printf("  image_copy_fail:          %lu\n", image_stats.image_copy_fail);
    printf("  image_bytes_copied:       %lu\n", image_stats.image_bytes_copied);
    printf("  image_remaining:          %d\n", image_stats.image_remaining);

    printf("\nSave worker stats:\n");
    printf("  save_requested:           %lu\n", save_worker->save_requested);
    printf("  save_enqueued:            %lu\n", save_worker->save_enqueued);
    printf("  save_completed:           %lu\n", save_worker->save_completed);
    printf("  save_request_dropped:     %lu\n", save_worker->save_request_dropped);
    printf("  save_worker_errors:       %lu\n", save_worker->save_worker_errors);
    printf("  save_queue_remaining:     %d\n", save_queue_remaining);
    printf("  save_worker_started:      %s\n", save_worker->worker_started ? "yes" : "no");
    printf("  save_worker_stopped:      %s\n", save_worker->worker_stopped ? "yes" : "no");

    printf("\nResult:\n");
    printf("  T265_IMAGE_SAVE_WORKER_RESULT: %s\n", result);

cleanup:
    if (reader) {
        t265_destroy_simple_reader(reader);
    }
    if (mq) {
        t265_multi_queue_destroy(mq);
    }
    if (save_enabled && !save_worker->worker_stopped) {
        image_save_worker_stop(save_worker);
    }
    image_save_worker_destroy(save_worker);

    (void)output_dir_ready;
    return rc;
}
