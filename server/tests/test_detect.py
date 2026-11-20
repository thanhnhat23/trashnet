"""Detection mode: boxes trigger sorting; white paper on a white tray is still caught."""
import cv2
import numpy as np

from fake_devices import BLUE, GREEN
from services import vision
from services.detector import Detector, best_detection, vote_detections, Det


def colour_detector(rgb, conf):
    """Fake YOLO: boxes the saturated blob; class by colour; GRAY blobs only appear at low confidence."""
    bgr = rgb[:, :, ::-1].astype(int)
    sat = bgr.max(axis=2) - bgr.min(axis=2)
    mask = (sat > 60).astype(np.uint8)
    if mask.sum() < 200:
        return []
    ys, xs = np.nonzero(mask)
    r, g, b = rgb[mask > 0].mean(axis=0)
    cls = 0 if b > 150 else 1 if g > 120 and r < 100 else 3 if r > 150 else 2
    return [(xs.min(), ys.min(), xs.max(), ys.max(), 0.9, cls)]


def _use_detector(env):
    env.container.detector = Detector(predict_fn=colour_detector)
    env.container.classifier._fn = None  # prove the detector alone drives the cycle


def test_detect_mode_needs_no_reference_and_sorts(env):
    _use_detector(env)
    env.devices.state.object = True
    env.devices.state.color = BLUE
    res = env.client.post("/sort/capture", json={}).json()
    assert res["mode"] == "detect" and res["label"] == "paper" and res["bin"] == 1
    assert res["box"] and res["dispatched"] and res["job_done"]
    env.devices.state.color = GREEN
    assert env.client.post("/sort/capture", json={}).json()["label"] == "glass"


def test_detect_mode_empty_tray_returns_no_object(env):
    _use_detector(env)
    env.devices.state.object = False
    assert env.client.post("/sort/capture", json={}).json()["has_object"] is False


def test_auto_triggers_without_reference(env):
    import time
    _use_detector(env)
    env.container.cfg.update({"auto_interval_sec": 0.1, "cooldown_sec": 0.05})
    env.devices.state.object = True
    env.devices.state.color = BLUE
    env.client.post("/sort/auto", json={"enabled": True})
    for _ in range(60):
        if env.devices.state.actions:
            break
        time.sleep(0.1)
    env.client.post("/sort/auto", json={"enabled": False})
    assert env.devices.state.actions and env.devices.state.actions[0]["bin"] == 1


def _white_tray(paper: bool, seed=0):
    rng = np.random.default_rng(seed)
    img = np.full((240, 320, 3), 238, np.float32)
    if paper:
        shadow = np.zeros((240, 320), np.float32)
        cv2.rectangle(shadow, (116, 90), (220, 172), 1.0, -1)
        shadow = cv2.GaussianBlur(shadow, (0, 0), 6)
        img -= 10 * shadow[..., None]                                             # faint soft shadow
        cv2.rectangle(img, (112, 82), (212, 162), (252, 252, 252), -1)           # white paper on top
    img += rng.normal(0, 1.5, img.shape)
    return np.clip(img, 0, 255).astype(np.uint8)


def test_white_paper_on_white_tray_found_by_change_cue():
    ref = vision.TrayReference()
    ref.set(_white_tray(False, 1))
    frame = _white_tray(True, 2)
    assert vision.find_object(frame, ref, 0.015) is None                         # classic threshold: invisible
    box = vision.find_object(frame, ref, 0.015, min_diff=8)
    assert box is not None and 60 < box.w < 140 and 50 < box.h < 120
    assert vision.find_object(_white_tray(False, 3), ref, 0.015, min_diff=8) is None   # no false alarm on grain


def test_cue_only_hit_goes_to_other_not_silence(env):
    """Detector blind (white paper): the cue still triggers and the item lands in 'other'."""
    env.container.detector = Detector(predict_fn=lambda rgb, conf: [])
    env.container.cfg.update({"min_area_frac": 0.01})
    st = env.devices.state
    st.frame = lambda: _white_tray(st.object, 7)
    st.jpeg = lambda: cv2.imencode(".jpg", st.frame(), [cv2.IMWRITE_JPEG_QUALITY, 95])[1].tobytes()
    st.object = False
    assert env.client.post("/camera/reference").json()["ok"]
    st.object = True
    res = env.client.post("/sort/capture", json={}).json()
    assert res["has_object"] and res["uncertain"] and res["label"] == "other" and res["bin"] == 3


def test_vote_and_best():
    a, b = Det(0, 0, 10, 10, 0.9, 0), Det(0, 0, 10, 10, 0.5, 1)
    assert best_detection([a, b]) is a and best_detection([]) is None
    probs, best = vote_detections([a, a, b], [1, 1, 1])
    assert best.cls == 0 and probs[0] > probs[1]
    assert vote_detections([None, None], [1, 1]) == (None, None)


def test_rescan_finds_devices(env):
    import time
    r = env.client.post("/devices/rescan?deep=false").json()["devices"]
    assert all(d["online"] for d in r)
    # stored URL points nowhere -> offline, then a manual fix + rescan brings it back
    env.container.registry.devices["cam"].url = "http://127.0.0.1:9"
    r = env.client.post("/devices/rescan?deep=false").json()["devices"]
    assert not {d["role"]: d for d in r}["cam"]["online"]
    env.client.post("/devices/manual", json={"role": "cam", "url": f"127.0.0.1:{env.devices.cam_port}"})
    r = env.client.post("/devices/rescan?deep=false").json()["devices"]
    assert {d["role"]: d for d in r}["cam"]["online"]
    assert env.client.post("/sort/cycle", json={"enabled": True}).status_code in (404, 405)   # test cycle removed: Auto covers it


def two_class_detector(rgb, conf):
    """Fake YOLO that is unsure: the same box as paper 0.7 and plastic 0.3 (and noise elsewhere at 0.05)."""
    rows = colour_detector(rgb, conf)
    if not rows:
        return []
    x1, y1, x2, y2, _, _ = rows[0]
    out = [(x1, y1, x2, y2, 0.7, 0), (x1 + 2, y1 + 2, x2, y2, 0.3, 3), (0, 0, 20, 20, 0.05, 1)]
    return [r for r in out if r[4] >= conf]


def test_scores_show_every_bin_and_margin(env):
    env.container.detector = Detector(predict_fn=two_class_detector)
    env.container.classifier._fn = None
    env.devices.state.object, env.devices.state.color = True, BLUE
    res = env.client.post("/sort/capture", json={}).json()
    assert res["label"] == "paper"
    sc = res["scores"]
    assert set(sc) == {"paper", "glass", "other", "plastic"}
    assert abs(sc["paper"] - 0.7) < 1e-3 and abs(sc["plastic"] - 0.3) < 1e-3
    assert sc["glass"] == 0          # the far-away box is another object, not a guess about this one
    assert abs(res["margin"] - 0.4) < 1e-3


def test_result_published_before_the_tray_finishes(env):
    """The card must show the decision while the actuator is still moving."""
    import asyncio
    _use_detector(env)
    bus = env.container.bus
    q = bus.open()
    try:
        env.devices.state.object, env.devices.state.color = True, BLUE
        env.client.post("/sort/capture", json={})
        msgs = []
        while not q.empty():
            msgs.append(q.get_nowait())
    finally:
        bus.close(q)
    results = [m["data"] for m in msgs if m["type"] == "result"]
    assert len(results) == 2
    assert results[0]["dispatch"]["state"] == "pending" and results[0]["label"] == "paper"
    assert results[1]["dispatch"]["state"] == "done" and results[1]["ts"] == results[0]["ts"]
    kinds = [m["type"] for m in msgs]
    first_result = kinds.index("result")
    waits = [i for i, m in enumerate(msgs) if m["type"] == "sort" and m["data"]["state"] == "wait"]
    assert waits and first_result < waits[0]


def test_capture_uses_live_stream_frames(env):
    """While someone watches the stream, sorting takes frames from it: no /capture, so the stream never freezes."""
    import threading
    import time as _t
    import httpx
    _use_detector(env)
    env.client.patch("/settings", json={"values": {"capture": {"res": "qvga", "q": 10, "flash": False, "warmup": 2}}})
    stop = threading.Event()

    def watch():
        with httpx.stream("GET", f"http://127.0.0.1:{env.port}/api/v1/camera/stream", timeout=10) as r:
            for _ in r.iter_bytes():
                if stop.is_set():
                    return

    t = threading.Thread(target=watch, daemon=True)
    t.start()
    for _ in range(40):
        if env.container.hub.live:
            break
        _t.sleep(0.1)
    assert env.container.hub.stats()["fps"] > 0
    env.devices.state.calls.clear()
    env.devices.state.object, env.devices.state.color = True, BLUE
    _t.sleep(0.3)
    res = env.client.post("/sort/capture", json={}).json()
    stop.set()
    assert res["label"] == "paper"
    assert "capture" not in env.devices.state.calls


def test_sse_result_arrives_while_actuator_still_moving(env):
    """End to end over real SSE: the first `result` event must reach the browser seconds before the POST returns."""
    import threading
    import time as _t
    import httpx
    _use_detector(env)
    env.devices.state.job_seconds = 3.0
    env.devices.state.object, env.devices.state.color = True, BLUE
    got = {}

    def listen():
        with httpx.stream("GET", f"http://127.0.0.1:{env.port}/api/v1/events", timeout=20) as r:
            ev = ""
            for line in r.iter_lines():
                if line.startswith("event: "):
                    ev = line[7:]
                elif line.startswith("data: ") and ev == "result":
                    got.setdefault("t", _t.monotonic())
                    got["state"] = line[6:]
                    return

    t = threading.Thread(target=listen, daemon=True)
    t.start()
    _t.sleep(0.5)
    t0 = _t.monotonic()
    env.client.post("/sort/capture", json={})
    t_post = _t.monotonic() - t0
    t.join(5)
    first = got["t"] - t0
    assert '"pending"' in got["state"]
    assert t_post > 2.5 and first < t_post - 2.0, (first, t_post)
