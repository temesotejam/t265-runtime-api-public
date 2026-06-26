# T265 API Completion Status

## 結論

T265 runtime の **API完成度は100%扱い** とする。

ここでの「100%」は、librealsense完全互換や製品版SDK完成ではなく、以下を満たしたことを意味する。

- 利用者向けPublic APIの入口が決まっている
- stable / experimental / utility / internal の分類が決まっている
- examplesで主要APIの使い方を確認できる
- Makefileでbuildできる
- final verification scriptで短時間確認できる
- return code方針が実装へ部分反映されている
- image / syncer / OpenCV optional の扱いが明確
- 実機full verificationが `PASS_WITH_WARNINGS` で完走している
- `failure_reasons: none`
- final USB status が runtime を維持している

## API 100% の根拠

最終確認ログ:

- `t265_logs/phase7_final_20260506_173609`

結果:

- `PHASE7_FINAL_VERIFICATION: PASS_WITH_WARNINGS`
- final USB status: runtime `8087:0b37`
- `failure_reasons: none`
- `watch_warnings: none`

Core:

- `example_latest_state: PASS`
- `example_simple_reader: PASS`
- `example_multi_queue: PASS`

Supplemental:

- `syncer: PASS`
- `image_queue_smoke: WARN`
- `image_save_worker_probe: WARN`
- `image_save_worker_utility: WARN`
- `pose_timestamp_reader: PASS`

Optional:

- `opencv_save_one: PASS`

重要値:

- `accepted_framesets: 10`
- `accepted_ratio_percent: 40`
- `fisheye_dropped: 0`
- `image_dropped: 0`
- `image_copy_fail: 0`
- `save_request_dropped: 0`
- `save_worker_errors: 0`
- `run_timeouts: 0`
- `libusb_no_device: 0`
- `raw_stream_control_error: 0`

WARN分類:

```text
tolerated_warnings: image_buffer_reused_only,single_or_low_libusb_timeout_runtime_ok
watch_warnings:     none
failure_reasons:    none
```

## API分類

### Stable

- `#include "t265.h"`
- device lifecycle
- reader lifecycle
- latest state API
- simple reader API
- multi-queue metadata path

### Stable with strict rules

- callback API
- callback -> multi-queue
- callback内はqueue push程度のみ

### Experimental but usable

- image queue / image payload
- image view / owned image
- syncer
- blocking pop

### Utility / optional

- `ensure_t265_runtime.sh`
- `diagnose_t265_usb.sh`
- `t265_image_save_worker`
- `build_opencv_examples.sh`
- OpenCV optional example

### Internal / debug

- low-level raw endpoint probes
- historical investigation probes
- boot helper internals

## API完成に含めるもの

- [x] Public API入口
- [x] examples
- [x] latest state
- [x] simple reader
- [x] callback rules
- [x] multi-queue metadata
- [x] image queue helper
- [x] syncer helper
- [x] OpenCV optional
- [x] Makefile
- [x] final verification script
- [x] return code方針
- [x] WARN分類
- [x] USB runtime復帰運用

## API完成に含めないもの

以下は「API完成後の製品化・運用強化タスク」とする。

- librealsense完全互換
- 30分/数時間級の標準耐久試験
- deb/rpm/package化
- CMake正式導入
- OpenCV必須化
- 完全なerror taxonomy追加
- 全低レベルlibusb errorのPublic API変換
- ABI保証
- semantic versioning運用

## 注意

`PASS_WITH_WARNINGS` は失敗ではない。
今回のWARNは以下の許容WARNである。

- `image_buffer_reused_only`: buffer再利用でありdropではない
- 少数の `LIBUSB_ERROR_TIMEOUT`: probe完走かつfinal USB runtime維持

一方で、次が出た場合はAPI完成状態でも調査対象にする。

- `failure_reasons != none`
- `image_dropped > 0`
- `image_copy_fail > 0`
- `save_request_dropped > 0`
- `save_worker_errors > 0`
- `LIBUSB_ERROR_NO_DEVICE`
- final USB status not runtime

## 最終評価

API完成度: **100%**

製品化・配布・長期耐久を含む総合完成度: **95%前後**

次の自然な作業は、API追加ではなく、release candidate packaging / README polish / 長時間回帰確認である。
