"""Bin labels loaded from shared/labels.json (single source of truth)."""
import json
from pathlib import Path

_CANDIDATES = [
    Path(__file__).resolve().parents[2] / "shared" / "labels.json",
    Path(__file__).resolve().parents[1] / "labels.json",
]


def _load() -> list[dict]:
    for path in _CANDIDATES:
        if path.exists():
            return json.loads(path.read_text("utf-8"))["bins"]
    raise FileNotFoundError("labels.json not found")


BINS: list[dict] = _load()
LABEL_IDS: list[str] = [b["id"] for b in sorted(BINS, key=lambda b: b["bin"])]
FALLBACK = "other"

CANONICAL_MAP = {
    "paper": "paper",
    "giấy": "paper",
    "giay": "paper",
    "burnable": "paper",
    "glass": "glass",
    "thủy tinh": "glass",
    "thuy tinh": "glass",
    "plastic": "plastic",
    "nhựa": "plastic",
    "nhua": "plastic",
    "other": "other",
    "khác": "other",
    "khac": "other",
    "metal": "other",
    "battery": "other",
    "bio": "other",
    "mask": "other",
}


def to_canonical(name: str) -> str:
    key = str(name).strip().lower()
    return CANONICAL_MAP.get(key, key if key in LABEL_IDS else FALLBACK)


def get(label: str) -> dict:
    """Return bin metadata for a label id, falling back to 'other'."""
    canon = to_canonical(label)
    for b in BINS:
        if b["id"] == canon:
            return b
    return next(b for b in BINS if b["id"] == FALLBACK)


def bin_of(label: str) -> int:
    return get(label)["bin"]
