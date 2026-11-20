"""Prediction and sorting-cycle routes."""
from fastapi import APIRouter, Depends, Request

from api.deps import require_key, tn
from domain.schemas import AutoRequest, SortRequest
from services.device_clients import DeviceError

router = APIRouter(tags=["sort"])


@router.post("/predict")
async def predict(request: Request):
    """Classify one JPEG (raw body or multipart field 'image'); never moves hardware."""
    c = tn(request)
    if not (c.detector.available or c.classifier.available):
        return {"ok": False, "error": "model not loaded: " + (c.detector.error or c.classifier.error)}
    ctype = request.headers.get("content-type", "")
    if ctype.startswith("multipart/"):
        form = await request.form()
        upload = form.get("image")
        data = await upload.read() if upload else b""
    else:
        data = await request.body()
    return c.orch.classify_image(data)


@router.post("/sort/capture", dependencies=[Depends(require_key)])
async def sort_capture(request: Request, body: SortRequest = SortRequest()):
    return await tn(request).orch.run_once("manual", force=body.force)


@router.post("/sort/auto", dependencies=[Depends(require_key)])
async def sort_auto(request: Request, body: AutoRequest):
    orch = tn(request).orch
    orch.set_auto(body.enabled)
    return {"auto": orch.auto}


@router.get("/sort/state")
async def sort_state(request: Request):
    orch = tn(request).orch
    return {"state": orch.state, "auto": orch.auto, "last": orch.last,
            "reference": tn(request).ref.ready}


@router.post("/camera/reference", dependencies=[Depends(require_key)])
async def set_reference(request: Request):
    """Capture the current (empty) tray as the presence reference."""
    try:
        return await tn(request).orch.set_reference()
    except DeviceError as exc:
        return {"ok": False, "error": str(exc)}

