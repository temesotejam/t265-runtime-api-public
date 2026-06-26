/*
 * T265の最小Poseデモ
 *
 * 目的:
 * - runtime状態のT265を1台だけ開く。
 * - pose / gyro / accel / fisheye metadata streamを開始する。
 * - 最新poseを約10Hzで標準出力へ表示する。
 * - 画像やCSVは保存しない。
 *
 * 使い方:
 *   ./demo_minimal_pose
 *   ./demo_minimal_pose 10
 *
 * 実行時間:
 * - 引数なしなら5秒間実行する。
 * - 引数で秒数を指定できる。例: 10秒なら ./demo_minimal_pose 10
 * - 最大指定時間は3600秒、つまり1時間。
 *
 * 期待する結果:
 *   DEMO_MINIMAL_POSE_RESULT: PASS
 */

#include "t265.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define DEMO_DEFAULT_DURATION_SEC 5
#define DEMO_POLL_HZ 10
#define DEMO_SLEEP_US (1000000 / DEMO_POLL_HZ)
#define DEMO_MAX_DURATION_SEC 3600

/*
 * 引数処理は最小限にしている:
 * - 引数なしなら、デフォルトの5秒スモークテスト。
 * - 正の整数を1つ渡すと、その秒数だけ実行する。
 */
static int parse_duration_sec(int argc, char **argv)
{
    char *end = NULL;
    long value;

    if (argc < 2) {
        return DEMO_DEFAULT_DURATION_SEC;
    }

    value = strtol(argv[1], &end, 10);
    if (!end || *end != '\0' || value <= 0 || value > DEMO_MAX_DURATION_SEC) {
        fprintf(stderr, "Usage: %s [duration_sec]\n", argv[0]);
        fprintf(stderr, "duration_sec must be 1..%d\n", DEMO_MAX_DURATION_SEC);
        return -1;
    }

    return (int)value;
}

int main(int argc, char **argv)
{
    t265_runtime *dev = NULL;
    t265_reader_context *reader = NULL;
    int duration_sec = parse_duration_sec(argc, argv);
    int loop_count;
    int pose_seen = 0;
    int gyro_seen = 0;
    int accel_seen = 0;
    int fisheye_seen = 0;
    int rc = 1;

    if (duration_sec <= 0) {
        printf("DEMO_MINIMAL_POSE_RESULT: FAIL\n");
        return 1;
    }

    loop_count = duration_sec * DEMO_POLL_HZ;

    printf("--- Minimal T265 Pose Demo ---\n");
    printf("duration_sec: %d\n", duration_sec);

    /*
     * t265_open() は1台だけ開くための簡易API。
     * ここでは、事前に ensure_t265_runtime.sh などで
     * カメラがruntime状態になっている前提で使う。
     */
    dev = t265_open();
    if (!dev) {
        fprintf(stderr, "Failed to open a runtime T265 device.\n");
        goto cleanup;
    }

    /*
     * このデモで使う3系統のデータを有効化する:
     * - fisheye metadata。画像保存はしない。
     * - IMU sample。gyroとaccelを含む。
     * - pose sample。
     */
    if (t265_configure_streams(dev, 1, 1, 1) != T265_OK) {
        fprintf(stderr, "Failed to configure pose/imu/fisheye streams.\n");
        goto cleanup;
    }

    if (t265_start(dev) != T265_OK) {
        fprintf(stderr, "Failed to start T265 streams.\n");
        goto cleanup;
    }

    /*
     * readerはUSB読み取り用のバックグラウンドスレッドを持つ。
     * 下のmain loopでは、蓄積済みの最新値だけを取得するため、
     * 全パケットを逐次処理する必要はない。
     */
    reader = t265_reader_create(dev);
    if (!reader) {
        fprintf(stderr, "Failed to create reader.\n");
        goto cleanup;
    }

    if (t265_reader_start(reader) != T265_OK) {
        fprintf(stderr, "Failed to start reader.\n");
        goto cleanup;
    }

    /*
     * 約10Hzでpollする。
     * poseは人間が見やすいように表示する。
     * gyro / accel / fisheye metadataは、出力を増やしすぎないため、
     * streamが生きているかだけを確認する。
     */
    for (int i = 0; i < loop_count; ++i) {
        t265_pose_sample pose;
        t265_imu_sample gyro;
        t265_imu_sample accel;
        uint32_t fe0_id = 0;
        uint32_t fe1_id = 0;
        uint64_t fe0_ts = 0;
        uint64_t fe1_ts = 0;

        if (t265_get_latest_pose(reader, &pose) == T265_OK) {
            pose_seen = 1;
            printf(
                "pose t=%" PRIu64 "ns x=%.3f y=%.3f z=%.3f q=(%.3f %.3f %.3f %.3f)\n",
                pose.timestamp_ns,
                pose.x,
                pose.y,
                pose.z,
                pose.quat_i,
                pose.quat_j,
                pose.quat_k,
                pose.quat_r
            );
        } else {
            printf("pose: waiting\n");
        }

        if (t265_get_latest_gyro(reader, &gyro) == T265_OK) {
            gyro_seen = 1;
        }

        if (t265_get_latest_accel(reader, &accel) == T265_OK) {
            accel_seen = 1;
        }

        if (t265_get_latest_fisheye_info(reader, &fe0_id, &fe1_id, &fe0_ts, &fe1_ts) == T265_OK) {
            fisheye_seen = 1;
        }

        usleep(DEMO_SLEEP_US);
    }

    /* reader statsは、バックグラウンドスレッドで受け取った累積カウント。 */
    {
        t265_reader_stats stats;
        if (t265_get_reader_stats(reader, &stats) == T265_OK) {
            printf("\nCounts:\n");
            printf("  pose_count:     %" PRIu64 "\n", stats.pose_count);
            printf("  gyro_count:     %" PRIu64 "\n", stats.gyro_count);
            printf("  accel_count:    %" PRIu64 "\n", stats.accel_count);
            printf("  fisheye0_count: %" PRIu64 "\n", stats.fisheye0_count);
            printf("  fisheye1_count: %" PRIu64 "\n", stats.fisheye1_count);
        }
    }

    printf("\nAvailability:\n");
    printf("  pose:    %s\n", pose_seen ? "yes" : "no");
    printf("  gyro:    %s\n", gyro_seen ? "yes" : "no");
    printf("  accel:   %s\n", accel_seen ? "yes" : "no");
    printf("  fisheye: %s\n", fisheye_seen ? "yes" : "no");

    /*
     * このデモの最小成功条件は、pose / gyro / accelが取得できること。
     * fisheye metadataも上で確認しているが、画像本体の保存やdecodeは
     * 意図的に行わない。
     */
    if (pose_seen && gyro_seen && accel_seen) {
        printf("DEMO_MINIMAL_POSE_RESULT: PASS\n");
        rc = 0;
    } else {
        printf("DEMO_MINIMAL_POSE_RESULT: FAIL\n");
    }

cleanup:
    /*
     * runtimeをstop/closeする前に、readerスレッドを先に止めて解放する。
     * 作成に成功したオブジェクトだけをcleanupする。
     */
    if (reader) {
        t265_reader_free(reader);
    }
    if (dev) {
        t265_stop(dev);
        t265_close(dev);
    }

    if (rc != 0) {
        printf("DEMO_MINIMAL_POSE_RESULT: FAIL\n");
    }

    return rc;
}
