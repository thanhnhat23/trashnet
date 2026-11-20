"""Runtime settings stored in server/var/settings.json (atomic writes)."""
import copy
import json
import os
import threading
from pathlib import Path

ROOT = Path(__file__).resolve().parent
VAR_DIR = Path(os.environ.get("TRASHNET_VAR", ROOT / "var"))
MODELS_DIR = Path(os.environ.get("TRASHNET_MODELS", ROOT / "models"))

DEFAULTS = {
    "api_key": "",
    "devices": {"cam": "", "actuator": ""},
    "device_keys": {"cam": "", "actuator": ""},
    "auto_enabled": False,
    "auto_interval_sec": 3.0,
    "cooldown_sec": 2.0,
    "job_timeout_sec": 30.0,
    "capture": {"res": "svga", "q": 10, "flash": False, "warmup": 2},
    "vote_frames": 3,
    "capture_from_stream": True,
    "blur_min": 40.0,
    "min_area_frac": 0.015,
    "tau": None,
    "delta": None,
    "det_conf": None,
    "det_conf_low": None,
    "det_tau": None,
    "presence_min_diff": 8,
}


def _merge(base: dict, patch: dict) -> dict:
    out = copy.deepcopy(base)
    for key, val in patch.items():
        if key not in base:
            continue
        if isinstance(base[key], dict) and isinstance(val, dict):
            out[key] = _merge(base[key], val)
        else:
            out[key] = val
    return out


class Settings:
    """Thread-safe settings with whitelist merge and atomic persistence."""

    def __init__(self, path: Path | None = None):
        self.path = Path(path) if path else VAR_DIR / "settings.json"
        self._lock = threading.Lock()
        self._data = copy.deepcopy(DEFAULTS)
        self._load()

    def _load(self) -> None:
        try:
            self._data = _merge(DEFAULTS, json.loads(self.path.read_text("utf-8")))
        except (OSError, ValueError):
            pass

    def get(self, key: str):
        with self._lock:
            return copy.deepcopy(self._data[key])

    def snapshot(self, hide_secrets: bool = False) -> dict:
        with self._lock:
            data = copy.deepcopy(self._data)
        if hide_secrets:
            data["api_key"] = bool(data["api_key"])
            data["device_keys"] = {k: bool(v) for k, v in data["device_keys"].items()}
        return data

    def update(self, patch: dict) -> dict:
        with self._lock:
            self._data = _merge(self._data, patch)
            data = copy.deepcopy(self._data)
        self._save(data)
        return data

    def _save(self, data: dict) -> None:
        self.path.parent.mkdir(parents=True, exist_ok=True)
        tmp = self.path.with_suffix(".tmp")
        tmp.write_text(json.dumps(data, indent=2), "utf-8")
        os.replace(tmp, self.path)

    def device_url(self, role: str) -> str:
        return self.get("devices").get(role, "")

    def device_key(self, role: str) -> str:
        return self.get("device_keys").get(role, "")
