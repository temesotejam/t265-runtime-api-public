/*
 * T265 Multi Latest Long Run
 *
 * Runs two T265 devices simultaneously and reports reader stats.
 */

#include "t265.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define LONG_RUN_DEVICE_COUNT 2

typedef struct long_run_device {
    t265_runtime *dev;
    t265_reader_context *reader;
    t265_device_info info;
} long_run_device;

static int parse_int_arg(int argc, char **argv, const char *name, int default_value)
{
    for (int i = 1; i + 1 < argc; ++i) {
        if (strcmp(argv[i], name) == 0) {
            int value = atoi(argv[i + 1]);
            return value > 0 ? value : default_value;
        }
    }
    return default_value;
}

static const char *parse_string_arg(int argc, char **argv, const char *name)
{
    for (int i = 1; i + 1 < argc; ++i) {
        if (strcmp(argv[i], name) == 0) {
            return argv[i + 1];
        }
    }
    return NULL;
}

static int start_device(t265_context *ctx, int index, const char *role,
                        long_run_device *slot)
{
    if (role && role[0] != '\0') {
        slot->dev = t265_context_open_device_by_role(ctx, role);
    } else {
        slot->dev = t265_context_open_device(ctx, index);
    }

    if (!slot->dev) {
        fprintf(stderr, "Failed to open device index=%d role=%s.\n",
                index, role ? role : "<none>");
        return 1;
    }

    if (t265_get_device_info(slot->dev, &slot->info) != T265_OK) {
        fprintf(stderr, "Failed to read device info.\n");
        return 1;
    }

    if (t265_configure_streams(slot->dev, 1, 1, 1) != T265_OK) {
        fprintf(stderr, "Failed to configure %s.\n", slot->info.label);
        return 1;
    }

    if (t265_start(slot->dev) != T265_OK) {
        fprintf(stderr, "Failed to start %s.\n", slot->info.label);
        return 1;
    }

    slot->reader = t265_reader_create(slot->dev);
    if (!slot->reader) {
        fprintf(stderr, "Failed to create reader for %s.\n", slot->info.label);
        return 1;
    }

    if (t265_reader_start(slot->reader) != T265_OK) {
        fprintf(stderr, "Failed to start reader for %s.\n", slot->info.label);
        return 1;
    }

    printf("started %s\n", slot->info.label);
    return 0;
}

static void stop_device(long_run_device *slot)
{
    if (slot->reader) {
        t265_reader_free(slot->reader);
        slot->reader = NULL;
    }
    if (slot->dev) {
        t265_stop(slot->dev);
        t265_close(slot->dev);
        slot->dev = NULL;
    }
}

static int stats_pass(const t265_reader_stats *stats)
{
    return stats->pose_count > 0 &&
           stats->gyro_count > 0 &&
           stats->accel_count > 0 &&
           stats->fisheye0_count > 0 &&
           stats->fisheye1_count > 0;
}

int main(int argc, char **argv)
{
    int duration_sec = parse_int_arg(argc, argv, "--duration-sec", 300);
    const char *role_file = parse_string_arg(argc, argv, "--role-file");
    const char *role0 = parse_string_arg(argc, argv, "--role0");
    const char *role1 = parse_string_arg(argc, argv, "--role1");
    t265_context *ctx = NULL;
    long_run_device devices[LONG_RUN_DEVICE_COUNT] = {0};
    int rc = 1;

    printf("--- T265 Multi Latest Long Run ---\n");
    printf("duration_sec: %d\n", duration_sec);

    ctx = t265_context_create();
    if (!ctx) {
        fprintf(stderr, "Failed to create T265 context.\n");
        return 1;
    }

    if (role_file) {
        int loaded = t265_context_load_roles(ctx, role_file);
        if (loaded <= 0) {
            fprintf(stderr, "Failed to load role file: %s\n", role_file);
            goto cleanup;
        }
        printf("loaded roles: %d\n", loaded);
    }

    if (t265_context_refresh_devices(ctx) < LONG_RUN_DEVICE_COUNT) {
        fprintf(stderr, "Need two runtime T265 devices.\n");
        goto cleanup;
    }

    if (start_device(ctx, 0, role0, &devices[0]) != 0 ||
        start_device(ctx, 1, role1, &devices[1]) != 0) {
        goto cleanup;
    }

    for (int sec = 0; sec < duration_sec; ++sec) {
        sleep(1);
        if ((sec + 1) % 10 == 0 || sec + 1 == duration_sec) {
            printf("elapsed: %d/%d sec\n", sec + 1, duration_sec);
        }
    }

    rc = 0;
    printf("\nReader stats:\n");
    for (int i = 0; i < LONG_RUN_DEVICE_COUNT; ++i) {
        t265_reader_stats stats;
        memset(&stats, 0, sizeof(stats));
        if (t265_get_reader_stats(devices[i].reader, &stats) != T265_OK) {
            rc = 1;
            continue;
        }

        printf("  %s\n", devices[i].info.label);
        printf("    pose=%lu gyro=%lu accel=%lu fisheye0=%lu fisheye1=%lu\n",
               stats.pose_count, stats.gyro_count, stats.accel_count,
               stats.fisheye0_count, stats.fisheye1_count);
        printf("    interrupt_fail=%lu interrupt_max_fail_run=%lu\n",
               stats.interrupt_fail, stats.interrupt_max_fail_run);
        printf("    fisheye_fail=%lu fisheye_max_fail_run=%lu\n",
               stats.fisheye_fail, stats.fisheye_max_fail_run);

        if (!stats_pass(&stats)) {
            rc = 1;
        }
    }

    if (t265_list_devices(NULL, 0) < LONG_RUN_DEVICE_COUNT) {
        fprintf(stderr, "Final runtime device count is below two.\n");
        rc = 1;
    }

    printf("T265_MULTI_LATEST_LONG_RUN_RESULT: %s\n", rc == 0 ? "PASS" : "FAIL");

cleanup:
    for (int i = LONG_RUN_DEVICE_COUNT - 1; i >= 0; --i) {
        stop_device(&devices[i]);
    }
    t265_context_destroy(ctx);
    return rc;
}
