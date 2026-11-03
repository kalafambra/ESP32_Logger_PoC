# Pickup event log over BLE

The firmware advertises **ESP32-Pickup**. Install **NimBLE-Arduino by h2zero,
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

1. Flash this version, then scan in nRF Connect and connect to **ESP32-Pickup**.
2. Expand custom service `c7390001-7a32-4ec3-bb3b-81ae5d9a0000`.
3. Tap **Read** (down arrow) for characteristic
   `c7390002-7a32-4ec3-bb3b-81ae5d9a0000`: metadata includes count, newest ID,
   current UTC, storage health and whether a save is pending.
4. Read event characteristic `c7390100-7a32-4ec3-bb3b-81ae5d9a0000`
   for slot 0, the newest event. Slots 1 through 9 have first groups
   `c7390101` through `c7390109`, with the same remaining UUID.
   The standard description labels each slot. Select UTF-8/text display in the app.
5. Read the other populated slots as needed. Each read returns one JSON object,
   below the 512-byte GATT value limit. Long reads are fragmented by GATT; no
   notifications need to be enabled. If the phone only shows an initial fragment,
   request MTU 247 (Android) and read again/use its long-read operation.
6. Read metadata again after a multi-slot download. If `newest_id` changed, a new
   event shifted the slots; reread and use record IDs to avoid duplicates.

Example response:

```json
{"slot":0,"id":12,"started_utc":"2026-11-03T14:25:00Z","ended_utc":"2026-11-03T14:25:06Z","duration_ms":6250,"state":"complete","reason_mask":6,"reasons":["tilt","rotation"]}
```

An unused slot returns `{"slot":9,"event":null}`. Active records have a live
elapsed duration and no end timestamp. Mask bits are acceleration=1, tilt=2,
rotation=4. Slots are snapshots taken at the start of each read.

For offline UTC setup, write the current **10-digit Unix timestamp in seconds** as
UTF-8/text (not hexadecimal/binary) to `c7390003-7a32-4ec3-bb3b-81ae5d9a0000`.
Read it back for `OK` or an error, then reread metadata to verify UTC. Accepted
years are 2024–2099. This affects future timestamps, not stored event times. NTP
may subsequently correct the clock. This basic service has no pairing requirement;
nearby clients can read the log and set its clock. No credentials are exposed.
On your PC, PowerShell can produce the value to paste into nRF Connect:
`[DateTimeOffset]::UtcNow.ToUnixTimeSeconds()`.

For comparison without a BLE client, `/api/pickup/events` returns the same slots
and metadata over Wi-Fi. It is a diagnostic route, not a new dashboard widget.

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

Environmental history is allocated after the radios/routes, retaining up to
602 reports while reserving 64 KiB of free heap for HTTP, debug, OTA and BLE
connections. If memory is constrained, the device backfill is shorter (reported
in serial); browser history still retains 600 seconds. A one-report fallback
avoids an allocation-failure crash. Check runtime heap/FIFO health with BLE and
the tuning page together; the reserve does not guarantee every later allocation.

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

To upload the existing build without compiling again, close Serial Monitor and
run the following from PowerShell (adjust COM5 if needed):

```powershell
& 'D:/ArduinoIDE/Arduino IDE/resources/app/lib/backend/resources/arduino-cli.exe' upload --config-file C:/Users/przemek/.arduinoIDE/arduino-cli.yaml --fqbn esp32:esp32:esp32:PartitionScheme=no_fs --port COM5 --input-dir D:/code/ESP32_1/build-ble D:/code/ESP32_1
```
This is a USB upload, not OTA, and does not invoke compilation. The rebooting
old firmware cannot accept an OTA recovery upload.

Check serial `[ble]` and `[events]` diagnostics, metadata clock/storage health,
and connect/read/disconnect/reconnect with nRF Connect. Generate a pickup, read
its active record, set the module down, and verify that the same ID becomes
complete with the graph's confirming causes and a plausible elapsed duration.
Generate at least 11 events and confirm only the newest 10 remain. Reboot after
completion to check persistence; reboot during an alert to check `interrupted`.
Test with no internet (null timestamps or phone-set clock), repeated long reads,
and simultaneous dashboard/100 Hz motion use. Watch heap, FIFO errors and OTA
availability. These require actual hardware/phone testing after the USB flash.

References: [NimBLE migration guide](https://github.com/h2zero/NimBLE-Arduino/blob/master/docs/Migration_guide.md),
[Arduino time example](https://github.com/espressif/arduino-esp32/blob/master/libraries/ESP32/examples/Time/SimpleTime/SimpleTime.ino),
and [Nordic nRF Connect for Mobile](https://www.nordicsemi.com/Products/Development-tools/nRF-Connect-for-mobile).
