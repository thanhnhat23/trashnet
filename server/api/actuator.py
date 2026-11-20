"""Actuator proxy routes (status, calibration, manual control)."""
from fastapi import APIRouter, Depends, HTTPException, Request

from api.deps import require_key, tn
from domain.schemas import Control
from services.device_clients import DeviceError

router = APIRouter(prefix="/actuator", tags=["actuator"])


@router.get("/status")
async def status(request: Request):
    try:
        return {"online": True, **await tn(request).act.status()}
    except DeviceError as exc:
        return {"online": False, "error": str(exc)}


@router.get("/calib")
async def calib_get(request: Request):
    try:
        return await tn(request).act.calib_get()
    except DeviceError as exc:
        raise HTTPException(502, str(exc))


@router.post("/calib", dependencies=[Depends(require_key)])
async def calib_set(request: Request, values: dict):
    try:
        return await tn(request).act.calib_set(values)
    except DeviceError as exc:
        raise HTTPException(502, str(exc))


@router.post("/control", dependencies=[Depends(require_key)])
async def control(request: Request, c: Control):
    act = tn(request).act
    routes = {
        "bin": lambda: act.action(c.bin, "manual", "manual"),
        "rotate": lambda: act.post("/rotate", {"bin": c.bin}),
        "dump": lambda: act.post("/servo", {"dump": 1}),
        "lid_open": lambda: act.post("/servo", {"lid": "open"}),
        "lid_close": lambda: act.post("/servo", {"lid": "close"}),
        "servo": lambda: act.post("/servo", {"angle": c.angle}),
        "lid_angle": lambda: act.post("/servo", {"lid_angle": c.angle}),
        "jog": lambda: act.post("/jog", {"dir": c.dir, "ms": c.ms}),
        "home_set": lambda: act.post("/home/set"),
        "neutral": lambda: act.post("/calib/neutral", {"us": c.us}),
    }
    if c.action not in routes:
        raise HTTPException(400, "unknown action")
    try:
        res = await routes[c.action]()
    except DeviceError as exc:
        raise HTTPException(502, str(exc))
    code, body = res if isinstance(res, tuple) else (200, res)
    if code == 409 or (isinstance(body, dict) and body.get("error") == "busy"):
        raise HTTPException(409, "Actuator đang bận, thử lại sau")
    if isinstance(body, dict) and body.get("error"):
        raise HTTPException(400, str(body["error"]))
    return body
