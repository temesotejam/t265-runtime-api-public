# 07. Troubleshooting

## T265がruntime modeか確認する

```bash
lsusb
```

runtime mode:

```text
8087:0b37
```

boot / movidius mode:

```text
03e7:2150
```

## boot modeから戻す

```bash
./ensure_t265_runtime.sh
```

udev rule導入済みなら通常ユーザーで実行できます。

失敗する場合:

```bash
sudo ./install_t265_udev_rules.sh
```

その後、T265を再接続するか、必要ならudevをtriggerします。

## よくある失敗

### `LIBUSB_ERROR_TIMEOUT`

単発で、probeが完走しfinal USB statusがruntimeならWARN扱いにできる場合があります。

繰り返す場合は以下を確認します。

- USBケーブル
- USBポート
- T265再接続
- `./ensure_t265_runtime.sh`

### `LIBUSB_ERROR_NO_DEVICE`

T265が途中で消えた可能性があります。

確認:

```bash
lsusb
```

`03e7:2150` ならruntime復帰します。

```bash
./ensure_t265_runtime.sh
```

### `DEV_RAW_STREAMS_CONTROL status=0x0008`

device state conflict の可能性があります。

対処:

1. failing probeを単体で再実行
2. `./ensure_t265_runtime.sh`
3. USB再接続
4. 再度 `lsusb` で `8087:0b37` 確認

## callbackでやってはいけないこと

- printf
- usleep / sleep
- file保存
- PGM保存
- OpenCV処理
- 重い画像処理

callbackではqueue pushだけにします。

## image queueで見るべき値

- `image_dropped == 0`
- `image_copy_fail == 0`
- `save_request_dropped == 0`
- `save_worker_errors == 0`

`image_buffer_reused` は内部循環bufferの再利用であり、それだけではdropではありません。
