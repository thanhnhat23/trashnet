"""Shared FastAPI dependencies."""
from fastapi import HTTPException, Request


def tn(request: Request):
    return request.app.state.tn


def require_key(request: Request) -> None:
    """Reject mutating calls when an API key is configured and not supplied."""
    key = request.app.state.tn.cfg.get("api_key")
    if key and request.headers.get("x-api-key") != key:
        raise HTTPException(401, "invalid api key")
