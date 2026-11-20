"""YOLO detection (bounding boxes) used to find AND classify the waste on the tray."""
import json
import logging
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Optional, Sequence

import numpy as np

from config import MODELS_DIR
from domain import labels

log = logging.getLogger("detect")
DEFAULT_CONF = 0.30
DEFAULT_CONF_LOW = 0.08
DEFAULT_TAU = 0.45

# (rgb image, min confidence) -> rows of (x1, y1, x2, y2, conf, class_index in labels.LABEL_IDS)
DetectFn = Callable[[np.ndarray, float], Sequence[Sequence[float]]]


@dataclass
class Det:
    x1: int
    y1: int
    x2: int
    y2: int
    conf: float
    cls: int

    @property
    def label(self) -> str:
        return labels.LABEL_IDS[self.cls]

    @property
    def area(self) -> int:
        return max(0, self.x2 - self.x1) * max(0, self.y2 - self.y1)

    def shifted(self, dx: int, dy: int) -> "Det":
        return Det(self.x1 + dx, self.y1 + dy, self.x2 + dx, self.y2 + dy, self.conf, self.cls)


def iou(a: Det, b: Det) -> float:
    ix = max(0, min(a.x2, b.x2) - max(a.x1, b.x1))
    iy = max(0, min(a.y2, b.y2) - max(a.y1, b.y1))
    inter = ix * iy
    union = a.area + b.area - inter
    return inter / union if union else 0.0


def class_scores(dets: Sequence[Det], target: Det, min_iou: float = 0.3) -> np.ndarray:
    """Highest confidence of every class among boxes on the same object (IoU with target).
    YOLO scores are per class and do not sum to 1: this is what the model 'thought' of each bin."""
    out = np.zeros(len(labels.LABEL_IDS))
    for d in dets:
        if d is target or iou(d, target) >= min_iou:
            out[d.cls] = max(out[d.cls], d.conf)
    return out


def best_detection(dets: Sequence[Det]) -> Optional[Det]:
    """One object per tray: highest confidence, ties broken by area."""
    return max(dets, key=lambda d: (round(d.conf, 3), d.area), default=None)


def vote_detections(per_frame: Sequence[Optional[Det]], weights: Sequence[float]):
    """Weighted class vote over frames. Returns (label_probs, best_det_of_winning_class)."""
    score = np.zeros(len(labels.LABEL_IDS))
    for d, w in zip(per_frame, weights):
        if d is not None:
            score[d.cls] += d.conf * w
    total = score.sum()
    if total <= 0:
        return None, None
    probs = score / total
    win = int(np.argmax(score))
    cands = [d for d in per_frame if d is not None and d.cls == win]
    best = max(cands, key=lambda d: d.conf)
    # confidence of the winner = its mean per-frame confidence, scaled by how many frames agreed
    agree = len(cands) / max(1, sum(d is not None for d in per_frame))
    best = Det(best.x1, best.y1, best.x2, best.y2, float(np.mean([d.conf for d in cands]) * (0.6 + 0.4 * agree)), win)
    return probs, best


class Detector:
    def __init__(self, model_path: Path | None = None, metrics_path: Path | None = None,
                 predict_fn: Optional[DetectFn] = None):
        self.model_path = Path(model_path) if model_path else MODELS_DIR / "trashnet-det.pt"
        self.metrics_path = Path(metrics_path) if metrics_path else MODELS_DIR / "det_metrics.json"
        self.imgsz = 640
        self.conf = DEFAULT_CONF
        self.conf_low = DEFAULT_CONF_LOW
        self.tau = DEFAULT_TAU
        self.metrics: dict = {}
        self.error = ""
        self._fn = predict_fn
        self._model = None
        self._order: dict[int, int] = {}
        self._read_metrics()

    @property
    def available(self) -> bool:
        return self._fn is not None

    def _read_metrics(self) -> None:
        try:
            self.metrics = json.loads(self.metrics_path.read_text("utf-8"))
        except (OSError, ValueError):
            return
        self.imgsz = int(self.metrics.get("imgsz", self.imgsz))
        self.conf = float(self.metrics.get("conf", self.conf))
        self.conf_low = float(self.metrics.get("conf_low", self.conf_low))
        self.tau = float(self.metrics.get("tau", self.tau))

    def load(self) -> bool:
        if self._fn is not None:
            return True
        if not self.model_path.exists():
            self.error = f"model not found: {self.model_path.name}"
            return False
        try:
            from ultralytics import YOLO
            self._model = YOLO(str(self.model_path))
            names = {int(k): v for k, v in self._model.names.items()}
            self._order = {i: labels.LABEL_IDS.index(labels.to_canonical(n)) for i, n in names.items() if labels.to_canonical(n) in labels.LABEL_IDS}
            self._fn = self._yolo_detect
            self._read_metrics()
            return True
        except Exception as exc:  # noqa: BLE001
            self.error = str(exc)
            log.error("detector load failed: %s", exc)
            return False

    def _yolo_detect(self, rgb: np.ndarray, conf: float):
        res = self._model.predict(source=rgb, imgsz=self.imgsz, conf=conf, iou=0.5, verbose=False)[0]
        rows = []
        for (x1, y1, x2, y2), c, k in zip(res.boxes.xyxy.cpu().numpy(), res.boxes.conf.cpu().numpy(),
                                          res.boxes.cls.cpu().numpy()):
            if int(k) in self._order:
                rows.append((x1, y1, x2, y2, float(c), self._order[int(k)]))
        return rows

    def detect(self, frame_bgr: np.ndarray, conf: Optional[float] = None) -> list[Det]:
        """All detections above conf on the full frame (BGR in, pixel boxes out)."""
        if not self.available:
            raise RuntimeError("detector not loaded")
        h, w = frame_bgr.shape[:2]
        rows = self._fn(np.ascontiguousarray(frame_bgr[:, :, ::-1]), self.conf if conf is None else conf)
        out = []
        for x1, y1, x2, y2, c, k in rows:
            d = Det(int(max(0, x1)), int(max(0, y1)), int(min(w, x2)), int(min(h, y2)), float(c), int(k))
            if d.area > 0:
                out.append(d)
        return out

    def detect_in(self, frame_bgr: np.ndarray, roi: tuple[int, int, int, int], conf: float) -> list[Det]:
        """Low-confidence pass restricted to a region (used when the change cue fires but the full pass is silent)."""
        x1, y1, x2, y2 = roi
        sub = frame_bgr[y1:y2, x1:x2]
        return [d.shifted(x1, y1) for d in self.detect(sub, conf)]
