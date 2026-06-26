# Public Release Checklist

Do not make this repository public until these items are checked.

## Remove local or redistributability-sensitive files

```sh
rm -f tools/t265_fw_target.bin
rm -f tools/t265_fw_target.o
rm -f t265_roles.conf
```

Then create a public-safe example role file:

```sh
cat > t265_roles.example.conf <<'CONF'
# Example T265 role binding file
# Format:
#   role=serial

left=YOUR_LEFT_T265_SERIAL
right=YOUR_RIGHT_T265_SERIAL
CONF
```

## Remove generated binaries

```sh
make clean
rm -f example_*
rm -f t265_boot_libusb
rm -f t265_multi_latest_long_run
rm -f t265_runtime_*
rm -f t265_image_save_worker
rm -f example_opencv_fisheye_view
```

## Add public license

Recommended for public release:

- `LICENSE`: Apache License 2.0
- `NOTICE`: mention this is an independent, unofficial project and that firmware binaries are not included.

## README wording for public release

Add clear notes:

- This project is unofficial and independent.
- It is not affiliated with Intel or RealSense.
- Firmware binaries are not included.
- Users are responsible for obtaining firmware from sources they are permitted to use.
