# 04. Multi Queue API

## 用途

multi-queue API はログ保存・解析・時系列処理向けです。

構成:

- Pose / Gyro / Accel -> `motion_queue`
- Fisheye0 / Fisheye1 metadata -> `fisheye_meta_queue`

single queueよりも、どちらの系統が詰まっているか見やすくなります。

## 基本方針

callback内では重い処理をしません。

やってよいこと:

- sampleを作る
- queueへpushする
- atomic counterを増やす

やらないこと:

- printf
- sleep / usleep
- file保存
- 画像処理
- OpenCV処理

## popの基本

```c
t265_queue_sample sample;

while (t265_multi_queue_pop_motion(mq, &sample) == T265_OK) {
    if (sample.type == T265_QUEUE_SAMPLE_POSE) {
        /* pose */
    } else if (sample.type == T265_QUEUE_SAMPLE_GYRO) {
        /* gyro */
    } else if (sample.type == T265_QUEUE_SAMPLE_ACCEL) {
        /* accel */
    }
}

while (t265_multi_queue_pop_fisheye_meta(mq, &sample) == T265_OK) {
    if (sample.type == T265_QUEUE_SAMPLE_FISHEYE0) {
        /* fisheye0 metadata */
    } else if (sample.type == T265_QUEUE_SAMPLE_FISHEYE1) {
        /* fisheye1 metadata */
    }
}
```

loopを抜けた理由が `T265_ERR_QUEUE_EMPTY` なら正常です。

## stats確認

```c
t265_multi_queue_stats stats;
if (t265_multi_queue_get_stats(mq, &stats) == T265_OK) {
    printf("motion dropped=%llu\n", (unsigned long long)stats.motion_dropped);
    printf("fisheye dropped=%llu\n", (unsigned long long)stats.fisheye_dropped);
}
```

見るべき値:

- `motion_dropped`
- `fisheye_dropped`
- `motion_remaining`
- `fisheye_remaining`

## 実験で分かったこと

`POP_WORK_US=2000` では motion_queue が先に詰まりました。

実測例:

```text
motion dropped: 2502
fisheye dropped: 0
motion remaining before final flush: 1024
fisheye remaining before final flush: 1
```

つまり、multi-queue化によりFisheye metadataはmotion系の詰まりから保護されやすくなりました。
