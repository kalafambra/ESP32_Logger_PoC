"""Run passive real-device tests and save reproducible PC-side evidence."""
import argparse
import asyncio
import csv
from datetime import datetime, timezone
import html
import json
from pathlib import Path
import sys
import time
from urllib.error import HTTPError
from urllib.parse import urlparse
from urllib.request import Request, urlopen
import uuid

from .analysis import Cache, Limits, Phase, result

PATHS = {"readings": "/api/readings", "motion": "/api/motion", "diagnostics": "/api/pickup/events"}


class Artifacts:
    """Flush observations as they arrive, retaining useful evidence even after interruption."""
    def __init__(self, folder):
        self.folder = folder
        folder.mkdir(parents=True, exist_ok=False)
        self.started = time.monotonic()
        self.started_utc = datetime.now(timezone.utc).isoformat()
        self.raw = (folder / "raw-api.jsonl").open("w", encoding="utf-8")
        self.browser = (folder / "browser.jsonl").open("w", encoding="utf-8")
        self.timeline = (folder / "timeline.csv").open("w", newline="", encoding="utf-8")
        self.columns = "utc elapsed_s phase role stream status rtt_ms sequence uptime_ms sample_age_ms sample_count sensor_ok temperature_ok fifo_overflows free_heap largest_free_block planned error".split()
        self.csv = csv.DictWriter(self.timeline, fieldnames=self.columns)
        self.csv.writeheader()
        self.plot = []

    def api(self, phase, stream, rtt_ms, status, data=None, error=None, role="primary", planned=False):
        now = time.monotonic()
        record = dict(utc=datetime.now(timezone.utc).isoformat(), elapsed_s=round(now-self.started, 3), phase=phase,
                      role=role, stream=stream, status=status, rtt_ms=round(rtt_ms, 3), planned=planned, error=error)
        if isinstance(data, dict):
            body = data.get("motion", {}) if stream == "motion" else data
            record.update({key: body.get(key) for key in ("sequence", "sample_age_ms", "sample_count", "fifo_overflows")})
            record.update(uptime_ms=data.get("uptime_ms"), sensor_ok=data.get("sensor_ok"),
                          temperature_ok=data.get("motion", {}).get("temperature_ok"),
                          free_heap=data.get("free_heap"), largest_free_block=data.get("largest_free_block"))
        self.csv.writerow({k: record.get(k) for k in self.columns})
        self.timeline.flush()
        self.raw.write(json.dumps(dict(**record, data=data), allow_nan=False) + "\n")
        self.raw.flush()
        self.plot.append(record)

    def ui(self, phase, data):
        self.browser.write(json.dumps(dict(phase=phase, elapsed_s=time.monotonic()-self.started, **data)) + "\n")
        self.browser.flush()

    def report(self, report):
        (self.folder / "report.json").write_text(json.dumps(report, indent=2, allow_nan=False), encoding="utf-8")
        sections = []
        for phase in report["phases"]:
            rows = "".join(f"<tr><td>{html.escape(c['test'])}</td><td class='{c['status']}'>{c['status']}</td><td>{html.escape(c['detail'])}<details><summary>Evidence</summary><pre>{html.escape(json.dumps(c['metrics'], indent=2))}</pre></details></td></tr>" for c in phase["checks"])
            sections.append(f"<h2>{html.escape(phase['name'])}: {phase['status']}</h2><p>{phase['duration_s']:.1f} seconds</p><table><tr><th>Check</th><th>Result</th><th>Details</th></tr>{rows}</table>")
        plot = json.dumps(self.plot).replace("<", "\\u003c")
        page = """<!doctype html><meta charset="utf-8"><title>ESP32 device test report</title>
<style>body{max-width:1150px;margin:30px auto;padding:0 20px;background:#11161d;color:#e4e9ef;font:15px system-ui}a{color:#91b6d8}table{width:100%;border-collapse:collapse}td,th{border:1px solid #46566a;padding:10px;text-align:left;vertical-align:top}pre{white-space:pre-wrap;overflow-wrap:anywhere}.PASS{color:#86b8ad}.FAIL,.ERROR{color:#df8a96}.WARN,.INCONCLUSIVE{color:#c9b789}canvas{width:100%;height:220px;background:#19212b}select{padding:6px}</style>"""
        page += f"<h1>ESP32 test report: {report['status']}</h1><p>{html.escape(report['scope'])} · {html.escape(report['url'])}</p>"
        page += "<p><a href='report.json'>JSON report</a> · <a href='timeline.csv'>Timeline CSV</a> · <a href='raw-api.jsonl'>Raw API captures</a> · <a href='browser.jsonl'>Browser evidence</a></p>"
        if report.get("error"):
            page += f"<pre>{html.escape(report['error'])}</pre>"
        page += "<label>Timeline: <select id='metric'><option value='rtt_ms'>Request round-trip (ms)</option><option value='sequence'>Published sequence</option><option value='sample_age_ms'>Source report age (ms)</option><option value='free_heap'>Free heap (bytes)</option><option value='largest_free_block'>Largest free block (bytes)</option></select></label><canvas id='plot' width='1100' height='220'></canvas>"
        page += "".join(sections)
        page += "<script>const rows=" + plot + ";" + """
function draw(){const c=document.getElementById('plot'),x=c.getContext('2d'),key=document.getElementById('metric').value;x.clearRect(0,0,c.width,c.height);const points=rows.filter(p=>Number.isFinite(p[key]));if(!points.length)return;const lo=Math.min(0,...points.map(p=>p[key])),hi=Math.max(lo+1,...points.map(p=>p[key])),end=Math.max(1,...points.map(p=>p.elapsed_s));x.font='12px system-ui';x.fillStyle='#a5b1bf';x.fillText(key+' '+lo+' .. '+hi,10,18);for(const [stream,color]of[['readings','#8db4d6'],['motion','#c9b789'],['diagnostics','#86b8ad']]){x.strokeStyle=color;x.beginPath();let started=false;for(const p of points.filter(p=>p.stream===stream)){const px=50+p.elapsed_s/end*1030,py=190-(p[key]-lo)/(hi-lo)*155;if(started)x.lineTo(px,py);else x.moveTo(px,py);started=true;}x.stroke();x.fillStyle=color;x.fillText(stream,50+['readings','motion','diagnostics'].indexOf(stream)*140,215);}}document.getElementById('metric').onchange=draw;draw();</script>"""
        (self.folder / "report.html").write_text(page, encoding="utf-8")

    def close(self):
        for stream in (self.raw, self.browser, self.timeline):
            stream.close()


class Suite:
    def __init__(self, args, artifacts):
        self.args, self.artifacts = args, artifacts
        self.cache = Cache()
        self.phase = None
        self.warmup = Phase("warmup", time.monotonic(), self.cache, args.limits)
        self.completed = []
        self.cursors = dict(readings=0, motion=0)
        self.page = self.context = self.browser = self.playwright = None
        self.primary_paused = False
        self.api_enabled = args.api_only
        self.tasks = set()
        self.started_requests = {}
        self.inflight = {}
        self.background = []
        self.closing = False

    @property
    def current(self):
        return self.phase or self.warmup

    def ingest(self, stream, data, rtt_ms, status=200, error=None, role="primary", planned=False):
        try:
            json.dumps(data, allow_nan=False)
        except ValueError:
            data, error = {"invalid_numeric_body": repr(data)}, "Non-finite number in JSON"
        self.artifacts.api(self.current.name, stream, rtt_ms, status, data, error, role, planned)
        if role != "primary" or planned:
            return
        if error or status != 200:
            self.current.request_error(stream, error or f"HTTP {status}")
            return
        if not isinstance(data, dict):
            self.current.error("schema", f"{stream}: response is not a JSON object")
            return
        self.current.observe(stream, data, rtt_ms, time.monotonic())

    def spawn(self, coroutine):
        task = asyncio.create_task(coroutine)
        self.tasks.add(task)
        task.add_done_callback(self.tasks.discard)
        task.add_done_callback(self.task_error)
        return task

    def task_error(self, task):
        if not task.cancelled() and task.exception():
            self.current.error("schema", f"Capture task: {task.exception()}")

    def http(self, path):
        started = time.monotonic()
        try:
            with urlopen(Request(self.args.url+path, headers={"Cache-Control": "no-cache"}), timeout=self.args.request_ms/1000) as response:
                payload = response.read()
                status = response.status
            elapsed = (time.monotonic()-started)*1000
            try:
                return json.loads(payload), elapsed, status, None
            except (ValueError, UnicodeError) as exc:
                return None, elapsed, status, f"Invalid JSON: {exc}"
        except Exception as exc:
            return None, (time.monotonic()-started)*1000, getattr(exc, "code", 0), str(exc)

    async def api_loop(self, stream):
        interval = 5 if stream == "diagnostics" else 1
        while True:
            started = time.monotonic()
            if stream == "diagnostics" or self.api_enabled:
                path = PATHS[stream]
                if stream != "diagnostics":
                    path += "?since=" + str(self.cursors[stream])
                data, elapsed, status, error = await asyncio.to_thread(self.http, path)
                role = "primary" if stream == "diagnostics" or self.api_enabled else "draining-probe"
                self.ingest(stream, data, elapsed, status, error, role=role)
                if not error and status == 200 and stream != "diagnostics" and isinstance(data, dict):
                    if stream == "readings":
                        self.cursors[stream] = data.get("sequence", 0)
                    else:
                        # Respect the firmware's 20-row motion backfill cap.
                        rows = data.get("history", [])
                        if rows:
                            self.cursors[stream] = rows[-1]["sequence"]
            await asyncio.sleep(max(.01, interval-(time.monotonic()-started)))

    async def route(self, route):
        request = route.request
        if request.method not in ("GET", "OPTIONS"):
            await route.abort("blockedbyclient")
        elif self.primary_paused and urlparse(request.url).path in (PATHS["readings"], PATHS["motion"]):
            await route.abort("internetdisconnected")
        else:
            await route.continue_()

    def request_started(self, request):
        if self.closing:
            return
        stream = {v: k for k, v in PATHS.items()}.get(urlparse(request.url).path)
        if stream not in ("readings", "motion"):
            return
        self.started_requests[request] = (time.monotonic(), stream, self.primary_paused)
        self.inflight[stream] = self.inflight.get(stream, 0)+1
        if self.inflight[stream] > 1 and not self.primary_paused:
            self.current.error("browser", f"Overlapping {stream} requests in the primary dashboard")

    async def request_finished(self, request, failed=False):
        if self.closing:
            return
        item = self.started_requests.pop(request, None)
        if not item:
            return
        started, stream, planned = item
        self.inflight[stream] = max(0, self.inflight.get(stream, 1)-1)
        elapsed = (time.monotonic()-started)*1000
        if failed:
            self.ingest(stream, None, elapsed, 0, request.failure or "Request failed", planned=planned)
            return
        try:
            response = await request.response()
            timing = request.timing
            if timing.get("responseEnd", -1) >= 0:
                elapsed = timing["responseEnd"]
            data = await response.json()
            self.ingest(stream, data, elapsed, response.status, planned=planned)
        except Exception as exc:
            self.ingest(stream, None, elapsed, 0, str(exc), planned=planned)

    async def start(self):
        if not self.args.api_only:
            try:
                from playwright.async_api import async_playwright
            except ImportError as exc:
                raise RuntimeError("Browser testing requires: pip install -r tests/device-requirements.txt; use --api-only for an explicitly API-only run") from exc
            self.playwright = await async_playwright().start()
            launch = dict(headless=not self.args.headed, args=["--disable-background-timer-throttling", "--disable-renderer-backgrounding", "--disable-backgrounding-occluded-windows"])
            if self.args.browser != "chromium":
                launch["channel"] = self.args.browser
            self.browser = await self.playwright.chromium.launch(**launch)
            self.context = await self.browser.new_context(viewport=dict(width=1280, height=900))
            self.page = await self.context.new_page()
            await self.page.add_init_script(path=str(Path(__file__).with_name("browser_probe.js")))
            await self.page.route("**/*", self.route)
            self.page.on("request", self.request_started)
            self.page.on("requestfinished", lambda request: self.spawn(self.request_finished(request)))
            self.page.on("requestfailed", lambda request: self.spawn(self.request_finished(request, True)))
            self.page.on("pageerror", lambda error: self.browser_error("pageerror", str(error)))
            self.page.on("console", lambda message: self.browser_error("console", message.text) if message.type == "error" else None)
            await self.page.goto(self.args.url, wait_until="domcontentloaded", timeout=15000)
        self.background = [asyncio.create_task(self.api_loop(s)) for s in ("readings", "motion", "diagnostics")]
        if self.page:
            self.background.append(asyncio.create_task(self.probe_loop()))

    def browser_error(self, kind, message):
        if self.closing:
            return
        self.artifacts.ui(self.current.name, dict(kind=kind, message=message, planned=self.primary_paused))
        if not self.primary_paused:
            self.current.browser_errors.append(dict(kind=kind, message=message))

    async def probe(self):
        return await self.page.evaluate("() => window.__deviceTestSnapshot?.() || null")

    async def probe_loop(self):
        while True:
            try:
                started = time.monotonic()
                snapshot = await asyncio.wait_for(self.probe(), timeout=max(5, self.args.ui_stall_ms/1000+1))
                if (time.monotonic()-started)*1000 > self.args.ui_stall_ms and not self.primary_paused:
                    self.current.error("browser", "Browser main thread/probe was blocked longer than the display budget")
                if snapshot:
                    self.artifacts.ui(self.current.name, dict(kind="probe", **snapshot))
                    self.current.observe_ui(snapshot, time.monotonic())
            except Exception as exc:
                self.current.error("browser", f"Probe failed: {exc}")
            await asyncio.sleep(.25)

    async def wait(self, seconds):
        end = time.monotonic()+seconds
        while time.monotonic() < end:
            await asyncio.sleep(min(.5, max(0, end-time.monotonic())))

    def begin_phase(self, name, history_required=False):
        self.phase = Phase(name, time.monotonic(), self.cache, self.args.limits, bool(self.page), history_required)
        print(f"Running {name}...", flush=True)

    async def end_phase(self):
        output = self.phase.finish(time.monotonic())
        self.phase = None
        if self.page:
            try:
                await self.page.screenshot(path=str(self.artifacts.folder / (output["name"]+".png")), full_page=True, timeout=5000)
            except Exception as exc:
                output["screenshot_error"] = str(exc)
        self.completed.append(output)
        print(f"{output['name']}: {output['status']}", flush=True)
        for check in output["checks"]:
            if check["status"] != "PASS":
                print(f"  {check['status']}: {check['test']} - {check['detail']}", flush=True)
        self.phase = None
        return output

    async def extra_page(self, path):
        # Keep late events tied to their own phase; closing a page aborts its
        # outstanding polls and must not report a failure in the next phase.
        owner = self.current
        context = await self.browser.new_context(viewport=dict(width=1280, height=900))
        page = await context.new_page()
        async def read_only(route):
            if route.request.method not in ("GET", "OPTIONS"):
                await route.abort("blockedbyclient")
            else:
                await route.continue_()
        await page.route("**/*", read_only)
        counters = {"ok": 0, "errors": 0, "closed": False}
        async def record(request):
            endpoint = urlparse(request.url).path
            if endpoint not in ("/api/readings", "/api/motion", "/api/pickup/debug") or self.closing or counters["closed"]:
                return
            try:
                response = await request.response()
                data = await response.json()
                if counters["closed"]:
                    return
                counters["ok" if response.status == 200 else "errors"] += 1
                self.artifacts.api(owner.name, endpoint, max(0, request.timing.get("responseEnd", 0)), response.status, data, role="secondary")
                if response.status != 200:
                    owner.request_error("secondary", f"{endpoint}: HTTP {response.status}")
            except Exception as exc:
                if not counters["closed"]:
                    counters["errors"] += 1
                    owner.request_error("secondary", str(exc))
        def failed(request):
            if not counters["closed"] and not self.closing:
                owner.request_error("secondary", request.failure or "Request failed")
        page.on("requestfinished", lambda request: self.spawn(record(request)))
        page.on("requestfailed", failed)
        await page.goto(self.args.url+path, wait_until="domcontentloaded", timeout=15000)
        return context, counters

    async def recovery(self, seconds):
        self.begin_phase(f"recovery-{seconds:g}s")
        before = await self.probe()
        self.phase.planned_pause = self.primary_paused = True
        self.api_enabled = True
        await self.wait(seconds)
        targets = {s: self.cache.latest.get(s, {}).get("sequence", 0) for s in ("readings", "motion")}
        resumed = time.monotonic()
        self.phase.ui_grace_until = resumed+5
        self.phase.planned_pause = self.primary_paused = False
        self.api_enabled = False
        recovered = None
        while time.monotonic()-resumed < 8:
            snapshot = await self.probe()
            if snapshot and all(snapshot.get(s, {}).get("sequence", -1) >= targets[s] for s in targets):
                recovered = snapshot
                break
            await self.wait(.25)
        if recovered:
            for stream, key in (("readings", "environment_ranges"), ("motion", "motion_ranges")):
                first = before.get(stream, {}).get("sequence", 0)+1
                coverage = {i for p in recovered[key] for i in range(p["first"], p["last"]+1)}
                missing = sorted(set(range(first, targets[stream]+1))-coverage)
                if missing:
                    self.phase.error("browser", f"{stream}: backfill missing {missing[:20]}")
        else:
            self.phase.error("browser", "Dashboard did not catch up within eight seconds after polling resumed")
        latency = (time.monotonic()-resumed)*1000
        await self.wait(5)
        output = await self.end_phase()
        check = result("Polling recovery", "PASS" if recovered and latency <= 5000 else "FAIL", "Only browser requests were paused; independent API probes observed acquisition", pause_s=seconds, catchup_ms=latency)
        output["checks"].append(check)
        if check["status"] == "FAIL":
            output["status"] = "FAIL"

    async def run(self):
        print(f"Warmup ({self.args.warmup:g}s)...", flush=True)
        await self.start()
        await self.wait(self.args.warmup)
        if not all(s in self.cache.latest for s in ("readings", "motion")):
            raise RuntimeError("Warmup did not receive valid schemas from both dashboard streams; see captures")
        for test in self.args.tests:
            if test == "recovery":
                for pause in self.args.recovery_pauses:
                    await self.recovery(pause)
                continue
            self.begin_phase(test, test == "history")
            extra = None
            try:
                if test in ("concurrency", "debug"):
                    extra = await self.extra_page("/pickup" if test == "debug" else "/")
                seconds = self.args.soak_duration if test == "soak" else self.args.history_duration if test == "history" else self.args.duration
                await self.wait(seconds)
                if test == "history" and self.page:
                    snapshot = await self.probe()
                    if snapshot and (snapshot["environment_points"] > 51 or snapshot["environment_time_span_ms"] > 601000 or snapshot["motion_points"] > 605):
                        self.phase.error("history", "Browser history exceeded its bounded retention")
                if extra and extra[1]["ok"] < max(1, seconds*.5):
                    self.phase.error("transport", "Secondary page did not generate the expected live traffic")
                await self.end_phase()
            finally:
                if extra:
                    extra[1]["closed"] = True
                    await extra[0].close()

    async def close(self):
        self.closing = True
        for task in self.background:
            task.cancel()
        await asyncio.gather(*self.background, return_exceptions=True)
        if self.tasks:
            await asyncio.gather(*list(self.tasks), return_exceptions=True)
        if self.browser:
            await self.browser.close()
        if self.playwright:
            await self.playwright.stop()


def arguments(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--url", required=True, help="Device origin, e.g. http://192.168.50.83")
    parser.add_argument("--tests", default="baseline", help="Comma-separated baseline,history,recovery,concurrency,debug,soak, or all")
    parser.add_argument("--duration", type=float, default=60, help="Baseline/concurrency/debug duration in seconds")
    parser.add_argument("--warmup", type=float, default=10)
    parser.add_argument("--history-duration", type=float, default=120)
    parser.add_argument("--soak-duration", type=float, default=900)
    parser.add_argument("--recovery-pauses", default="5,20,40")
    parser.add_argument("--api-only", action="store_true", help="Explicitly exclude browser/rendering tests")
    parser.add_argument("--browser", choices=("chrome", "msedge", "chromium"), default="chrome")
    parser.add_argument("--headed", action="store_true")
    parser.add_argument("--output", type=Path, help="New output directory; existing directories are never overwritten")
    parser.add_argument("--condition", default="", help="Label manual conditions such as 'phone connected over BLE'")
    parser.add_argument("--p95-ms", type=float, default=500)
    parser.add_argument("--request-ms", type=float, default=2000)
    parser.add_argument("--freshness-ms", type=float, default=2500)
    parser.add_argument("--ui-stall-ms", type=float, default=3000)
    parser.add_argument("--sample-tolerance", type=float, default=.01)
    args = parser.parse_args(argv)
    parsed = urlparse(args.url)
    if parsed.scheme not in ("http", "https") or not parsed.netloc or parsed.path not in ("", "/") or parsed.query or parsed.fragment or parsed.username:
        parser.error("--url must be an HTTP(S) origin without credentials, path or query")
    args.url = args.url.rstrip("/")
    allowed = ["baseline", "history", "recovery", "concurrency", "debug", "soak"]
    args.tests = allowed if args.tests == "all" else args.tests.split(",")
    if not args.tests or any(t not in allowed for t in args.tests):
        parser.error("Unknown test name")
    if args.api_only and any(t in args.tests for t in ("recovery", "concurrency", "debug")):
        parser.error("recovery/concurrency/debug require the real browser; omit --api-only")
    try:
        args.recovery_pauses = [float(p) for p in args.recovery_pauses.split(",")]
    except ValueError:
        parser.error("Invalid recovery pauses")
    if any(not 0 < n < 86400 for n in [args.duration, args.warmup, args.history_duration, args.soak_duration, *args.recovery_pauses]):
        parser.error("Durations must be positive and below 24 hours")
    if any(not 0 < n < 100000 for n in (args.p95_ms, args.request_ms, args.freshness_ms, args.ui_stall_ms)) or not 0 < args.sample_tolerance < 1:
        parser.error("Invalid thresholds")
    args.limits = Limits(args.p95_ms, args.request_ms, args.freshness_ms, args.ui_stall_ms, args.sample_tolerance)
    if args.output is None:
        stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
        args.output = Path("test-results/hardware") / f"{stamp}-{uuid.uuid4().hex[:6]}"
    return args


async def execute(args):
    artifacts = Artifacts(args.output)
    suite = Suite(args, artifacts)
    error = None
    try:
        await suite.run()
    except (Exception, asyncio.CancelledError) as exc:
        error = f"{type(exc).__name__}: {exc}"
        print(error, file=sys.stderr, flush=True)
        if suite.phase:
            partial = suite.phase.finish(time.monotonic())
            partial["status"] = "ERROR"
            suite.completed.append(partial)
    finally:
        try:
            await suite.close()
        except Exception as exc:
            error = (error or "") + f" Cleanup: {exc}"
        statuses = [p["status"] for p in suite.completed]
        status = "ERROR" if error else "FAIL" if "FAIL" in statuses else "INCONCLUSIVE" if not statuses or "INCONCLUSIVE" in statuses else "WARN" if "WARN" in statuses else "PASS"
        report = dict(status=status, url=args.url, scope=("API only" if args.api_only else "Real browser and dashboard APIs") + ("; short exploratory run (not a one-minute baseline)" if args.duration < 60 else ""),
                      condition=args.condition, thresholds=vars(args.limits), phases=suite.completed, error=error,
                      run=dict(started_utc=artifacts.started_utc, finished_utc=datetime.now(timezone.utc).isoformat(),
                               python=sys.version.split()[0], browser=None if args.api_only else args.browser,
                               headed=args.headed, profiles=args.tests, warmup_s=args.warmup),
                      notes=["One-way latency is not measured.", "Report counts are firmware telemetry, not an independent measurement of physical ADC activity.",
                             "Older firmware may omit historical motion sample counts; missing counts are inconclusive, never assumed to be 100.",
                             "Old coarse buckets without captured fine constituents cannot be independently recomputed.",
                             "No device settings, calibration, firmware, Wi-Fi or BLE configuration is changed."])
        artifacts.report(report)
        artifacts.close()
        print(f"{status}: {args.output.resolve() / 'report.html'}", flush=True)
    return 1 if status == "FAIL" else 2 if status in ("ERROR", "INCONCLUSIVE") else 0


def main(argv=None):
    args = arguments(argv)
    try:
        return asyncio.run(execute(args))
    except KeyboardInterrupt:
        return 130


if __name__ == "__main__":
    raise SystemExit(main())
