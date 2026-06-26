# 02. API Overview

## 推奨include

利用者向けの入口はこれです。

```c
#include "t265.h"
```

`t265.h` から以下がまとめてincludeされます。

- `t265_types.h`
- `t265_latest.h`
- `t265_multi_queue.h`
- `t265_image.h`
- `t265_syncer.h`

## API分類

### Stable

通常利用の第一候補です。

- device lifecycle
- reader lifecycle
- latest state API
- simple reader API
- multi-queue metadata path

### Stable with strict rules

使ってよいが、守るべきルールがあります。

- callback API
- callback -> multi-queue

ルール:

- callback内ではqueue push程度にする
- callback内でprintfしない
- callback内でsleepしない
- callback内でファイル保存しない
- callback内で画像処理しない

### Experimental but usable

実用可能ですが、ownershipやthresholdを理解して使います。

- image queue / image payload
- image view / owned image
- syncer
- blocking pop

### Utility / optional

アプリ本体APIではなく、補助ツールです。

- `ensure_t265_runtime.sh`
- `diagnose_t265_usb.sh`
- `t265_image_save_worker`
- `build_opencv_examples.sh`
- OpenCV optional example

### Internal / debug

通常アプリでは使いません。

- low-level raw endpoint probes
- historical investigation probes
- boot helper internals

## Return codeの読み方

| Code | Value | 意味 |
|:--|--:|:--|
| `T265_OK` | 0 | 成功 |
| `T265_ERR_USB` | -1 | USB / 汎用I/O失敗 |
| `T265_ERR_INVALID_STATE` | -2 | NULL引数、未初期化、状態不整合 |
| `T265_ERR_TIMEOUT` | -3 | timeout |
| `T265_ERR_QUEUE_FULL` | -4 | queue push不可 |
| `T265_ERR_QUEUE_EMPTY` | -5 | queueが空 |
| `T265_ERR_NOT_FOUND` | -6 | 対象sample未取得 |

注意:

- non-blocking popで `T265_ERR_QUEUE_EMPTY` が返るのは通常状態です。
- `t265_syncer_process()` は `1` がframeset ready、`0` がno frameset、負値がerrorです。
