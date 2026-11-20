"""Shared helpers for the ML pipeline."""
import hashlib
from pathlib import Path

import cv2
import numpy as np

ROOT = Path(__file__).resolve().parents[1]
IMG_EXT = {".jpg", ".jpeg", ".png", ".bmp", ".webp"}
FILL = 114  # must match server/services/vision.py


def letterbox(img: np.ndarray, size: int, fill: int = FILL) -> np.ndarray:
    """Resize keeping aspect ratio and pad to size x size (same as the server)."""
    h, w = img.shape[:2]
    scale = size / max(h, w)
    nh, nw = max(1, round(h * scale)), max(1, round(w * scale))
    resized = cv2.resize(img, (nw, nh), interpolation=cv2.INTER_AREA if scale < 1 else cv2.INTER_LINEAR)
    canvas = np.full((size, size, 3), fill, np.uint8)
    top, left = (size - nh) // 2, (size - nw) // 2
    canvas[top:top + nh, left:left + nw] = resized
    return canvas


def read_image(path: Path):
    """cv2.imread that also works with non-ASCII Windows paths; returns BGR or None."""
    try:
        data = np.fromfile(str(path), dtype=np.uint8)
        img = cv2.imdecode(data, cv2.IMREAD_COLOR)
    except OSError:
        return None
    return img


def write_jpeg(path: Path, img: np.ndarray, quality: int = 92) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    ok, buf = cv2.imencode(".jpg", img, [cv2.IMWRITE_JPEG_QUALITY, quality])
    if ok:
        buf.tofile(str(path))


def iter_images(root: Path):
    for p in sorted(Path(root).rglob("*")):
        if p.is_file() and p.suffix.lower() in IMG_EXT:
            yield p


def short_hash(text: str, n: int = 8) -> str:
    return hashlib.sha1(text.encode("utf-8", "ignore")).hexdigest()[:n]


def phash(img: np.ndarray) -> int:
    """64-bit DCT perceptual hash."""
    gray = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)
    small = cv2.resize(gray, (32, 32), interpolation=cv2.INTER_AREA).astype(np.float32)
    low = cv2.dct(small)[:8, :8]
    bits = (low > np.median(low[1:].ravel() if low.size > 1 else low)).flatten()
    return int("".join("1" if b else "0" for b in bits), 2)


def hamming(a: int, b: int) -> int:
    return (a ^ b).bit_count()
