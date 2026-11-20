"""Request bodies for the REST API."""
from typing import Any, Optional

from pydantic import BaseModel, Field


class Heartbeat(BaseModel):
    role: str
    ip: str
    id: str = ""
    fw: str = ""
    rssi: int = 0
    uptime: int = 0
    heap: int = 0
    state: str = ""
    stream_port: Optional[int] = None


class ManualDevice(BaseModel):
    role: str
    url: str = ""
    key: Optional[str] = None


class SettingsPatch(BaseModel):
    values: dict[str, Any] = Field(default_factory=dict)


class SortRequest(BaseModel):
    force: bool = False


class AutoRequest(BaseModel):
    enabled: bool


class Control(BaseModel):
    action: str
    bin: int = 1
    dir: int = 1
    ms: int = 100
    angle: int = 0
    us: int = 1500


class FlashRequest(BaseModel):
    on: bool
