# ESP32 environmental and motion monitor

ESP32-WROOM Arduino firmware with an offline fox-themed dashboard, BME280,
MPU6050, serial diagnostics and password-protected ArduinoOTA.

## Current acquisition and publication rates

| Sensor | Acquisition | Serial/API/graph publication |
| --- | --- | --- |
| BME280 | 5 Hz (every 200 ms) | 1 Hz: mean/min/max of exactly five readings |
| MPU6050 | 100 Hz, interrupt + FIFO | 1 Hz: mean XYZ, plus min/max in the API |

Both dashboard endpoints are polled once per second, with no overlapping
requests within a polling stream. Motion includes gravity. All graphs retain
up to 600 seconds in the browser, expanding to fill their width when history
is shorter. Environmental extrema remain at 30% opacity. Motion plots mean
X/Y/Z as three colored lines; raw 100 Hz frames are not transmitted.

## Files

- ESP32_1.ino: BME acquisition/publication, Wi-Fi, OTA and HTTP routes.
- motion.h: MPU6050 configuration, ISR, FIFO draining, aggregates and motion API.
- aggregation.h: BME channel means and extrema.
- config.h: private Wi-Fi/OTA credentials, altitude and GPIO settings; ignored by Git.
- config.example.h: shareable configuration with placeholder credentials.
- dashboard.h: embedded HTML/CSS/JavaScript; no internet dependencies.
- fox.png and fox_image.h: original image and generated flash-resident PNG data.
- scripts/embed_fox.py: regenerate the PNG header after replacing fox.png.
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
and gives more wiring margin than the former 400 kHz setting. The BME LED pulse
is 100 ms on each valid reading so pulses remain visible at 5 Hz.

## Arduino IDE setup and initial USB upload

1. Open ESP32_1.ino; keep the containing folder named ESP32_1.
2. Install esp32 by Espressif Systems in Boards Manager. Additional URL:
   https://espressif.github.io/arduino-esp32/package_esp32_index.json
3. Install Adafruit BME280 Library and its BusIO/Unified Sensor dependencies.
   MPU access uses Wire directly and needs no additional library.
4. Set Wi-Fi credentials, DEFAULT_ALTITUDE_M and OTA_PASSWORD in config.h.
   Wi-Fi must be 2.4 GHz. OTA requires at least 12 password characters; empty
   disables OTA. Do not share configuration or compiled firmware containing secrets.
5. Select ESP32 Dev Module and the default OTA-capable partition scheme.
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
During transfer, sampling/HTTP pause, the LED turns off and MPU FIFO generation
is stopped. A failed transfer resets the FIFO and discards interrupted windows.
A successful update reboots and clears device RAM history.

If discovery fails, check the IP, same-LAN access, VPN/client isolation and
private-network firewall permissions. mDNS uses UDP 5353; OTA listens on UDP
3232 and connects back to the uploader TCP listener. The helper uses local TCP
32320 and does not put the password in command arguments or logs.

Current build directory: build-fox/ (the earlier build/ had a locked file).
Both folders are ignored by Git. Compile with an available Arduino CLI:

```text
arduino-cli compile --fqbn esp32:esp32:esp32 --build-path ./build-fox .
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
main loop, in batches up to 20 six-byte frames. A 5 ms safety poll handles lost
notifications; missing INT edges produce a warning. IRQ edges may coalesce and
must not be mistaken for the exact raw sample count.

The 1024-byte FIFO holds about 170 frames, now about **1.7 seconds** of buffering.
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

BME history holds 602 reports (~10 minutes); API backfill is capped at 60 per
request to bound heap/network work. Motion history holds 128 reports (~128 s)
and sends at most 20 per request. Incremental backfill repairs missed polls;
sequences missing beyond the retained buffers produce graph gaps. Browser
history is time-bounded to ten minutes. Reload can restore only device history.

## API and derived values

- GET /: dashboard. GET /fox.png: original PNG served from flash.
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
5. BME LED pulses for 100 ms at 5 Hz; slow rotation changes the MPU axes, whose
   stationary magnitude is roughly 9.81 m/s^2. Acceleration includes gravity.
6. Altitude/dew point, fox image, Wi-Fi recovery and offline LAN operation work.

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
