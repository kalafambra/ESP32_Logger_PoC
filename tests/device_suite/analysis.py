"""Analyze observations without confusing repeated polls with duplicate sensor reports."""
from dataclasses import dataclass, asdict
import math
import statistics


def percentile(values, fraction):
    if not values:
        return None
    values = sorted(values)
    index = (len(values) - 1) * fraction
    low = int(index)
    return values[low] + (values[math.ceil(index)] - values[low]) * (index - low)


@dataclass
class Limits:
    p95_ms: float = 500
    request_ms: float = 2000
    freshness_ms: float = 2500
    ui_stall_ms: float = 3000
    sample_tolerance: float = .01


def result(name, status, detail, **metrics):
    return dict(test=name, status=status, detail=detail, metrics=metrics)


class Cache:
    """Keep bounded source evidence across warmup/phases for independent averaging checks."""
    def __init__(self):
        self.latest = {}
        self.fine = {}
        self.motion = {}

    def trim(self, sequence):
        self.fine = {k: v for k, v in self.fine.items() if k >= sequence - 1200}
        if self.motion:
            newest = max(self.motion)
            self.motion = {k: v for k, v in self.motion.items() if k >= newest - 1200}


class Phase:
    def __init__(self, name, started, cache, limits=None, browser=False, history_required=False):
        self.name, self.started, self.cache = name, started, cache
        self.limits = limits or Limits()
        self.browser, self.history_required = browser, history_required
        self.browser_history = False
        self.seeds = {k: dict(v) for k, v in cache.latest.items()}
        self.last = {k: dict(v) for k, v in cache.latest.items()}
        self.coverage = {"readings": set(), "motion": set()}
        self.fine_seen, self.motion_seen = {}, {}
        self.errors = []
        self.latencies = {"readings": [], "motion": [], "diagnostics": []}
        self.age = {"readings": [], "motion": []}
        self.age_upper = {"readings": [], "motion": []}
        self.memory, self.ui = [], []
        self.restarts = 0
        self.bucket_checks = self.complete_bucket_checks = 0
        self.consistency_checks = 0
        self._checked_buckets = set()
        self.browser_errors = []
        self.planned_pause = False
        self.ui_grace_until = started
        self.last_advance = {k: started for k in ("readings", "motion")}
        self.max_source_stall = {k: 0 for k in self.last_advance}

    def error(self, category, detail):
        item = dict(category=category, detail=detail)
        if item not in self.errors:
            self.errors.append(item)

    def request_error(self, stream, detail, planned=False):
        if not planned:
            self.error("transport", f"{stream}: {detail}")

    def observe(self, stream, data, rtt_ms, at):
        if stream == "diagnostics":
            self.latencies[stream].append(rtt_ms)
            for key in ("free_heap", "largest_free_block"):
                if not isinstance(data.get(key), (int, float)):
                    self.error("schema", f"Diagnostics missing {key}")
                    return
            self.memory.append(dict(at=at, free=data["free_heap"], largest=data["largest_free_block"], ble_ready=data.get("ble_ready")))
            return
        if stream not in self.coverage:
            return
        self.latencies[stream].append(rtt_ms)
        if data.get("history_mode") == "browser":
            self.browser_history = True
        try:
            body = data if stream == "readings" else data["motion"]
            seq, uptime = int(body["sequence"]), int(data["uptime_ms"])
            age = body["sample_age_ms"]
            if not isinstance(age, (int, float)) or age < 0:
                raise ValueError("invalid/missing sample_age_ms")
            history = data["history"]
            if not isinstance(history, list):
                raise ValueError("missing history array")
        except (KeyError, TypeError, ValueError) as exc:
            self.error("schema", f"{stream}: {exc}")
            return
        previous = self.last.get(stream)
        if previous and uptime < previous["uptime"]:
            self.restarts += 1
            self.error("continuity", f"{stream}: uptime moved backwards ({previous['uptime']} -> {uptime}); reboot or timer wrap")
            self.cache.fine.clear()
            self.cache.motion.clear()
        elif previous and seq < previous["sequence"]:
            self.error("continuity", f"{stream}: out-of-order sequence {seq} after {previous['sequence']}")
        if not previous or seq > previous["sequence"]:
            self.max_source_stall[stream] = max(self.max_source_stall[stream], (at - self.last_advance[stream]) * 1000)
            self.last_advance[stream] = at
        else:
            self.max_source_stall[stream] = max(self.max_source_stall[stream], (at - self.last_advance[stream]) * 1000)
        current = dict(sequence=seq, uptime=uptime, sample_ms=uptime-age, at=at, overflows=body.get("fifo_overflows", 0))
        self.last[stream] = self.cache.latest[stream] = current
        self.age[stream].append(age)
        self.age_upper[stream].append(age + rtt_ms)
        if age > self.limits.freshness_ms:
            self.error("freshness", f"{stream}: source report age {age} ms")
        seen = set()
        seed = self.seeds.get(stream, {}).get("sequence", seq)
        for point in history:
            try:
                end = int(point["sequence"])
                first = int(point.get("first_sequence", end))
                revision = point.get("revision", end)
                identity = (first, end, point.get("resolution_ms", 1000), revision)
                if identity in seen:
                    self.error("continuity", f"{stream}: duplicate history row {identity}")
                seen.add(identity)
                if first > end or end - first > 1000 or end > seq:
                    raise ValueError("invalid history sequence range")
                self.coverage[stream].update(range(max(first, seed + 1), end + 1))
                if stream == "readings":
                    if point.get("resolution_ms", 1000) == 1000:
                        signature = {k: point.get(k) for k in ("ok", "sample_count", "temperature_c", "humidity_pct", "pressure_hpa", "mpu_temperature_c")}
                        old = self.cache.fine.get(end)
                        if old and old != signature:
                            self.error("integrity", f"Environmental report {end} changed after publication")
                        self.cache.fine[end] = signature
                        if end > seed:
                            self.fine_seen[end] = signature
                            if not point.get("ok") or point.get("sample_count") != 5:
                                self.error("sensor", f"BME history report {end} is incomplete/invalid")
                            if point.get("ok"):
                                for channel in ("temperature_c", "humidity_pct", "pressure_hpa"):
                                    values = point.get(channel)
                                    if not isinstance(values, list) or len(values) != 3 or not all(isinstance(v, (int, float)) and math.isfinite(v) for v in values) or not values[1]-.002 <= values[0] <= values[2]+.002:
                                        self.error("integrity", f"BME history report {end}: invalid {channel} mean/extrema")
                    else:
                        self.check_bucket(point)
                elif end > seed:
                    if not point.get("ok"):
                        self.error("sensor", f"MPU history report {end} is invalid: {point.get('error', '')}")
                    # New firmware retains real counts in backfill. Older
                    # firmware remains inconclusive rather than assuming 100.
                    if isinstance(point.get("sample_count"), (int, float)):
                        prior = self.motion_seen.get(end)
                        if prior and prior.get("count") != point["sample_count"]:
                            self.error("integrity", f"Motion history report {end}: conflicting sample count")
                        elif not prior:
                            self.motion_seen[end] = dict(count=point["sample_count"])
            except (KeyError, TypeError, ValueError) as exc:
                self.error("schema", f"{stream} history: {exc}")
        if seq > seed:
            self.coverage[stream].add(seq)
        if stream == "readings":
            if seq > seed and (not data.get("sensor_ok") or data.get("sample_count") != 5):
                self.error("sensor", f"BME report {seq}: n={data.get('sample_count')}, {data.get('error') or 'invalid report'}")
            if not data.get("history_self_tests_passed", False):
                self.error("sensor", "Firmware history self-tests not passing or not exposed")
            self.check_motion(data, at)
        else:
            self.check_motion(data, at)
            baseline = self.seeds.get(stream, {}).get("overflows", body.get("fifo_overflows", 0))
            if body.get("fifo_overflows", 0) > baseline:
                self.error("sensor", f"MPU FIFO overflow count increased: {baseline} -> {body['fifo_overflows']}")
        self.cache.trim(seq if stream == "readings" else self.cache.latest.get("readings", {}).get("sequence", 0))

    def check_motion(self, data, at):
        m = data.get("motion", {})
        if not isinstance(m, dict) or not isinstance(m.get("sequence"), (int, float)):
            self.error("schema", "Missing motion status")
            return
        seq = int(m["sequence"])
        signature = dict(ok=m.get("ok"), count=m.get("sample_count"), acceleration=m.get("acceleration_m_s2"),
                         gyro=m.get("gyroscope_deg_s"))
        prior = self.cache.motion.get(seq)
        if prior:
            self.consistency_checks += 1
            if prior != signature:
                self.error("integrity", f"Motion report {seq} disagrees across responses/endpoints")
        self.cache.motion[seq] = signature
        seed = self.seeds.get("motion", {}).get("sequence", seq)
        if seq <= seed:
            return
        historical = self.motion_seen.get(seq)
        if historical and historical.get("count") != signature["count"]:
            self.error("integrity", f"Motion report {seq}: live/history sample counts disagree")
        self.motion_seen[seq] = dict(**signature, sample_ms=data["uptime_ms"]-m.get("sample_age_ms", 0))
        if not m.get("ok"):
            self.error("sensor", f"MPU report {seq}: {m.get('error') or 'invalid report'}")
        if not m.get("temperature_ok") or not isinstance(m.get("temperature_c"), (int, float)) or not math.isfinite(m["temperature_c"]):
            self.error("sensor", f"MPU temperature: {m.get('temperature_error') or 'unavailable'}")

    def check_bucket(self, point):
        key = (point["first_sequence"], point["sequence"], point.get("revision"))
        if key in self._checked_buckets:
            return
        source = [self.cache.fine.get(i) for i in range(key[0], key[1]+1)]
        if not source or any(s is None for s in source):
            return  # No fabricated source data for old buckets captured before this run.
        self._checked_buckets.add(key)
        self.bucket_checks += 1
        self.complete_bucket_checks += point.get("report_count") == 30
        if point.get("report_count") != len(source):
            self.error("history", f"Bucket {key}: report_count mismatch")
        valid = all(s["ok"] for s in source)
        if bool(point.get("ok")) != valid:
            self.error("history", f"Bucket {key}: sensor gap was lost")
        n = sum(s["sample_count"] for s in source)
        if n != point.get("sample_count"):
            self.error("history", f"Bucket {key}: sample_count mismatch")
        if valid and n:
            for field in ("temperature_c", "humidity_pct", "pressure_hpa"):
                try:
                    expected = sum(s[field][0]*s["sample_count"] for s in source)/n
                    values = point[field]
                    if abs(expected-values[0]) > .002 or min(s[field][1] for s in source) != values[1] or max(s[field][2] for s in source) != values[2]:
                        self.error("history", f"Bucket {key}: {field} mean/extrema mismatch")
                except (TypeError, IndexError):
                    self.error("history", f"Bucket {key}: invalid channel data")
        temperatures = [s.get("mpu_temperature_c") for s in source]
        target = point.get("mpu_temperature_c")
        if all(isinstance(t, (int, float)) for t in temperatures):
            if target is None or abs(sum(temperatures)/len(temperatures)-target) > .002:
                self.error("history", f"Bucket {key}: MPU temperature average mismatch")
        elif target is not None:
            self.error("history", f"Bucket {key}: MPU temperature gap was lost")

    def observe_ui(self, snapshot, at):
        self.ui.append(snapshot)
        if self.planned_pause or at < self.ui_grace_until:
            return
        for stream in ("readings", "motion"):
            item = snapshot.get(stream)
            if not item:
                continue
            if item["pending_ms"] > self.limits.ui_stall_ms:
                self.error("browser", f"{stream}: received data did not reach graph/model for {item['pending_ms']:.0f} ms")
            if item["advance_age_ms"] > self.limits.ui_stall_ms:
                self.error("browser", f"{stream}: displayed sequence stalled for {item['advance_age_ms']:.0f} ms")
            if item["draw_age_ms"] > self.limits.ui_stall_ms:
                self.error("browser", f"{stream}: canvas redraw stalled")
            if item.get("render_delay_ms") is not None and item["render_delay_ms"] > self.limits.ui_stall_ms:
                self.error("browser", f"{stream}: receive-to-draw latency exceeded the display budget")
            if item.get("value_mismatch"):
                self.error("browser", f"{stream}: displayed values disagree with consumed response")

    def finish(self, ended):
        checks = []
        def category(name, groups, detail, **metrics):
            problems = [e for e in self.errors if e["category"] in groups]
            checks.append(result(name, "FAIL" if problems else "PASS", detail, problems=problems, **metrics))
        for stream in ("readings", "motion"):
            seed, last = self.seeds.get(stream), self.last.get(stream)
            if not seed or not last or last["sequence"] <= seed["sequence"]:
                checks.append(result(f"{stream} continuity", "FAIL", "No advancing reports observed"))
                continue
            span = last["sequence"]-seed["sequence"]
            missing = sorted(set(range(seed["sequence"]+1, min(last["sequence"], seed["sequence"]+10000)+1))-self.coverage[stream]) if span > 0 else []
            if missing:
                self.error("continuity", f"{stream}: missing source sequences {missing[:20]}")
            if self.max_source_stall[stream] > self.limits.ui_stall_ms:
                self.error("continuity", f"{stream}: no newly observed source sequence for {self.max_source_stall[stream]:.0f} ms (delivery or acquisition delay)")
            device_span = last["uptime"]-seed["uptime"]
            rate = span/(device_span/1000) if device_span > 0 else 0
            rate_bad = rate < .95 or rate > 1.05
            if device_span < 30000:  # Short smoke runs have a one-report phase-boundary uncertainty.
                rate_bad = abs(span-device_span/1000) > 1.5
            if rate_bad:
                self.error("continuity", f"{stream}: publication rate {rate:.3f} reports/s outside 0.95..1.05")
            checks.append(result(f"{stream} continuity", "FAIL" if missing or self.restarts or rate_bad or self.max_source_stall[stream] > self.limits.ui_stall_ms else "PASS",
                                 "Repeated polls are allowed; missing source ranges are not", reports=span, missing=missing[:100], rate_hz=rate, max_stall_ms=self.max_source_stall[stream]))
        category("Data validity", {"sensor", "schema", "integrity", "continuity"}, "Source errors and conflicting immutable records")
        for stream, values in self.latencies.items():
            if not values:
                checks.append(result(f"{stream} latency", "INCONCLUSIVE", "No completed requests"))
                continue
            p95 = percentile(values, .95)
            checks.append(result(f"{stream} latency", "FAIL" if p95 > self.limits.p95_ms or max(values) > self.limits.request_ms else "PASS",
                                 "Round-trip including response body; not one-way latency", median_ms=statistics.median(values), p95_ms=p95, max_ms=max(values), requests=len(values)))
        category("Transport errors", {"transport"}, "Unexpected timeouts, HTTP and browser request failures")
        category("Source freshness", {"freshness"}, "Server-reported age; arrival upper bound also recorded",
                 source_age_max_ms={s: max(v) if v else None for s, v in self.age.items()},
                 arrival_age_upper_max_ms={s: max(v) if v else None for s, v in self.age_upper.items()})
        if checks[-1]["status"] == "PASS" and any(v and max(v) > self.limits.freshness_ms for v in self.age_upper.values()):
            checks[-1]["status"] = "WARN"
            checks[-1]["detail"] = "Source age is within limit, but the arrival upper bound exceeds it; true one-way timing is unknown"
        seeds, last = self.seeds.get("motion"), self.last.get("motion")
        if seeds and last and 0 < last["sequence"]-seeds["sequence"] <= 10000:
            required = range(seeds["sequence"]+1, last["sequence"]+1)
            missing_counts = [s for s in required if s not in self.motion_seen or not isinstance(self.motion_seen[s].get("count"), (int, float))]
            if missing_counts or self.restarts:
                checks.append(result("MPU sample accounting", "INCONCLUSIVE", "Required sample counts were not captured or exposed in backfill; no counts are invented", missing_counts=missing_counts[:100]))
            else:
                count = sum(self.motion_seen[s]["count"] for s in required)
                expected = (last["sample_ms"]-seeds["sample_ms"])*.1
                deviation = abs(count-expected)/expected if expected > 0 else 1
                counts = [self.motion_seen[s]["count"] for s in required]
                checks.append(result("MPU sample accounting", "PASS" if deviation <= self.limits.sample_tolerance else "FAIL", "100 Hz processing counts over complete report windows", actual=count, expected=expected, relative_error=deviation, per_report_min=min(counts), per_report_max=max(counts)))
        else:
            checks.append(result("MPU sample accounting", "INCONCLUSIVE", "No complete report interval"))
        bucket_status = "FAIL" if any(e["category"] == "history" for e in self.errors) else "PASS" if self.bucket_checks else "INCONCLUSIVE"
        if self.browser_history and bucket_status != "FAIL":
            bucket_status = "PASS"
        if self.history_required and not self.browser_history and not self.complete_bucket_checks and bucket_status != "FAIL":
            bucket_status = "INCONCLUSIVE"
        checks.append(result("History averages", bucket_status, "Browser-owned plots; no device coarse buckets" if self.browser_history else "Only buckets with captured constituent reports can be independently checked", checked=self.bucket_checks, full_30_second_buckets=self.complete_bucket_checks, required=self.history_required,
                             problems=[e for e in self.errors if e["category"] == "history"]))
        if self.browser:
            category("Browser display", {"browser"}, "Sequence progress and redraws, not changing sensor values", probes=len(self.ui))
            for stream in ("readings", "motion"):
                delays = {p[stream].get("draw_at"): p[stream]["render_delay_ms"] for p in self.ui if stream in p and p[stream].get("render_delay_ms") is not None}
                checks[-1]["metrics"][stream+"_receive_to_draw_p95_ms"] = percentile(list(delays.values()), .95)
            if not all(any(stream in p for p in self.ui) for stream in ("readings", "motion")):
                checks[-1]["status"] = "INCONCLUSIVE"
            if self.browser_errors:
                checks[-1]["status"] = "FAIL"
                checks[-1]["metrics"]["console_errors"] = self.browser_errors
        if len(self.memory) >= 3:
            def trend(key):
                n = max(1, len(self.memory)//4)
                return statistics.median(p[key] for p in self.memory[-n:])-statistics.median(p[key] for p in self.memory[:n])
            free_drop, block_drop = trend("free"), trend("largest")
            sustained = len(self.memory) >= 12 and free_drop < -4096 and block_drop < -4096
            checks.append(result("Memory trend", "WARN" if sustained else "PASS", "A sustained decline is a leak/fragmentation signal, not proof of a leak", minimum_free=min(p["free"] for p in self.memory), minimum_largest_block=min(p["largest"] for p in self.memory), free_change=free_drop, largest_block_change=block_drop, observations=len(self.memory)))
        else:
            checks.append(result("Memory trend", "INCONCLUSIVE", "Too few memory observations"))
        required = [c for c in checks if c["test"] != "History averages" or self.history_required or c["status"] == "FAIL"]
        status = "FAIL" if any(c["status"] == "FAIL" for c in checks) else "INCONCLUSIVE" if any(c["status"] == "INCONCLUSIVE" for c in required) else "WARN" if any(c["status"] == "WARN" for c in checks) else "PASS"
        return dict(name=self.name, duration_s=ended-self.started, status=status, thresholds=asdict(self.limits), checks=checks)
