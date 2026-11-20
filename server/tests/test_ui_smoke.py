"""Browser smoke test: loads every view, runs one sort cycle against fake devices."""
import os

import pytest

pytest.importorskip("playwright.sync_api")
from playwright.sync_api import sync_playwright  # noqa: E402

from fake_devices import BLUE  # noqa: E402

SHOTS = os.environ.get("TN_SHOTS", "")


def test_ui_flow(env):
    errors = []
    with sync_playwright() as p:
        browser = p.chromium.launch()
        page = browser.new_page(viewport={"width": 1100, "height": 760})
        page.on("pageerror", lambda e: errors.append(str(e)))
        page.on("console", lambda m: errors.append(m.text) if m.type == "error" else None)
        page.goto(f"http://127.0.0.1:{env.port}/")
        try:
            page.wait_for_selector("#nav a", timeout=8000)
        except Exception:
            raise AssertionError(f"UI did not render: {errors}")

        env.devices.state.object = False
        page.get_by_text("Đặt khay trống").click()
        page.wait_for_selector(".toast")
        env.devices.state.object, env.devices.state.color = True, BLUE
        page.get_by_text("Chụp & phân loại").click()
        page.wait_for_selector(".result >> text=Ngăn 1", timeout=15000)
        if SHOTS:
            page.screenshot(path=f"{SHOTS}/live.png")
        page.locator(".tabs >> text=Điều khiển").click()
        page.wait_for_selector(".bin-dot")
        assert page.locator(".bin-dot").count() == 4
        if SHOTS:
            page.screenshot(path=f"{SHOTS}/live_remote.png")

        for name in ("history", "stats", "devices", "settings"):
            page.goto(f"http://127.0.0.1:{env.port}/#/{name}")
            page.wait_for_timeout(700)
            if SHOTS:
                page.screenshot(path=f"{SHOTS}/{name}.png")
        assert page.locator("#status .dot.ok").count() >= 2
        browser.close()
    real = [e for e in errors if "favicon" not in e and "Failed to load resource" not in e]
    assert not real, real
