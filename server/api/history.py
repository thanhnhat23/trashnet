"""History routes."""
from fastapi import APIRouter, Depends, HTTPException, Request
from fastapi.responses import FileResponse

from api.deps import require_key, tn

router = APIRouter(prefix="/history", tags=["history"])


@router.get("")
async def list_history(request: Request, limit: int = 100, offset: int = 0, label: str = ""):
    h = tn(request).history
    return {"items": h.list(min(limit, 500), offset, label), "stats": h.stats()}


@router.get("/stats")
async def stats(request: Request):
    return tn(request).history.stats()


@router.get("/{rec_id}/thumb")
async def thumb(request: Request, rec_id: str):
    path = tn(request).history.thumb_path(rec_id)
    if not path.exists():
        raise HTTPException(404)
    return FileResponse(path, media_type="image/jpeg")


@router.delete("/{rec_id}", dependencies=[Depends(require_key)])
async def delete_one(request: Request, rec_id: str):
    tn(request).history.delete(rec_id)
    return {"ok": True}


@router.delete("", dependencies=[Depends(require_key)])
async def clear(request: Request):
    tn(request).history.clear()
    return {"ok": True}
