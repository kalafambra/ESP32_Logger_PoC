"""Headless dashboard check with synthetic audio; does not contact the ESP32."""
import json
import math
from pathlib import Path

from playwright.sync_api import sync_playwright

ROOT = Path(__file__).resolve().parents[1]
html = (ROOT / "dashboard.h").read_text(encoding="utf-8").split('R"DASHBOARD(', 1)[1].split(')DASHBOARD"', 1)[0]
spectrum = dict(ok=True, sample_rate_hz=16000, fft_size=1024, bin_hz=15.625, bins_dbfs=[-90 + 45 * math.exp(-((i - 64) / 3) ** 2) for i in range(513)],
                bands_dbfs=[-45, -35, -55], rms_dbfs=-33, peak_hz=1000, age_ms=10, frames=31,
                dma_overflows=0, clipped_samples=0)
with sync_playwright() as p:
    browser = p.chromium.launch(channel="chrome", headless=True)
    page = browser.new_page(viewport={"width": 1100, "height": 900}, device_scale_factor=1)
    errors = []
    page.on("pageerror", lambda error: errors.append(str(error)))

    def respond(route):
        url = route.request.url
        if "/api/microphone" in url:
            route.fulfill(content_type="application/json", body=json.dumps(spectrum))
        elif "/api/" in url:
            route.fulfill(content_type="application/json", body="{}")
        else:
            route.fulfill(content_type="text/html", body=html)

    page.route("http://spectrum.test/**", respond)
    page.goto("http://spectrum.test/")
    page.wait_for_function("!document.getElementById('spectrumSave').disabled")
    page.locator("#spectrumSave").click()
    spectrum.update(rms_dbfs=-43, bands_dbfs=[-55, -45, -65], bins_dbfs=[v - 10 for v in spectrum["bins_dbfs"]])
    page.wait_for_function("document.getElementById('spectrumComparison').textContent.includes('overall -10.0 dB')")
    output = ROOT / "test-results"
    output.mkdir(exist_ok=True)
    page.locator("#spectrumChart").locator("..").screenshot(path=str(output / "spectrum-desktop.png"))
    page.set_viewport_size({"width": 390, "height": 844})
    page.wait_for_timeout(100)
    assert page.evaluate("document.documentElement.scrollWidth <= window.innerWidth"), "Mobile horizontal overflow"
    page.locator("#spectrumChart").locator("..").screenshot(path=str(output / "spectrum-mobile.png"))
    spectrum.update(ok=False, error="Audio spectrum stale")
    page.wait_for_function("document.getElementById('spectrumStatus').textContent === 'Audio spectrum stale'")
    assert page.locator("#spectrumSave").is_disabled()
    assert "waiting for fresh audio" in page.locator("#spectrumComparison").inner_text()
    page.locator("#spectrumClear").click()
    assert page.locator("#spectrumClear").is_disabled()
    assert not errors, errors
    browser.close()
print("PASS: browser spectrum rendering, exposed reference, dB comparison, mobile fit and stale/error handling")
