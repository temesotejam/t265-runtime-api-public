# T265 API Complete Set

This folder is the completed portable T265 runtime API set.

It supports:

- One or more Intel RealSense T265 devices.
- Runtime device enumeration.
- Opening by index, USB bus/address, serial number, or role name.
- Bootloader mode recovery from `03e7:2150` to runtime mode `8087:0b37`.
- Two-device latest-state examples.
- Two-device role-based long-run verification.

## Requirements

- Linux
- `gcc`
- `make`
- `objcopy`
- `libusb-1.0.so.0`
- `pthread`

The core API does not require librealsense.

## Folder Contents

| Path | Purpose |
|:--|:--|
| `include/` | Public API headers |
| `src/` | Core libusb runtime implementation |
| `examples/` | Public API examples |
| `tools/` | Boot helper, long-run test, and verification tools |
| `scripts/fetch_t265_firmware.sh` | Downloads the T265 runtime firmware locally |
| `Makefile` | Builds examples and selected tools |
| `ensure_t265_runtime.sh` | Converts bootloader devices to runtime mode |
| `t265_roles.example.conf` | Example role-to-serial mapping |
| `99-t265-runtime.rules` | Optional udev rule |
| `docs/`, `api_guide/` | Extended documentation |

## Quick Start

This repository does not redistribute the Intel RealSense T265 firmware
binary. Before building `t265_boot_libusb`, fetch the firmware locally:

```sh
chmod +x scripts/fetch_t265_firmware.sh
./scripts/fetch_t265_firmware.sh
```

This creates:

```text
tools/t265_fw_target.bin
```

Expected SHA1:

```text
c3940ccbb0e3045603e4aceaa2d73427f96e24bc
```

See [docs/T265_FIRMWARE_SETUP.md](docs/T265_FIRMWARE_SETUP.md) for manual
download commands and verification details.

Build:

```sh
make all
```

## Minimal One-Device Demo

For the smallest end-to-end check, fetch the firmware, build the boot helper
and the demo, move the T265 to runtime mode, then run:

```sh
chmod +x scripts/fetch_t265_firmware.sh
./scripts/fetch_t265_firmware.sh
make t265_boot_libusb demo_minimal_pose
./ensure_t265_runtime.sh
./demo_minimal_pose
```

To run for a specific number of seconds:

```sh
./demo_minimal_pose 10
```

`demo_minimal_pose` opens one T265, starts pose / gyro / accel / fisheye
metadata streams, prints the latest pose at about 10 Hz, and saves no images or
CSV files. The firmware binary is not included in this repository; the fetch
script downloads it locally as `tools/t265_fw_target.bin`.

Basic operation is OK when the demo ends with:

```text
DEMO_MINIMAL_POSE_RESULT: PASS
```

Check USB state:

```sh
lsusb
```

If T265 devices are visible as bootloader devices:

```text
03e7:2150 Intel Myriad VPU
```

run:

```sh
./ensure_t265_runtime.sh
```

Then verify runtime devices:

```sh
./example_list_devices
```

Expected with two T265 devices:

```text
runtime devices: 2
[0] serial=...
[1] serial=...
```

## Role Binding

Copy the example role file and edit the serial numbers for your devices:

```sh
cp t265_roles.example.conf t265_roles.conf
```

Edit `t265_roles.conf`:

```text
left=YOUR_LEFT_T265_SERIAL
right=YOUR_RIGHT_T265_SERIAL
```

Then verify role-based open:

```sh
./example_open_by_role t265_roles.conf left
./example_open_by_role t265_roles.conf right
```

## Two-Device Verification

Short smoke test:

```sh
./example_multi_latest_state
```

Long-run test:

```sh
./t265_multi_latest_long_run --duration-sec 300 --role-file t265_roles.conf --role0 left --role1 right
```

Expected result:

```text
T265_MULTI_LATEST_LONG_RUN_RESULT: PASS
```

## Public API Highlights

```c
t265_context *ctx = t265_context_create();
t265_context_refresh_devices(ctx);

t265_runtime *dev0 = t265_context_open_device(ctx, 0);
t265_runtime *dev1 = t265_context_open_device_by_serial(ctx, "YOUR_T265_SERIAL");

t265_context_load_roles(ctx, "t265_roles.conf");
t265_runtime *left = t265_context_open_device_by_role(ctx, "left");
```

Existing one-device code still works:

```c
t265_runtime *dev = t265_open();
```

## Completion Criteria Already Verified

This set was verified with:

- `make all`
- two runtime T265 devices
- serial enumeration
- role-based open for `left` and `right`
- two-device latest-state example
- 300-second two-device role-based long-run test

The 300-second long-run completed with:

```text
T265_MULTI_LATEST_LONG_RUN_RESULT: PASS
```
