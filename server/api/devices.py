"""Device registry, heartbeats and settings."""
from fastapi import APIRouter, Depends, Request

from api.deps import require_key, tn
from domain.schemas import Heartbeat, ManualDevice, SettingsPatch
from services.discovery import normalize_role
from services.rescan import rescan

router = APIRouter(tags=["devices"])


@router.get("/devices")
async def devices(request: Request):
    return tn(request).registry.snapshot()


@router.post("/devices/heartbeat")
async def heartbeat(request: Request, hb: Heartbeat):
    """Devices announce themselves here every 15 s (works even if the server starts later)."""
    info = hb.model_dump(exclude={"role", "ip"})
    info = {k: v for k, v in info.items() if v not in (None, "")}
    tn(request).registry.update(hb.role, hb.ip, info, "heartbeat")
    return {"ok": True}


@router.post("/devices/manual", dependencies=[Depends(require_key)])
async def manual(request: Request, body: ManualDevice):
    c = tn(request)
    role = normalize_role(body.role)
    if not role:
        return {"ok": False, "error": "unknown role"}
    patch = {"devices": {role: body.url}}
    if body.key is not None:
        patch["device_keys"] = {role: body.key}
    c.cfg.update(patch)
    c.bus.publish("device", c.registry.describe(role))
    return {"ok": True, "device": c.registry.describe(role)}


@router.get("/settings")
async def get_settings(request: Request):
    c = tn(request)
    out = c.cfg.snapshot(hide_secrets=True)
    out["_meta"] = _settings_meta(c)
    return out


def _settings_meta(c) -> dict:
    """What an empty threshold means right now (the model's own value) and the factory defaults, for the UI."""
    from config import DEFAULTS
    d = c.detector
    model = {"det_conf": d.conf, "det_conf_low": d.conf_low, "det_tau": d.tau}
    try:
        model["tau"], model["delta"] = c.classifier.thresholds(None, None)
    except Exception:  # noqa: BLE001
        pass
    keep = ("auto_interval_sec", "cooldown_sec", "vote_frames", "blur_min", "min_area_frac", "presence_min_diff",
            "capture", "capture_from_stream", "job_timeout_sec")
    return {"detect_mode": d.available, "model": model, "defaults": {k: DEFAULTS[k] for k in keep}}


@router.patch("/settings", dependencies=[Depends(require_key)])
async def patch_settings(request: Request, body: SettingsPatch):
    c = tn(request)
    c.cfg.update(body.values)
    return c.cfg.snapshot(hide_secrets=True)


@router.post("/devices/rescan", dependencies=[Depends(require_key)])
async def rescan_devices(request: Request, deep: bool = True):
    """Reconnect button: re-probe known URLs, try mDNS names, then sweep the LAN for missing boards."""
    return {"devices": await rescan(tn(request), deep=deep)}
