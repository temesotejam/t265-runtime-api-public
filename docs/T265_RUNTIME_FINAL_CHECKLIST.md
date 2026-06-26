# T265 Runtime Final Checklist

## 現在の完成度

T265 runtime は、librealsenseなしで Intel RealSense T265 を扱う実用ライブラリ候補として、現時点で **API完成度100%** と評価する。

製品化・配布・長期耐久・パッケージングを含む総合完成度は **95%前後**。
残りはAPI機能追加ではなく、release candidate運用と配布形態の仕上げである。

## 完了済みPhase

- [x] Phase 1: 低レベルUSB / latest state / callback / queue基盤
- [x] Phase 2: include/src/examples/tools構成、syncer / image queue初期確認
- [x] Phase 3: Public API整理、examples実用化、README、udev運用改善
- [x] Phase 4: long-run、syncer stats、image save worker、Phase 4 final verification
- [x] Phase 5: syncer stats整理、image save worker utility整理、Public API分類、Phase 5 final verification
- [x] Phase 6: error code docs、syncer threshold sweep、OpenCV optional example
- [x] Phase 7: Public API最終分類、Phase 7 final verification、Phase 7 completion
- [x] Phase 8 plan: release candidate準備方針

## 基準ログ

| Phase | Log | Result |
|:--|:--|:--|
| Phase 2 | `t265_logs/phase2_final_20260503_215719` | PASS |
| Phase 3 | `t265_logs/phase3_final_20260504_181800` | PASS |
| Phase 4 | `t265_logs/phase4_final_20260506_020238` | PASS_WITH_WARNINGS |
| Phase 5 | `t265_logs/phase5_final_20260506_084604` | PASS_WITH_WARNINGS |
| Phase 6 syncer sweep | `t265_logs/syncer_threshold_sweep_20260506_122819` | PASS |
| Phase 7 | `t265_logs/phase7_final_20260506_173609` | PASS_WITH_WARNINGS |

## Stable API 状態

- [x] 推奨includeは `#include "t265.h"`
- [x] device lifecycle APIあり
- [x] reader lifecycle APIあり
- [x] latest state APIあり
- [x] simple reader APIあり
- [x] multi-queue metadata pathあり
- [x] examplesでstable APIを確認済み
- [x] Phase 7 final verificationでcore examples PASS

## Stable with strict rules

- [x] callback APIあり
- [x] callback -> multi-queue pathあり
- [x] callback内で重い処理をしない方針が明文化済み
- [x] callback内で画像保存 / printf / sleep / OpenCV処理しない方針が明文化済み

## Experimental but usable

- [x] image queueあり
- [x] image viewあり
- [x] owned imageあり
- [x] image queue smoke確認済み
- [x] syncerあり
- [x] syncer threshold sweep確認済み
- [x] default syncer threshold 20 ms推奨
- [x] 15 ms strict candidate確認済み
- [x] OpenCV optional example確認済み

## Utility / optional

- [x] `ensure_t265_runtime.sh`
- [x] sudoなしruntime復帰確認済み（udev rule導入後）
- [x] `diagnose_t265_usb.sh`
- [x] `t265_image_save_worker`
- [x] `build_opencv_examples.sh`
- [x] `example_opencv_fisheye_view`
- [x] OpenCV optional build PASS
- [x] OpenCV optional run PASS

## Phase 7 Final Verification Key Result

Log:

- `t265_logs/phase7_final_20260506_173609`

Overall:

- `PHASE7_FINAL_VERIFICATION: PASS_WITH_WARNINGS`

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

Important stats:

- `fisheye_dropped: 0`
- `image_dropped: 0`
- `image_copy_fail: 0`
- `save_request_dropped: 0`
- `save_worker_errors: 0`
- `run_timeouts: 0`
- `libusb_no_device: 0`
- `raw_stream_control_error: 0`
- final USB status: runtime

## WARN分類

許容WARN候補:

- [x] `image_buffer_reused_only`
- [x] 単発 `LIBUSB_ERROR_TIMEOUT` かつprobe完走 / final USB runtime

改善対象WARN:

- [ ] `image_remaining > 0`
- [ ] `save_queue_remaining > 0`
- [ ] `motion_dropped > 0`

FAIL寄り:

- [ ] `fisheye_dropped > 0`
- [ ] `image_dropped > 0`
- [ ] `image_copy_fail > 0`
- [ ] `save_request_dropped > 0`
- [ ] `save_worker_errors > 0`
- [ ] `LIBUSB_ERROR_NO_DEVICE`
- [ ] `DEV_RAW_STREAMS_CONTROL status=0x0008`

## API完成までの作業

- [x] README / USAGEにQuick startを追加する
- [x] README / USAGEにMakefile build entryを追加する
- [x] WARN分類をfinal verification summaryに追加する
- [x] return code実装統一計画を作る
- [x] public headerコメントをPhase 7/8方針に揃える
- [x] optional OpenCVのPASS/WARN扱いをverificationに含める
- [x] Makefileを追加し、`make all` / `make opencv` を確認する
- [x] return code小patch後のrelease candidate基準ログを固定する


## Phase 8 追加確認

- [x] `T265_PHASE8_RETURN_CODE_IMPLEMENTATION_PLAN.md` を追加
- [x] `T265_PHASE8_WARN_CLASSIFICATION_PLAN.md` を追加
- [x] `Makefile` を追加
- [x] public headerコメントをPhase 7/8方針へ更新
- [x] queue / image / multi-queue周辺のreturn code小patchを実施
- [x] `make -B all` PASS
- [x] `make opencv` PASS
- [x] `./run_t265_phase7_final_verification.sh --build-only` PASS
- [x] `QUEUE_PROBE_RESULT: PASS`
- [x] `MULTI_QUEUE_PROBE_RESULT: PASS`
- [x] README / USAGE にMakefile build entryを追記
- [x] return code小patch後の実機 `./run_t265_phase7_final_verification.sh --full` 再確認


## Release Candidate 状態

- [x] `T265_RUNTIME_RELEASE_CANDIDATE_CHECKLIST.md` を追加
- [x] RC合格条件を定義
- [x] RC許容WARNを定義
- [x] RCでは許容しにくいFAIL条件を定義
- [x] return code小patch後の実機full再確認

現時点は `API complete / RC-ready`。

## 最終評価

現時点では、T265 runtimeのAPI完成度は **100%**。製品化・配布・長期耐久を含む総合完成度は **95%前後**。

既に以下は成立している。

- T265 runtime device `8087:0b37` をlibusbで直接扱える
- Pose / Gyro / Accel / Fisheye metadata を取得できる
- latest state API が制御用途として使える
- callback -> multi-queue がログ/解析用途として使える
- syncerでtimestamp対応付けができる
- image queue / owned image / image save worker が使える
- OpenCV optional exampleが使える
- Phase 7 final verificationが `PASS_WITH_WARNINGS`

残る5%前後は、APIそのものではなく、配布形態、長期耐久ログ、パッケージング、外部利用者向けREADME整理である。
