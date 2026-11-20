"""Camera stream fan-out, snapshot and flash."""
import asyncio

from fastapi import APIRouter, Depends, HTTPException, Request, Response
from fastapi.responses import StreamingResponse

from api.deps import require_key, tn
from domain.schemas import FlashRequest
from services.device_clients import DeviceError

router = APIRouter(prefix="/camera", tags=["camera"])
BOUNDARY = "frame"


@router.get("/stream")
async def stream(request: Request):
    """MJPEG stream; every client shares a single upstream connection to the ESP32-CAM."""
    hub = tn(request).hub
    q = await hub.subscribe()

    async def gen():
        try:
            while True:
                frame = await asyncio.wait_for(q.get(), 10)
                yield (f"--{BOUNDARY}\r\nContent-Type: image/jpeg\r\nContent-Length: {len(frame)}\r\n\r\n"
                       .encode() + frame + b"\r\n")
        except asyncio.TimeoutError:
            return
        finally:
            hub.unsubscribe(q)

    return StreamingResponse(gen(), media_type=f"multipart/x-mixed-replace; boundary={BOUNDARY}",
                             headers={"Cache-Control": "no-store"})


@router.get("/stats")
async def stats(request: Request):
    """Stream health for the web watchdog: fps, bitrate, age of the last frame."""
    return tn(request).hub.stats()


@router.get("/snapshot")
async def snapshot(request: Request):
    c = tn(request)
    cap = c.cfg.get("capture")
    try:
        data = await c.cam.capture(cap["res"], cap["q"])
    except DeviceError as exc:
        raise HTTPException(502, str(exc))
    return Response(data, media_type="image/jpeg", headers={"Cache-Control": "no-store"})


@router.post("/flash", dependencies=[Depends(require_key)])
async def flash(request: Request, body: FlashRequest):
    try:
        return await tn(request).cam.flash(body.on)
    except DeviceError as exc:
        raise HTTPException(502, str(exc))
