"""Simulated ESP32-CAM and actuator for tests and hardware-free development."""
import asyncio
import socket
import threading
import time

import cv2
import numpy as np
import uvicorn
from fastapi import FastAPI, Request, Response
from fastapi.responses import StreamingResponse

BLUE, GREEN, RED, YELLOW, GRAY = (255, 0, 0), (0, 200, 0), (0, 0, 255), (0, 220, 220), (128, 128, 128)  # BGR


class FakeState:
    def __init__(self) -> None:
        self.object = False
        self.color = BLUE
        self.actions: list[dict] = []
        self.busy_until = 0.0
        self.job_seconds = 0.4
        self.job_id = ""
        self.calib = {"neutral_us": 1500, "delta_us": 200, "t90_fwd_ms": 950, "t90_rev_ms": 950,
                      "trim_ms": [0, 0, 0, 0]}
        self.calls: list[str] = []
        self.full = [False, False, False, False]

    def frame(self) -> np.ndarray:
        rng = np.random.default_rng(3)
        img = np.full((240, 320, 3), 150, np.uint8)
        img = np.clip(img + rng.normal(0, 2, img.shape), 0, 255).astype(np.uint8)
        if self.object:
            rng2 = np.random.default_rng(5)
            patch = np.full((90, 110, 3), self.color, np.uint8)
            patch = np.clip(patch.astype(int) + rng2.integers(-35, 35, patch.shape), 0, 255).astype(np.uint8)
            img[75:165, 105:215] = patch
        return img

    def jpeg(self) -> bytes:
        return cv2.imencode(".jpg", self.frame(), [cv2.IMWRITE_JPEG_QUALITY, 90])[1].tobytes()


def cam_control_app(st: FakeState, stream_port: int) -> FastAPI:
    app = FastAPI()

    @app.get("/status")
    def status():
        return {"role": "cam", "id": "fake-cam", "fw": "sim", "stream_port": stream_port}

    @app.get("/capture")
    def capture():
        st.calls.append("capture")
        return Response(st.jpeg(), media_type="image/jpeg")

    @app.get("/flash")
    def flash(on: int = 0):
        return {"flash": bool(on)}

    return app


def cam_stream_app(st: FakeState) -> FastAPI:
    app = FastAPI()

    @app.get("/stream")
    async def stream():
        async def gen():
            while True:
                data = st.jpeg()
                yield (b"--123456789000000000000987654321\r\nContent-Type: image/jpeg\r\nContent-Length: "
                       + str(len(data)).encode() + b"\r\n\r\n" + data + b"\r\n")
                await asyncio.sleep(0.05)
        return StreamingResponse(gen(), media_type="multipart/x-mixed-replace;boundary=123456789000000000000987654321")

    return app


def actuator_app(st: FakeState, job_seconds: float = 0.4) -> FastAPI:
    app = FastAPI()

    @app.get("/status")
    def status():
        busy = time.time() < st.busy_until
        return {"role": "actuator", "id": "fake-act", "state": "moving" if busy else "idle",
                "job_id": st.job_id, "job_done": bool(st.job_id) and not busy, "position": 1,
                "step": "idle", "full": list(st.full), "bin1_full": st.full[0], "needs_home": False,
                "fill_cm": [3.0 if f else 12.0 for f in st.full], "dist_full_cm": 12.0, "dist_lid_cm": 78.0}

    @app.post("/action")
    async def action(request: Request):
        body = await request.json()
        if time.time() < st.busy_until:
            return Response('{"error":"busy"}', status_code=409, media_type="application/json")
        st.job_id = f"job{len(st.actions) + 1}"
        st.actions.append(body)
        st.busy_until = time.time() + (st.job_seconds or job_seconds)
        return Response(f'{{"job_id":"{st.job_id}"}}', status_code=202, media_type="application/json")

    @app.get("/calib")
    def calib_get():
        return st.calib

    @app.post("/calib")
    async def calib_set(request: Request):
        st.calib.update(await request.json())
        return st.calib

    def job(path: str, body: dict):
        if time.time() < st.busy_until:
            return Response('{"error":"busy"}', status_code=409, media_type="application/json")
        st.calls.append(path)
        st.job_id = f"job{len(st.actions) + len(st.calls)}"
        st.busy_until = time.time() + 0.2
        return Response(f'{{"job_id":"{st.job_id}"}}', status_code=202, media_type="application/json")

    for path in ("/jog", "/servo", "/rotate"):
        def make(p):
            async def handler(request: Request):
                return job(p, await request.json())
            return handler
        app.post(path)(make(path))

    for path in ("/home/set", "/calib/neutral"):
        def make_ok(p):
            async def handler(request: Request):
                st.calls.append(p)
                return {"ok": True, "path": p}
            return handler
        app.post(path)(make_ok(path))

    return app


class _Server(uvicorn.Server):
    def install_signal_handlers(self) -> None:  # run inside a thread
        pass


def free_port() -> int:
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def serve(app: FastAPI, port: int) -> _Server:
    server = _Server(uvicorn.Config(app, host="127.0.0.1", port=port, log_level="error"))
    threading.Thread(target=server.run, daemon=True).start()
    for _ in range(100):
        if server.started:
            return server
        time.sleep(0.05)
    raise RuntimeError("fake server did not start")


class FakeDevices:
    def __init__(self) -> None:
        self.state = FakeState()
        self.cam_port, self.stream_port, self.act_port = free_port(), free_port(), free_port()
        self.servers = [
            serve(cam_control_app(self.state, self.stream_port), self.cam_port),
            serve(cam_stream_app(self.state), self.stream_port),
            serve(actuator_app(self.state), self.act_port),
        ]

    def stop(self) -> None:
        for s in self.servers:
            s.should_exit = True


if __name__ == "__main__":
    dev = FakeDevices()
    print(f"cam http://127.0.0.1:{dev.cam_port}  stream :{dev.stream_port}  actuator http://127.0.0.1:{dev.act_port}")
    while True:
        time.sleep(1)
