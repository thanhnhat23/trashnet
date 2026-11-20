"""Fit temperature T, reject threshold tau and margin delta on the blurred validation sets."""
import argparse
import json
from pathlib import Path

import numpy as np

from common import ROOT
from evaluate import collect, macro_f1, yolo_predictor


def _softmax_T(probs: np.ndarray, T: float) -> np.ndarray:
    p = np.power(np.clip(probs, 1e-9, 1), 1.0 / T)
    return p / p.sum(1, keepdims=True)


def fit_temperature(y, probs) -> float:
    best, bt = 1e9, 1.0
    for T in np.arange(0.5, 3.01, 0.05):
        p = _softmax_T(probs, T)
        nll = -np.log(np.clip(p[np.arange(len(y)), y], 1e-9, 1)).mean()
        if nll < best:
            best, bt = nll, float(T)
    return round(bt, 2)


def fit_reject(y, probs, other_idx=2, min_cover=0.6) -> tuple[float, float]:
    """Grid-search tau/delta maximising macro-F1 when rejects fall back to 'other'."""
    top2 = np.sort(probs, 1)[:, -2:]
    pmax, p2 = top2[:, 1], top2[:, 0]
    pred0 = probs.argmax(1)
    best = (-1.0, 0.0, 0.0)
    for tau in np.arange(0.0, 0.81, 0.05):
        for delta in np.arange(0.0, 0.41, 0.05):
            reject = (pmax < tau) | ((pmax - p2) < delta)
            if 1 - reject.mean() < min_cover:
                continue
            pred = np.where(reject, other_idx, pred0)
            f = macro_f1(list(y), list(pred))
            if f > best[0] + 1e-9:
                best = (f, float(tau), float(delta))
    return round(best[1], 2), round(best[2], 2)


def calibrate(data: Path, predict, imgsz=224) -> dict:
    ys, ps = [], []
    for name in ["val_blur_1", "val_blur_2", "val_blur_3", "val"]:
        d = data / name
        if d.exists():
            y, p = collect(d, predict, imgsz)
            ys.append(y); ps.append(p)
    y, p = np.concatenate(ys), np.concatenate(ps)
    T = fit_temperature(y, p)
    tau, delta = fit_reject(y, _softmax_T(p, T))
    return {"temperature": T, "tau": tau, "delta": delta, "n": int(len(y))}


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--weights", type=Path, required=True)
    ap.add_argument("--data", type=Path, default=ROOT / "ml" / "data" / "trashnet4")
    ap.add_argument("--imgsz", type=int, default=224)
    a = ap.parse_args()
    print(json.dumps(calibrate(a.data, yolo_predictor(a.weights), a.imgsz), indent=1))
