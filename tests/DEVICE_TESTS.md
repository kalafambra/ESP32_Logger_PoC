# Live device and dashboard tests

These tests run on the PC against the currently installed firmware. They capture
the dashboard's own requests and inspect a real browser, so the baseline does
not create a second sensor-polling client. A small diagnostic request every five
seconds records free heap and the largest free block. No firmware upload,
calibration, settings write, or persistent device debug logging is performed.
The recovery and load profiles deliberately change browser traffic.

## Setup (PowerShell, from the project folder)

Use Python 3.10 or newer and an installed Chrome browser:

```powershell
python -m venv .venv-device-tests
.\.venv-device-tests\Scripts\python.exe -m pip install -r tests/device-requirements.txt
```

This environment is already installed on the development PC. No Arduino
compilation is needed. Keep the ESP32 powered and connected to the same LAN.
Close other dashboard tabs for a controlled baseline; otherwise label the
conditions with `--condition "phone connected over BLE"`, for example.

## Run

One-minute baseline after a ten-second warmup:

```powershell
.\.venv-device-tests\Scripts\python.exe scripts/test_device.py --url http://192.168.50.83
```

All profiles (approximately 23 minutes, including a 15-minute soak):

```powershell
.\.venv-device-tests\Scripts\python.exe scripts/test_device.py --url http://192.168.50.83 --tests all
```

Selected profiles:

```powershell
.\.venv-device-tests\Scripts\python.exe scripts/test_device.py --url http://192.168.50.83 --tests history,recovery,concurrency,debug
```

Add `--headed` to watch Chrome. Use `--browser msedge` for installed Edge.
Alternatively, install Playwright Chromium with
`.\.venv-device-tests\Scripts\python.exe -m playwright install chromium`
and select `--browser chromium`.

Without browser dependencies, `python scripts/test_device.py --url
http://192.168.50.83 --api-only` runs API checks only. That mode cannot diagnose
rendering freezes or run recovery/concurrency/debug profiles.

## Profiles

| Name | Default duration | Checks |
| --- | --- | --- |
| `baseline` | 60 s | Source sequence continuity, publication rate, sample accounting, validity, latency, freshness, browser progress and rendering, heap observations |
| `history` | 120 s | Baseline checks, independent MPU temperature handling and bounded browser history; legacy coarse-bucket checks apply only to older firmware |
| `recovery` | 5/20/40 s polling pauses plus recovery | Pauses only the primary browser's sensor requests; independent API probes check that recording continues; verifies graph backfill and catch-up |
| `concurrency` | 60 s | A second main-dashboard browser while monitoring the first |
| `debug` | 60 s | Main dashboard plus the pickup tuning page and its debug polling |
| `soak` | 900 s | Extended baseline checks and heap trends |

Recovery tests browser polling loss, not an actual Wi-Fi outage. No sensor
disconnect, reboot, physical motion, or BLE connection is induced automatically.
Repeat runs under those manually controlled conditions and label each run.
Opening `/pickup` can allocate the firmware's small unacknowledged telemetry buffer;
that expected memory cost is part of the debug-load scenario. Compare memory
within a phase rather than treating every difference between profiles as a leak.

## Default criteria

| Measurement | Criterion |
| --- | --- |
| Published data | Approximately 1 Hz; no missing sequence ranges, conflicting immutable records, or backwards uptime/sequence |
| Observed update gap | At most 3 s outside planned pauses |
| BME280 | Valid reports with 5 samples each |
| MPU6050 | Valid samples; aggregate 100 Hz within 1%; no new FIFO overflows |
| Request latency | p95 at most 500 ms; maximum at most 2000 ms, including response body |
| Source freshness | Firmware-reported age at most 2500 ms |
| Browser | Both streams advance, graph redraws keep up, displayed values match received values, no unexpected request or script errors |
| Recovery | Catch up within 5 s with required history present |
| Memory | Warn if both free heap and largest block fall by more than 4096 bytes between first/last quarter medians (at least 12 observations) |

Change limits with `--p95-ms`, `--request-ms`, `--freshness-ms`,
`--ui-stall-ms`, and `--sample-tolerance` (a fraction, e.g. `0.01`). Durations
are configurable through `--duration`, `--history-duration`, `--soak-duration`,
`--warmup`, and `--recovery-pauses 5,20,40`. Short runs are marked exploratory;
they are not substitutes for the one-minute baseline.

Constant sensor values are healthy if sequence numbers and acquisition windows
advance. An observed gap means the client did not observe a new report promptly;
it does not by itself prove acquisition stopped. Backfill can show that recording
continued during delayed delivery.

Round-trip latency and source age are separate. Source age plus round-trip time
is recorded as a conservative arrival-age upper bound; this suite does not
measure one-way network latency. Sample accounting relies on firmware telemetry,
not an independent measurement of physical ADC conversions. Current motion backfill
includes per-window sample counts and errors. When older firmware omits counts, missing count evidence produces
`INCONCLUSIVE`, never an invented count of 100. Old coarse history cannot be
independently recomputed unless its fine constituent reports were captured.
Heap trends suggest a problem; they do not prove a leak.

## Results

Each run creates a new directory under `test-results/hardware/`, ignored by git.
Use `--output path/to/new-folder` to select a destination; existing folders are
never overwritten.

- `report.html`: self-contained summary and selectable latency/sequence/age/heap timeline.
- `report.json`: machine-readable checks, thresholds, metrics and failure reasons.
- `timeline.csv`: timestamped request latency, sequence, freshness, sample counts, errors and memory.
- `raw-api.jsonl`: raw API responses, including secondary browser traffic.
- `browser.jsonl`: browser probes and console errors.
- Phase screenshots: final dashboard state for completed phases.

Open `report.html` locally in a browser. `FAIL` means at least one criterion
failed; `INCONCLUSIVE` means required evidence was unavailable. `WARN` is
non-fatal. Exit codes: 0 PASS/WARN, 1 FAIL, 2 ERROR/INCONCLUSIVE, 130 interrupted.
Optional history checks can be inconclusive during a baseline without making
the entire baseline inconclusive. Recovery deliberately suppresses expected
network errors during its planned polling pauses.

Captures contain sensor readings and pickup event history; share them with that
in mind. They do not read `config.h` or collect Wi-Fi/OTA passwords.

## Offline analyzer verification

```powershell
python -m unittest discover -s tests -p test_device_suite.py -v
node tests/acquisition.test.cjs
```

These fault-injection tests exercise constant healthy data, missing/backfilled
reports, frozen sequences, reboots, wrong sample counts, FIFO errors, conflicting
data, incorrect history averages, browser stalls, planned pauses and heap trends.
They validate the test analyzer and do not establish hardware correctness.
The acquisition regression test executes environment.cpp's timing/status branches
with deterministic time and sensor stubs (C++ type/reference syntax is adapted
for JavaScript). It covers late completed conversions, busy-sensor timeout, I2C
failure, scheduling jitter, skipped slots and fixed report-boundary reset.

Firmware self-checks and persistent device debug logs are a separate later step.
