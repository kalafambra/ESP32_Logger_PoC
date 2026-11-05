"""Offline fault-injection tests for the external test suite (no board required)."""
import copy
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest

from device_suite.analysis import Cache, Phase
from device_suite.runner import arguments, Artifacts, Suite


def fine(seq, value=23):
    return dict(sequence=seq, first_sequence=seq, resolution_ms=1000, revision=seq,
                report_count=1, sample_count=5, ok=True, age_ms=20,
                temperature_c=[value, value-1, value+1], humidity_pct=[45,44,46],
                pressure_hpa=[1000,999,1001], mpu_temperature_c=31.5)


def packet(seq):
    return dict(sequence=seq, uptime_ms=seq*1000, sample_age_ms=20, sample_count=5,
                sensor_ok=True, history_self_tests_passed=True, temperature_c=23,
                humidity_pct=45, pressure_hpa=1000, error="", history=[fine(seq)],
                motion=dict(sequence=seq, sample_age_ms=20, sample_count=100, ok=True,
                            error="", fifo_overflows=0, acceleration_m_s2=[0,0,9.81],
                            gyroscope_deg_s=[0,0,0], temperature_ok=True, temperature_c=31.5))


def motion_packet(p):
    return dict(uptime_ms=p["uptime_ms"], motion=copy.deepcopy(p["motion"]),
                history=[dict(sequence=p["sequence"], age_ms=20, ok=True, a=[0,0,9.81])])


def phase(browser=False):
    cache=Cache()
    for stream in ("readings", "motion"):
        cache.latest[stream]=dict(sequence=100, uptime=100000, sample_ms=99980, at=0, overflows=0)
    return Phase("baseline", 0, cache, browser=browser)


def run_window(transform=None, browser=False):
    p=phase(browser)
    for i in range(1,61):
        env=packet(100+i)
        if transform:
            env=transform(env,i)
        if env is not None:
            p.observe("readings", env, 20, i)
            p.observe("motion", motion_packet(env), 20, i+.01)
        if i%5==0:
            p.observe("diagnostics", dict(free_heap=80000,largest_free_block=60000,ble_ready=True),15,i)
        if browser:
            stream=dict(sequence=100+i, pending_ms=0, advance_age_ms=0, draw_age_ms=0,
                        render_delay_ms=10, draw_at=i*1000, value_mismatch=False)
            p.observe_ui(dict(readings=stream,motion=stream),i)
    return p,p.finish(60)


def check(report, name):
    return next(c for c in report["checks"] if c["test"]==name)


class AnalyzerTests(unittest.TestCase):
    def test_constant_values_are_continuous_and_not_a_browser_freeze(self):
        _, report=run_window(browser=True)
        self.assertEqual(report["status"],"PASS")
        self.assertEqual(check(report,"MPU sample accounting")["metrics"]["actual"],6000)

    def test_expected_repeated_polls_are_not_duplicate_reports(self):
        p,_=run_window()
        p.observe("readings",packet(160),20,60.1)
        self.assertFalse([e for e in p.errors if e["category"]=="continuity"])

    def test_missing_report_is_detected(self):
        _,report=run_window(lambda p,i:None if i==30 else p)
        self.assertEqual(check(report,"readings continuity")["metrics"]["missing"],[130])
        self.assertEqual(report["status"],"FAIL")

    def test_backfill_restores_coverage_but_cannot_invent_motion_counts(self):
        p=phase()
        for i in range(1,61):
            if i==30:continue
            env=packet(100+i);motion=motion_packet(env)
            if i==31:
                env["history"].insert(0,fine(130))
                motion["history"].insert(0,dict(sequence=130,ok=True,a=[0,0,9.81]))
            p.observe("readings",env,20,i);p.observe("motion",motion,20,i+.01)
        report=p.finish(60)
        self.assertEqual(check(report,"readings continuity")["status"],"PASS")
        self.assertEqual(check(report,"motion continuity")["status"],"PASS")
        self.assertEqual(check(report,"MPU sample accounting")["status"],"INCONCLUSIVE")

    def test_counted_backfill_verifies_missed_live_windows(self):
        for recovered_count in (100, 0):
            with self.subTest(count=recovered_count):
                p=phase()
                for i in range(1,61):
                    if i==30:continue
                    env=packet(100+i);motion=motion_packet(env)
                    if i==31:
                        env["history"].insert(0,fine(130))
                        motion["history"].insert(0,dict(sequence=130,ok=True,a=[0,0,9.81],sample_count=recovered_count,error=""))
                    p.observe("readings",env,20,i);p.observe("motion",motion,20,i+.01)
                report=p.finish(60)
                accounting=check(report,"MPU sample accounting")
                self.assertEqual(accounting["status"],"PASS" if recovered_count==100 else "FAIL")
                self.assertEqual(accounting["metrics"]["actual"],5900+recovered_count)

    def test_history_count_conflicting_with_live_count_is_rejected(self):
        p=phase();env=packet(101);motion=motion_packet(env)
        motion["history"][0]["sample_count"]=80
        p.observe("motion",motion,20,1)
        self.assertTrue(any(e["category"]=="integrity" for e in p.errors))

    def test_invalid_bme_window_is_not_hidden_by_next_good_report(self):
        def fault(p,i):
            if i==10:p["sensor_ok"]=False;p["sample_count"]=4;p["error"]="Incomplete window"
            return p
        _,report=run_window(fault)
        self.assertEqual(check(report,"Data validity")["status"],"FAIL")

    def test_lost_mpu_samples_fail_total_count(self):
        def fault(p,i):p["motion"]["sample_count"]=90;return p
        _,report=run_window(fault)
        self.assertEqual(check(report,"MPU sample accounting")["status"],"FAIL")

    def test_fifo_overflow_is_detected(self):
        def fault(p,i):p["motion"]["fifo_overflows"]=int(i>20);return p
        _,report=run_window(fault)
        self.assertEqual(check(report,"Data validity")["status"],"FAIL")

    def test_fast_http_with_frozen_sequence_still_fails(self):
        def fault(p,i):p["sequence"]=101;p["history"]=[fine(101)];p["motion"]["sequence"]=101;return p
        _,report=run_window(fault)
        self.assertEqual(check(report,"readings continuity")["status"],"FAIL")

    def test_reboot_and_regression_are_detected(self):
        p=phase();p.observe("readings",packet(101),20,1);p.observe("readings",packet(1),20,2)
        self.assertEqual(p.restarts,1)
        self.assertTrue(any(e["category"]=="continuity" for e in p.errors))

    def test_latency_and_age_have_separate_results(self):
        p,_=run_window();p.latencies["readings"]=[800]*60
        p.observe("readings",dict(packet(160),sample_age_ms=3000),20,60.1)
        report=p.finish(60.1)
        self.assertEqual(check(report,"readings latency")["status"],"FAIL")
        self.assertEqual(check(report,"Source freshness")["status"],"FAIL")

    def test_arrival_age_upper_bound_is_a_warning_not_fake_one_way_latency(self):
        p,_=run_window();p.age_upper["readings"].append(2600)
        self.assertEqual(check(p.finish(60),"Source freshness")["status"],"WARN")

    def test_conflicting_cross_endpoint_motion_is_detected(self):
        p=phase();a=packet(101);p.observe("readings",a,20,1)
        b=motion_packet(a);b["motion"]["acceleration_m_s2"]=[1,0,9.81]
        p.observe("motion",b,20,1.01)
        self.assertTrue(any(e["category"]=="integrity" for e in p.errors))

    def test_averaging_extrema_and_independent_mpu_gap(self):
        p=phase()
        for i in range(1,31):p.cache.fine[i]={k:v for k,v in fine(i,float(i)).items() if k in ("ok","sample_count","temperature_c","humidity_pct","pressure_hpa","mpu_temperature_c")}
        bucket=dict(fine(30),first_sequence=1,resolution_ms=30000,revision=60,report_count=30,sample_count=150,temperature_c=[15.5,0,31])
        p.check_bucket(bucket)
        self.assertFalse(p.errors);self.assertEqual(p.complete_bucket_checks,1)
        broken=dict(bucket,revision=61,temperature_c=[18,0,31]);p.check_bucket(broken)
        self.assertTrue(any(e["category"]=="history" for e in p.errors))
        p.errors.clear();p.cache.fine[2]["mpu_temperature_c"]=None
        p.check_bucket(dict(bucket,revision=62))
        self.assertTrue(any("MPU temperature gap" in e["detail"] for e in p.errors))

    def test_uncaptured_coarse_source_is_inconclusive(self):
        p=phase();p.history_required=True
        p.check_bucket(dict(fine(30),first_sequence=1,resolution_ms=30000,revision=60,report_count=30,sample_count=150))
        self.assertEqual(check(p.finish(60),"History averages")["status"],"INCONCLUSIVE")

    def test_browser_stall_and_late_render_are_detected(self):
        p=phase(browser=True)
        p.observe_ui(dict(readings=dict(pending_ms=0,advance_age_ms=0,draw_age_ms=0,render_delay_ms=4000,value_mismatch=False)),4)
        self.assertTrue(any(e["category"]=="browser" for e in p.errors))

    def test_deliberate_pause_suppresses_expected_ui_stall(self):
        p=phase(browser=True);p.planned_pause=True
        p.observe_ui(dict(readings=dict(pending_ms=10000,advance_age_ms=10000,draw_age_ms=0)),10)
        self.assertFalse(p.errors)

    def test_missing_browser_instrumentation_is_inconclusive(self):
        p,_=run_window(browser=True);p.ui=[{}]
        self.assertEqual(check(p.finish(60),"Browser display")["status"],"INCONCLUSIVE")

    def test_memory_trend_ignores_single_temporary_dip(self):
        p,_=run_window();p.memory[5]["free"]=10000
        self.assertEqual(check(p.finish(60),"Memory trend")["status"],"PASS")
        for i,s in enumerate(p.memory):s["free"]-=i*1000;s["largest"]-=i*1000
        self.assertEqual(check(p.finish(60),"Memory trend")["status"],"WARN")

    def test_default_and_selectable_profiles(self):
        args=arguments(["--url","http://127.0.0.1"])
        self.assertEqual(args.duration,60);self.assertEqual(args.tests,["baseline"])
        args=arguments(["--url","http://127.0.0.1","--tests","all"])
        self.assertIn("recovery",args.tests);self.assertEqual(args.soak_duration,900)


class CaptureTests(unittest.TestCase):
    def test_reports_preserve_raw_evidence_and_escape_html(self):
        with tempfile.TemporaryDirectory() as folder:
            artifacts=Artifacts(Path(folder)/"run")
            try:
                artifacts.api("baseline","readings",25,200,packet(101))
                report=dict(status="PASS",scope="<script>bad</script>",url="http://localhost",phases=[],error=None)
                artifacts.report(report)
            finally:
                artifacts.close()
            raw=json.loads((artifacts.folder/"raw-api.jsonl").read_text())
            self.assertEqual(raw["data"]["sequence"],101)
            self.assertEqual(raw["rtt_ms"],25)
            self.assertEqual(json.loads((artifacts.folder/"report.json").read_text()),report)
            page=(artifacts.folder/"report.html").read_text(encoding="utf-8")
            self.assertIn("&lt;script&gt;bad&lt;/script&gt;",page)
            self.assertNotIn("<script>bad</script>",page)
            self.assertIn("rtt_ms",(artifacts.folder/"timeline.csv").read_text())
            with self.assertRaises(FileExistsError):
                Artifacts(artifacts.folder)

    def test_nonfinite_api_response_is_saved_as_failure_not_lost(self):
        with tempfile.TemporaryDirectory() as folder:
            artifacts=Artifacts(Path(folder)/"run")
            try:
                args=arguments(["--url","http://127.0.0.1","--api-only"])
                suite=Suite(args,artifacts)
                suite.ingest("readings",dict(temperature_c=float("nan")),20)
                self.assertTrue(any(e["category"]=="transport" for e in suite.current.errors))
            finally:
                artifacts.close()
            raw=json.loads((artifacts.folder/"raw-api.jsonl").read_text())
            self.assertIn("Non-finite",raw["error"])
            self.assertIn("invalid_numeric_body",raw["data"])


class BrowserLifecycleTests(unittest.IsolatedAsyncioTestCase):
    async def test_secondary_close_does_not_contaminate_next_phase(self):
        class Page:
            def __init__(self): self.handlers={}
            async def route(self,*args): pass
            async def goto(self,*args,**kwargs): pass
            def on(self,event,handler): self.handlers[event]=handler
        page=Page()
        class Context:
            async def new_page(self): return page
        class Browser:
            async def new_context(self,**kwargs): return Context()
        with tempfile.TemporaryDirectory() as folder:
            artifacts=Artifacts(Path(folder)/"run")
            try:
                suite=Suite(arguments(["--url","http://localhost"]),artifacts)
                suite.browser=Browser()
                owner=phase();suite.phase=owner
                _, counters=await suite.extra_page("/")
                following=phase();suite.phase=following
                page.handlers["requestfailed"](SimpleNamespace(failure="network error"))
                self.assertEqual(len(owner.errors),1)
                self.assertFalse(following.errors)
                counters["closed"]=True
                page.handlers["requestfailed"](SimpleNamespace(failure="net::ERR_ABORTED"))
                self.assertEqual(len(owner.errors),1)
                self.assertFalse(following.errors)
            finally:
                artifacts.close()


if __name__=="__main__":unittest.main()
