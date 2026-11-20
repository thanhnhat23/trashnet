"""Classical vision helpers: tray presence, sharpness gate, letterbox."""
from dataclasses import dataclass
from typing import Optional

import cv2
import numpy as np

MIN_DIFF = 22
MAX_AREA_FRAC = 0.9


@dataclass
class Box:
    x: int
    y: int
    w: int
    h: int
    area_frac: float

    def as_list(self) -> list[int]:
        return [self.x, self.y, self.x + self.w, self.y + self.h]


def decode_jpeg(data: bytes) -> Optional[np.ndarray]:
    arr = np.frombuffer(data, np.uint8)
    return cv2.imdecode(arr, cv2.IMREAD_COLOR) if arr.size else None


def encode_jpeg(img: np.ndarray, quality: int = 85) -> bytes:
    ok, buf = cv2.imencode(".jpg", img, [cv2.IMWRITE_JPEG_QUALITY, quality])
    return buf.tobytes() if ok else b""


def to_gray(frame: np.ndarray) -> np.ndarray:
    gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
    return cv2.GaussianBlur(gray, (7, 7), 0)


def sharpness(img: np.ndarray) -> float:
    """Variance of the Laplacian; low values mean a blurry image."""
    gray = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY) if img.ndim == 3 else img
    return float(cv2.Laplacian(gray, cv2.CV_64F).var())


def letterbox(img: np.ndarray, size: int, fill: int = 114) -> np.ndarray:
    """Resize keeping aspect ratio and pad to a size x size square."""
    h, w = img.shape[:2]
    scale = size / max(h, w)
    nh, nw = max(1, round(h * scale)), max(1, round(w * scale))
    resized = cv2.resize(img, (nw, nh), interpolation=cv2.INTER_AREA if scale < 1 else cv2.INTER_LINEAR)
    canvas = np.full((size, size, 3), fill, np.uint8)
    top, left = (size - nh) // 2, (size - nw) // 2
    canvas[top:top + nh, left:left + nw] = resized
    return canvas


class TrayReference:
    """Empty-tray reference image, refreshed slowly while the tray stays empty."""

    def __init__(self) -> None:
        self.gray: Optional[np.ndarray] = None

    @property
    def ready(self) -> bool:
        return self.gray is not None

    def set(self, frame: np.ndarray) -> None:
        self.gray = to_gray(frame).astype(np.float32)

    def update(self, frame: np.ndarray, alpha: float = 0.05) -> None:
        g = to_gray(frame).astype(np.float32)
        if self.gray is None or self.gray.shape != g.shape:
            self.gray = g
        else:
            self.gray = (1 - alpha) * self.gray + alpha * g


def find_object(frame: np.ndarray, ref: TrayReference, min_area_frac: float = 0.015,
                min_diff: int = MIN_DIFF) -> Optional[Box]:
    """Return the bbox of the largest region differing from the empty tray, or None.

    min_diff is the smallest grey-level change that counts; a white tray with white paper needs ~6-10.
    The threshold also rises with the measured sensor noise so a low min_diff does not trigger on grain.
    """
    if not ref.ready:
        return None
    gray = to_gray(frame).astype(np.float32)
    ref_gray = ref.gray
    if ref_gray.shape != gray.shape:
        ref_gray = cv2.resize(ref_gray, (gray.shape[1], gray.shape[0]))
    if min_diff < MIN_DIFF:  # low-contrast mode: blur away grain before differencing
        gray, ref_gray = cv2.GaussianBlur(gray, (7, 7), 0), cv2.GaussianBlur(ref_gray, (7, 7), 0)
    signed = gray - ref_gray
    signed -= np.median(signed)  # ignore global brightness shifts (flash, AEC)
    noise = 1.4826 * float(np.median(np.abs(signed)))
    diff = np.clip(np.abs(signed), 0, 255).astype(np.uint8)
    otsu, _ = cv2.threshold(diff, 0, 255, cv2.THRESH_BINARY + cv2.THRESH_OTSU)
    thr = max(min_diff, 4.0 * noise, otsu if min_diff >= MIN_DIFF else 0)
    _, mask = cv2.threshold(diff, thr, 255, cv2.THRESH_BINARY)
    k_close = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (15, 15))
    k_open = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (5, 5))
    mask = cv2.morphologyEx(mask, cv2.MORPH_CLOSE, k_close)
    mask = cv2.morphologyEx(mask, cv2.MORPH_OPEN, k_open)
    contours, _ = cv2.findContours(mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
    if not contours:
        return None
    best = max(contours, key=cv2.contourArea)
    frame_area = frame.shape[0] * frame.shape[1]
    frac = cv2.contourArea(best) / frame_area
    if frac < min_area_frac or frac > MAX_AREA_FRAC:
        return None
    x, y, w, h = cv2.boundingRect(best)
    return Box(x, y, w, h, float(frac))


def crop_box(frame: np.ndarray, box: Optional[Box], pad: float = 0.1) -> np.ndarray:
    """Crop the box with relative padding; whole frame when box is None."""
    if box is None:
        return frame
    ph, pw = int(box.h * pad), int(box.w * pad)
    h, w = frame.shape[:2]
    x1, y1 = max(0, box.x - pw), max(0, box.y - ph)
    x2, y2 = min(w, box.x + box.w + pw), min(h, box.y + box.h + ph)
    return frame[y1:y2, x1:x2]
