# T265 Firmware Setup

This repository does not redistribute Intel RealSense T265 firmware binaries.
Users must obtain the runtime firmware themselves before building the boot
loader helper.

The firmware file used by this project is:

```text
target-0.2.0.951.mvcmd
```

It is downloaded from:

```text
https://librealsense.intel.com/Releases/TM2/FW/target/0.2.0.951/target-0.2.0.951.mvcmd
```

For compatibility with the existing Makefile, save it as:

```text
tools/t265_fw_target.bin
```

## Automatic Setup

Run:

```sh
chmod +x scripts/fetch_t265_firmware.sh
./scripts/fetch_t265_firmware.sh
```

The script downloads the firmware, writes `tools/t265_fw_target.bin`, and
checks this SHA1:

```text
c3940ccbb0e3045603e4aceaa2d73427f96e24bc
```

If the SHA1 does not match, do not use the file.

## Manual Setup

Using `curl`:

```sh
mkdir -p tools
curl -L \
  https://librealsense.intel.com/Releases/TM2/FW/target/0.2.0.951/target-0.2.0.951.mvcmd \
  -o tools/t265_fw_target.bin
sha1sum tools/t265_fw_target.bin
```

Using `wget`:

```sh
mkdir -p tools
wget \
  https://librealsense.intel.com/Releases/TM2/FW/target/0.2.0.951/target-0.2.0.951.mvcmd \
  -O tools/t265_fw_target.bin
sha1sum tools/t265_fw_target.bin
```

Expected SHA1:

```text
c3940ccbb0e3045603e4aceaa2d73427f96e24bc
```

Do not commit `tools/t265_fw_target.bin`, `.mvcmd` files, or generated object
files. Firmware binaries are intentionally ignored by `.gitignore`.
