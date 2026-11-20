"""Sorting state machine: capture -> presence -> quality -> classify -> dispatch -> wait."""
import asyncio
import json
import logging
import time
from typing import Optional

import cv2
import numpy as np

from domain import labels
from services import detector as det_mod
from services import inference, vision
from services.device_clients import DeviceError

log = logging.getLogger("sort")


class Orchestrator:
    def __init__(self, c) -> None:
        self.c = c  # container with cfg, bus, cam, act, classifier, history, ref
        self.state = "idle"
        self.auto = False
        self._lock = asyncio.Lock()
        self.last: Optional[dict] = None
        self._full_until = 0.0
        # actuator monitor: the only place that polls GET /status (ESP32 httpd serves one request at a time)
        self.act_status: dict = {}
        self.act_ts = 0.0
        self._act_evt = asyncio.Event()
        self._fast_until = 0.0

    def _set(self, state: str, **extra) -> None:
        self.state = state
        self.c.bus.publish("sort", {"state": state, "auto": self.auto, **extra})

    def set_auto(self, enabled: bool) -> None:
        self.auto = enabled
        self.c.cfg.update({"auto_enabled": enabled})
        self._set(self.state)

    # ---- reference ---------------------------------------------------
    async def set_reference(self) -> dict:
        cap = self.c.cfg.get("capture")
        frame = vision.decode_jpeg(await self.c.cam.capture(cap["res"], cap["q"]))
        if frame is None:
            raise DeviceError("bad frame")
        self.c.ref.set(frame)
        self.c.save_reference(frame)
        return {"ok": True, "size": [frame.shape[1], frame.shape[0]]}

    # ---- single image (upload / phone) --------------------------------
    def classify_image(self, data: bytes) -> dict:
        frame = vision.decode_jpeg(data)
        if frame is None:
            return {"ok": False, "error": "bad image"}
        if self.c.detector.available:
            det = self._locate(frame)
            if det is None:
                return {"ok": True, "has_object": False}
            return self._result(self._decide_det(det, None), self._box_of(det, frame), frame, "upload", False)
        box = vision.find_object(frame, self.c.ref, self.c.cfg.get("min_area_frac")) if self.c.ref.ready else None
        if self.c.ref.ready and box is None:
            return {"ok": True, "has_object": False}
        crop = vision.crop_box(frame, box)
        probs = self.c.classifier.frame_probs([crop])[0]
        tau, delta = self.c.classifier.thresholds(self.c.cfg.get("tau"), self.c.cfg.get("delta"))
        return self._result(inference.decide(probs, tau, delta), box, frame, "upload", False)

    # ---- full cycle ---------------------------------------------------
    async def run_once(self, source: str = "manual", force: bool = False,
                       first_frame: Optional[np.ndarray] = None) -> dict:
        if self._lock.locked():
            return {"ok": False, "error": "busy"}
        async with self._lock:
            try:
                result = await self._cycle(source, force, first_frame)
            except DeviceError as exc:
                self._set("error", message=str(exc))
                self.c.bus.publish("error", {"message": str(exc)})
                result = {"ok": False, "error": str(exc)}
            finally:
                cooldown = float(self.c.cfg.get("cooldown_sec"))
                if self.state != "error":
                    self._set("cooldown")
                    await asyncio.sleep(cooldown)
                self._set("idle")
            return result

    RES_W = {"qvga": 320, "cif": 400, "vga": 640, "svga": 800, "xga": 1024, "hd": 1280, "sxga": 1280, "uxga": 1600}

    async def _stream_frames(self, n: int) -> list[np.ndarray]:
        """Take fresh frames from the running MJPEG stream instead of /capture: the stream never freezes and
        there is no extra HTTP round-trip. Only when the stream has the capture resolution and no flash is needed."""
        cap, hub = self.c.cfg.get("capture"), self.c.hub
        if not self.c.cfg.get("capture_from_stream") or cap.get("flash") or not hub.live:
            return []
        frames = []
        for data in await hub.next_frames(n):
            img = vision.decode_jpeg(data)
            if img is None or img.shape[1] != self.RES_W.get(cap["res"], img.shape[1]):
                return []
            frames.append(img)
        return frames if len(frames) == n else []

    async def _grab(self, n: int, first: Optional[np.ndarray]) -> list[np.ndarray]:
        cap = self.c.cfg.get("capture")
        frames = [first] if first is not None else []
        if len(frames) < n:
            frames += await self._stream_frames(n - len(frames))
        while len(frames) < n:
            data = await self.c.cam.capture(cap["res"], cap["q"], cap["flash"], cap["warmup"])
            img = vision.decode_jpeg(data)
            if img is not None:
                frames.append(img)
        return frames

    async def _peek(self) -> Optional[np.ndarray]:
        """One frame for the auto loop: the live stream frame when fresh, else a capture."""
        hub = self.c.hub
        if self.c.cfg.get("capture_from_stream") and hub.live:
            img = vision.decode_jpeg(hub.latest)
            if img is not None:
                return img
        cap = self.c.cfg.get("capture")
        return vision.decode_jpeg(await self.c.cam.capture(cap["res"], cap["q"]))

    async def _cycle(self, source: str, force: bool, first: Optional[np.ndarray]) -> dict:
        cfg, n = self.c.cfg, int(self.c.cfg.get("vote_frames"))
        if self.c.detector.available:
            return await self._cycle_detect(source, first)
        if not self.c.classifier.available:
            return {"ok": False, "error": "model not loaded: " + self.c.classifier.error}
        if not force and not self.c.ref.ready:
            return {"ok": False, "error": "no_reference"}
        blurry, crops, boxes, sharp, frames = False, [], [], [], []
        for attempt in range(3):
            self._set("capture", attempt=attempt)
            frames = await self._grab(n, first if attempt == 0 else None)
            self._set("presence")
            crops, boxes, sharp = [], [], []
            for f in frames:
                box = None if force and not self.c.ref.ready else vision.find_object(
                    f, self.c.ref, cfg.get("min_area_frac"))
                if box is None and not force:
                    continue
                crops.append(vision.crop_box(f, box))
                boxes.append(box)
                sharp.append(vision.sharpness(crops[-1]))
            if not crops:
                return {"ok": True, "has_object": False}
            blurry = max(sharp) < cfg.get("blur_min")
            if not blurry:
                break
        self._set("classify")
        probs = self.c.classifier.frame_probs(crops)
        weights = np.clip(sharp, 1.0, None)
        tau, delta = self.c.classifier.thresholds(cfg.get("tau"), cfg.get("delta"))
        decision = inference.decide(inference.vote(probs, weights), tau, delta)
        if blurry:
            decision.confidence *= 0.85
            if decision.confidence < tau:
                decision.uncertain, decision.label = True, labels.FALLBACK
        best = int(np.argmax(sharp))
        result = self._result(decision, boxes[best], frames[best], source, blurry)
        self._attach_scores(result, [decision.probs.get(k, 0.0) for k in labels.LABEL_IDS])
        return await self._finish(result, crops[best], source)

    # ---- detection mode (YOLO bounding boxes) -----------------------------
    def _det_params(self) -> tuple[float, float, float]:
        cfg, d = self.c.cfg, self.c.detector
        pick = lambda key, default: default if cfg.get(key) is None else float(cfg.get(key))  # noqa: E731
        return pick("det_conf", d.conf), pick("det_conf_low", d.conf_low), pick("det_tau", d.tau)

    SCORE_FLOOR = 0.02

    def _locate(self, frame: np.ndarray, dets: Optional[list] = None) -> Optional[det_mod.Det]:
        """Find the item: detector first; if silent, the change cue (works for white paper on a white tray)
        proposes a region and the detector re-runs there with a low threshold. Cue-only hits become 'other'."""
        d, cfg = self.c.detector, self.c.cfg
        conf, low, _ = self._det_params()
        if dets is None:
            dets = d.detect(frame, conf)
        best = det_mod.best_detection([x for x in dets if x.conf >= conf])
        if best is not None or not self.c.ref.ready:
            return best
        box = vision.find_object(frame, self.c.ref, cfg.get("min_area_frac"), int(cfg.get("presence_min_diff")))
        if box is None:
            return None
        h, w = frame.shape[:2]
        px, py = int(box.w * 0.15), int(box.h * 0.15)
        roi = (max(0, box.x - px), max(0, box.y - py), min(w, box.x + box.w + px), min(h, box.y + box.h + py))
        best = det_mod.best_detection(d.detect_in(frame, roi, low))
        if best is not None:
            return best
        return det_mod.Det(box.x, box.y, box.x + box.w, box.y + box.h, 0.0, labels.LABEL_IDS.index(labels.FALLBACK))

    def _box_of(self, det: det_mod.Det, frame: np.ndarray) -> vision.Box:
        h, w = frame.shape[:2]
        return vision.Box(det.x1, det.y1, det.x2 - det.x1, det.y2 - det.y1, det.area / float(h * w))

    def _decide_det(self, det: det_mod.Det, probs: Optional[np.ndarray]) -> inference.Decision:
        _, _, tau = self._det_params()
        uncertain = det.conf < tau
        label = labels.FALLBACK if uncertain else det.label
        if probs is None:
            probs = np.zeros(len(labels.LABEL_IDS))
            probs[det.cls] = 1.0
        return inference.Decision(label, float(det.conf), uncertain, {k: float(v) for k, v in zip(labels.LABEL_IDS, probs)})

    async def _cycle_detect(self, source: str, first: Optional[np.ndarray]) -> dict:
        cfg, n = self.c.cfg, int(self.c.cfg.get("vote_frames"))
        frames, dets, sharp, alls, blurry = [], [], [], [], False
        for attempt in range(3):
            self._set("capture", attempt=attempt)
            frames = await self._grab(n, first if attempt == 0 else None)
            self._set("detect")
            # one low-threshold pass per frame: used both to find the item and to score every bin
            alls = [self.c.detector.detect(f, self.SCORE_FLOOR) for f in frames]
            dets = [self._locate(f, a) for f, a in zip(frames, alls)]
            if not any(dets):
                return {"ok": True, "has_object": False}
            sharp = [vision.sharpness(vision.crop_box(f, self._box_of(d, f) if d else None)) for f, d in zip(frames, dets)]
            blurry = max(sharp) < cfg.get("blur_min")
            if not blurry:
                break
        weights = np.clip(sharp, 1.0, None)
        probs, best = det_mod.vote_detections(dets, weights)
        if best is None:  # only cue hits: something is on the tray but the model cannot name it
            best = next(d for d in dets if d is not None)
        decision = self._decide_det(best, probs)
        if blurry:
            decision.confidence *= 0.85
            if decision.confidence < self._det_params()[2]:
                decision.uncertain, decision.label = True, labels.FALLBACK
        idx = max((i for i, d in enumerate(dets) if d is not None), key=lambda i: sharp[i])
        box = self._box_of(best, frames[idx])
        result = self._result(decision, box, frames[idx], source, blurry)
        result["mode"] = "detect"
        sc, wsum = np.zeros(len(labels.LABEL_IDS)), 0.0
        for a, d, w in zip(alls, dets, weights):
            if d is not None:
                sc += det_mod.class_scores(a, d) * w
                wsum += w
        self._attach_scores(result, sc / wsum if wsum else sc)
        return await self._finish(result, vision.crop_box(frames[idx], box), source)

    def _attach_scores(self, result: dict, scores) -> None:
        """Per-bin scores (0..1) and the gap between the two best, for the result card."""
        sc = {k: round(float(v), 4) for k, v in zip(labels.LABEL_IDS, scores)}
        top = sorted(sc.values(), reverse=True) + [0.0, 0.0]
        result["scores"] = sc
        result["margin"] = round(top[0] - top[1], 4)

    async def _finish(self, result: dict, crop, source: str) -> dict:
        """Show the decision at once, then send it to the actuator and update the same result when done."""
        result["dispatch"] = {"state": "pending", "job_id": "", "ms": 0}
        self.c.bus.publish("result", {**result, "dispatch": dict(result["dispatch"])})  # snapshot: dict mutates below
        await self._dispatch(result)
        rec = self.c.history.add(result["label"], result["confidence"], result["uncertain"], source, crop)
        result["id"] = rec["id"]
        self.last = result
        self.c.bus.publish("result", result)
        return result

    def _result(self, d: inference.Decision, box, frame, source: str, blurry: bool) -> dict:
        meta = labels.get(d.label)
        return {
            "ok": True, "has_object": True, "label": d.label, "bin": meta["bin"],
            "name_vi": meta["name_vi"], "confidence": round(d.confidence, 4),
            "uncertain": d.uncertain, "blurry": blurry, "probs": d.probs,
            "box": box.as_list() if box else None,
            "frame": [frame.shape[1], frame.shape[0]], "source": source,
            "dispatched": False, "job_id": "", "job_done": False, "ts": time.time(),
        }

    async def _bin_full(self, bin_no: int) -> bool:
        """True when the actuator reports the target bin as full (per-bin flags, or bin1_full on old firmware)."""
        st = await self._status(max_age=1.5)
        flags = st.get("full")
        if isinstance(flags, list) and 1 <= bin_no <= len(flags):
            return bool(flags[bin_no - 1])
        return bin_no == 1 and bool(st.get("bin1_full"))

    async def _dispatch(self, result: dict) -> None:
        """Send the bin to the actuator and wait for the job; result['dispatch'] says what happened."""
        self._set("dispatch", label=result["label"])
        t0 = time.monotonic()
        info = {"state": "sent", "job_id": "", "ms": 0}
        result["dispatch"] = info
        try:
            if await self._bin_full(result["bin"]):
                result["error"], info["state"] = "bin_full", "bin_full"
                self._full_until = time.monotonic() + 10.0
                self.c.bus.publish("alert", {"kind": "bin_full", "bin": result["bin"]})
                return
            deadline = time.monotonic() + float(self.c.cfg.get("job_timeout_sec"))
            while True:  # 409 busy (e.g. lid auto-open): retry until the deadline
                code, data = await self.c.act.action(result["bin"], result["label"], result["name_vi"])
                if code != 409 or data.get("error") != "busy" or time.monotonic() > deadline:
                    break
                await asyncio.sleep(1.0)
            if code not in (200, 202):
                err = data.get("error", f"actuator {code}")
                result["error"], info["state"] = err, ("busy" if err == "busy" else "error")
                return
            result["dispatched"], result["job_id"] = True, data.get("job_id", "")
            info["job_id"] = result["job_id"]
            self._set("wait", job_id=result["job_id"])
            result["job_done"] = await self._wait_job(result["job_id"])
            info["state"] = "done" if result["job_done"] else "timeout"
            if not result["job_done"]:
                result["error"] = "job timeout"
        except DeviceError as exc:
            result["error"], info["state"] = str(exc), "error"
        finally:
            info["ms"] = int((time.monotonic() - t0) * 1000)

    async def _status(self, max_age: float = 1.5) -> dict:
        """Latest actuator status from the monitor; polls directly only when the monitor is stale."""
        if time.monotonic() - self.act_ts <= max_age and self.act_status.get("online"):
            return self.act_status
        try:
            st = {"online": True, **await self.c.act.status()}
        except DeviceError:
            return {}
        self._store_status(st)
        return st

    def _store_status(self, st: dict) -> None:
        self.act_status, self.act_ts = st, time.monotonic()
        self._act_evt.set()
        self._act_evt = asyncio.Event()

    async def _wait_job(self, job_id: str) -> bool:
        timeout = float(self.c.cfg.get("job_timeout_sec"))
        deadline = time.monotonic() + timeout
        self._fast_until = deadline           # monitor polls every 250 ms while the job runs
        step = ""
        try:
            while time.monotonic() < deadline:
                evt = self._act_evt
                try:
                    await asyncio.wait_for(evt.wait(), 1.0)
                    st = self.act_status
                except asyncio.TimeoutError:     # monitor not running (tests, no SSE): poll ourselves
                    st = await self._status(max_age=0)
                    await asyncio.sleep(0.25)
                if st.get("job_id") == job_id and st.get("job_done"):
                    return True
                if st.get("step") and st["step"] != step:
                    step = st["step"]
                    self._set("wait", job_id=job_id, step=step)   # live "what the servo is doing now"
            return False
        finally:
            self._fast_until = 0.0

    # ---- background loops ----------------------------------------------
    async def auto_loop(self) -> None:
        """Poll the tray; start a cycle when an object stays for two polls."""
        streak, empty = 0, 0
        while True:
            await asyncio.sleep(float(self.c.cfg.get("auto_interval_sec")))
            detect_mode = self.c.detector.available
            if not self.auto or self._lock.locked() or time.monotonic() < self._full_until or (not detect_mode and not self.c.ref.ready):
                streak = 0
                continue
            try:
                frame = await self._peek()
                if frame is None:
                    continue
                if detect_mode:
                    box = self._locate(frame)
                else:
                    box = vision.find_object(frame, self.c.ref, self.c.cfg.get("min_area_frac"))
                if box is None:
                    streak, empty = 0, empty + 1
                    if empty % 5 == 0 and self.c.ref.ready:
                        self.c.ref.update(frame)  # keep the empty-tray reference fresh
                    continue
                streak, empty = streak + 1, 0
                if streak >= 2:
                    streak = 0
                    await self.run_once("auto", first_frame=frame)
            except DeviceError:
                streak = 0
            except Exception:  # noqa: BLE001
                log.exception("auto loop")

    @staticmethod
    def _signature(st: dict) -> str:
        """Status without the jitter of the ultrasonic readings, to publish only real changes."""
        q = dict(st)
        for k in ("dist_full_cm", "dist_lid_cm"):
            if isinstance(q.get(k), (int, float)):
                q[k] = round(q[k] * 2) / 2
        q.pop("uptime", None)
        q.pop("uptime_s", None)
        return json.dumps(q, sort_keys=True, default=str)

    async def status_loop(self) -> None:
        """Actuator monitor: the single poller of GET /status. 1 s idle, 250 ms while a job runs.
        SSE gets a message only when something changed (plus a 5 s heartbeat)."""
        last_sig, last_pub = "", 0.0
        while True:
            fast = time.monotonic() < self._fast_until
            await asyncio.sleep(0.25 if fast else 1.0)
            if not self.c.registry.url("actuator") or (self.c.bus.count == 0 and not fast):
                continue
            try:
                st = {"online": True, **await self.c.act.status()}
            except DeviceError:
                st = {"online": False}
            if st["online"]:
                self._store_status(st)
            sig, now = self._signature(st), time.monotonic()
            if sig != last_sig or now - last_pub > 5.0:
                self.c.bus.publish("actuator", st)
                last_sig, last_pub = sig, now
            self.c.registry.sweep()
