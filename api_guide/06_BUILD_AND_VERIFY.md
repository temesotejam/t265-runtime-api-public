# 06. Build and Verify

## Build

core build:

```bash
make all
```

examplesのみ:

```bash
make examples
```

toolsのみ:

```bash
make tools
```

OpenCV optional:

```bash
make opencv
```

clean:

```bash
make clean
```

## 最小確認コマンド

```bash
./ensure_t265_runtime.sh
make all
./example_latest_state
./example_simple_reader
./example_multi_queue
```

## final verification

```bash
./run_t265_phase7_final_verification.sh --full
```

良い結果:

```text
PHASE7_FINAL_VERIFICATION: PASS
```

または:

```text
PHASE7_FINAL_VERIFICATION: PASS_WITH_WARNINGS
failure_reasons: none
final USB status: runtime
```

## 基準ログ

API完成基準ログ:

```text
t265_logs/phase7_final_20260506_173609
PHASE7_FINAL_VERIFICATION: PASS_WITH_WARNINGS
failure_reasons: none
final USB status: runtime 8087:0b37
```

## OpenCV

OpenCVは必須ではありません。

使う場合だけ:

```bash
make opencv
./example_opencv_fisheye_view --save-one opencv_fisheye.png
```

core runtimeは `libusb` + `pthread` で動きます。
