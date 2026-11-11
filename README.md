# ESP32 environmental and motion monitor

ESP32-WROOM Arduino firmware with an offline dark slate dashboard, BME280,
MPU6050, INMP441 audio spectrum, serial diagnostics and password-protected ArduinoOTA.

The default build is **BLE-free** and uses the original ESP32 `default`
partition scheme. BLE code is preserved and can be enabled with `--ble`.
See [BUILD_VARIANTS.md](BUILD_VARIANTS.md) for both variants, USB repartitioning
and the restoration guide for future changes.

## Current acquisition and publication rates

| Sensor | Acquisition | Serial/API/graph publication |
| --- | --- | --- |
| BME280 | 5 Hz (every 200 ms) | 1 Hz: mean/min/max of exactly five readings |
| MPU6050 | 100 Hz, interrupt + FIFO | 1 Hz: mean XYZ, plus min/max in the API |
| MPU6050 chip temperature | 1 Hz, direct temperature-register read | 1 Hz, on the shared temperature graph |
| INMP441 microphone | 16 kHz, hardware I2S + DMA | About 1 Hz: averaged 256-point spectrum and band levels |

Dashboard sensor endpoints are polled once per second, with no overlapping
requests within a polling stream. Motion includes gravity. All graphs retain
up to 600 seconds in the browser, expanding to fill their width when history
is shorter. Environmental extrema remain at 30% opacity. Motion plots mean
X/Y/Z as three colored lines; raw 100 Hz frames are not transmitted.

The temperature graph overlays blue BME280 ambient readings and amber MPU6050
chip readings on one Celsius axis, with separately labeled current values.
MPU temperature is read from signed TEMP_OUT_H/L (0x41/0x42), using
`raw / 340 + 36.53` from the [manufacturer register map](https://invensense.tdk.com/wp-content/uploads/2015/02/MPU-6000-Register-Map.pdf).
It measures chip temperature, so it may differ from the BME280 ambient reading.
The extra read does not enable temperature in the FIFO or change its 12-byte layout.
Both temperature series share the 30-second fine / older 30-second average
history. Missing MPU temperatures make gaps independently of BME status;
partial temperature failure within a coarse bucket makes that sensor's bucket
unavailable. Dew point continues using the BME280 reading.
Current MPU temperature and its independent health/error are available as
`motion.temperature_c`, `motion.temperature_ok`, `motion.temperature_error`;
history rows include `mpu_temperature_c`. Serial motion reports include chip T.

## Files

All firmware sources stay at the sketch root so Arduino compiles the `.cpp` files
automatically. Headers describe interfaces and data; implementations own mutable
state in their `.cpp` files. Tiny arithmetic helpers, constants and the history
visitor templates remain in headers.

| Files | Responsibility |
| --- | --- |
| `ESP32_1.ino` | Hardware startup and cooperative service order |
| `environment.h/.cpp` | BME280 forced conversions, five-sample reports, timing counters and environmental history ownership |
| `environment_history.h/.cpp`, `aggregation.h` | Bounded one-second / 30-second history and channel statistics |
| `motion.h/.cpp` | MPU6050 registers, ISR/FIFO, calibration, temperature, reports and diagnostic snapshots |
| `microphone.h/.cpp` | INMP441 I2S/DMA capture, borrowed audio blocks and OTA pause/resume |
| `spectrum_analysis.h/.cpp` | Hardware-independent Hann FFT and digital power normalization |
| `audio_config.h`, `microphone_spectrum.h/.cpp` | Audio settings, frame feeding, power averaging and published spectrum |
| `pickup_detector.h/.cpp` | Hardware-independent pickup state machine and thresholds |
| `pickup_service.h/.cpp` | Detector integration, settings validation/persistence, debug subscription and exclusive LED timer |
| `pickup_debug.h/.cpp`, `pickup_events.h/.cpp` | Leased debug history and persistent event journal |
| `pickup_ble.h/.cpp` | Optional BLE service and callbacks using locked event snapshots |
| `network_service.h/.cpp` | Wi-Fi callbacks/retries, time synchronization and deferred service startup |
| `ota_service.h/.cpp` | OTA lifecycle and acquisition pause/recovery |
| `http_api.h/.cpp` | Sole WebServer, all routes and response handling, plus the one-second idle-client deadline |
| `web_assets.h/.cpp` | Flash-resident dashboard, tuning page and downloadable BLE UUID definitions |
| `self_tests.h/.cpp` | Existing startup history/detector/journal checks, reusing live buffers |
| `config.h`, `config.example.h` | Private credentials/GPIO settings and shareable example |

The sketch initializes the shared Wire bus once. Wi-Fi reserves memory before
I2S and optional BLE. Each loop drains audio before sensor work and after HTTP;
it closes an environmental report before starting the next conversion. Keep
these orders when adding services. OTA callbacks run through the loop service;
the motion and LED ISRs and Wi-Fi/BLE callbacks have their own documented limits.
Sensor reports and history are read-only views for HTTP; callers must consume
them before the next service call. The event journal uses locked copies for
concurrent BLE reads. NVS record formats and endpoint URLs are unchanged.

Use `network_service.h`, since `network.h` would shadow the ESP32 library's
`Network.h` on Windows. After renaming source files, clean the Arduino build
directory or use `arduino-cli compile --clean` to remove stale copied headers.

`fox.png` and `fox_image.h` remain archived, unused assets. The scripts
`embed_fox.py` and `quantize_fox.py` maintain those assets; `upload_ota.py` reads
the private configuration for authenticated uploads.

Run the offline JavaScript regressions from the project root with Node.js:

```powershell
Get-ChildItem tests -Filter '*.test.cjs' | ForEach-Object { node $_.FullName; if ($LASTEXITCODE -ne 0) { throw $_.Name } }
```

The acquisition, DSP and DMA tests read the implementation files and translate
selected C++ code into deterministic JavaScript fixtures. They complement a full
Arduino compile; they do not execute the C++ binary or validate physical hardware.
Offline Python test-suite checks use
`python -m unittest discover -s tests -p 'test_*.py'`. The mocked browser check is
`python tests/spectrum_browser.py`; live board checks are described separately in
[tests/DEVICE_TESTS.md](tests/DEVICE_TESTS.md).

## Wiring

Power down before changing wiring. Both sensors share one I2C bus.

| Signal | ESP32 |
| --- | --- |
| BME280 SDA and MPU6050 SDA | GPIO21 / D21 |
| BME280 SCL and MPU6050 SCL | GPIO22 / D22 |
| Both GND | GND |
| Breakout VCC | 3V3 if supported by the particular breakout |
| MPU6050 AD0 | GND, selecting 0x68 |
| MPU6050 INT | GPIO17 / D17 |
| User LED | GPIO2, active HIGH by default |
| INMP441 SCK (BCLK) | GPIO18 |
| INMP441 WS (LRCLK) | GPIO19 |
| INMP441 SD (data into ESP32) | GPIO23 |
| INMP441 VDD / GND | 3V3 / GND |
| INMP441 L/R | GND (left channel) |

Leave MPU XDA/XCL unused. INT is active-high, push-pull and latched until status
is read. Do not leave AD0 floating. Use 3.3 V I2C logic and pull-ups, even if a
breakout regulator accepts a higher supply. Check the exact breakout schematic.
BME addresses 0x76/0x77 do not conflict with MPU address 0x68. Some carrier
boards have another LED pin/polarity or only a power LED; configure accordingly.

The bus runs at **100 kHz**. This is sufficient for the reduced acquisition rate
and gives more wiring margin than the former 400 kHz setting. The LED is reserved exclusively for pickup detection; acquisition no longer drives it.

### INMP441 capture

The current **microphone build** sets `I2C_SENSORS_ENABLED = false` in config.h
and config.example.h. It never starts Wire, initializes BME280/MPU6050, or attaches
the MPU interrupt. Low-level register helpers also reject calls without touching
Wire, including calls from diagnostics and OTA recovery. Environmental/motion
APIs explicitly report disabled sensors; the dashboard stops their periodic
polling once the mode is received and continues polling audio. Wi-Fi, OTA, BLE
event access and all GPIO assignments remain available. Motion-based pickup
detection is unavailable while its sensor is disabled.

Set the flag to `true` and rebuild to restore the combined sensor firmware.
Do not change wiring or GPIO assignments to switch build modes.

The microphone uses the ESP32 hardware I2S0 peripheral as master/receiver,
independently of Wire and the MPU interrupt. The INMP441 is the clock slave
and audio transmitter. `MICROPHONE_BCLK_PIN`, `MICROPHONE_WS_PIN` and
`MICROPHONE_DATA_PIN` are defined in both config.h and config.example.h.
When upgrading an existing private config.h, add those constants as 18, 19 and 23.
Existing sensor, interrupt and LED pins keep their assignments.

Capture uses the ESP32 core's standard I2S driver (Arduino-ESP32 3.x), with
16 kHz Philips I2S, 32-bit slots and only the left slot delivered to the application.
Each stereo clock frame has 64 bit clocks (1.024 MHz BCLK, 16 kHz WS);
the microphone's signed 24-bit sample occupies the upper 24 bits of its slot.
Six 256-frame DMA buffers provide about 96 ms of buffering using 6 KiB of
sample storage, plus driver overhead. No extra microphone library is needed.

`serviceMicrophone()` makes one nonblocking read of at most 256 samples and
converts the captured words to signed 24-bit values in int32_t storage.
`serviceMicrophoneAudio()` drains up to six queued blocks before sensor work
and again after HTTP handling. This lets capture catch up after a slow loop
visit while keeping the work bounded; each block enters analysis immediately.
For future processing, consume the returned `AudioBlock.samples[0..count)`
immediately after `serviceMicrophone()`; the next call replaces the borrowed block.
`microphoneStatus()` returns delivered sample and overflow counters, readiness,
pause state and the latest driver result. A timeout
is normal when no complete DMA buffer is available. Initialization is logged
once, and failure leaves other application services running.

DMA is paused during OTA and resumed after a failed
update. Long loop stalls beyond the DMA capacity can lose audio; this is not a
lossless recorder. Driver initialization cannot verify that the microphone is
physically connected; confirm clocks and sample response on the wired device.

### Live spectrum and box comparison

Configure audio globally in `audio_config.h`, then compile and upload:

```cpp
constexpr unsigned AUDIO_FFT_SIZE = 256;
constexpr uint32_t AUDIO_SAMPLE_RATE_HZ = 16000;
constexpr uint32_t AUDIO_SPECTRUM_AVERAGE_MS = 1000;
```

Capture clocks, FFT frequency conversion, API metadata, dashboard frequency
axis and band labels follow these settings. Compile-time checks require a
power-of-two FFT of 128–2048 samples for this memory layout, a sample rate of
8–48 kHz, and an averaging interval that fits at least one full frame.
The configured rate range is a conservative subset of the timing limits in the
[INMP441 datasheet](https://product.tdk.com/system/files/dam/doc/product/sw_piezo/mic/mems-mic/data_sheet/inmp441.pdf).
At the defaults there are about 62–63 FFT frames per second, each 16 ms.
The API's `frames` and `averaged_audio_ms` report the actual amount averaged;
DMA gaps reduce it. `average_target_ms` reports the requested averaging interval.

Each complete frame is DC-removed, Hann-windowed and transformed, then its
**linear bin powers** are averaged across the interval before conversion to dB.
Averaging raw sample windows before a single FFT is coherent waveform averaging:
opposite-phase sounds cancel, so it is unsuitable for ambient-noise/box tests.
Power averaging smooths variation without requiring phase alignment; it does
not increase frequency resolution, which remains sample rate divided by FFT size.

The dashboard's Microphone spectrum card plots 0–8 kHz on a fixed -120 to 0
dBFS scale. The firmware removes each frame's DC mean, applies a periodic Hann
window, and computes a 256-point radix-2 FFT (16 ms frames, 62.5 Hz bin spacing).
Nonoverlapping frames are averaged in **linear power**, then published about
once per second at `/api/microphone`. The spectrum uses one-sided RMS power per
bin, normalized by Hann window energy, with a -120 dBFS floor. This is not
power per Hz, calibrated dB SPL, or an amplitude-corrected peak-tone plot.
A full-scale sine has approximately -3.01 dBFS overall RMS; its plotted energy
spreads across neighboring bins. Broadband RMS is computed separately from
the unwindowed, DC-removed samples.

The API includes `bins_dbfs` (129 bins at the defaults, including DC/Nyquist), `rms_dbfs`,
`peak_hz` (strongest bin above 80 Hz, not an interpolated pitch estimate),
`bands_dbfs` (80–500, 500–2000, 2000–8000 Hz), FFT `frames`, `age_ms`,
`clipped_samples` and cumulative `dma_overflows`. Band boundaries use bin
centers. Summed band powers and overall RMS have different bandwidths.
FFT size/bin spacing in the API are derived from `SpectrumAnalysis::SIZE` and
the sample rate. The dashboard validates that metadata, displays the actual
resolution, and clears a saved reference if the FFT configuration changes.
Known DMA gaps discard the partial FFT frame; already completed frames remain
in the average. Clipping and gaps are visible in the card. No raw audio is sent
or stored. FFT storage is fixed; no additional audio task or library is used.
The Hann table stores only its symmetric half and power output reuses FFT
scratch storage, preserving heap for the existing radio services.
Browser polling has independent timeout/error handling and clears stale live
data after 2.5 seconds. A failed request preserves the last reading only while
it remains fresh; retries use a one-second cadence that includes request time.
Periodic environmental, motion and spectrum requests share a browser-side queue,
including body reads, to avoid competing for the ESP32's single HTTP handler.
Request timeouts start when each queued request actually begins. Wi-Fi modem
sleep is disabled to favor dashboard response latency over power savings.
Accepted HTTP connections that supply no request bytes are closed after one
second instead of occupying the single-client server for its five-second default.
This does not shorten active body handling or ArduinoOTA transfers.
`audio_samples` and `dma_capacity_ms` expose capture progress and buffer capacity.
OTA invalidates the interrupted analysis window.
The stale limit follows the averaging interval (twice its duration plus 500 ms;
2.5 seconds with the defaults).
Five-second `[audio]` serial logs include sequence/age, FFT frame count, total
samples, DMA overflows, Wi-Fi RSSI and heap, so capture can be checked even when
network delivery fails.

Serial diagnosis of the spectrum dropouts also captured repeated CPU panics
inside the installed ESP32 I2C driver's combined transmit/receive operation
called by MPU6050 FIFO reads. `motionRead()` now completes register selection
with STOP before starting the read, using the MPU's retained register pointer.
NACKs and short replies still invalidate the motion report. The shared I2C
pins, 100 kHz clock, interrupt wiring and FIFO format are unchanged. Test the
transaction/error handling with `node tests/motion-read.test.cjs`.

For an initial exposed/boxed comparison:

1. Play steady broadband noise from a fixed external sound source. Keep its
   volume, distance and orientation unchanged throughout the comparison.
2. Leave the microphone exposed and wait for stable spectrum readings. Click
   **Save exposed reference** to freeze the latest one-second spectrum in this tab.
3. Place the microphone in the box and let the readings settle. Blue is live;
   amber is the exposed reference. Compare overall and low/mid/high band changes
   in dB; negative changes mean less received sound in that band.
4. Repeat open/closed trials in the same setup. The reference is cleared by
   reloading the tab or selecting **Clear reference**.

This is a measurement tool, not an automatic covered/uncovered detector.
Room noise, source movement and the box's resonances also change the spectrum.
Silence alone cannot distinguish an enclosed microphone from a quiet room or
missing microphone data. Thresholds need representative repeated measurements
before they can support a coverage decision.

DSP checks: `node tests/spectrum.test.cjs`; DMA scheduling checks:
`node tests/audio-drain.test.cjs`; dashboard/reference checks:
`node tests/dashboard.test.cjs`. The DSP test translates C++ declarations and
executes the firmware's FFT loops with synthetic signals; device timing and
physical audio response still require hardware validation.
With the device-test Python environment and Google Chrome installed, run
`python tests/spectrum_browser.py` for a mocked desktop/mobile browser check;
screenshots are written to ignored `test-results/spectrum-*.png` files.

## Arduino IDE setup and initial USB upload

1. Open ESP32_1.ino; keep the containing folder named ESP32_1.
2. Install esp32 by Espressif Systems in Boards Manager. Additional URL:
   https://espressif.github.io/arduino-esp32/package_esp32_index.json
3. Install Adafruit BME280 Library and its BusIO/Unified Sensor dependencies.
   Install **NimBLE-Arduino by h2zero, version 2.5.2** in Library Manager for BLE.
   MPU access uses Wire directly and needs no additional library.
4. Set Wi-Fi credentials, DEFAULT_ALTITUDE_M and OTA_PASSWORD in config.h.
   Wi-Fi must be 2.4 GHz. OTA requires at least 12 password characters; empty
   disables OTA. Do not share configuration or compiled firmware containing secrets.
5. Select ESP32 Dev Module and **Default 4MB with spiffs** for the BLE-free build.
   Optional BLE requires **No FS 4MB (2MB APP x2)**; see BUILD_VARIANTS.md.
   First install a changed partition layout by USB; both layouts support OTA.
6. Verify, select COM5 (or your current port), and Upload. Close Serial Monitor
   first. If Connecting stalls, hold BOOT until writing starts, then release.
7. Open serial at 115200 baud, reset, and use the printed dashboard http:// IP.
   Computer and ESP32 must be on the same LAN; guest isolation can block access.
   Refresh the browser after firmware updates.

No internet connection is required for the dashboard. HTTP is unauthenticated;
use a trusted LAN without port forwarding. Wi-Fi retries cancel the old attempt
after 30 seconds and restart 250 ms later using a timer. Check serial reason
codes and DHCP progress when troubleshooting; credentials are not printed.

## ArduinoOTA

After the first USB installation of OTA-enabled firmware, wait for the one-time
`[ota] Ready: esp32-bme280 at <IP>` message. In Arduino IDE, select the network
port under Tools > Port, keep ESP32 Dev Module selected, Upload and enter the
OTA password. USB may still provide power during a wireless test.

If changing OTA_PASSWORD, authenticate the update using the OLD password; the
new one takes effect after reboot. Keep OTA support and working Wi-Fi in future
firmware. USB remains the recovery method if the board cannot connect or boot.
Use an OTA-capable partition scheme; No OTA/Huge APP schemes are unsuitable.
During transfer, sampling/HTTP pause and MPU FIFO generation stops. The independent pickup LED timer continues an already-active alert. A failed transfer resets the FIFO and discards interrupted windows.
A successful update reboots and clears device RAM graph history. The pickup
event journal survives in NVS; an unfinished event is marked interrupted.

If discovery fails, check the IP, same-LAN access, VPN/client isolation and
private-network firewall permissions. mDNS uses UDP 5353; OTA listens on UDP
3232 and connects back to the uploader TCP listener. The helper uses local TCP
32320 and does not put the password in command arguments or logs.

Current default build directory: build-default/. Optional BLE uses build-ble/.
Earlier builds used build-fox/ and build/; all are ignored by Git.
Compile the BLE-free variant with an available Arduino CLI:

```text
arduino-cli compile --fqbn esp32:esp32:esp32:PartitionScheme=default --build-property compiler.cpp.extra_flags=-DPICKUP_BLE_ENABLED=0 --build-path ./build-default .
```

Alternatively, run these standard-library Python helpers from the project root:

```powershell
python scripts/compile_firmware.py
python scripts/flash_ota.py
```

For USB flashing on COM5, use `python scripts/flash_usb.py` instead of the OTA
command. Close any serial monitor using that port first. The USB script uses
Arduino's ESP32 upload recipe for the bootloader, partition table and application,
with the layout recorded by compilation (`default` or `no_fs`). It checks build freshness and required
images before opening the port. Override the port with `--port COM6`; it also
supports `--verbose`, `--dry-run`, `--cli`, `--config` and `--build-dir`.

The compiler script builds the current saved sources and `config.h` into
`build-default/` with the OTA-capable `default` layout and BLE compiled out.
Add `--ble` to enable BLE with `no_fs` in `build-ble/`; select that directory
explicitly with `--build-dir build-ble` when flashing. See BUILD_VARIANTS.md
before switching the device partition layout. It detects Arduino CLI on PATH
or in common Arduino IDE locations (including this PC's D: installation), and
uses `~/.arduinoIDE/arduino-cli.yaml` when present. Override detection with
`--cli PATH` (or `ARDUINO_CLI`) and `--config PATH`. Add `--clean` after source
renames or when cached build files cause problems. Use
`python scripts/compile_firmware.py --verbose` (or `-v`) to show detailed compiler
commands and build output.

The flash script defaults to the current device at `192.168.50.83`; use
`--ip ADDRESS` if DHCP changes its address. It locates `espota.py` from the build's
platform metadata or the installed ESP32 core; `--uploader PATH` overrides this.
It delegates authentication to `upload_ota.py`, so the password from `config.h`
is never placed in command arguments. It refuses missing application images
or images older than the sketch sources/configuration: compile first.
Both scripts accept `--build-dir PATH` and `--dry-run`. Flashing does not compile
automatically. Run them with the same Python interpreter; no pip packages are
required. The existing `.venv-device-tests/Scripts/python.exe` also works.

To enable I2C sensors, set `I2C_SENSORS_ENABLED = true` in `config.h`, save, then
compile and flash with the commands above. This enables BME280, MPU6050 and
pickup detection alongside the microphone. Restore the shared SDA/SCL wiring
and MPU interrupt connection using the configured pins (defaults GPIO21,
GPIO22 and GPIO17). BME280 probes 0x76/0x77; MPU6050 expects 0x68. Existing saved
calibration is restored; enabling the flag does not automatically recalibrate.
Set the flag back to `false` and rebuild for microphone-only operation.

For a precompiled wireless application image, scripts/upload_ota.py accepts
--ip, --firmware and --uploader (the installed Espressif espota.py). Do not OTA
upload a merged flash image or bootloader; use the sketch application .bin.

## Timing, buffering and errors

BME forced x1 conversions are triggered every 200 ms, with completion checked
after at least 15 ms and timeout at 80 ms. Each one-second window must contain
exactly five valid readings; missing readings or bus errors produce a null/error
report instead of a partial average. Statistics are calculated before rounding.
Each serial BME report includes current Wi-Fi status and IP, including errors.

MPU uses DLPF=3 (44 Hz acceleration bandwidth) and sample divider=9 on the
filtered 1 kHz clock, producing 100 Hz. Range is +/-2 g (16384 counts/g).
The ISR only sets a flag and counts interrupt edges. I2C/aggregation run in the
main loop, in batches up to 10 twelve-byte accel+gyro frames. A 5 ms safety poll handles lost
notifications; missing INT edges produce a warning. IRQ edges may coalesce and
must not be mistaken for the exact raw sample count.

The 1024-byte FIFO holds about 85 accel+gyro frames, now about **0.85 seconds** of buffering.
The one-second motion report typically contains about 100 samples; counts vary
with sensor clock tolerance and scheduling. FIFO frames are grouped by when
they are drained, so backlog can add latency. Means smooth/cancel fast vibration;
per-axis minima/maxima remain available in JSON, not as a raw waveform stream.
Overflow, failed I2C reads, empty windows or badly delayed publication mark the
report invalid and reset framing when necessary. Stale limits are 2500 ms for
both latest-report APIs. No NaN values are serialized.

Acquisition has no deliberate blocking delay or conversion wait loop. I2C,
serial and HTTP are synchronous, so rates remain best effort. Wire timeout is
20 ms. Inspect actual counts/errors under browser load rather than assuming
error-free timing. Sensor initialization failure requires repairing wiring and
resetting; transient acquisition failures attempt recovery.

BME history keeps only the last 30 seconds of one-second reports. Older data
uses time-aligned 30-second buckets across approximately ten minutes, with at
most 51 graph points (including a partial older bucket). Means are weighted by
source reading counts; minima/maxima retain the extrema across the bucket. Any
invalid constituent report makes its bucket unavailable. Browser updates replace
summarized fine points instead of retaining duplicate old data. The current
partial bucket updates incrementally until complete. Motion history holds 128 reports (~128 s)
and sends at most 20 per request. Incremental backfill repairs missed polls;
sequences missing beyond the retained buffers produce graph gaps. Browser
history is time-bounded to ten minutes. Reload can restore only device history.

Environmental history metadata includes `first_sequence`, `sequence`, `revision`,
`resolution_ms`, `report_count`, and `sample_count`. The `since` cursor is a live
report revision: promotions resend changed older buckets, so a browser replaces
their earlier versions and removes the fine reports they summarize. Buckets are
aligned to 30-second device-uptime windows and plotted at their source-time
midpoint. Startup/edge buckets can be partial; sensor-error buckets remain gaps.
The fixed history uses 3,688 bytes including the additional chip-temperature series. HTTP response reservation follows the number
of selected rows, replacing the previous unconditional 16,000-byte reservation.

The archived fox image uses a 4-bit indexed palette (16 entries, including transparency):
4,304 bytes versus 48,676 previously, with unchanged 255 x 328 dimensions and
unchanged per-pixel alpha. `scripts/quantize_fox.py` uses Pillow 13+ and preserves
pixel-art edges without dithering; `--from-git` reads the committed image as its
input. The generated `fox_image.h` matches `fox.png` (the script regenerates it).
The dashboard now uses a text-only header and muted slate/blue styling; the fox
include and `/fox.png` route have been removed, so no image bytes enter the firmware.

Validation for this optimization: ESP32/no_fs build passed (1,428,944 application
bytes / 70%; 77,268 static RAM bytes / 23%). The fixed history moved about 3.2 KB
into static storage but replaced the much larger dynamically allocated history.
Dashboard, tuning and BLE-definition tests passed; USB flashing/hashes verified.
Startup history/detector/journal self-tests passed. A restart recovered a failed
initial BME280 probe, followed by valid five-reading reports and BLE ready.
Live comparison checked 36 bucket updates, including one complete 30-second
bucket: weighted means agreed within 0.002 of rounded API inputs and extrema
matched exactly. A subsequent snapshot showed 74,392 free heap bytes / 65,524
bytes in the largest block. Heap values vary with clients and active debug capture.

## API and derived values

- GET /: dashboard. The former /fox.png resource is no longer served.
- GET /api/readings?since=N: BME latest report and incremental BME history.
  sample_interval_ms=200, publish_interval_ms=1000, sample_count=5 for valid
  reports. Channels in history use [mean, minimum, maximum].
- GET /api/motion?since=N: latest motion status plus incremental mean XYZ history.
  target_hz=100, publish_interval_ms=1000, sample_count indicates raw frames
  averaged. Also returns interrupts_seen, fifo_overflows, warning, error,
  acceleration_m_s2, minimum_m_s2 and maximum_m_s2. History uses a:[X,Y,Z].

Measured pressure remains local hPa in API/serial. The altitude field changes
estimated sea-level pressure in the current browser tab, including all pressure
history, using `localHpa / (1 - altitudeM / 44330)^5.255`. Reload restores
DEFAULT_ALTITUDE_M; edits do not write device flash. Allowed altitude: -500 to
9000 m. This is a standard-atmosphere estimate, not a pressure-reference calibration.

Dew point uses one-second mean temperature and humidity with the Bolton
approximation: gamma=ln(RH/100)+17.67*T/(243.5+T), Td=243.5*gamma/(17.67-gamma).
RH=0 is unavailable. This is derived from mean inputs, not an average of raw
dew-point calculations. Network/sensor errors clear current derived values.

## Validation checklist for the reduced-rate update

Run `node tests/dashboard.test.cjs` for simulated browser logic checks. These
cover polling schedules, graphs/opacity, derived values, errors, deduplication,
backfill and retained history. They do not validate visual appearance or hardware.

After upload and browser refresh, manually verify:

1. BME serial reports arrive about once per second, n=5 and min<=mean<=max.
2. MPU reports arrive about once per second, n around 100, target_hz=100 and
   publish_interval_ms=1000. Interrupt counts increase on GPIO17.
3. Both graphs update at 1 Hz without artificial gaps; all four graphs still
   expand from short histories to 600 seconds.
4. FIFO overflows stay zero and I2C error reports are absent over a longer run.
   Check with multiple tabs, network interruptions/backfill and OTA separately.
5. LED stays OFF while armed and blinks 1 Hz/50% during a pickup alert; slow rotation changes the MPU axes, whose
   stationary magnitude is roughly 9.81 m/s^2. Acceleration includes gravity.
6. Altitude/dew point, dark dashboard styling, Wi-Fi recovery and offline LAN operation work.

## References

- https://invensense.tdk.com/wp-content/uploads/2015/02/MPU-6000-Register-Map1.pdf
- https://docs.espressif.com/projects/arduino-esp32/en/latest/api/gpio.html
- https://github.com/adafruit/Adafruit_BME280_Library
- https://unidata.github.io/MetPy/latest/api/generated/metpy.calc.dewpoint.html

## Reduced-rate deployment result

Compilation and OTA upload to 192.168.50.83 succeeded. Flash: 1138907 bytes
(86% of application slot); static RAM: 95152 bytes (29%). Browser logic tests
passed with both polling streams at 1 Hz. Six post-reboot API spot checks all
reported valid MPU data with 100 raw frames/report and valid BME data with
five readings/report. MPU target_hz=100, BME sample_interval_ms=200, and both
publication intervals were 1000 ms. Interrupt counts increased; FIFO overflows
were zero and both error fields were empty in all six checks. These short
checks do not establish long-term error-free acquisition or visual smoothness.
Refresh the browser to load the updated one-second polling code.

## MPU6050 reset, scale verification and flat-pose calibration

Incorrect static values were investigated using independent direct-register
reads. Startup now explicitly resets the MPU device and accel/gyro/temperature
signal paths, waits for settling only in setup, enables all axes and reads back
scale, rate, FIFO layout and interrupt settings. Configuration mismatches are
errors. Signed big-endian decoding is shared by FIFO/direct reads and includes
compile-time checks for +1g/-1g, -1 count and negative full scale.

After deploying the reset fix, direct and FIFO values agreed near X=0.41,
Y=-0.41, Z=11.16 m/s^2. ACCEL_CONFIG was verified as 0 (16384 counts/g at +/-2g).
This corrected the large axis errors but exposed remaining stationary bias.
No clamping or normalization to 1g is used.

The dashboard's **Calibrate flat (Z up)** button starts 500 raw samples (~5 s at
100 Hz). Keep the module flat, completely still, with positive Z upward. The
user must explicitly initiate calibration; there is no calibration at boot.
The algorithm estimates constant offsets against [0,0,9.80665] m/s^2, preserving
gravity and subsequent acceleration changes. It rejects samples with excessive
variance, an implausible flat pose, excessive offsets or I2C/OTA interruptions.
This is a single-pose bias calibration, not a six-position scale/cross-axis
calibration. Check other orientations afterward if absolute accuracy matters.

Offsets are saved as one NVS blob in Preferences namespace mpu6050, bias-v1,
and restored after reboot. Completing calibration clears old motion history
and the partially averaged window so corrected and uncorrected values are not
mixed. BME history remains unaffected. Failed calibration does not apply new
bias values; the dashboard displays the reason and enables retry.

POST /api/motion/calibrate starts calibration; GET /api/motion reports
calibrating, calibration_samples, calibration_error and bias_m_s2. GET
/api/motion/diagnostics exposes raw counts, live scale/register readback,
uncorrected direct acceleration/magnitude and corrected direct acceleration.
These diagnostic reads do not change offsets or acquisition settings.

Static-reading fix deployment: compiled successfully (1145291 flash bytes,
95224 static RAM bytes), browser tests passed including calibration progress
and rejection messages, and OTA upload succeeded. A five-second/500-frame flat
Z-up calibration was performed on the module in the user's stated desk pose.
It passed stationarity/pose checks and saved bias [0.391, -0.399, 1.372] m/s^2.
Three subsequent one-second reports were [0.003,0.003,9.806],
[-0.001,0.002,9.808], and [0.003,0.000,9.810] m/s^2, all marked valid.
Calibration storage write succeeded; restoration across a future reboot and
accuracy in other orientations have not yet been physically verified.

## Pickup detection and LED

See [PICKUP_DETECTOR.md](PICKUP_DETECTOR.md) for the reviewed flow, algorithm
blocks, defaults/limits, persistent dashboard settings, APIs and manual tests.
The LED now belongs exclusively to pickup alerts: OFF normally, 1 Hz at 50%
duty while alerting by default. Acquisition pulses have been removed. Alerts
clear automatically after a fresh quiet window at the current orientation.
Gyro data shares the existing 100 Hz FIFO (12-byte accel+gyro frames), with no
new wiring or change to GPIO17. Motion/BME graph publication stays at 1 Hz.


Pickup deployment validation: compilation/OTA succeeded; algorithm self-tests
passed on the board, settings validation and reboot persistence were verified,
and the final live state was armed with the dedicated LED timer available and
no sensor errors/overflows in three checks. The temporary persistence-test
setting was restored to its original value. See PICKUP_DETECTOR.md for details.
Physical pickup and LED timing remain manual verification tasks.

Pickup tuning page: open /pickup for persistent configuration and live plots of
all thresholded signals, filtering/timing and LED timing. It includes historical
threshold overlays, pause/clear controls and CSV export. See PICKUP_DETECTOR.md
for sampling, buffering and signal interpretation details.

Main-dashboard gyro readout: XYZ one-second means now appear immediately below
acceleration, in rad/s, before detector zero-rate bias subtraction. The motion
API adds gyroscope_rad_s; invalid/stale readings are null and the browser clears
the readout on errors. No extra gyro history buffer or graph is allocated.
Compilation, dashboard/tuning tests and OTA upload passed. After reboot, GET /
returned HTTP 200 with the gyro element, /api/motion returned valid gyro means
[-0.047,0.004,0.002] rad/s with 100 samples, and detector self-tests passed.
Flash: 1195427 bytes (91%); static RAM: 119760 bytes (36%). Browser appearance
still requires user confirmation.

Gyroscope update: use Calibrate gyro (keep stationary) for a five-second saved
zero-rate calibration, independent of flat-pose accelerometer calibration.
Gyro readouts, rotation debug plots, configuration controls and debug CSV now
use deg/s. The API/stored detector settings retain rad/s, with display conversion
metadata for compatibility. See PICKUP_DETECTOR.md for details and caveats.

Optional BLE event history: see [BLE_EVENTS.md](BLE_EVENTS.md) for nRF Connect UUIDs,
the single **Pickup lists** text characteristic and Android naming import,
ten-event persistent storage, UTC handling, and phone/USB verification. This
BLE variant requires the **No FS 4MB (2MB APP x2)** partition
scheme and a one-time USB upload before OTA can resume with the larger binary.

Live device/dashboard test suite: see [tests/DEVICE_TESTS.md](tests/DEVICE_TESTS.md)
for the one-minute continuity/latency test, browser rendering checks, history,
recovery, concurrency and soak profiles, and local HTML/JSON/CSV reports.

Acquisition timing: BME readings follow a fixed 200 ms schedule and reports
follow fixed one-second boundaries. Missed slots are skipped, never replaced
with burst readings. `/api/readings` exposes cumulative `missed_sample_deadlines`
(skipped sampling slots), `missed_report_deadlines` (whole skipped report windows)
and `late_conversion_services` (completed conversions retrieved more than 250 ms
after triggering). A conversion fault means the sensor was still busy at the next
200 ms service deadline; there is no repeated readiness polling. Motion history includes
`sample_count` and `error` for every row, allowing sample accounting after backfill.

## Acquisition and processing (blocks 2–7)

After startup, the `sensor-acquire` FreeRTOS task exclusively owns sensor I2C.
MPU6050 GPIO17 interrupts only set a pending flag and notify this task; they never
perform I2C. A 10 ms task-wait timeout keeps acquisition running if INT is missing.
The owner services MPU FIFO first, then BME; their bus transactions cannot overlap.
Diagnostics and OTA pause/resume use acknowledged owner commands. No network send
holds the acquisition-state mutex. That mutex can briefly delay a status-copy caller
until an in-progress bus service finishes; it is not an interrupt-disabling lock.

BME conversions follow a fixed 200 ms grid. Each deadline reads the previous forced
conversion once before triggering the next. The fifth result is queued before the
one-second boundary, so it belongs to the preceding reporting window. Samples carry
actual trigger times; MPU times are nominal 10 ms cadence estimates anchored to FIFO
retrieval, not hardware timestamps. Independent MPU clock drift is not corrected.

A static 128-packet queue carries raw motion frames, validated BME values, faults and
window boundaries to the sole consumer, the `sensor-process` task. Queue sends never wait. Overflow
increments a loss generation so downstream state is invalidated before accepting
post-loss data. Motion samples older than the configured maximum gap are rejected
rather than generating delayed historical alerts. Queued samples from before a
calibration command do not count toward that calibration.

Aggregation, calibration, pickup detection/settings, event-journal writes and plot
history are owned by the processing task. Every accepted queue packet notifies it;
a 20 ms idle timeout also services detector gaps, the LED state and journal writes.
Calibration, settings and debug subscription requests are acknowledged processing-owner
commands. HTTP copies bounded reports/history/status under a recursive processing
mutex and serializes/sends after releasing it. The environment history snapshot and
up-to-24-row debug snapshot use temporary checked heap allocations; motion snapshots
contain at most 20 reports. Allocation failure returns HTTP 503 without modifying
live state. Debug timestamps stay monotonic when a FIFO estimate precedes a newer
service timestamp. Both sensor acquisition and processing continue during slow HTTP
transfers; CPU, allocator and flash/NVS contention still exist. Dashboard hosting and
history delivery are unchanged.

OTA start stops the acquisition owner, clears queued samples, then waits for processing
to pause and cancel calibration. Failed OTA resets FIFO/BME scheduling, queues a fresh
window boundary, and resumes processing. The LED timer remains independent. Acquisition
uses priority 2 and a 4096-byte stack; processing uses priority 1 and a 6144-byte stack.
Both tasks are unpinned. Worker creation failure prevents acquisition from starting.
When no processing worker exists (including disabled I2C builds), the main loop still
maintains journal/LED state. Processing-state locks precede acquisition-state locks;
neither is held while waiting for an owner command or sending an HTTP response.

### Acquisition timing diagnostics

`GET /api/timing` provides lightweight, cumulative diagnostics since boot without
adding periodic serial output or changing sampling deadlines. `environment_samples`
counts validated BME readings; `motion_frames` counts decoded MPU FIFO frames.
Compare counter differences against `uptime_ms` differences over at least 10 seconds
to estimate achieved rates (normally 5 and 100 samples/s after startup).

The `loop`, `environment`, `motion`, `processing` and `http` objects report call counts,
last/maximum/mean elapsed duration in microseconds and maximum start-to-start gap.
Durations include blocking and preemption, not CPU utilization. Environment timing
covers acquisition-owner BME calls; motion timing excludes disabled/OTA-paused
calls. Most BME calls do no I2C work, so mean duration is not conversion latency.
Motion service runs on owner wakeups, including fallback/empty checks. Loop duration
includes HTTP and network/audio services, not independent acquisition or processing.
BME timing includes its periodic MPU chip-temperature read; reports are published by processing.
HTTP timing includes idle checks and response transmission.
The current timing response excludes its own unfinished HTTP/loop duration.
Maxima include startup settling, OTA and other stalls; they do not reset on reads.
Unsigned microsecond differences support normal timer wraparound, but gaps of
71 minutes or longer cannot be measured reliably. Timing updates/copies use brief
spinlocks; no sensor I2C is performed under these spinlocks or inside an ISR.
The response also exposes `acquisition_running`, `queue_capacity`, `queue_depth`,
`queue_high_water`, `queue_dropped`, `acquisition_stack_free_bytes`, `processing_running`
and `processing_stack_free_bytes`, plus free/minimum heap and largest free-block sizes.
Task stack allocations are runtime heap costs, additional to reported static RAM use.

For baseline verification, compare `/api/timing`, `/api/readings` and `/api/motion`
before/after normal dashboard and `/pickup` use. Check rates, existing deadline/error
counters and FIFO overflows together. A timing peak alone does not establish data loss.

Hardware verification after installing the default build:

1. Confirm `acquisition_running:true` and `processing_running:true`, advancing sample counters (approximately 5/100
   per second), five-sample BME reports and healthy motion reports after startup.
2. Exercise the main dashboard, `/pickup` and `/api/motion/diagnostics`; watch queue
   drops, FIFO overflows, I2C errors and stack headroom. Sampling counters should keep
   advancing during HTTP work, with processing and report sequences advancing independently
   of browser transfer completion. Check both stack margins and heap under tuning-page load.
3. Exercise both calibration commands while stationary and confirm completion and
   retained bias after reboot. Move the board and verify pickup/LED behavior.
4. Verify a normal OTA update and, when convenient, recovery from an interrupted
   transfer. Build/test success alone does not establish any of these hardware results.
