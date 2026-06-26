# T265 API Guide

このフォルダは、完成扱いにした T265 runtime API を実際に使うための短いまとめです。

詳しい開発履歴やPhaseごとの検証結果はリポジトリ直下の各種 `T265_*.md` に残しています。ここでは、自分のアプリから使うときに必要な情報だけを優先して整理します。

## まず読むもの

1. [01_QUICK_START.md](01_QUICK_START.md)
2. [02_API_OVERVIEW.md](02_API_OVERVIEW.md)
3. [03_LATEST_STATE_API.md](03_LATEST_STATE_API.md)
4. [04_MULTI_QUEUE_API.md](04_MULTI_QUEUE_API.md)
5. [05_IMAGE_AND_SYNCER.md](05_IMAGE_AND_SYNCER.md)
6. [06_BUILD_AND_VERIFY.md](06_BUILD_AND_VERIFY.md)
7. [07_TROUBLESHOOTING.md](07_TROUBLESHOOTING.md)

## 結論

個人利用では、このT265 runtimeは完成扱いです。

推奨API構成は次の通りです。

| 用途 | 推奨API |
|:--|:--|
| ロボット制御 / 現在値取得 | latest state API |
| 最短起動サンプル | simple reader API |
| ログ / 解析 / 時系列処理 | callback -> multi-queue API |
| Fisheye画像本体 | image queue + owned image |
| timestamp対応付け | syncer |
| OpenCV表示 / 保存 | optional example |

基本includeはこれだけです。

```c
#include "t265.h"
```

## API完成状態

API完成度は100%扱いです。

基準ログ:

```text
t265_logs/phase7_final_20260506_173609
PHASE7_FINAL_VERIFICATION: PASS_WITH_WARNINGS
failure_reasons: none
final USB status: runtime 8087:0b37
```

`PASS_WITH_WARNINGS` は失敗ではありません。この基準ログでは、sensor drop / image copy fail / worker error は出ていません。

## 大事な方針

- core runtimeはOpenCV必須ではありません。
- OpenCVはoptionalです。
- callback内で重い処理をしません。
- callback内で画像保存しません。
- Fisheye画像を保持したい場合は `t265_owned_image` へdeep-copyします。
- T265が `03e7:2150` のboot modeなら `./ensure_t265_runtime.sh` でruntime復帰します。
