# T265 Runtime Usage


## 0. Quick start

T265をruntime modeへ戻し、examplesをbuildして、latest stateを確認します。

```bash
./ensure_t265_runtime.sh
make examples
./example_latest_state
```

Phase 7の短時間総合確認:

```bash
./run_t265_phase7_final_verification.sh --full
```

OpenCV optional確認:

```bash
make opencv
./example_opencv_fisheye_view --save-one opencv_fisheye.png
```

OpenCVは必須ではありません。core runtimeは `libusb` + `pthread` のみで動きます。

## 1. 目的

- Intel RealSense T265 を `librealsense` に依存せず、`libusb` 経由で直接扱う。
- Ubuntu / Linux バージョン差の影響を受けにくい独自 runtime 基盤を作る。
- T265 runtime device `8087:0b37` を対象にする。
- Pose / IMU / Fisheye metadata を取得できる API を構築する。
- Fisheye 画像本体の本格的な buffer 管理はまだ別フェーズとする。

## 2. 現在の到達点

- 低レベル USB 通信が動作している。
- stream configuration が動作している。
- Pose / Gyro / Accel / Fisheye metadata の取得が動作している。
- 2-thread reader 構成が単一スレッドより安定することを確認した。
- latest state API が実機 PASS 済み。
- callback API が実験済み。
- queue API が単体 PASS 済み。
- callback -> single queue が実機 PASS 済み。
- multi-queue API が単体 PASS 済み。
- callback -> multi-queue probe が実装済み。
- `motion_queue` / `fisheye_meta_queue` を分けることで、詰まりの原因を分離して観測できた。

## 3. 最終推奨構成

- 推奨構成は **A + C**。
- 利用者向けの推奨includeは `#include "t265.h"`。

### A. latest state API

- 制御用。
- 最新値だけを読む。
- 古い sample を溜めない。
- 遅れにくい。
- ロボット制御向き。

### C. callback -> multi-queue API

- ログ・解析・時系列処理用。
- callback 内は queue push だけ行う。
- `motion_queue` と `fisheye_meta_queue` に分ける。
- pop 側で処理する。
- `dropped` / `remaining` / `backlog` を監視する。

### B. callback -> single queue API

- 検証用として残す。
- 最終推奨ではない。
- single queue は単純だが、IMU が queue を圧迫しやすい。
- multi-queue 比較用として有用。

### 3.1 Phase 7時点のAPI分類

| API / Tool | 位置づけ | 用途 |
|:--|:--|:--|
| `#include "t265.h"` | stable entry | 利用者向けの推奨入口 |
| latest state API | stable | 制御用途 |
| simple reader API | stable | examples / 最短起動 |
| callback registration | stable with strict rules | callback内は軽量処理のみ |
| multi-queue metadata path | stable | ログ / 解析 |
| image payload queue | experimental but usable | 画像本体。ownership注意 |
| image view / owned image | experimental but useful | deep-copy規則を守る |
| syncer | experimental but usable | T265専用timestamp対応付け |
| blocking pop | experimental | worker thread設計後に再評価 |
| `t265_image_save_worker` | optional standalone utility | public APIではない |
| OpenCV example | optional | core runtimeには不要 |
| tools/probe | debug / verification | 一般アプリ用APIではない |

Phase 7では、latest state、simple reader、metadata multi-queue を安定中心APIとして扱う。
image payload path と syncer は実用可能だが、ownership / timestamp threshold / WARN基準を確認しながら使う。
OpenCV example は optional であり、core runtime の完成条件には含めない。

### 3.1.1 複数台 API

複数の T265 を扱う場合は `t265_context` を作り、runtime mode
(`8087:0b37`) のデバイス一覧を refresh してから index 指定で開く。

```c
t265_context *ctx = t265_context_create();
int count = t265_context_refresh_devices(ctx);

t265_runtime *dev0 = t265_context_open_device(ctx, 0);
t265_runtime *dev1 = t265_context_open_device(ctx, 1);
```

列挙結果の `t265_device_info.serial` には USB descriptor の serial number が
入る。抜き差しで index / bus / address が変わる環境では serial 指定で開く。

```c
t265_runtime *dev = t265_context_open_device_by_serial(ctx, "YOUR_T265_SERIAL");
```

左右 / 前後などの役割を固定する場合は role file を使う。

```text
left=YOUR_LEFT_T265_SERIAL
right=YOUR_RIGHT_T265_SERIAL
```

```c
t265_context_load_roles(ctx, "t265_roles.conf");
t265_runtime *left = t265_context_open_device_by_role(ctx, "left");
```

`t265_open()` は互換 API として残しており、内部では context を作って
index 0 の T265 を開く。既存の1台用コードはそのまま使える。

列挙確認:

```sh
make examples
./example_list_devices
./example_multi_latest_state
./example_open_by_role t265_roles.conf left
./t265_multi_latest_long_run --duration-sec 300 --role-file t265_roles.conf --role0 left --role1 right
```

注意: `03e7:2150` として見えている T265 は bootloader / Movidius mode。
複数台 API は runtime mode の `8087:0b37` を対象にするため、
`./ensure_t265_runtime.sh` 等で runtime mode に戻してから実行する。

`t265_boot_libusb` は複数の bootloader device を列挙し、bus/address ごとに
firmware を送る。`ensure_t265_runtime.sh` は起動時に見えていた T265 台数分の
runtime device が揃うまで待機する。

### 3.2 Error / return code の読み方

Phase 6時点の基本方針:

| Code | Value | 意味 |
|:--|--:|:--|
| `T265_OK` | 0 | 成功 |
| `T265_ERR_USB` | -1 | USB / 汎用I/O失敗 |
| `T265_ERR_INVALID_STATE` | -2 | NULL引数、未初期化、状態不整合など |
| `T265_ERR_TIMEOUT` | -3 | timeout |
| `T265_ERR_QUEUE_FULL` | -4 | queue push不可 |
| `T265_ERR_QUEUE_EMPTY` | -5 | queueが空 |
| `T265_ERR_NOT_FOUND` | -6 | 対象sample未取得など |

注意:

- non-blocking pop APIの `T265_ERR_QUEUE_EMPTY` は失敗ではなく、通常の「今はsampleがない」状態。
- `t265_syncer_process()` は特殊で、`1` がframeset ready、`0` がno frameset、負値がerror候補。
- `LIBUSB_ERROR_TIMEOUT` はdebug logに出るlibusb生error。Public API境界では `T265_ERR_TIMEOUT` へ寄せる方針。
- 現状の実装には汎用 `-1` 返却も残るため、Phase 6ではまずドキュメント上で `T265_ERR_USB` / 汎用失敗相当として扱う。

non-blocking popの例:

```c
while (t265_multi_queue_pop_motion(mq, &sample) == T265_OK) {
    /* process motion sample */
}
```

このloopを抜けた理由が `T265_ERR_QUEUE_EMPTY` なら正常です。

### 3.3 Syncer threshold の使い分け

syncer は、Fisheye frameset timestamp に最も近い Pose / Gyro / Accel を対応付ける T265専用helper。
Phase 6では `SYNC_THRESHOLD_NS` によるthreshold sweepと60秒long-runを確認した。

Phase 6時点の推奨:

| threshold | 位置づけ | 備考 |
|:--|:--|:--|
| 20 ms | default / recommended | 安全側の初期値として維持 |
| 15 ms | strict candidate | 60秒long-runでPASS。厳密解析候補 |
| 10 ms | analysis candidate | syncはPASS。ただし60秒runで `LIBUSB_ERROR_TIMEOUT` が2回 |
| 30 ms | relaxed candidate | 60秒long-runでPASS |
| 50 ms | default非推奨 | 短時間sweepでPose deltaが約39 msまで広がった |

確認済みログ:

- threshold sweep: `t265_logs/syncer_threshold_sweep_20260506_122819`
- 10 ms 60秒long-run: `t265_logs/phase4_long_run_20260506_123308`
- 15 ms 60秒long-run: `t265_logs/phase4_long_run_20260506_123415`
- 20 ms 60秒long-run: `t265_logs/phase4_long_run_20260506_123536`
- 30 ms 60秒long-run: `t265_logs/phase4_long_run_20260506_123644`

probeでthresholdを変える例:

```bash
SYNC_THRESHOLD_NS=15000000 ./t265_runtime_sync_probe
SYNC_THRESHOLD_NS=20000000 ./t265_runtime_sync_probe
```

読み方:

- `accepted_ratio_percent` は品質指標であり、Phase 6時点では単独のFAIL条件ではない。
- `skipped_out_of_threshold` は、thresholdを狭めたときの観測値として有用。
- defaultは20 msを維持し、厳密に見たい場合は15 msを第一候補にする。

## 4. latest state API の使い方

用途:

- ロボット制御
- 現在姿勢の取得
- 現在の Gyro / Accel 取得
- 最新 Fisheye metadata 確認

代表的な流れ:

```text
t265_open()
t265_configure_streams()
t265_start()
t265_reader_init()
t265_reader_start()
t265_get_latest_pose()
t265_get_latest_gyro()
t265_get_latest_accel()
t265_get_latest_fisheye_info()
t265_reader_stop()
t265_reader_destroy()
t265_close()
```

注意:

- すべての sample 履歴は残らない。
- 最新値を読む用途に特化する。
- 制御用途ではこれを第一候補にする。
- heap reader を `t265_reader_create()` で作った場合は、cleanup に `t265_reader_free()` を使う。
- stack reader を `t265_reader_init()` で初期化した場合は、従来どおり `t265_reader_destroy()` まででよい。

## 4.1 simple reader API の使い方

用途:

- 初心者向けの最短起動
- examples 用の短い確認コード
- `open/configure/start/reader_start` をまとめて扱いたい場合

代表的な流れ:

```text
t265_multi_queue_create()
t265_create_simple_reader(NULL, 1, 1, 1, mq)
t265_get_latest_pose()
t265_multi_queue_pop_motion()
t265_multi_queue_pop_fisheye_meta()
t265_destroy_simple_reader()
t265_multi_queue_destroy()
```

所有権:

- `t265_create_simple_reader(NULL, ...)` は内部で `t265_runtime` を作る。
- 内部作成された runtime は `t265_destroy_simple_reader()` が stop/close する。
- 外部 `t265_runtime *dev` を渡した場合、simple reader はその `dev` を close しない。
- simple reader の戻り値を `t265_reader_destroy()` だけで片付けない。
- queue を併用する場合は、reader を止めた後に queue を destroy する。

## 5. callback -> multi-queue API の使い方

用途:

- ログ保存
- 解析
- 時系列処理
- motion 系と Fisheye metadata 系を分けて処理したい場合

基本構成:

- Pose / Gyro / Accel callback -> `motion_queue`
- Fisheye0 / Fisheye1 callback -> `fisheye_meta_queue`
- main thread / worker thread が別々に pop する

重要:

- callback 内では重い処理をしない。
- callback 内では `printf` しない。
- callback 内ではファイル保存しない。
- callback 内では画像処理しない。
- callback 内では queue push だけ行う。
- Fisheye 画像本体は queue に入れない。
- metadata のみ扱う。

## 6. 実験結果から分かったこと

### callback 内処理

- `CALLBACK_WORK_US=2000us` 付近が安全圏。
- `CALLBACK_WORK_US=5000us` 級では T265 が不安定になる可能性がある。
- callback 内で重い処理をする設計は避ける。

### single queue

- `POP_WORK_US=0 / 500us` では dropped なし。
- `POP_WORK_US=2000us` 以上では dropped が発生する。
- queue 容量 `1024` から `4096` へ増やすと改善するが、根本解決にはならない。

### multi-queue

- `motion_queue` と `fisheye_meta_queue` に分けることで、詰まりの原因を分離できる。
- `POP_WORK_US=2000` では `motion_queue` が詰まり、`fisheye_meta_queue` はほぼ保護された。
- 実測例:
  - `motion dropped: 2502`
  - `fisheye dropped: 0`
  - `motion remaining before final flush: 1024`
  - `fisheye remaining before final flush: 1`
  - `stop_requested: no`
  - `main_loop_completed: yes`
  - `final_flush_completed: yes`

解釈:

- motion 系、特に Gyro / Accel は高頻度で queue を圧迫しやすい。
- Fisheye metadata は multi-queue 化により motion 系の詰まりから分離できた。
- 次の最適化対象は `motion_queue` 側である。

### Phase 4 image save worker

Phase 4では、Fisheye画像本体のPGM保存をmain pop pathで行う方式と、worker threadへ分離する方式を比較した。
さらに、worker保存方式を手動確認しやすいoptional standalone utilityとして `t265_image_save_worker` に切り出した。

方針:

- callback内では引き続き画像保存しない。
- image queueからpopした後、保存対象だけをworker queueへenqueueする。
- PGM保存I/Oはworker thread側で行う。
- sensor sample dropと保存要求dropを分けて監視する。
- 保存要求が追いつかない場合は `save_request_dropped` として扱い、`motion_dropped` / `fisheye_dropped` / `image_dropped` を優先して守る。
- `image_buffer_reused` は内部循環buffer再利用の観測値であり、dropではない。
- `image_buffer_overwritten` は `image_buffer_reused` の旧aliasとして当面残す。
- `t265_image_save_worker` はoptional standalone utilityであり、public APIではない。

用語:

| Item | 意味 | 優先度 |
|:--|:--|:--|
| `image_dropped` | image queue側でsensor image sampleを失った | 高 |
| `save_request_dropped` | 保存要求をworker queueへ入れられなかった | 中 |
| `image_buffer_reused` | 内部循環buffer slotの再利用 | dropではない |
| `image_buffer_overwritten` | `image_buffer_reused` の旧alias | 互換表示 |

optional utility:

```bash
./t265_image_save_worker --duration-sec 60 --save-every-sec 1 --output-dir t265_images
```

確認済み:

- 10秒実機確認で `T265_IMAGE_SAVE_WORKER_RESULT: WARN`
- 60秒実機確認で `T265_IMAGE_SAVE_WORKER_RESULT: WARN`
- 60秒確認では `image_count: 3599`, `save_completed: 59`
- 60秒確認では `motion_dropped: 0`, `fisheye_dropped: 0`, `image_dropped: 0`, `image_copy_fail: 0`
- 60秒確認では `save_request_dropped: 0`, `save_worker_errors: 0`, `save_queue_remaining: 0`
- WARNは主に `image_buffer_reused > 0` による
- `run_t265_phase4_long_run.sh --build-only` で `t265_image_save_worker: PASS`
- 5分utility単体確認では `image_count: 18001`, `save_completed: 294`
- 5分utility単体確認でも `motion_dropped: 0`, `fisheye_dropped: 0`, `image_dropped: 0`, `image_copy_fail: 0`
- 5分utility単体確認でも `save_request_dropped: 0`, `save_worker_errors: 0`, `save_queue_remaining: 0`
- 5分utility単体確認後もUSB statusはruntime `8087:0b37` を維持

5分比較:

| save interval | worker log | saved_count | save_request_dropped | motion_dropped | fisheye_dropped | image_dropped | image_copy_fail | libusb_timeout |
|:--|:--|--:|--:|--:|--:|--:|--:|--:|
| 1s | `t265_logs/phase4_long_run_20260505_193032` | 294 | 0 | 0 | 0 | 0 | 0 | 0 |
| 2s | `t265_logs/phase4_long_run_20260505_193918` | 149 | 0 | 0 | 0 | 0 | 0 | 0 |
| 3s | `t265_logs/phase4_long_run_20260505_194935` | 100 | 0 | 0 | 0 | 0 | 0 | 0 |
| 4s | `t265_logs/phase4_long_run_20260505_195524` | 75 | 0 | 0 | 0 | 0 | 0 | 0 |
| 5s | `t265_logs/phase4_long_run_20260505_200043` | 60 | 0 | 0 | 0 | 0 | 0 | 1 |

10分確認:

| save interval | worker log | duration | saved_count | save_request_dropped | motion_dropped | fisheye_dropped | image_dropped | image_copy_fail | libusb_timeout |
|:--|:--|--:|--:|--:|--:|--:|--:|--:|--:|
| 1s | `t265_logs/phase4_long_run_20260505_201212` | 600s | 589 | 0 | 0 | 0 | 0 | 0 | 1 |

30分確認:

| save interval | worker log | duration | saved_count | save_request_dropped | motion_dropped | fisheye_dropped | image_dropped | image_copy_fail | libusb_timeout |
|:--|:--|--:|--:|--:|--:|--:|--:|--:|--:|
| 1s | `t265_logs/phase4_long_run_20260505_203426` | 1801s | 1767 | 0 | 0 | 0 | 0 | 0 | 0 |

main path保存との差:

- main path保存1秒では `motion_dropped: 827` が出たが、worker保存1秒では `motion_dropped: 0` だった。
- main path保存2秒では `motion_dropped: 9781` / `fisheye_dropped: 380` が出たが、worker保存2秒ではどちらも0だった。
- main path保存4秒では `motion_dropped: 1011` が出たが、worker保存4秒では0だった。
- worker保存では1〜5秒の全条件でsensor側dropを0に保てた。
- 10分・1秒保存でもsensor側dropと保存要求dropを0に保てた。
- 30分・1秒保存でもsensor側dropと保存要求dropを0に保てた。

解釈:

- PGM保存I/Oはmain pop pathから切り離す方が安全である。
- 画像保存や将来の画像処理は、worker thread方式をPhase 4の推奨候補にする。
- 5秒worker条件で `libusb_timeout: 2` が出たため、10分以上のlong-runで再確認する価値がある。

## 7. 推奨する使い分け

| 用途 | 推奨API |
|:--|:--|
| ロボット制御 | latest state API |
| 現在姿勢取得 | latest state API |
| 最新IMU取得 | latest state API |
| ログ保存 | callback -> multi-queue API |
| 時系列解析 | callback -> multi-queue API |
| デバッグ・比較 | callback -> single queue API |
| queue基本動作確認 | queue probe |
| motion/fisheye分離確認 | callback -> multi-queue probe |

### 7.1 examplesの位置づけ

Phase 7では、examplesをPublic APIの使い方の基準として扱う。

| Example | 位置づけ | 役割 |
|:--|:--|:--|
| `example_latest_state` | core | 制御用途のlatest state最小例 |
| `example_simple_reader` | core | simple readerによる最短起動例 |
| `example_multi_queue` | core | metadata multi-queueのログ/解析例 |
| `example_image_queue` | supplemental | image view / owned image / image stats確認 |

tools/probeはdebug / verification用途であり、一般アプリ向けの最初の入口はexamplesを優先する。

## 8. 実行コマンド例

Phase 3 README:

```bash
cat T265_RUNTIME_README.md
```

USB 診断:

```bash
./diagnose_t265_usb.sh
```

Phase 3 examples確認:

```bash
./run_t265_phase3_examples_verification.sh --build-only
./run_t265_phase3_examples_verification.sh --full
```

latest state 確認:

```bash
./run_t265_combined_tests.sh --latest-only
```

queue flow 確認:

```bash
./run_t265_combined_tests.sh --queue-flow
```

callback queue 確認:

```bash
./run_t265_combined_tests.sh --callback-queue-only
```

callback queue pop sweep:

```bash
./run_t265_combined_tests.sh --callback-queue-pop-sweep
```

queue capacity sweep:

```bash
./run_t265_combined_tests.sh --callback-queue-capacity-sweep
```

multi-queue pop sweep:

```bash
./run_t265_combined_tests.sh --callback-multi-queue-pop-sweep
```

Phase 4 image save worker long-run:

```bash
./run_t265_phase4_long_run.sh --duration-sec 300 --image-save-worker --save-every-sec 1
./run_t265_phase4_long_run.sh --duration-sec 300 --image-save-worker --save-every-sec 2
./run_t265_phase4_long_run.sh --duration-sec 600 --image-save-worker --save-every-sec 1
```

Optional image save worker utility:

```bash
./t265_image_save_worker --duration-sec 60 --save-every-sec 1 --output-dir t265_images
```

utility単体の長め確認:

```bash
./t265_image_save_worker --duration-sec 300 --save-every-sec 1 --output-dir t265_images_test
```

Optional OpenCV example:

```bash
./build_opencv_examples.sh
./example_opencv_fisheye_view --save-one opencv_fisheye_test.png
```

OpenCV exampleの位置づけ:

- OpenCVはoptional依存であり、core runtimeには不要。
- `build_opencv_examples.sh` は `opencv4` がない環境では `OPENCV_EXAMPLES_BUILD_RESULT: SKIPPED` を出す。
- `cv::Mat` はqueue-owned bufferへのnon-owning wrapperとして作れるが、長期保持する場合は `clone()` または `t265_owned_image` を使う。
- callback内でOpenCV処理をしない。

確認済み:

- OpenCV version: `4.5.4`
- build result: `OPENCV_EXAMPLES_BUILD_RESULT: PASS`
- run result: `EXAMPLE_OPENCV_FISHEYE_VIEW_RESULT: PASS`
- saved file: `opencv_fisheye_test.png`
- saved image: `848 x 800`, 8-bit grayscale PNG
- `image_dropped: 0`
- `image_copy_fail: 0`
- final USB status: runtime `8087:0b37`

手動実行:

```bash
POP_WORK_US=0 ./t265_runtime_callback_multi_queue_probe
POP_WORK_US=1000 ./t265_runtime_callback_multi_queue_probe
POP_WORK_US=1500 ./t265_runtime_callback_multi_queue_probe
POP_WORK_US=2000 ./t265_runtime_callback_multi_queue_probe
```

## 9. 既知の制限

- Fisheye 画像本体はまだ安全に queue 化していない。
- 現在は Fisheye metadata のみを扱う。
- 画像本体を扱うには buffer 所有権、コピー、寿命管理が必要。
- image payload queueはPhase 7時点では experimental but usable であり、metadata pathより注意が必要。
- syncerはT265専用timestamp対応付けhelperであり、`librealsense` 互換syncerではない。
- `t265_image_save_worker` はoptional standalone utilityであり、public APIではない。
- `motion_queue` は高頻度 IMU で詰まりやすい。
- `POP_WORK_US=2000` 級の重い処理を全 sample に行うのは危険。
- queue 容量を増やすだけでは根本解決にならない。
- `dropped_count` / `remaining` / `backlog` の監視が必要。
- T265 は USB ケーブルやポート相性に敏感である。
- streaming probe は device state を変えるため、`DEV_STOP` や再接続が必要になる場合がある。
- PGM保存のようなI/Oをmain pop pathで行うと、motion/fisheye queueへ影響する場合がある。
- 画像保存はworker threadへ分離する方針がPhase 4時点では有力である。
- `image_buffer_reused` は内部循環buffer再利用の観測値であり、dropではない。
- 旧名 `image_buffer_overwritten` は互換aliasとして当面残す。

## 10. 今後の課題

### A. motion_queue の最適化

- Pose と IMU を別 queue に分ける。
- Gyro / Accel を間引く。
- Pose を優先する。

### B. worker thread 方式

- queue pop 後の重い処理を別 thread へ逃がす。
- main loop を詰まらせない。
- Phase 4のPGM保存比較では、worker thread分離により1〜5秒保存条件でsensor側dropを0に保てた。
- 今後はworker保存方式をexampleまたはoptional utilityへ昇格するか検討する。

### C. Fisheye画像本体対応

- frame buffer の所有権管理
- コピー方式か参照方式か
- メモリ使用量
- PGM 保存や OpenCV 連携

### D. timestamp 同期

- Pose / IMU / Fisheye metadata の時刻対応付け
- `librealsense` の syncer に近い仕組み

### E. API整理

- 実験 probe と公開 API の切り分け
- stable / experimental / utility の分類をREADMEへ反映
- ヘッダコメント整理
- 使用例の整備

## 11. 最終結論

- 制御用途は latest state API を使う。
- ログ・解析用途は callback -> multi-queue API を使う。
- callback -> single queue API は検証・比較用として残す。
- callback 内では重い処理をしない。
- queue pop 側でも重い処理は間引きや worker 化を検討する。
- 画像保存や重い画像処理は main pop path ではなく worker thread 側へ逃がす。
- 今の段階で、T265 を `librealsense` なしで直接扱う実験 runtime 基盤は Phase 1 完成に近い。

## Phase 8: WARN classification の読み方

Phase 7 final verification の `PASS_WITH_WARNINGS` は、WARNの中身を見て判断します。

許容WARN候補:

- `image_buffer_reused_only`: 内部循環buffer再利用。dropではありません。
- 単発 `LIBUSB_ERROR_TIMEOUT`: probeが完走し、final USB statusがruntimeなら許容WARN候補です。
- OpenCV `SKIPPED`: OpenCVはoptionalなのでcore failureではありません。

要観察WARN:

- `image_remaining > 0`
- `save_queue_remaining > 0`
- `motion_dropped > 0`
- repeated `LIBUSB_ERROR_TIMEOUT`

FAIL寄り:

- `fisheye_dropped > 0`
- `image_dropped > 0`
- `image_copy_fail > 0`
- `save_request_dropped > 0`
- `save_worker_errors > 0`
- `LIBUSB_ERROR_NO_DEVICE`
- `DEV_RAW_STREAMS_CONTROL status=0x0008`
- final USB status が runtime ではない

Phase 7基準ログ:

```text
t265_logs/phase7_final_20260506_173609
PHASE7_FINAL_VERIFICATION: PASS_WITH_WARNINGS
```

このrunでは、sensor drop / image copy fail / worker error / no device / raw stream control error は出ていません。
WARNは主に `image_buffer_reused` と単発timeoutであり、実用上は良好な結果として扱えます。

## Phase 8: Makefile build entry

最小のbuild入口として `Makefile` を追加しました。

Core examples:

```bash
make examples
```

Selected verification tools:

```bash
make tools
```

Core examples + selected tools:

```bash
make all
```

Optional OpenCV example:

```bash
make opencv
```

OpenCVはoptionalです。core runtime、include、srcはOpenCVに依存しません。
