"""YOLO classification with letterbox, flip TTA, temperature scaling and rejection."""
import json
import logging
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Optional, Sequence

import numpy as np

from config import MODELS_DIR
from domain import labels
from services.vision import letterbox

log = logging.getLogger("infer")
DEFAULT_TAU = 0.60
DEFAULT_DELTA = 0.15

PredictFn = Callable[[list[np.ndarray]], np.ndarray]


@dataclass
class Decision:
    label: str
    confidence: float
    uncertain: bool
    probs: dict[str, float]


def apply_temperature(p: np.ndarray, temperature: float) -> np.ndarray:
    """Equivalent to softmax(logits / T) when p = softmax(logits)."""
    t = max(temperature, 1e-3)
    q = np.power(np.clip(p, 1e-9, 1.0), 1.0 / t)
    return q / q.sum(axis=-1, keepdims=True)


def vote(prob_list: Sequence[np.ndarray], weights: Optional[Sequence[float]] = None) -> np.ndarray:
    """Weighted mean of per-frame probability vectors."""
    arr = np.stack(prob_list)
    w = np.ones(len(arr)) if weights is None else np.asarray(weights, dtype=float)
    w = w / w.sum() if w.sum() > 0 else np.ones(len(arr)) / len(arr)
    return (arr * w[:, None]).sum(axis=0)


def decide(p: np.ndarray, tau: float, delta: float) -> Decision:
    """Pick a label; low-confidence or ambiguous results fall back to 'other'."""
    order = np.argsort(p)[::-1]
    top, second = float(p[order[0]]), float(p[order[1]])
    label = labels.LABEL_IDS[int(order[0])]
    uncertain = top < tau or (top - second) < delta
    if uncertain:
        label = labels.FALLBACK
    return Decision(label, top, uncertain, {k: float(v) for k, v in zip(labels.LABEL_IDS, p)})


class Classifier:
    def __init__(self, model_path: Path | None = None, metrics_path: Path | None = None,
                 predict_fn: Optional[PredictFn] = None):
        self.model_path = Path(model_path) if model_path else MODELS_DIR / "trashnet-cls.pt"
        self.metrics_path = Path(metrics_path) if metrics_path else MODELS_DIR / "metrics.json"
        self.imgsz = 224
        self.temperature = 1.0
        self.tau = DEFAULT_TAU
        self.delta = DEFAULT_DELTA
        self.metrics: dict = {}
        self._fn = predict_fn
        self._model = None
        self._order: list[int] = []
        self.error = ""
        self._read_metrics()

    @property
    def available(self) -> bool:
        return self._fn is not None or self._model is not None

    def _read_metrics(self) -> None:
        try:
            self.metrics = json.loads(self.metrics_path.read_text("utf-8"))
        except (OSError, ValueError):
            return
        self.imgsz = int(self.metrics.get("imgsz", self.imgsz))
        self.temperature = float(self.metrics.get("temperature", 1.0))
        self.tau = float(self.metrics.get("tau", DEFAULT_TAU))
        self.delta = float(self.metrics.get("delta", DEFAULT_DELTA))

    def load(self) -> bool:
        """Load the YOLO model if present; returns availability."""
        if self._fn is not None:
            return True
        if not self.model_path.exists():
            self.error = f"model not found: {self.model_path.name}"
            return False
        try:
            from ultralytics import YOLO
            self._model = YOLO(str(self.model_path))
            names = {int(k): labels.to_canonical(v) for k, v in self._model.names.items()}
            self._order = [next(i for i, n in names.items() if n == lab) for lab in labels.LABEL_IDS]
            self._fn = self._yolo_predict
            self._read_metrics()
            return True
        except Exception as exc:  # noqa: BLE001
            self.error = str(exc)
            log.error("model load failed: %s", exc)
            return False

    def _yolo_predict(self, images_rgb: list[np.ndarray]) -> np.ndarray:
        results = self._model.predict(source=images_rgb, imgsz=self.imgsz, verbose=False)
        rows = [r.probs.data.cpu().numpy()[self._order] for r in results]
        return np.stack(rows)

    def frame_probs(self, crops_bgr: list[np.ndarray]) -> list[np.ndarray]:
        """Per-frame calibrated probabilities (original + horizontal flip averaged)."""
        if not self.available:
            raise RuntimeError("model not loaded")
        boxed = [letterbox(c, self.imgsz)[:, :, ::-1] for c in crops_bgr]
        both = boxed + [np.ascontiguousarray(b[:, ::-1]) for b in boxed]
        probs = np.asarray(self._fn(both), dtype=float)
        n = len(boxed)
        merged = (probs[:n] + probs[n:]) / 2
        return [apply_temperature(p, self.temperature) for p in merged]

    def thresholds(self, tau: Optional[float], delta: Optional[float]) -> tuple[float, float]:
        return (self.tau if tau is None else tau, self.delta if delta is None else delta)
