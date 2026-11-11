# BLE-free firmware and restoring BLE

The default variant excludes BLE so the application fits the original ESP32
partition scheme. The complete NimBLE implementation is retained behind
`PICKUP_BLE_ENABLED` in `pickup_ble.cpp`. Wi-Fi, ArduinoOTA, the dashboard,
pickup detection, persistent event journal and sensor configuration remain
available. The current private `config.h` enables I2C sensors and disables the
microphone; switching BLE variants does not edit those choices.

| Variant | Compile command | Directory | Partition scheme | Each OTA slot |
| --- | --- | --- | --- | --- |
| Default, BLE-free | `py -3 scripts/compile_firmware.py` | `build-default/` | `default` | 1,310,720 bytes |
| Preserved BLE | `py -3 scripts/compile_firmware.py --ble` | `build-ble/` | `no_fs` | 2,031,616 bytes |

Sizes come from ESP32 core 3.3.12's `tools/partitions/default.csv` and
`no_fs.csv`. Both use two OTA application slots on the 4 MB module. The default
layout also reserves 1,441,792 bytes for a filesystem; this firmware does not
currently use it. The last BLE image before this change was 1,450,864 bytes,
which exceeded the default slot by 140,144 bytes.

## Build switch and preserved behavior

`firmware_features.h` defaults `PICKUP_BLE_ENABLED` to `0`. The Python compiler
helper explicitly sets it to `0`, or to `1` with `--ble`, and pairs the feature
with its partition scheme. `--build-dir PATH` overrides the output directory.
The header default is useful for Arduino IDE builds; changing it alone does
not override the Python helper's explicit selection.

With BLE disabled, no NimBLE headers or implementation are compiled into
`pickup_ble.cpp`. Its three functions remain as disabled stubs, so the setup,
loop and HTTP callers keep their interface. Startup logs `[ble] Disabled at
build time`; `/api/pickup/events` returns `ble_ready:false`. BLE advertising,
phone GATT reads and phone clock writes are unavailable. HTTP event JSON and
text access, NVS event storage and Wi-Fi NTP synchronization remain available.
The UUID-definition download is retained for future BLE restoration.

With `--ble`, the original service, callbacks, heap guards and advertising
recovery are compiled. It still uses **NimBLE-Arduino by h2zero 2.5.2**. Keep
using NimBLE; the older Bluedroid implementation exhausted runtime RAM on
this board. Protocol, phone setup, UUIDs and hardware checks are preserved in
[BLE_EVENTS.md](BLE_EVENTS.md).

## Install the BLE-free default layout

Compile, close any serial monitor, then perform one USB upload:

```powershell
py -3 scripts/compile_firmware.py
py -3 scripts/flash_usb.py --port COM5
```

`flash_usb.py` defaults to `build-default/`. It reads the selected build's FQBN
from `build.options.json`, accepts the project's `default` and `no_fs` layouts,
and uploads the matching bootloader, partition table and application using
Arduino's serial recipe. It checks source/header freshness before opening the
port. Build and upload helpers support `--dry-run`.

A normal ArduinoOTA upload replaces an application only. It does **not**
install a different partition table. Uploading the smaller application over
OTA to an existing `no_fs` device would leave that larger layout in place.

Do not erase all flash when switching layouts. Both tables keep NVS at
`0x9000`, size `0x5000`, so saved settings, calibration and pickup events can
remain in place. Application slot 1 moves, and filesystem contents must not be
assumed preserved. The USB recipe also resets OTA boot selection. After reboot,
check serial at 115200 and the dashboard/API before resuming OTA:

```powershell
py -3 scripts/flash_ota.py
```

## Restore BLE and the larger layout in a future chat

1. Read this guide and `BLE_EVENTS.md`; preserve the existing UUIDs, event
   format, sensor flags, wiring and private credentials.
2. Ensure NimBLE-Arduino 2.5.2 is installed. Build with `--ble`; no BLE source
   restoration or rewriting is necessary.
3. Install the `no_fs` partition table with USB on COM5. Explicitly select the
   BLE build directory so an old default artifact cannot be selected by mistake.
4. Verify serial BLE readiness and free heap, phone discovery/read/UTC write,
   HTTP event access and normal sensor acquisition.
5. Subsequent OTA uploads use that same BLE directory and installed layout.

```powershell
py -3 scripts/compile_firmware.py --ble
py -3 scripts/flash_usb.py --port COM5 --build-dir build-ble
# Only after the larger layout has been installed over USB:
py -3 scripts/flash_ota.py --build-dir build-ble
```

For Arduino IDE, set the fallback in `firmware_features.h` to `1`, select
**ESP32 Dev Module / No FS 4MB (2MB APP x2)**, then upload over USB. To return
to BLE-free, set it back to `0`, select **Default 4MB with spiffs**, and USB
upload again. Always check the compiled application fits the selected slot;
do not send a larger BLE image to a device with default-sized slots.

## Validation

BLE-free build verified on 2026-11-11 with ESP32 core 3.3.12 and the existing
private sensor configuration: 1,194,352 application bytes (91% of the default
slot), 1,194,496 bytes in the upload image, and 72,368 static RAM bytes (22%).
The image leaves 116,224 bytes of slot capacity. The ELF contains no NimBLE or
Bluetooth initialization symbols, and the generated partition table matches
`default.csv`. USB and OTA dry runs select `build-default/` correctly. The
variant-creation step did not deploy firmware; the later USB installation is
recorded below.

The preserved `--ble` variant also compiled successfully: 1,450,708 application
bytes (71% of the `no_fs` slot), 1,450,864 bytes in the upload image, and 82,452
static RAM bytes (25%). Disabling BLE saves 256,368 bytes in the upload image
and 10,084 static RAM bytes with this configuration. Runtime heap savings
have not been measured on hardware.

All 42 offline Python tests and all 12 JavaScript regression files passed.
On this PC, `node` is not on PATH; the JavaScript checks used the existing
`.venv-device-tests/Lib/site-packages/playwright/driver/node.exe` runtime.

Run `py -3 -m unittest discover -s tests -p 'test_*.py'` for offline Python
checks, including build selection, upload FQBN selection and failure handling.
Build both variants to check C++ compilation and application size. These
checks do not establish runtime behavior; flashing and the hardware checks
above are separate deployment steps.

### USB installation on 2026-11-11

The BLE-free `build-default/` firmware and standard `default` partition table
were uploaded on COM5 with `py -3 scripts/flash_usb.py --port COM5`. The
bootloader, partition table, OTA boot-selection data and application all passed
flash hash verification, followed by an RTS reset. No full-flash erase was used.
The board at `192.168.50.83` matched the flashed MAC `c8:f0:9e:f2:04:d4`.

Post-upload HTTP checks showed Wi-Fi connected, `ble_ready:false`, valid BME280
reports with five samples and MPU6050 reports with 100 samples, passing history
self-tests, restored accelerometer bias `[0.51, -0.438, 1.371]`, and ten event
records with `storage_ok:true`. Three follow-up readings advanced normally with
zero FIFO overflows and zero I2C write errors. The cumulative I2C read-error
counter increased from 15 in the initial check to 16 before the follow-up
checks, then remained 16 across all three; both sensors reported healthy at
each check. This records intermittent read errors, not an error-free soak test.
The installed layout is now `default`; future BLE restoration still needs a
USB upload of `no_fs` before larger BLE OTA images can be used.
