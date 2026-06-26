# 01. Quick Start

## 1. T265をruntime modeへ戻す

通常は以下で確認します。

```bash
lsusb
```

runtime device が見えていればOKです。

```text
8087:0b37
```

boot mode の場合は次を実行します。

```bash
./ensure_t265_runtime.sh
```

udev rule導入済みなら、通常ユーザーで成功する想定です。失敗する場合だけsudoやudev ruleを確認します。

## 2. build

```bash
make all
```

examplesだけなら:

```bash
make examples
```

OpenCV optional exampleも使う場合:

```bash
make opencv
```

## 3. 最小確認

```bash
./example_latest_state
./example_simple_reader
./example_multi_queue
```

## 4. 総合確認

```bash
./run_t265_phase7_final_verification.sh --full
```

期待:

```text
PHASE7_FINAL_VERIFICATION: PASS
```

または:

```text
PHASE7_FINAL_VERIFICATION: PASS_WITH_WARNINGS
failure_reasons: none
final USB status: runtime
```

## 5. 自分のアプリで使う最小方針

制御用途なら latest state API を使います。

```c
#include "t265.h"
```

ログや解析用途なら multi-queue を使います。

画像を保存したい場合は、callbackでは保存せず、queue pop後またはworker側で保存します。
