# ESP32 environmental and motion monitor

ESP32-WROOM Arduino firmware with an offline dark slate dashboard, BME280,
MPU6050, serial diagnostics and password-protected ArduinoOTA.

## Current acquisition and publication rates

| Sensor | Acquisition | Serial/API/graph publication |
| --- | --- | --- |
| BME280 | 5 Hz (every 200 ms) | 1 Hz: mean/min/max of exactly five readings |
| MPU6050 | 100 Hz, interrupt + FIFO | 1 Hz: mean XYZ, plus min/max in the API |
| MPU6050 chip temperature | 1 Hz, direct temperature-register read | 1 Hz, on the shared temperature graph |

Both dashboard endpoints are polled once per second, with no overlapping
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

- ESP32_1.ino: BME acquisition/publication, Wi-Fi, OTA and HTTP routes.
- motion.h: MPU6050 configuration, ISR, FIFO draining, aggregates and motion API.
- aggregation.h: BME channel means and extrema.
- config.h: private Wi-Fi/OTA credentials, altitude and GPIO settings; ignored by Git.
- config.example.h: shareable configuration with placeholder credentials.
- dashboard.h: embedded HTML/CSS/JavaScript; no internet dependencies.
- fox.png and fox_image.h: archived image assets, no longer embedded or served by the firmware.
- scripts/embed_fox.py: regenerate the PNG header after replacing fox.png.
- scripts/quantize_fox.py: 16-color conversion (Pillow), preserving source dimensions and alpha values.
- environment_history.h: bounded one-second / 30-second environmental history and its self-tests.
- scripts/upload_ota.py: authenticated uploader wrapper; reads password from config.h.
- tests/dashboard.test.cjs: simulated DOM/canvas tests, run with Node.js.

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

Leave MPU XDA/XCL unused. INT is active-high, push-pull and latched until status
is read. Do not leave AD0 floating. Use 3.3 V I2C logic and pull-ups, even if a
breakout regulator accepts a higher supply. Check the exact breakout schematic.
BME addresses 0x76/0x77 do not conflict with MPU address 0x68. Some carrier
boards have another LED pin/polarity or only a power LED; configure accordingly.

The bus runs at **100 kHz**. This is sufficient for the reduced acquisition rate
and gives more wiring margin than the former 400 kHz setting. The LED is reserved exclusively for pickup detection; acquisition no longer drives it.

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
5. Select ESP32 Dev Module and **No FS 4MB (2MB APP x2)** (this supports OTA).
   Keep this OTA-capable layout for the BLE firmware; first install it by USB.
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

Current BLE build directory: build-ble/. Earlier builds used build-fox/ and
build/; all are ignored by Git. Compile with an available Arduino CLI:

```text
arduino-cli compile --fqbn esp32:esp32:esp32:PartitionScheme=no_fs --build-path ./build-ble .
```

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

BLE event history: see [BLE_EVENTS.md](BLE_EVENTS.md) for nRF Connect UUIDs,
the single **Pickup lists** text characteristic and Android naming import,
ten-event persistent storage, UTC handling, and phone/USB verification. This
version requires the **No FS 4MB (2MB APP x2)** partition
scheme and a one-time USB upload before OTA can resume with the larger binary.
