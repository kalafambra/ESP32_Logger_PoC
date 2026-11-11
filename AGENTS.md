# Firmware build variants

- BLE-free firmware with the standard ESP32 `default` partition layout is the
  project default. `py -3 scripts/compile_firmware.py` writes `build-default/`.
- Preserve the BLE implementation in `pickup_ble.cpp`, its public API, UUIDs
  and event journal. It is intentionally compiled out, not obsolete code.
- To restore BLE, read [BUILD_VARIANTS.md](BUILD_VARIANTS.md) first. Use
  `py -3 scripts/compile_firmware.py --ble` for `build-ble/` and `no_fs`.
  Changing the device partition table requires USB, not a normal OTA upload.
- Keep both variants buildable when changing shared code. Do not change GPIOs,
  credentials, sensor flags, NVS formats or saved calibrations to switch BLE.
- Compiling a variant does not deploy it. Keep hardware deployment claims
  separate from build/test results.
