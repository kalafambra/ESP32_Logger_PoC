# ESP32-WROOM environmental monitor

BME280 sampling once per second. Every five seconds, publishes the mean, minimum and
maximum of exactly 5 readings to serial and an offline local Wi-Fi dashboard.

## Files and GPIO map

- ESP32_1.ino: acquisition, five-second publication, history API, Wi-Fi, HTTP.
- aggregation.h: arithmetic mean and extrema for each channel.
- config.h: private credentials/pins; ignored by Git. Credentials preserved
  when updating the sampling interval to 1000 ms.
- config.example.h: shareable placeholder configuration.
- dashboard.h: HTML/CSS/JavaScript in flash; no external assets.
- tests/dashboard.test.cjs: simulated browser logic checks, run with Node.js.

| Function | ESP32 |
| --- | --- |
| BME280 SDA | GPIO21 / D21 |
| BME280 SCL | GPIO22 / D22 |
| BME280 GND | GND |
| BME280 VCC/VIN | 3V3, if supported by your breakout |
| User LED | GPIO2, active HIGH by default |

Use 3.3 V I2C logic and appropriate pull-ups. Check the breakout documentation.
If exposed, CS/CSB must be high for I2C; SDO selects 0x76 or 0x77 (both tried).
This requires a BME280, not a BMP280. Boards vary in LED pin/polarity; adjust
STATUS_LED_PIN and LED_ON, or use -1 to disable. Power down before rewiring.

## Arduino IDE setup

1. Open ESP32_1.ino in this folder. Keep folder/sketch names matched.
2. Install esp32 by Espressif Systems in Boards Manager. Additional URL:
   https://espressif.github.io/arduino-esp32/package_esp32_index.json
3. Install Adafruit BME280 Library, accepting Adafruit BusIO and Unified Sensor
   dependencies. Wire, WiFi and WebServer come with the ESP32 core.
4. Set credentials in config.h (copy config.example.h if missing). Use a
   2.4 GHz network. Do not share config.h or compiled firmware containing secrets.
5. Select ESP32 Dev Module; Verify, select COM5 (or your current port), Upload.
   Close Serial Monitor first. If Connecting stalls, hold BOOT until writing starts.
6. Open Serial Monitor at 115200 baud and reset. Open the printed http:// IP
   on the same LAN. Refresh the browser after a firmware update.

Credentials are not returned by the API. No internet connection is required.
Client isolation can block local access. HTTP is unauthenticated; use a trusted
LAN without port forwarding. Wi-Fi retries cancel the old attempt after 30 s,
then restart 250 ms later using a timer. Disconnect reasons and DHCP progress
are logged; no credentials are printed. DHCP may change the device IP.

## Acquisition and publication

Every 1000 ms the firmware triggers a forced x1-oversampling conversion, checks
completion after at least 15 ms, and reads compensated temperature, humidity
and pressure. Conversion timeout is 80 ms. There is no delay() or conversion
wait loop in acquisition. Library startup waits occur only during setup.

Non-overlapping five-second windows contain five conversions, starting near
0, 1000, ..., 4000 ms relative to the window start. At the next five-second boundary,
mean = sum / 5, minimum and maximum are published independently per channel.
Averages are calculated before rounding. API/serial use three decimal places;
current-value cards display two. API/serial pressure is measured local hPa; the dashboard displays estimated sea-level hPa.

Only a window with exactly five valid readings and no acquisition errors is
published as valid. A missing conversion, I2C failure or badly delayed window
produces a five-second error report and null values, never a mean of fewer than
five readings. Windows are reset after publication; late conversions do not
carry over. Sensor initialization failure requires correcting wiring and reset.
Transient read failures are retried on later acquisition intervals.

Serial prints one mean/min/max report every five seconds (or a window error), plus
Wi-Fi status (connected/disconnected and numeric status) and IP in every report,
including error reports. Disconnected reports use IP=0.0.0.0.
The LED pulses for 250 ms on each valid measurement, once per second.
Additional Wi-Fi diagnostics appear on connection events.

I2C and HTTP operations remain synchronous: this is best-effort timing, not
hard real time. A slow client or bus fault may make a window incomplete. Wire
timeout is 20 ms. Use serial report timestamps and n=5 to assess actual timing.
Adafruit compensated reads do not expose every transient bus error; extra bus,
identity, raw-data and range checks catch common failures.

## Graphs and the previous broken lines

Each channel has its own labeled graph. The mean is a solid full-opacity line;
minimum and maximum are two lines at 30% opacity. The vertical scale includes
all three series. Natural sensor quantization and physical changes still show;
we do not add interpolation or smoothing beyond the requested arithmetic mean.

Previously only the latest point was served and the browser broke lines at
1.25-second timestamp gaps. Missed HTTP polls could produce broken segments.
The device now retains 122 five-second reports in RAM and serves the last 600 s.
The browser polls once per second and asks for all reports since its last
sequence, backfilling missed requests. Lines connect consecutive report
sequences regardless of browser-poll jitter; invalid windows or reports lost
beyond the buffer produce real gaps. Browser-side retention is 600 seconds.
Reload backfills available device history; device reboot clears that history.
Network errors clear current cards but preserve graph data for recovery.

## API

GET / serves the dashboard. GET /api/readings returns JSON, no-store caching.
GET /api/readings?since=12 includes only buffered records newer than report 12.
Omitting since returns all available reports within 600 seconds. A since value
beyond the latest sequence is treated as a device restart and returns history.

Top-level temperature_c, humidity_pct, pressure_hpa are the latest means.
Other fields: sequence, uptime_ms, sample_ms, sample_age_ms, sample_count,
sample_interval_ms (1000), publish_interval_ms (5000), sensor_ok, error,
wifi_connected, rssi_dbm. Values are null if invalid/stale (>6500 ms).

The history array contains records with sequence, age_ms, sample_count, ok,
and channel arrays in the order [mean, minimum, maximum], for example:

```json
{"sequence":12,"age_ms":20,"sample_count":5,"ok":true,"temperature_c":[23.456,23.410,23.500],"humidity_pct":[45.123,45.000,45.220],"pressure_hpa":[1001.234,1001.100,1001.300]}
```

Invalid history records use [null,null,null]. No NaN JSON values are emitted.

## Validation and manual tests

The user confirmed the previous firmware connected with corrected credentials
and displayed a live dashboard. That confirmation does not validate this update.

1. Verify in Arduino IDE or compile with Arduino CLI:
   `arduino-cli compile --fqbn esp32:esp32:esp32 --build-path ./build .`
2. Upload and open serial at 115200. Confirm one report every five seconds, n=5,
   and min <= mean <= max for every channel. Confirm a 250 ms LED pulse every second, not only on reports.
3. Refresh the dashboard. Confirm three lines per graph, with extrema faint,
   five-second updates, and a growing time axis up to a rolling 600-second window.
4. Interrupt the browser's network for 5-10 s, then restore it. Confirm missing
   reports backfill without artificial breaks. Interrupt longer than 610 s to
   confirm unavailable old reports are not fabricated.
5. Compare serial means/extrema to JSON history, including three-decimal values.
   Inspect /api/readings?since=N for incremental results and sequence continuity.
6. Check sensor-failure reporting, Wi-Fi reconnect, and offline LAN operation.
   Power down before disconnecting/reconnecting sensor wiring; reset afterward.
7. Monitor under multiple browser tabs. Incomplete windows should be reported
   explicitly if load prevents all five readings from finishing on time.

Automated dashboard checks: `node tests/dashboard.test.cjs`.
These use a simulated DOM/canvas; visual browser and physical timing tests
must still be performed manually. Upload status is recorded below.

The altitude/dew-point/600-second update compiled successfully for ESP32 Dev Module
and was uploaded on COM5. Flash verification passed and the board reset.
Flash: 984080 bytes (75%); static RAM: 55888 bytes (17%).
Actual sensor, Wi-Fi and dashboard operation for this update still needs confirmation.
JavaScript checks passed for the existing graphs, corrections, known dew point,
altitude input validation, raw-history preservation, short-history full-width
scaling, and 600-second retention/axis cap. Real browser appearance and hardware
operation remain manual checks.

## Altitude, pressure and dew point

Before compiling, edit `DEFAULT_ALTITUDE_M` in config.h (meters above sea level;
placeholder 0.0f, allowed range -500 to 9000). Credentials are preserved.
The API includes `default_altitude_m` so the dashboard starts at that altitude.

The editable altitude field changes sea-level pressure in the current tab only.
Reload restores the configured default; it does not write config.h or device
flash. Measured pressure in the API and serial remains local pressure. All
three pressure lines and the current pressure value use:

`seaLevelHpa = localHpa / (1 - altitudeM / 44330)^5.255`

This is an approximate standard-atmosphere reduction, matching Adafruit's
seaLevelForAltitude, rather than calibration against a pressure reference.
Changing altitude recalculates the whole pressure history from stored raw values.

Dew point (C) uses the five-second mean temperature and RH, with the Bolton
approximation over liquid water: `gamma = ln(RH/100) + 17.67*T/(243.5+T)` and
`Td = 243.5*gamma/(17.67-gamma)`. It is derived from mean inputs, not a mean of
five independently calculated dew points. RH=0 has no finite dew point and is
shown as unavailable. Sensor/network errors clear the current dew-point field.

Graphs are stacked vertically. History is retained for up to 600 seconds; each
axis spans from the oldest retained report to the newest, using the full width
for shorter histories. Labels indicate seconds relative to now (the newest
report can be several seconds old). Min/max lines retain 30% opacity.

Manual checks after compilation/upload: confirm the configured starting altitude,
change it and verify pressure changes instantly (including extrema/history),
check a saturated-humidity dew point approaches temperature, and watch the axes
grow from a short span to 600 seconds. Reload and verify the default is restored.

## References

- https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bme280-ds002.pdf
- https://github.com/adafruit/Adafruit_BME280_Library
- https://docs.espressif.com/projects/arduino-esp32/en/latest/api/wifi.html


- Pressure reduction: https://github.com/adafruit/Adafruit_BME280_Library/blob/master/Adafruit_BME280.cpp
- Dew-point formula: https://unidata.github.io/MetPy/latest/api/generated/metpy.calc.dewpoint.html



## ArduinoOTA: enable once over USB, then upload over Wi-Fi

ArduinoOTA-enabled firmware compiled successfully and was uploaded over USB to COM5.
Flash verification passed and the board reset. Flash: 1042523 bytes (79%);
static RAM: 60480 bytes (18%). Wireless upload has not yet been tested.
The initial USB installation is complete; confirm the OTA Ready line below.
No extra library installation is needed: ArduinoOTA ships with the ESP32 core.
The installed core's default partition table has two 0x140000-byte (1.25 MiB)
OTA application slots, plus otadata, and supports OTA. Continue using the default
ESP32 Dev Module partition scheme; do not select a No OTA/Huge APP scheme.
Compilation must confirm that the enlarged application still fits its slot.

1. Set `OTA_PASSWORD` in config.h to a unique password of at least 12 characters.
   It starts empty, which disables OTA. Do not share the password in chat.
   `OTA_HOSTNAME` defaults to `esp32-bme280`; use a unique name per board.
   Your Wi-Fi credentials and altitude remain unchanged.
2. Compile and upload the OTA-enabled sketch once over USB to COM5. Close
   Serial Monitor before uploading. Reopen it at 115200 baud after upload.
3. Wait for Wi-Fi to connect and look for:
   `[ota] Ready: esp32-bme280 at <device-IP> (port 3232, password required)`.
   If OTA reports Disabled, set a password of sufficient length and repeat USB upload.
4. Connect the computer to the same LAN. In Arduino IDE 2, look under Tools >
   Port for a network port named esp32-bme280 (or the selected hostname),
   associated with the device IP. It may take a few seconds to appear.
   Keep ESP32 Dev Module selected, and select this network port instead of COM5.
5. For the first wireless test, upload the same sketch again. Enter the OTA
   password when prompted. Confirm upload success, reboot, and dashboard return.
   USB may stay connected for power/serial during this first test: selecting the
   network port makes the firmware transfer use Wi-Fi. Later you may use a
   separate suitable power supply instead of a computer USB connection.
6. Future firmware must retain the OTA code, working Wi-Fi credentials and
   an OTA-capable partition layout. Upload again using the network port.
   If changing OTA_PASSWORD, authenticate this upload with the OLD password;
   after reboot, subsequent uploads use the NEW password.

The existing dashboard is still ordinary HTTP; ArduinoOTA is a separate service.
Do not upload a merged flash image or bootloader as an OTA sketch; the Arduino
network upload uses the application image. Keep USB available for recovery if
firmware fails to boot/connect or accidentally removes OTA support.

During an OTA transfer, sampling and HTTP handling pause and the LED is turned
off. A successful update reboots and clears RAM graph history. On an OTA error,
the interrupted acquisition window is discarded and its next report is invalid;
normal sampling then continues. These recovery paths still need hardware testing.

If the network port does not appear, confirm the Ready line/IP and that both
devices are on the same LAN without guest/client isolation or a blocking VPN.
Restart the IDE if needed. Discovery uses mDNS (UDP 5353); OTA listens on UDP
3232 and the ESP32 connects back to the uploader's TCP listener during transfer.
Allow Arduino IDE/its uploader through Windows Firewall on the private LAN if
prompted. A visible dashboard alone does not prove OTA traffic is permitted.
For Authentication failed, verify the OTA password (separate from Wi-Fi).
For transfer connection failures, inspect firewall/client isolation and serial
OTA error output. Do not expose OTA by forwarding router ports.

Manual acceptance tests: initial USB upload, Ready message, network discovery,
password-authenticated Wi-Fi upload, reboot, dashboard/sensor recovery, and a
second wireless update. Initial USB upload passed; the remaining OTA tests are pending.


## Fox dashboard theme

The header uses the supplied fox.png in a peach frame at the top left. Dark
plum panels, burgundy borders, cream text and peach/rose/gold graph lines echo
the mascot's palette. Responsive styling keeps the stacked graphs readable.
The square CSS image frame crops the source's blank upper margin without
modifying the supplied PNG. Mean and 30%-opacity extrema behavior is preserved.

fox_image.h embeds the original 48676-byte PNG in flash. GET /fox.png serves
it locally as image/png; no SPIFFS upload or internet request is required.
After replacing fox.png, run `python scripts/embed_fox.py`, then rebuild.

This theme uses build-fox/ because a file in the previous build directory was
locked. Both build folders are ignored by Git and can contain credentials in
compiled firmware. Use the .bin from the build folder you actually compiled.

scripts/upload_ota.py wraps the installed Espressif uploader. It reads the OTA
password directly from config.h without putting it in command arguments/logs.
Pass --ip, --firmware and --uploader (path to the installed espota.py).
The password must match the firmware currently on the board. The uploader uses
UDP 3232 and local TCP port 32320; allow it through your private-LAN firewall.

Fox-theme validation: ESP32 Dev Module compilation passed (1093199 bytes,
83% of the application slot; static RAM 60480 bytes, unchanged). Dashboard
logic tests passed, and the embedded PNG was verified byte-for-byte against
fox.png. Authenticated OTA upload to 192.168.50.83 completed successfully with
an OK result. After reboot, HTTP GET / returned 200 with the new fox header
and palette; GET /fox.png returned 200, image/png, 48676 bytes. Visual rendering
and physical sensor/LED behavior for this update still require user confirmation.

## MPU6050 fourth graph

Power off before wiring. The MPU6050 shares the BME280 bus:

| Keyes MPU6050 | ESP32 |
| --- | --- |
| SDA | GPIO21 / D21 (same as BME280 SDA) |
| SCL | GPIO22 / D22 (same as BME280 SCL) |
| GND | GND |
| VCC | 3V3, provided your particular breakout supports 3.3 V supply |
| AD0 | GND, selecting I2C address 0x68 |
| INT, XDA, XCL | Unconnected for this polling implementation |

Do not leave AD0 floating. If tied to 3V3 instead, the address becomes 0x69;
change MPU_ADDRESS in motion.h accordingly. MPU identity WHO_AM_I remains 0x68.
Use 3.3 V logic/pull-ups on the shared ESP32 bus, even if a breakout regulator
supports a higher supply. Check your exact Keyes board's schematic for its
regulator and pull-ups. BME280 uses 0x76/0x77 so it does not conflict with 0x68.
No additional GPIO or interrupt connection is necessary.

motion.h implements checked register reads/writes using the existing Wire bus;
no additional MPU library is required. It configures +/-2 g acceleration,
5 Hz hardware low-pass filtering and 10 Hz internal sampling, then snapshots
X/Y/Z at 1 Hz. Scaling is 16384 counts/g and 9.80665 m/s^2 per g. Burst reads
keep all three axes together. Application sampling does not use delay().
The fourth graph displays raw X/Y/Z acceleration, not averaged, in m/s^2,
including gravity. It retains up to 600 seconds in the browser tab. At rest,
the vector magnitude should be near 9.81 m/s^2; direction depends on mounting.
This is an initial slow-motion display, not an orientation solution or impact
recorder. Gyroscope data is not plotted in this first stage.

/api/readings now includes motion with sequence, sample_age_ms, ok, error and
acceleration_m_s2: [X,Y,Z]. Errors emit nulls. Each acquisition attempt advances
the motion sequence so failures are visible as gaps. MPU data is independent
of the BME280 averaging window. Motion values are polled once per second;
missed browser polls create gaps, with no device-side motion backfill. Only
BME history is backfilled. Disconnecting the MPU at runtime reports I2C errors;
initialization failure requires fixing wiring and resetting the board.

Manual verification: check startup detection at 0x68, one MPU serial message
per second, fourth-graph X/Y/Z units/legend, response to slow rotations, gravity
magnitude at rest, and continued BME readings. Test missing MPU error reporting
and shared-bus behavior. Firmware has not yet been uploaded for this addition.

References:
- https://product.tdk.com/system/files/dam/doc/product/sensor/mortion-inertial/imu/data_sheet/mpu-6000-datasheet1.pdf
- https://learn.adafruit.com/mpu6050-6-dof-accelerometer-and-gyro?view=all

MPU6050 addition validation: compilation passed for ESP32 Dev Module,
flash 1097779 bytes (83%); static RAM 60512 bytes (18%). Dashboard tests passed,
including X/Y/Z display, signed acceleration, duplicate suppression and motion
errors independent of BME errors. No upload or actual MPU6050 test performed.
