# Pickup event log over BLE

BLE is now **disabled by default**, with its implementation preserved. See
[BUILD_VARIANTS.md](BUILD_VARIANTS.md) for the BLE-free/default-partition build
and the exact steps to restore BLE and USB-install the larger `no_fs` layout.
The protocol and historical validation below describe the optional BLE variant.

The BLE variant advertises **Pickup lists**. Install **NimBLE-Arduino by h2zero,
version 2.5.2** in Arduino Library Manager. It replaces the bundled Bluedroid
BLE host to reduce runtime RAM use. No Bluetooth Serial/SPP application is needed.
The existing Wi-Fi dashboard and OTA service remain enabled.

## Event meaning and storage

- A record starts when the detector confirms pickup, after trigger persistence.
- Duration ends when the alert clears after the full stationary window. It includes
  the quiet-window waiting time, rather than measuring only time in physical motion.
- Reasons are the acceleration departure, tilt and/or filtered rotation limits
  exceeded on the confirming frame. They are not the stationary blockers.
- Ten newest events, including the active event, form a ring in RAM. Each start/end
  is saved as one versioned NVS blob from the main loop, outside FIFO processing.
  The oldest event is overwritten by the eleventh. There is no remote erase command.
- Flash failures retain the RAM journal, retry every five seconds and set
  `storage_ok:false`. Abrupt power loss before a save can lose that transition.
- A reboot marks a previously active event `interrupted`, with `duration_ms:null`:
  the firmware cannot determine when the device stopped moving while powered off.
  Completed records retain their duration and timestamps across reboots/OTA.

UTC is synchronized asynchronously with `pool.ntp.org` / `time.nist.gov` after
Wi-Fi connects. No acquisition loop waits for time synchronization. Start/end
timestamps captured before clock sync are `null`; they are never invented or
retroactively inferred. Monotonic time measures duration even if UTC changes.
UTC timestamps use ISO 8601, for example `2026-11-03T14:25:00Z`.

## Read with nRF Connect for Mobile

1. After updating, disconnect/reconnect and use **Refresh services** to clear the
   old GATT cache. The former metadata, clock and ten slot characteristics are gone.
2. Scan for **Pickup lists**. Its custom service UUID remains
   `c7390001-7a32-4ec3-bb3b-81ae5d9a0000`.
3. There is **one custom characteristic**:
   `c7390004-7a32-4ec3-bb3b-81ae5d9a0000` (READ / WRITE).
   Tap **Read** and select UTF-8/text display if necessary. No notifications are needed.
4. A single read returns a coherent snapshot of up to ten events, newest first,
   separated by newlines. Read again to refresh active duration/new events.
   If the app only shows a fragment, request MTU 247 and use its long-read support.

```text
UTC|dur|state|reasons
2026-11-03 18:20:10|6.2s|done|tilt+gyro
2026-11-03 18:10:04|?|reboot|accel
unknown|2.1s|active|gyro
```

`done` means the alert cleared after stillness; `active` means it is ongoing;
`reboot` means it was interrupted by a reboot, with unknown duration (`?`).
`unknown` means the start timestamp was captured before clock synchronization.
`accel`, `tilt`, `gyro` name the confirming acceleration departure, tilt and
filtered rotation threshold(s). Times are UTC; the first column is the event start.

Durations below 100 seconds show tenths. Longer durations use whole seconds,
then minutes (`m`), hours (`h`), days (`d`) or 365-day years (`y`) to keep fields
within five characters; displayed values are truncated. Exact milliseconds
remain available through the unchanged JSON API. An empty log says
`No pickup events.`. The header plus ten worst-case rows fit **512 bytes**,
the GATT value limit, without truncating any event. ATT handles fragmentation;
this is not a stream of notifications. Startup self-tests check the full-size list.

### Name the service and characteristic on Android

A custom GATT service transmits a UUID, not a human-readable service name.
Firmware sets the advertised device name and a characteristic description to
**Pickup lists**. nRF Connect resolves its service heading from local definitions.

- On the phone, open `http://<ESP32-IP>/pickup-uuids.json` and save the download.
- In nRF Connect, open **menu → Definitions → three-dot menu → Import**, and select
  `pickup-uuids.json`. It maps this service and its single characteristic to
  **Pickup lists** and sets the value format to **TEXT**. Reconnect/refresh services.
- Alternatively, long-press the service/characteristic and tap the edit icon to
  assign its local name. Menu wording may vary with nRF Connect version.

These are app-side labels; standard Generic Access / Generic Attribute entries
remain present and are not additional custom event characteristics.
See [Nordic's Android naming instructions](https://devzone.nordicsemi.com/f/nordic-q-a/109155/how-to-add-custom-uuids-definitions-in-nrf-connect-for-mobile/504333)
and [definition import guidance](https://devzone.nordicsemi.com/f/nordic-q-a/67038/adding-a-human-readable-description-for-custom-characteristics).

For offline UTC setup, write `TIME ` followed by the current **10-digit Unix
timestamp in seconds**, as UTF-8/text, to the same list characteristic. Bare ten
digits are also accepted. Every read still returns the event list; validation
errors/clock-setting results are logged to serial. Verify current UTC through
the HTTP metadata if needed. Accepted years are 2024–2099; only future event
timestamps change, and NTP may later correct the clock. PowerShell can produce
the value to paste: `[DateTimeOffset]::UtcNow.ToUnixTimeSeconds()`.
The basic service requires no pairing; nearby clients can read events/set UTC.

`/api/pickup/events/text` returns the exact BLE list for browser comparison.
`/api/pickup/events` retains full JSON records/metadata and memory diagnostics
for tools that need exact durations, IDs and end timestamps.

## Compile and flash

BLE increases firmware size. Use **ESP32 Dev Module → Partition Scheme →
No FS 4MB (2MB APP x2)**. This preserves two OTA slots
on the standard 4 MB module. No filesystem is used by this application.
Wi-Fi and HTTP routes are initialized before BLE. HTTP listening, NTP and OTA
start once Wi-Fi has an IP; only then is BLE attempted. With no network, BLE is
attempted after the initial 30-second Wi-Fi attempt. Failed Wi-Fi driver startup
is logged/retried without opening a socket. BLE requires 80 KiB free / 32 KiB
contiguous before initialization and is shut down if actual remaining heap is
below 48 KiB after startup. Serial prints both pre-start and post-start memory.
This replaces the premature 100,000-byte guard that could skip NimBLE before
its actual cost was measured. Initialization failures release the BLE stack.

The detector now allocates only its configured stationary window: 7,200 bytes
for the default three seconds instead of a fixed 24,000-byte ten-second buffer.
All existing duration settings remain supported. Growth is preflighted before
NVS writes; allocation failure leaves current settings/evidence unchanged.
Shrinkage frees unused space when replacement allocation succeeds. Startup
self-tests exercise ten-second growth and return to the default window.

Environmental history now uses fixed bounded storage: the most recent 30 seconds
at one-second resolution, older data in 30-second buckets across approximately
600 seconds, at most 51 points. This replaces the adaptive allocation of up to
602 one-second reports. Current HTTP buffers are sized by selected history rows;
startup/HTTP memory diagnostics still need monitoring under concurrent use.

The first upload after this partition change must be over **USB**, because a
normal ArduinoOTA upload does not replace the partition table. Select the new
partition scheme and upload in Arduino IDE (COM5 if unchanged). Do not erase
all flash: the NVS partition location is unchanged, preserving saved settings
and calibration. Afterwards, OTA updates built for this same layout can resume.
Do not send this larger binary to a device still using the old 1.2 MB slots.

CLI equivalent (using the installed IDE toolchain and configuration):

```powershell
& 'D:/ArduinoIDE/Arduino IDE/resources/app/lib/backend/resources/arduino-cli.exe' compile --config-file C:/Users/przemek/.arduinoIDE/arduino-cli.yaml --fqbn esp32:esp32:esp32:PartitionScheme=no_fs --build-path D:/code/ESP32_1/build-ble D:/code/ESP32_1
```

## Manual acceptance checks

The original Bluedroid build compiled but failed on hardware: serial showed
25,660 bytes free after BLE, followed by `wifi:create wifi task: failed to create
queue` and repeated abort/reboots. This was runtime RAM exhaustion; the static
RAM percentage did not account for radio allocations. Its journal/detector
self-tests did pass on the user's board before the failure. The replacement
uses NimBLE and guarded startup; hardware results must be checked after reflashing.
The replacement compiled with ESP32 core 3.3.12 / `PartitionScheme=no_fs`:
1,464,120 application bytes (72%) and 98,148 static RAM bytes (29%). Both main
dashboard and tuning-page tests passed. The corrected version has not yet been
uploaded or measured on hardware; check the new `[memory]` diagnostic after USB
recovery. Static RAM figures exclude runtime radio/connection allocations.
Those figures describe the first NimBLE version. The follow-up memory fix adds
adaptive detector storage and moves history allocation until after networking
and BLE. GET `/api/pickup/events`
now includes `ble_ready`, `free_heap`, `largest_free_block`, and
`history_capacity`, without subscribing to the larger debug telemetry buffer.

Follow-up validation: 1,465,108 application bytes (72%), 74,140 static RAM bytes
(22%); main/tuning browser tests passed. OTA upload to 192.168.50.83 succeeded.
After reboot, the API reported `ble_ready:true`, passing detector self-tests
(including window growth/shrink), and an armed detector. Idle checks showed
48,880 / 48,572 free bytes and 30,708 bytes in the largest block. Adaptive device
history held 215 environmental reports; the browser still retains 600 seconds.
With debug capture, its response reported 28,560 free bytes (including its
temporary response allocation); the tuning page returned HTTP 200. One transient
MPU6050 I2C status/count error occurred during checks, then recovered to valid
100-sample reports with zero FIFO overflows. Monitor if that error recurs.
BLE stack startup is verified; phone discovery, connection and GATT reads still
require nRF Connect confirmation.

Single-list update validation: firmware compiled with 1,468,696 application
bytes (72%) / 74,060 static RAM bytes (22%). Dashboard/tuning tests and Android
definition-schema tests passed. Two OTA transfers failed (timeout / host-side
connection abort); USB upload on COM5 succeeded and flash hashes verified.
After reboot, BLE was ready with 59,064 free heap bytes. All six saved events
remained present; the 247-byte text endpoint matched their newest-first JSON
order, timestamps and states. The definitions download returned both names as
`Pickup lists` and format `TEXT`; detector self-tests passed. Refreshing Android's
GATT cache, importing the names and reading the new characteristic on the phone
remain the final UI checks. A full ten-row boundary case is included in journal
startup self-tests; its worst-case formatted size is 512 bytes.

To upload the existing build without compiling again, close Serial Monitor and
run the following from PowerShell (adjust COM5 if needed):

```powershell
& 'D:/ArduinoIDE/Arduino IDE/resources/app/lib/backend/resources/arduino-cli.exe' upload --config-file C:/Users/przemek/.arduinoIDE/arduino-cli.yaml --fqbn esp32:esp32:esp32:PartitionScheme=no_fs --port COM5 --input-dir D:/code/ESP32_1/build-ble D:/code/ESP32_1
```
This is a USB upload, not OTA, and does not invoke compilation. It also provides
recovery when an OTA transfer fails or the old firmware cannot connect.

Check serial `[ble]` and `[events]` diagnostics, metadata clock/storage health,
and connect/read/disconnect/reconnect with nRF Connect. Generate a pickup, read
its active row, set the module down, and verify that it becomes `done`, with
the graph's confirming causes and a plausible elapsed duration. The HTTP JSON
record ID can be used to track it unambiguously if several events occur.
Generate at least 11 events and confirm only the newest 10 remain. Reboot after
completion to check persistence; reboot during an alert to check `interrupted`.
Test with no internet (null timestamps or phone-set clock), repeated long reads,
and simultaneous dashboard/100 Hz motion use. Watch heap, FIFO errors and OTA
availability. These require actual hardware/phone testing after the USB flash.

References: [NimBLE migration guide](https://github.com/h2zero/NimBLE-Arduino/blob/master/docs/Migration_guide.md),
[Arduino time example](https://github.com/espressif/arduino-esp32/blob/master/libraries/ESP32/examples/Time/SimpleTime/SimpleTime.ino),
and [Nordic nRF Connect for Mobile](https://www.nordicsemi.com/Products/Development-tools/nRF-Connect-for-mobile).
