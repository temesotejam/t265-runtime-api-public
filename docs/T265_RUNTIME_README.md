# T265 Runtime README

## 目的

このプロジェクトは Intel RealSense T265 を `librealsense` に依存せず、`libusb` 経由で直接扱うための実験runtime基盤です。

対象デバイス:

- Runtime mode: `8087:0b37`
- Bootloader / Movidius mode: `03e7:2150`

Phase 2までに、Pose / Gyro / Accel / Fisheye metadata の取得、latest state API、multi-queue、syncer、image queueの初期確認が完了しています。
Phase 3では、Public API、examples、README、画像本体所有権、syncer強化を実用に近づけます。
Phase 4では、長時間・高負荷確認、image queueの保存負荷評価、worker thread分離を進めています。
Phase 5では、Public API境界、syncer stats、image save worker utilityの扱いを実用者向けに整理しました。
Phase 6では、error / return code 方針、syncer threshold 評価、OpenCV optional example を追加しました。
Phase 7では、Public API分類、final verification、README / USAGE を実用ライブラリ候補として最終安定化します。


## Quick start

通常の確認は次の順で行います。

```bash
./ensure_t265_runtime.sh
make examples
./example_latest_state
./example_simple_reader
./example_multi_queue
```

短時間の総合確認:

```bash
./run_t265_phase7_final_verification.sh --full
```

OpenCVはoptionalです。必要な環境だけで実行します。

```bash
make opencv
./example_opencv_fisheye_view --save-one opencv_fisheye.png
```

T265が `03e7:2150` boot mode の場合は、udev rule導入後であれば通常ユーザーで `./ensure_t265_runtime.sh` を実行します。sudoなしで失敗する場合だけ、udev ruleや権限を確認してください。

## 現在の推奨API

推奨構成は **A + C** です。

| 用途 | 推奨API |
|:--|:--|
| ロボット制御 | latest state API |
| 現在姿勢取得 | latest state API |
| 最新IMU取得 | latest state API |
| ログ保存 | callback -> multi-queue API |
| 時系列解析 | callback -> multi-queue API |
| 最短起動サンプル | simple reader API |
| デバッグ・比較 | callback -> single queue API / tools |
| Fisheye画像本体の初期確認 | image queue probe |
| Fisheye画像保存 | image save worker probe / worker thread方式 |

## include方針

利用者向けの推奨includeは以下です。

```c
#include "t265.h"
```

`t265.h` は次の公開候補APIをまとめます。

- device lifecycle
- latest state reader
- simple reader
- multi-queue
- syncer

低レベルUSB/protocol helperは、原則として通常利用者向けではなく tools/probe 向けです。

Phase 7時点のAPI分類:

| API / Tool | 位置づけ | 備考 |
|:--|:--|:--|
| `#include "t265.h"` | stable entry | 利用者向けの推奨入口 |
| device lifecycle | stable | `t265_open()` / `t265_close()` など |
| latest state API | stable | 制御用途の中心 |
| simple reader API | stable | examplesの最短起動path |
| callback registration | stable with strict rules | callback内は軽量処理のみ |
| multi-queue metadata path | stable | ログ/解析用途の中心 |
| image payload queue | experimental but usable | ownership / lifetimeに注意 |
| image view / owned image | experimental but useful | 長く保持する場合はdeep-copy |
| syncer | experimental but usable | T265専用timestamp対応付けhelper |
| blocking pop | experimental | worker thread設計後に再評価 |
| `t265_image_save_worker` | optional standalone utility | public APIではない |
| OpenCV example | optional | core runtimeには依存させない |
| tools/probe | debug / verification | 一般アプリに組み込むAPIではない |

## Error / return code 方針

Phase 6時点では、Public APIの戻り値は以下の意味で扱います。

| Code | Value | 意味 | 見方 |
|:--|--:|:--|:--|
| `T265_OK` | 0 | 成功 | 通常の成功 |
| `T265_ERR_USB` | -1 | USB / 汎用I/O失敗 | 現状の汎用 `-1` と互換扱い |
| `T265_ERR_INVALID_STATE` | -2 | NULL引数、未初期化、状態不整合など | cleanup順序や初期化状態を確認 |
| `T265_ERR_TIMEOUT` | -3 | timeout | Public API境界ではtimeout扱い |
| `T265_ERR_QUEUE_FULL` | -4 | queue push不可 | dropped / backlogを確認 |
| `T265_ERR_QUEUE_EMPTY` | -5 | queueが空 | non-blocking popでは正常状態 |
| `T265_ERR_NOT_FOUND` | -6 | 対象sample未取得など | latest未取得時の候補 |

重要な例:

- `t265_multi_queue_pop_motion()` / `t265_multi_queue_pop_fisheye_meta()` の `T265_ERR_QUEUE_EMPTY` は「今はsampleがない」という通常状態です。
- `while (pop(...) == T265_OK) { ... }` のように空になるまで処理する使い方が基本です。
- `t265_syncer_process()` は例外的に `1` がframeset ready、`0` がno frameset、負値がerror候補です。
- `LIBUSB_ERROR_TIMEOUT` はtools/probe/debug log上のlibusb生errorです。Public APIとしては将来的に `T265_ERR_TIMEOUT` へ寄せる方針です。
- 単発 `LIBUSB_ERROR_TIMEOUT` は、probeがPASSしfinal USB statusがruntimeならWARNとして扱える場合があります。

## Syncer threshold 方針

syncer は、Fisheye frameset timestamp に最も近い Pose / Gyro / Accel を対応付ける T265専用helperです。
Phase 6では threshold sweep と60秒long-runで timestamp threshold の初期評価を行いました。

Phase 6時点の推奨:

| threshold | 位置づけ | メモ |
|:--|:--|:--|
| 20 ms | default / recommended | 安全側の初期値として維持 |
| 15 ms | strict candidate | 60秒long-runでPASS。より厳密な解析候補 |
| 10 ms | analysis candidate | sync自体はPASSしたが、60秒runで `LIBUSB_ERROR_TIMEOUT` が2回出た |
| 30 ms | relaxed candidate | 60秒long-runでPASS。許容幅を広げたい場合の候補 |
| 50 ms | not recommended as default | 短時間sweepでPose deltaが約39 msまで広がった |

実測ログ:

- threshold sweep: `t265_logs/syncer_threshold_sweep_20260506_122819`
- 10 ms 60秒long-run: `t265_logs/phase4_long_run_20260506_123308`
- 15 ms 60秒long-run: `t265_logs/phase4_long_run_20260506_123415`
- 20 ms 60秒long-run: `t265_logs/phase4_long_run_20260506_123536`
- 30 ms 60秒long-run: `t265_logs/phase4_long_run_20260506_123644`

probeでthresholdを変える場合:

```bash
SYNC_THRESHOLD_NS=15000000 ./t265_runtime_sync_probe
SYNC_THRESHOLD_NS=20000000 ./t265_runtime_sync_probe
```

まとめ:

- defaultは20 msを維持します。
- より厳密に見たい場合は15 msを第一候補にします。
- accepted ratioは品質指標であり、Phase 6時点では単独のFAIL条件ではありません。
- `skipped_out_of_threshold` は、thresholdを狭めたときの有用な観測値として扱います。

## APIの使い分け

### latest state API

制御用途の第一候補です。
古いsampleをすべて処理せず、現在値を読む用途に使います。

代表的な流れ:

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
t265_reader_free()
t265_stop()
t265_close()
```

注意:

- `t265_reader_create(dev)` は `dev` を所有しません。
- `t265_reader_free(reader)` はheap reader用です。
- stack readerを `t265_reader_init(&reader, dev)` で使うprobeでは `t265_reader_destroy(&reader)` までで、`free()` してはいけません。

### simple reader API

examples向け・最短起動向けの高レベルAPIです。
`open/configure/start/reader_start` をまとめて実行します。

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

multi-queue所有権:

- `t265_multi_queue_create()` はheap queueを返します。
- heap queueは `t265_multi_queue_destroy()` で本体まで解放されます。
- 呼び出し側で `free()` しないでください。
- tools/probe内部ではstack queueを `t265_multi_queue_init()` で使う場合があります。この場合も後始末は `t265_multi_queue_destroy()` ですが、stack object自体は解放されません。

所有権:

- `t265_create_simple_reader(NULL, ...)` は内部でruntimeを作ります。
- 内部runtimeは `t265_destroy_simple_reader()` が `t265_stop()` / `t265_close()` します。
- 外部 `t265_runtime *dev` を渡した場合、simple readerはその `dev` をcloseしません。
- simple readerを `t265_reader_destroy()` だけで片付けないでください。

### callback -> multi-queue API

ログ保存、解析、時系列処理向けです。
callback内では重い処理をせず、queue pushだけを行います。

基本方針:

- Pose / Gyro / Accel -> `motion_queue`
- Fisheye0 / Fisheye1 metadata -> `fisheye_meta_queue`
- main thread / worker thread 側でpopして処理
- `dropped` / `remaining` / `backlog` を監視

禁止事項:

- callback内で `printf` しない
- callback内で `usleep` しない
- callback内でファイル保存しない
- callback内で画像処理しない
- Fisheye画像本体を無計画にqueueへ入れない

## examples

examplesは、Public APIの使い方の基準です。
Phase 7では、stable API examplesをcore checksとして扱います。

| Example | 目的 | 期待結果 |
|:--|:--|:--|
| `example_latest_state` | 制御用途のlatest state最小例 | `EXAMPLE_LATEST_STATE_RESULT: PASS` |
| `example_simple_reader` | high-level simple reader最小例 | `EXAMPLE_SIMPLE_READER_RESULT: PASS` |
| `example_multi_queue` | multi-queueログ/解析例 | `EXAMPLE_MULTI_QUEUE_RESULT: PASS` |
| `example_image_queue` | image view / owned image / image stats例 | `EXAMPLE_IMAGE_QUEUE_RESULT: PASS` |

Phase 7 core examples:

- `example_latest_state`
- `example_simple_reader`
- `example_multi_queue`

Supplemental example:

- `example_image_queue`

## ビルド確認

Phase 3 examplesだけを確認する場合:

```bash
./run_t265_phase3_examples_verification.sh --build-only
```

実機でexamplesを確認する場合:

```bash
./run_t265_phase3_examples_verification.sh --full
```

`example_image_queue` 実行後の `summary.txt` には、画像本体経路の観測値も表示されます。

- `image_count`
- `saved_count`
- `fisheye_pushed` / `fisheye_popped` / `fisheye_dropped`
- `image_pushed` / `image_popped` / `image_dropped`
- `image_buffer_overwritten`
- `image_copy_fail`
- `image_bytes_copied`
- `image_remaining`

## Phase 4 image save worker

PGM保存のようなI/O処理は、main pop pathで直接行うとmotion/fisheye queueへ影響する場合があります。
Phase 4では、画像保存をworker threadへ分離する方式を確認し、optional standalone utilityとして `t265_image_save_worker` を追加しました。

方針:

- callback内では画像保存を行いません。
- image queueからpopした後、保存対象だけをworker queueへenqueueします。
- PGM保存I/Oはworker thread側で行います。
- sensor側dropと保存要求dropを分けて監視します。
- `image_buffer_reused` は内部循環bufferの再利用回数であり、dropではありません。

実行例:

```bash
./run_t265_phase4_long_run.sh --duration-sec 300 --image-save-worker --save-every-sec 1
./run_t265_phase4_long_run.sh --duration-sec 300 --image-save-worker --save-every-sec 2
```

長めに見る場合:

```bash
./run_t265_phase4_long_run.sh --duration-sec 600 --image-save-worker --save-every-sec 1
```

Optional utility:

```bash
./t265_image_save_worker --duration-sec 60 --save-every-sec 1 --output-dir t265_images
```

ビルド確認:

```bash
./run_t265_phase4_long_run.sh --build-only
```

`run_t265_phase4_long_run.sh --build-only` は `t265_image_save_worker` もビルド対象に含みます。

初回確認:

- `tools/t265_image_save_worker.c` を追加済み

## Optional OpenCV example

OpenCV連携はoptionalです。
core runtime、`include/`、`src/` はOpenCVに依存しません。

Phase 6では、image queueからpopしたFisheye画像を `t265_image_view` にし、OpenCVの `cv::Mat` へ変換してPNG保存する最小exampleを追加しました。

追加ファイル:

- `optional/opencv/example_opencv_fisheye_view.cpp`
- `build_opencv_examples.sh`

ビルド:

```bash
./build_opencv_examples.sh
```

OpenCVがない場合、build scriptは `OPENCV_EXAMPLES_BUILD_RESULT: SKIPPED` を出し、core runtimeの失敗とは扱いません。

実行例:

```bash
./example_opencv_fisheye_view --save-one opencv_fisheye_test.png
```

確認済み:

- OpenCV version: `4.5.4`
- build result: `OPENCV_EXAMPLES_BUILD_RESULT: PASS`
- run result: `EXAMPLE_OPENCV_FISHEYE_VIEW_RESULT: PASS`
- saved file: `opencv_fisheye_test.png`
- saved image: `848 x 800`, 8-bit grayscale PNG
- `image_dropped: 0`
- `image_copy_fail: 0`
- final USB status: runtime `8087:0b37`

注意:

- `cv::Mat` をqueue-owned bufferへのnon-owning viewとして作る場合、buffer寿命に注意してください。
- 長く保持する場合、別threadへ渡す場合、保存を遅延する場合は `clone()` または `t265_owned_image` を使ってください。
- callback内でOpenCV処理をしないでください。
- 10秒実機確認で `T265_IMAGE_SAVE_WORKER_RESULT: WARN`
- `motion_dropped: 0`
- `fisheye_dropped: 0`
- `image_dropped: 0`
- `image_copy_fail: 0`
- `save_request_dropped: 0`
- `save_worker_errors: 0`
- `t265_images_test/` にPGM 10枚保存
- 60秒実機確認でも `T265_IMAGE_SAVE_WORKER_RESULT: WARN`
- 60秒確認では `save_completed: 59`
- 60秒確認でも `motion_dropped: 0`, `fisheye_dropped: 0`, `image_dropped: 0`, `image_copy_fail: 0`
- 60秒確認でも `save_request_dropped: 0`, `save_worker_errors: 0`
- `run_t265_phase4_long_run.sh --build-only` で `t265_image_save_worker: PASS`
- 5分utility単体確認でも `T265_IMAGE_SAVE_WORKER_RESULT: WARN`
- 5分確認では `image_count: 18001`, `save_completed: 294`
- 5分確認でも `motion_dropped: 0`, `fisheye_dropped: 0`, `image_dropped: 0`, `image_copy_fail: 0`
- 5分確認でも `save_request_dropped: 0`, `save_worker_errors: 0`, `save_queue_remaining: 0`
- 5分確認後もUSB statusはruntime `8087:0b37` を維持

WARNの主な理由:

- 現時点では `image_buffer_reused > 0` をWARNとして扱います。
- これは内部循環bufferが再利用された観測値であり、単独では画像dropや保存失敗を意味しません。
- 重要な確認値は `motion_dropped`, `fisheye_dropped`, `image_dropped`, `image_copy_fail`, `save_request_dropped`, `save_worker_errors` です。

用語の読み方:

| Item | 意味 | 見方 |
|:--|:--|:--|
| `image_dropped` | image queue側でsensor image sampleを失った | 重要なFAIL要因 |
| `save_request_dropped` | 保存要求をworker queueへ入れられなかった | sensor sample dropとは別。保存頻度が高すぎる可能性 |
| `image_buffer_reused` | 内部循環buffer slotの再利用回数 | dropではない。Phase 5の主名称 |
| `image_buffer_overwritten` | `image_buffer_reused` の旧alias | 互換表示として当面残す |

worker方式の狙い:

- 保存I/Oが遅い場合でも、まず `motion_dropped` / `fisheye_dropped` / `image_dropped` を守ります。
- 保存側が追いつかない場合は、sensor sampleではなく保存要求側の `save_request_dropped` として観測します。
- `T265_IMAGE_SAVE_WORKER_RESULT: WARN` でも、WARN理由が `image_buffer_reused` のみであれば、Phase 7では `PASS_WITH_OPTIONAL_WARNINGS` 相当として扱える候補です。

5分比較結果:

| save interval | log | saved_count | save_request_dropped | motion_dropped | fisheye_dropped | image_dropped | image_copy_fail | libusb_timeout |
|:--|:--|--:|--:|--:|--:|--:|--:|--:|
| 1s | `t265_logs/phase4_long_run_20260505_193032` | 294 | 0 | 0 | 0 | 0 | 0 | 0 |
| 2s | `t265_logs/phase4_long_run_20260505_193918` | 149 | 0 | 0 | 0 | 0 | 0 | 0 |
| 3s | `t265_logs/phase4_long_run_20260505_194935` | 100 | 0 | 0 | 0 | 0 | 0 | 0 |
| 4s | `t265_logs/phase4_long_run_20260505_195524` | 75 | 0 | 0 | 0 | 0 | 0 | 0 |
| 5s | `t265_logs/phase4_long_run_20260505_200043` | 60 | 0 | 0 | 0 | 0 | 0 | 1 |

10分確認:

| save interval | log | duration | saved_count | save_request_dropped | motion_dropped | fisheye_dropped | image_dropped | image_copy_fail | libusb_timeout |
|:--|:--|--:|--:|--:|--:|--:|--:|--:|--:|
| 1s | `t265_logs/phase4_long_run_20260505_201212` | 600s | 589 | 0 | 0 | 0 | 0 | 0 | 1 |

30分確認:

| save interval | log | duration | saved_count | save_request_dropped | motion_dropped | fisheye_dropped | image_dropped | image_copy_fail | libusb_timeout |
|:--|:--|--:|--:|--:|--:|--:|--:|--:|--:|
| 1s | `t265_logs/phase4_long_run_20260505_203426` | 1801s | 1767 | 0 | 0 | 0 | 0 | 0 | 0 |

main path保存との差:

- main path保存1秒では `motion_dropped: 827` が出ましたが、worker保存1秒では0でした。
- main path保存2秒では `motion_dropped: 9781` / `fisheye_dropped: 380` が出ましたが、worker保存2秒ではどちらも0でした。
- main path保存4秒では `motion_dropped: 1011` が出ましたが、worker保存4秒では0でした。
- worker保存1秒は10分でも `motion_dropped: 0`, `fisheye_dropped: 0`, `image_dropped: 0`, `save_request_dropped: 0` でした。
- worker保存1秒は30分でも `motion_dropped: 0`, `fisheye_dropped: 0`, `image_dropped: 0`, `save_request_dropped: 0` でした。

現時点の推奨:

- callback内では画像保存しません。
- image queue pop後のPGM保存や重い画像処理はworker threadへ分離します。
- 保存要求が溢れた場合は `save_request_dropped` として扱い、sensor sample dropとは分けて監視します。
- `motion_dropped` / `fisheye_dropped` / `image_dropped` を優先して守ります。

Phase 2全体の基準確認をする場合:

```bash
./run_t265_phase2_final_verification.sh --full
```

短時間のbuild-only確認:

```bash
./run_t265_phase2_final_verification.sh --build-only
```

## 手動ビルド例

```bash
gcc -Wall -Wextra -Iinclude -Isrc \
    examples/example_latest_state.c \
    src/t265_runtime.c \
    src/t265_runtime_threads.c \
    src/t265_runtime_queue.c \
    src/t265_runtime_multi_queue.c \
    src/t265_syncer.c \
    /lib/x86_64-linux-gnu/libusb-1.0.so.0 \
    -lpthread \
    -o example_latest_state
```

同じ構成で `examples/example_simple_reader.c` と `examples/example_multi_queue.c` もビルドできます。

## USB診断

まずUSB状態を確認します。

```bash
lsusb
./diagnose_t265_usb.sh
```

期待するruntime mode:

```text
8087:0b37 Intel Corp. Intel(R) RealSense(TM) Tracking Camera T265
```

bootloader / Movidius modeの場合:

```text
03e7:2150 Intel Myriad VPU [Movidius Neural Compute Stick]
```

この場合は、runtime firmwareへの遷移が必要です。

```bash
sudo ./ensure_t265_runtime.sh
```

この環境では、ただのUSB再接続だけでは `8087:0b37` に戻らないことがあります。
`sudo ./ensure_t265_runtime.sh` を実行するとruntime modeへ戻ることが多いです。

毎回sudoを打ちたくない場合は、udev ruleを一度だけインストールします。

```bash
sudo ./install_t265_udev_rules.sh
```

その後、T265を挿し直してから通常ユーザーで確認します。

```bash
./ensure_t265_runtime.sh
lsusb
```

`ensure_t265_runtime.sh` は boot helper を複数回retryします。
調整したい場合は以下の環境変数を使います。

```bash
T265_BOOT_RETRIES=5 T265_RUNTIME_WAIT_SECONDS=10 ./ensure_t265_runtime.sh
```

期待:

```text
8087:0b37 Intel Corp. Intel(R) RealSense(TM) Tracking Camera T265
```

sudoを使えない、または helper の挙動を切り分けたい場合:

```bash
./t265_boot_libusb
```

## よく見るログ

Phase 3 examples verification:

```bash
cat t265_logs/phase3_examples_*/summary.txt
```

Phase 3 examples 基準ログ:

- `t265_logs/phase3_examples_20260504_181121`
- `PHASE3_EXAMPLES_VERIFICATION: PASS`
- `example_latest_state: PASS`
- `example_simple_reader: PASS`
- `example_multi_queue: PASS`
- `example_image_queue: PASS`

この基準ログでは、udev rule導入後に通常ユーザーの `./ensure_t265_runtime.sh` が成功し、T265は `8087:0b37` として認識された。

image queue stats:

- `image_count: 30`
- `saved_count: 1`
- `fisheye_dropped: 0`
- `image_dropped: 0`
- `image_buffer_overwritten: 0`
- `image_copy_fail: 0`
- `image_remaining: 0`

Phase 2 final verification:

```bash
cat t265_logs/phase2_final_*/summary.txt
```

summaryで見る項目:

- Initial USB status
- ensure_t265_runtime
- Build results
- Example run results
- Failure reasons
- Overall

## トラブルシュート

### T265が `03e7:2150` で見える

意味:

- T265がruntime modeではなくbootloader / Movidius modeにいる可能性があります。
- この環境では、USB再接続だけでは `8087:0b37` に戻らないことがあります。

推奨対処:

```bash
sudo ./ensure_t265_runtime.sh
lsusb
```

`8087:0b37` に戻ったことを確認してから、examplesやverificationを実行します。

補足:

- `sudo` が必要なので、verification script は自動でこれを強制実行しません。
- `sudo ./install_t265_udev_rules.sh` を一度実行してudev ruleを入れると、以後は通常ユーザーで `./ensure_t265_runtime.sh` を実行できる可能性があります。
- `ensure_t265_runtime.sh` は `T265_BOOT_RETRIES` 回だけ boot helper をretryします。
- `./t265_boot_libusb` は環境や状態によって `LIBUSB_ERROR_TIMEOUT` になることがあります。
- `sudo ./ensure_t265_runtime.sh` 後も戻らない場合に、USB再接続を試します。

### `DEV_RAW_STREAMS_CONTROL: status=0x0008`

意味:

- stream configurationがdevice stateと衝突している可能性があります。
- 直前のprobeがstreaming stateを変えた可能性もあります。

対処:

- failing probeを単体で再実行する
- `sudo ./ensure_t265_runtime.sh` を実行する
- それでも戻らない場合はUSB再接続する
- `summary.txt` の Failure reasons を確認する

### `LIBUSB_ERROR_TIMEOUT`

意味:

- interrupt read / bulk read がtimeoutしています。
- device state、USBポート、ケーブル、直前probeの影響が考えられます。
- Phase 4 syncer 5分long-runでは、82回中3回のinterrupt read timeoutが出ましたが、該当runはいずれも `SYNC_PROBE_RESULT: PASS` で終了し、final USB statusもruntimeを維持しました。
- そのため、単発timeoutはWARN要因として扱い、probeがPASSしてdeviceがruntimeを維持している場合は継続観測対象にします。

対処:

- failing probeを単体で再実行する
- `dmesg_tail` ログを見る
- 必要なら `sudo ./ensure_t265_runtime.sh` でruntime modeへ戻す
- それでも戻らない場合はUSB再接続する

### `LIBUSB_ERROR_NO_DEVICE`

意味:

- 実行中にT265がUSBから落ちた、またはboot modeに戻った可能性があります。

対処:

- `after_*_lsusb.txt` を確認する
- `03e7:2150` なら `sudo ./ensure_t265_runtime.sh` を実行する
- 改善しない場合はUSB再接続する

## 既知の制限

- `librealsense` 完全互換ではありません。
- Fisheye画像本体の所有権管理はPhase 3で初期整理済みですが、実用APIとしてはPhase 4以降も検証が必要です。
- image queueは短時間・5分確認済みですが、公開APIとしての安定化は継続課題です。
- image payload queue / syncerはPhase 7時点では experimental but usable であり、latest stateやmetadata multi-queueより注意して扱います。
- `t265_image_save_worker` はoptional standalone utilityであり、public APIではありません。
- PGM保存のようなI/Oをmain pop pathで行うと、motion/fisheye queueへ影響する場合があります。
- 画像保存はworker threadへ分離する方針がPhase 4時点では有力です。
- syncerは簡易frameset化段階です。
- OpenCV依存はまだ導入しません。
- file loggerは未実装です。
- 5分long-runは実施済みですが、10分以上の長時間安定性試験は継続課題です。
- motion_queueは高頻度IMUで詰まりやすいです。
- queue容量を増やすだけでは根本解決にならない場合があります。

## Phase 4で次に進めること

優先度の高い候補:

1. Phase 4 final verificationを継続的な回帰確認に使う
2. syncerのtimestamp対応付け強化
3. motion_queue最適化
4. OpenCV optional exampleの検討
5. 必要に応じてutility単体10分/30分確認を追加する

Phase 4 final verification:

```bash
./run_t265_phase4_final_verification.sh --build-only
./run_t265_phase4_final_verification.sh --full
```

初回確認:

- build-only log: `t265_logs/phase4_final_20260506_020215`
- build-only result: `PHASE4_FINAL_VERIFICATION: BUILD_ONLY_PASS`
- full log: `t265_logs/phase4_final_20260506_020238`
- full result: `PHASE4_FINAL_VERIFICATION: PASS_WITH_WARNINGS`
- fullではlatest/syncer/image queue/image save worker/pose timestamp readerを短時間確認済み
- final USB statusはruntime `8087:0b37` を維持

## 参照ドキュメント

- `T265_RUNTIME_USAGE.md`
- `T265_RUNTIME_PUBLIC_API_PLAN.md`
- `T265_SIMPLE_READER_OWNERSHIP_PLAN.md`
- `T265_FISHEYE_IMAGE_OWNERSHIP_PLAN.md`
- `T265_IMAGE_VIEW_API_PLAN.md`
- `T265_PHASE3_IMAGE_QUEUE_VERIFICATION.md`
- `T265_PHASE3_COMPLETION_CHECKLIST.md`
- `T265_PHASE2_COMPLETION_CHECKLIST.md`
- `T265_PHASE4_PLAN.md`
- `T265_PHASE4_LONG_RUN_TEST_PLAN.md`
- `T265_PHASE4_FINAL_VERIFICATION.md`
- `T265_PHASE4_COMPLETION_CHECKLIST.md`
- `T265_PHASE4_IMAGE_SAVE_WORKER_PROBE_PLAN.md`
- `T265_PHASE4_IMAGE_SAVE_WORKER_UTILITY_PLAN.md`
- `T265_PHASE4_WORKER_THREAD_PLAN.md`
- `T265_PHASE3_PLAN.md`
- `NEXT_SESSION_T265.md`

## 現時点の結論

- 制御用途は latest state API を使います。
- 最短起動・examples用途は simple reader API を使います。
- ログ・解析用途は callback -> multi-queue API を使います。
- callback内では重い処理をしません。
- Fisheye画像本体は所有権設計を守って扱います。
- PGM保存や重い画像処理はworker threadへ分離する方針がPhase 4時点では有力です。
- Phase 3は `t265_logs/phase3_final_20260504_181800` を基準ログとして完了扱いです。
- Phase 4は `t265_logs/phase4_final_20260506_020238` を短時間core check基準ログとして完了扱いです。
- Phase 4 final verificationは `PHASE4_FINAL_VERIFICATION: PASS_WITH_WARNINGS` で完走済みです。
- Phase 4では、image save workerの5分比較で1〜5秒保存条件のsensor側drop 0を確認済みです。
- 10分・1秒保存でもsensor側drop 0と保存要求drop 0を確認済みです。
- 30分・1秒保存でもsensor側drop 0と保存要求drop 0を確認済みです。
- `image_buffer_reused` を内部循環buffer再利用の主名称とし、旧 `image_buffer_overwritten` は互換aliasとして残しています。
- 名称変更後の実機確認log: `t265_logs/phase4_long_run_20260506_013127`
- このlogでは `image_buffer_reused: 570` と `image_buffer_overwritten: 570` が同じ値で出ています。
- 次はPhase 5として、API安定化、syncer stats整理、worker save方式の扱い、OpenCV optional example、motion_queue最適化へ進みます。

## Phase 8 WARN classification

Phase 7 final verification は `PASS_WITH_WARNINGS` になる場合があります。
これは必ずしも悪い結果ではありません。WARNの中身を分類して判断します。

### Tolerated warnings

| WARN | 意味 | 扱い |
|:--|:--|:--|
| `image_buffer_reused_only` | 内部循環buffer slotが再利用された | dropではない。許容WARN候補 |
| single `LIBUSB_ERROR_TIMEOUT` with runtime final USB | 単発USB timeoutだがprobe完走 | T265/USBでは起こりうる。runtime維持なら許容WARN候補 |
| OpenCV `SKIPPED` | OpenCV未導入 | OpenCVはoptionalなのでcore failureではない |

### Watch warnings

| WARN | 意味 | 扱い |
|:--|:--|:--|
| `image_remaining > 0` | image queueに残りがある | pop側負荷を確認 |
| `save_queue_remaining > 0` | save worker queueに残りがある | 保存処理の遅れを確認 |
| `motion_dropped > 0` | motion queueでdrop | 条件次第で改善対象 |
| repeated `LIBUSB_ERROR_TIMEOUT` | timeoutが多い | USB環境 / 負荷を確認 |

### Fail-oriented warnings

| Condition | 扱い |
|:--|:--|
| `fisheye_dropped > 0` | FAIL寄り |
| `image_dropped > 0` | FAIL |
| `image_copy_fail > 0` | FAIL |
| `save_request_dropped > 0` | FAIL寄り |
| `save_worker_errors > 0` | FAIL |
| `LIBUSB_ERROR_NO_DEVICE` | FAIL |
| `DEV_RAW_STREAMS_CONTROL status=0x0008` | FAIL / device state conflict |
| final USB status not runtime | FAIL |

Phase 7基準ログ `t265_logs/phase7_final_20260506_173609` では、`image_dropped == 0`、`image_copy_fail == 0`、`save_request_dropped == 0`、`save_worker_errors == 0`、final USB status runtime でした。
そのため `PASS_WITH_WARNINGS` は実用上良好な結果として扱えます。

## Phase 8 build entry

Phase 8では、手動gccコマンドに加えて最小 `Makefile` を追加しました。
core runtimeは引き続き `libusb` + `pthread` のみでbuildできます。
OpenCVはoptional targetです。

```bash
make examples
make tools
make all
```

Optional OpenCV example:

```bash
make opencv
```

OpenCVがない環境では、従来どおり `build_opencv_examples.sh` が `SKIPPED` 扱いにできます。
OpenCVはcore runtimeの必須依存ではありません。
