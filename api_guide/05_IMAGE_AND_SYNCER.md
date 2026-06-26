# 05. Image and Syncer

## Image API

Fisheye画像本体はmetadataより重いので、扱いに注意します。

基本方針:

- callback内で画像保存しない
- callback内で画像処理しない
- queue pop後に処理する
- 保持したい画像はdeep-copyする

## image view と owned image

`t265_image_view` は非所有です。

- 軽い
- 元bufferの寿命に依存する
- 後で使う用途には危険

`t265_owned_image` はdeep-copyです。

- 保存、worker thread、OpenCV受け渡し向け
- 使用後に `t265_owned_image_destroy()` が必要

例:

```c
t265_image_view view;
t265_owned_image image;

if (t265_image_view_from_queue_sample(&sample, &view) == T265_OK) {
    if (t265_owned_image_copy_from_view(&view, &image) == T265_OK) {
        t265_write_pgm_from_owned_image("fisheye.pgm", &image);
        t265_owned_image_destroy(&image);
    }
}
```

## image stats

見るべき値:

- `image_dropped`
- `image_copy_fail`
- `image_buffer_reused`
- `image_remaining`

注意:

- `image_buffer_reused` はdropではありません。
- dropを見るなら `image_dropped` と `image_copy_fail` を重視します。

## Syncer API

syncerは、Fisheye timestampを基準にPose / Gyro / Accelを対応付けるhelperです。

これはlibrealsense互換syncerではなく、T265専用の薄いtimestamp対応付けです。

基本:

```c
t265_syncer *syncer = t265_syncer_create();
t265_frameset frameset;

int ret = t265_syncer_process(syncer, &sample, &frameset);
if (ret == 1) {
    /* frameset ready */
} else if (ret == 0) {
    /* no frameset yet */
} else {
    /* error */
}

t265_syncer_destroy(syncer);
```

## threshold

推奨初期値は20 msです。

| threshold | 使い方 |
|:--|:--|
| 20 ms | default / recommended |
| 15 ms | strict candidate |
| 30 ms | relaxed candidate |

```c
t265_syncer_set_threshold_ns(syncer, 20000000ULL);
```

見るべきstats:

- `frameset_emitted`
- `pose_delta_abs_max_ns`
- `gyro_delta_abs_max_ns`
- `accel_delta_abs_max_ns`
- `pose_out_of_threshold`
- `gyro_out_of_threshold`
- `accel_out_of_threshold`
