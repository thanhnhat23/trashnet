"""Health, model info and the SSE event stream."""
import asyncio
import time

from fastapi import APIRouter, Request
from fastapi.responses import StreamingResponse

from api.deps import tn
from domain import labels
from services.event_bus import sse_format

router = APIRouter(tags=["system"])


@router.get("/health")
async def health(request: Request):
    c = tn(request)
    return {"ok": True, "time": time.time(),
            "model": {"available": c.detector.available or c.classifier.available, "mode": "detect" if c.detector.available else "classify",
                      "error": c.detector.error or c.classifier.error},
            "reference": c.ref.ready}


BUILD = "2026-11-20 · kết quả sớm + stream"


@router.get("/info")
async def info(request: Request):
    c = tn(request)
    k = c.classifier
    tau, delta = k.thresholds(c.cfg.get("tau"), c.cfg.get("delta"))
    return {"build": BUILD, "labels": labels.BINS, "imgsz": k.imgsz, "temperature": k.temperature,
            "tau": tau, "delta": delta, "metrics": k.metrics,
            "mode": "detect" if c.detector.available else "classify", "det_metrics": c.detector.metrics}


@router.get("/events")
async def events(request: Request, once: bool = False):
    """Server-sent events: device, sort, result, actuator, error."""
    c = tn(request)
    q = c.bus.open()

    async def gen():
        try:
            yield sse_format({"type": "hello", "data": {
                "devices": c.registry.snapshot(), "state": c.orch.state, "auto": c.orch.auto}})
            if once:
                return
            while True:
                try:
                    yield sse_format(await asyncio.wait_for(q.get(), 15))
                except asyncio.TimeoutError:
                    yield ": keepalive\n\n"
        finally:
            c.bus.close(q)

    return StreamingResponse(gen(), media_type="text/event-stream",
                             headers={"Cache-Control": "no-store", "X-Accel-Buffering": "no"})
