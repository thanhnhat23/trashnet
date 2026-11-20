"""HTTP clients for the ESP32-CAM and actuator, plus a single-upstream MJPEG hub."""
import asyncio
import time
import logging
from typing import Any, Optional

import httpx

from config import Settings
from services.discovery import Registry

log = logging.getLogger("iot")


class DeviceError(Exception):
    """Raised when a device is unknown or unreachable."""


class _Base:
    role = ""

    def __init__(self, cfg: Settings, registry: Registry, http: httpx.AsyncClient):
        self.cfg = cfg
        self.registry = registry
        self.http = http

    def base(self) -> str:
        url = self.registry.url(self.role)
        if not url:
            raise DeviceError(f"{self.role} not found")
        return url

    def headers(self) -> dict:
        key = self.cfg.device_key(self.role)
        return {"X-Key": key} if key else {}

    async def request(self, method: str, path: str, *, json_body: Any = None, params: Optional[dict] = None,
                      timeout: float = 4.0) -> httpx.Response:
        try:
            return await self.http.request(method, self.base() + path, json=json_body, params=params,
                                           headers=self.headers(), timeout=timeout)
        except httpx.HTTPError as exc:
            raise DeviceError(f"{self.role} unreachable: {exc.__class__.__name__}") from exc


class CameraClient(_Base):
    role = "cam"

    async def status(self) -> dict:
        r = await self.request("GET", "/status", timeout=2.5)
        return r.json()

    async def capture(self, res: str = "svga", q: int = 10, flash: bool = False, warmup: int = 0) -> bytes:
        params = {"res": res, "q": q}
        if flash:
            params.update({"flash": 1, "warmup": warmup})
        r = await self.request("GET", "/capture", params=params, timeout=8.0)
        if r.status_code != 200 or len(r.content) < 100:
            raise DeviceError(f"capture failed ({r.status_code})")
        return r.content

    async def flash(self, on: bool) -> dict:
        r = await self.request("GET", "/flash", params={"on": int(on)})
        return r.json()


class ActuatorClient(_Base):
    role = "actuator"

    async def status(self) -> dict:
        r = await self.request("GET", "/status", timeout=2.5)
        return r.json()

    async def action(self, bin_no: int, label: str, name: str = "") -> tuple[int, dict]:
        r = await self.request("POST", "/action", json_body={"bin": bin_no, "label": label, "name": name})
        return r.status_code, r.json()

    async def calib_get(self) -> dict:
        return (await self.request("GET", "/calib")).json()

    async def calib_set(self, values: dict) -> dict:
        return (await self.request("POST", "/calib", json_body=values)).json()

    async def post(self, path: str, body: Optional[dict] = None) -> dict:
        r = await self.request("POST", path, json_body=body or {})
        return r.json() if r.content else {}


def _frames(buf: bytearray):
    """Yield complete JPEG frames from a byte buffer (SOI..EOI) and trim it."""
    while True:
        start = buf.find(b"\xff\xd8")
        if start < 0:
            buf.clear()
            return
        end = buf.find(b"\xff\xd9", start + 2)
        if end < 0:
            del buf[:start]
            return
        yield bytes(buf[start:end + 2])
        del buf[:end + 2]


class StreamHub:
    """One upstream MJPEG connection fanned out to many clients."""

    def __init__(self, registry: Registry, cfg: Settings, http: httpx.AsyncClient):
        self.registry, self.cfg, self.http = registry, cfg, http
        self.subs: set[asyncio.Queue] = set()
        self.task: Optional[asyncio.Task] = None
        self.latest: Optional[bytes] = None
        self.latest_ts = 0.0
        self.seq = 0
        self.fps = 0.0
        self.kbps = 0.0
        self._waiters: set[asyncio.Queue] = set()

    @property
    def live(self) -> bool:
        """True when frames are arriving right now (a client is watching and the upstream is healthy)."""
        return self.latest is not None and time.monotonic() - self.latest_ts < 1.0

    def stats(self) -> dict:
        age = time.monotonic() - self.latest_ts if self.latest_ts else None
        return {"clients": len(self.subs), "fps": round(self.fps, 1), "kbps": round(self.kbps),
                "age_ms": int(age * 1000) if age is not None else None, "frames": self.seq}

    async def next_frames(self, n: int, timeout: float = 1.5) -> list[bytes]:
        """The next n frames that arrive after this call (fresh frames, no stale buffer)."""
        q: asyncio.Queue = asyncio.Queue(maxsize=n + 2)
        self._waiters.add(q)
        out: list[bytes] = []
        try:
            end = time.monotonic() + timeout
            while len(out) < n:
                out.append(await asyncio.wait_for(q.get(), max(0.05, end - time.monotonic())))
        except asyncio.TimeoutError:
            pass
        finally:
            self._waiters.discard(q)
        return out

    async def subscribe(self) -> asyncio.Queue:
        q: asyncio.Queue = asyncio.Queue(maxsize=1)
        self.subs.add(q)
        if self.task is None or self.task.done():
            self.task = asyncio.create_task(self._run())
        return q

    def unsubscribe(self, q: asyncio.Queue) -> None:
        self.subs.discard(q)

    def _push(self, frame: bytes) -> None:
        now = time.monotonic()
        if self.latest_ts:
            dt = max(1e-3, now - self.latest_ts)
            self.fps = 0.8 * self.fps + 0.2 * (1.0 / dt)
            self.kbps = 0.8 * self.kbps + 0.2 * (len(frame) * 8 / 1000 / dt)
        self.latest, self.latest_ts = frame, now
        self.seq += 1
        for q in list(self._waiters):
            if not q.full():
                q.put_nowait(frame)
        for q in list(self.subs):
            if q.full():
                q.get_nowait()
            q.put_nowait(frame)

    async def _run(self) -> None:
        while self.subs:
            url = self.registry.stream_url()
            if not url:
                await asyncio.sleep(1.0)
                continue
            try:
                headers = {"X-Key": self.cfg.device_key("cam")} if self.cfg.device_key("cam") else {}
                async with self.http.stream("GET", url, headers=headers, timeout=httpx.Timeout(5.0, read=10.0)) as r:
                    buf = bytearray()
                    async for chunk in r.aiter_bytes(16384):
                        if not self.subs:
                            break
                        buf.extend(chunk)
                        for frame in _frames(buf):
                            self._push(frame)
            except (httpx.HTTPError, OSError) as exc:
                log.debug("stream upstream error: %s", exc)
            await asyncio.sleep(1.0)

    async def stop(self) -> None:
        self.subs.clear()
        if self.task:
            self.task.cancel()
