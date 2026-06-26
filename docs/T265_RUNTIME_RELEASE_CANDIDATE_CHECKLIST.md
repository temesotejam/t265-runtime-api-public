# T265 Runtime Release Candidate Checklist

## Release Candidate 状態

T265 runtime は release candidate 目前の状態。

現時点では **API complete / RC-ready** とする。

理由:

- Phase 1〜7 は完了扱い
- Phase 8 release candidate準備はほぼ完了
- Public API分類は整理済み
- README / USAGE Quick start整備済み
- Makefile追加済み
- return code小patchはbuild-only / queue単体 / multi-queue単体でPASS
- return code小patch後の実機 `--full` はPASS_WITH_WARNINGSで確認済み

## RC候補として完了済み

- [x] `#include "t265.h"` を推奨入口として固定
- [x] stable / experimental / utility / internal 分類を整理
- [x] latest state API を制御用途のstable APIとして整理
- [x] simple reader API をexamples向けstable APIとして整理
- [x] multi-queue metadata pathをログ/解析用途のstable APIとして整理
- [x] image queue / image view / owned imageをexperimental but usableとして整理
- [x] syncerをexperimental but usableとして整理
- [x] OpenCVをoptionalとして整理
- [x] `Makefile` 追加
- [x] `make examples` targetあり
- [x] `make tools` targetあり
- [x] `make all` targetあり
- [x] `make opencv` targetあり
- [x] `run_t265_phase7_final_verification.sh` 追加
- [x] Phase 7 build-only PASS
- [x] Phase 8 API完成基準ログあり（return code小patch後）
- [x] WARN classification 追加
- [x] return code implementation plan追加
- [x] queue / image / multi-queue return code小patch実施
- [x] reader / latest / syncer return code小patch実施
- [x] queue単体probe PASS
- [x] multi-queue単体probe PASS

## RC前に必須の残確認

- [x] return code小patch後の実機 `./run_t265_phase7_final_verification.sh --full`
- [x] final USB status runtime
- [x] core examples PASS
- [x] syncer PASS
- [x] OpenCV optional PASSまたはSKIPPED
- [x] `failure_reasons: none`
- [x] sensor drop / image copy fail / worker error が0
- [x] Phase 8 RC基準ログとして固定

RC基準ログ:

- `t265_logs/phase7_final_20260506_173609`
- `PHASE7_FINAL_VERIFICATION: PASS_WITH_WARNINGS`
- final USB status: runtime `8087:0b37`
- `failure_reasons: none`

## RC合格条件

以下を満たせば、T265 runtime RC扱いにしてよい。

- `PHASE7_FINAL_VERIFICATION: PASS` または `PASS_WITH_WARNINGS`
- `failure_reasons: none`
- final USB status: runtime `8087:0b37`
- `example_latest_state: PASS`
- `example_simple_reader: PASS`
- `example_multi_queue: PASS`
- `syncer: PASS`
- `pose_timestamp_reader: PASS`
- `image_dropped: 0`
- `image_copy_fail: 0`
- `save_request_dropped: 0`
- `save_worker_errors: 0`
- OpenCV optionalは `PASS` または `SKIPPED`

## RCで許容できるWARN

- `image_buffer_reused_only`
- 少数の `LIBUSB_ERROR_TIMEOUT` かつprobe完走 / final USB runtime
- OpenCV optional `SKIPPED`

## RCでは許容しにくいWARN / FAIL

- `failure_reasons != none`
- `image_dropped > 0`
- `image_copy_fail > 0`
- `save_request_dropped > 0`
- `save_worker_errors > 0`
- `LIBUSB_ERROR_NO_DEVICE`
- `DEV_RAW_STREAMS_CONTROL status=0x0008`
- final USB status not runtime

## 現時点の評価

API完成度は **100%**。総合完成度は **95%前後**。

実装・API境界・examples・tools・OpenCV optional・Makefile・verification script・documentation は揃っている。
残る最大項目は、APIそのものではなく、配布形態、長期耐久ログ、パッケージング、外部利用者向けREADME整理である。

## 次の一手

API completion後の次の一手は、release candidate配布準備である。

- README / USAGEの古いPhase履歴を後半へ整理する
- release candidate tarball / package構成を決める
- `make install` やCMake/pkg-configを導入するか検討する
- 長時間回帰ログを追加で固定する
