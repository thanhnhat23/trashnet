import json
import threading
import time

import httpx

from fake_devices import BLUE, GRAY, GREEN, RED


def _sort(env, color, **kw):
    env.devices.state.object = True
    env.devices.state.color = color
    return env.client.post("/sort/capture", json=kw).json()


def test_heartbeat_registers_devices(env):
    devs = {d["role"]: d for d in env.client.get("/devices").json()}
    assert devs["cam"]["online"] and devs["actuator"]["online"]
    assert devs["cam"]["source"] == "heartbeat"


def test_sort_needs_reference_then_dispatches(env):
    st = env.devices.state
    st.object = False
    assert env.client.post("/sort/capture", json={}).json()["error"] == "no_reference"
    assert env.client.post("/camera/reference").json()["ok"]

    empty = env.client.post("/sort/capture", json={}).json()
    assert empty["ok"] and empty["has_object"] is False

    res = _sort(env, BLUE)
    assert res["label"] == "paper" and res["bin"] == 1
    assert res["dispatched"] and res["job_done"] and not res["uncertain"]
    assert st.actions[-1]["bin"] == 1

    res = _sort(env, GREEN)
    assert res["label"] == "glass" and res["bin"] == 2
    res = _sort(env, RED)
    assert res["label"] == "plastic" and res["bin"] == 4


def test_uncertain_goes_to_other(env):
    env.devices.state.object = False
    env.client.post("/camera/reference")
    res = _sort(env, GRAY)
    assert res["uncertain"] and res["label"] == "other" and res["bin"] == 3


def test_history_and_stats(env):
    env.devices.state.object = False
    env.client.post("/camera/reference")
    _sort(env, BLUE)
    _sort(env, GREEN)
    data = env.client.get("/history").json()
    assert len(data["items"]) == 2 and data["stats"]["total"] == 2
    thumb = env.client.get(data["items"][0]["thumb"].removeprefix("/api/v1"))
    assert thumb.status_code == 200 and thumb.headers["content-type"] == "image/jpeg"
    env.client.delete(f"/history/{data['items'][0]['id']}")
    assert env.client.get("/history/stats").json()["total"] == 1


def test_predict_single_image(env):
    env.devices.state.object = False
    env.client.post("/camera/reference")
    env.devices.state.object, env.devices.state.color = True, BLUE
    jpg = env.devices.state.jpeg()
    r = env.client.post("/predict", content=jpg, headers={"content-type": "image/jpeg"}).json()
    assert r["label"] == "paper" and r["box"] is not None
    assert env.devices.state.actions == []  # predict never moves hardware


def test_actuator_proxy(env):
    assert env.client.get("/actuator/status").json()["online"]
    assert env.client.post("/actuator/calib", json={"t90_fwd_ms": 980}).json()["t90_fwd_ms"] == 980
    assert env.client.get("/actuator/calib").json()["t90_fwd_ms"] == 980
    assert env.client.post("/actuator/control", json={"action": "jog", "dir": 1, "ms": 20}).json()["job_id"]
    assert "/jog" in env.devices.state.calls
    assert env.client.post("/actuator/control", json={"action": "nope"}).status_code == 400


def test_stream_fanout_single_upstream(env):
    frames = []

    def read():
        with httpx.stream("GET", f"http://127.0.0.1:{env.port}/api/v1/camera/stream", timeout=10) as r:
            buf = b""
            for chunk in r.iter_bytes():
                buf += chunk
                if buf.count(b"\xff\xd8") >= 3:
                    frames.append(buf.count(b"\xff\xd8"))
                    return

    threads = [threading.Thread(target=read) for _ in range(2)]
    [t.start() for t in threads]
    [t.join(12) for t in threads]
    assert len(frames) == 2


def test_sse_hello_and_result_event(env):
    hello = httpx.get(f"http://127.0.0.1:{env.port}/api/v1/events?once=1", timeout=5).text
    assert hello.startswith("event: hello")

    seen = []

    def listen():
        with httpx.stream("GET", f"http://127.0.0.1:{env.port}/api/v1/events", timeout=20) as r:
            for line in r.iter_lines():
                if line.startswith("event: "):
                    seen.append(line[7:])
                if "result" in seen:
                    return

    t = threading.Thread(target=listen)
    t.start()
    time.sleep(0.5)
    env.devices.state.object = False
    env.client.post("/camera/reference")
    _sort(env, BLUE)
    t.join(10)
    assert "result" in seen and "sort" in seen


def test_settings_api_key_guard(env):
    env.client.patch("/settings", json={"values": {"api_key": "s3cret"}})
    assert env.client.post("/camera/reference").status_code == 401
    ok = env.client.post("/camera/reference", headers={"x-api-key": "s3cret"})
    assert ok.status_code == 200
    assert env.client.get("/settings").json()["api_key"] is True


def test_missing_model_reports_cleanly(env):
    env.container.classifier._fn = None
    env.container.classifier._model = None
    env.container.classifier.error = "model not found"
    r = env.client.post("/sort/capture", json={"force": True}).json()
    assert r["ok"] is False and "model" in r["error"]



def test_full_bin_blocks_dispatch_and_busy_maps_to_409(env):
    env.devices.state.full = [True, False, False, False]
    env.client.post("/camera/reference")
    env.devices.state.object = True
    r = env.client.post("/sort/capture", json={}).json()
    assert r.get("error") == "bin_full" and not r.get("dispatched")
    env.devices.state.full = [False] * 4
    assert env.client.post("/actuator/control", json={"action": "dump"}).status_code == 200
    assert env.client.post("/actuator/control", json={"action": "lid_open"}).status_code == 409


def test_dispatch_info_in_result(env):
    env.client.post("/camera/reference")
    env.devices.state.object = True
    r = env.client.post("/sort/capture", json={}).json()
    assert r["dispatch"]["state"] == "done" and r["dispatch"]["job_id"] and r["dispatch"]["ms"] >= 0
