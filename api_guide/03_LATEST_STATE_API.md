# 03. Latest State API

## 用途

latest state API は制御用途の第一候補です。

向いている用途:

- ロボット制御
- 現在姿勢の取得
- 最新Gyro / Accel取得
- 最新Fisheye metadata確認
- 古いsampleを全部処理したくない場合

向いていない用途:

- 全sampleのログ保存
- 時系列解析
- 全Fisheye画像の保存

## 基本の流れ

```text
t265_open()
t265_configure_streams()
t265_start()
t265_reader_create()
t265_reader_start()
t265_get_latest_pose()
t265_get_latest_gyro()
t265_get_latest_accel()
t265_get_latest_fisheye_info()
t265_reader_stop()
t265_reader_free()
t265_stop()
t265_close()
```

## 最小コード骨格

```c
#include "t265.h"
#include <stdio.h>
#include <unistd.h>

int main(void)
{
    t265_runtime *dev = t265_open();
    if (!dev) return 1;

    if (t265_configure_streams(dev, 1, 1, 1) != T265_OK) return 1;
    if (t265_start(dev) != T265_OK) return 1;

    t265_reader_context *reader = t265_reader_create(dev);
    if (!reader) return 1;

    if (t265_reader_start(reader) != T265_OK) return 1;

    for (int i = 0; i < 20; ++i) {
        t265_pose_sample pose;
        if (t265_get_latest_pose(reader, &pose) == T265_OK) {
            printf("pose timestamp_ns=%llu\n", (unsigned long long)pose.timestamp_ns);
        }
        usleep(100000);
    }

    t265_reader_stop(reader);
    t265_reader_free(reader);
    t265_stop(dev);
    t265_close(dev);
    return 0;
}
```

## 注意

- latest state API は履歴を残しません。
- 制御用途では古いsampleを処理しないことが利点です。
- readerを止めてからdeviceをstop/closeします。
- heap readerは `t265_reader_free()` で解放します。
