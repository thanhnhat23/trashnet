"""Evaluate a classifier on clean and blurred sets; report macro-F1 and confusion."""
import argparse
import json
from pathlib import Path

import numpy as np

from common import ROOT, iter_images, letterbox, read_image
from labeling import LABELS


def macro_f1(y_true, y_pred, n=len(LABELS)) -> float:
    f = []
    for c in range(n):
        tp = sum(1 for t, p in zip(y_true, y_pred) if t == c and p == c)
        fp = sum(1 for t, p in zip(y_true, y_pred) if t != c and p == c)
        fn = sum(1 for t, p in zip(y_true, y_pred) if t == c and p != c)
        f.append(0.0 if tp == 0 else 2 * tp / (2 * tp + fp + fn))
    return float(np.mean(f))


def collect(split_dir: Path, predict, imgsz: int):
    """predict(list[BGR]) -> (N,4) probabilities in LABELS order. Returns y_true, probs."""
    ys, imgs, probs = [], [], []
    for p in iter_images(split_dir):
        if p.parent.name not in LABELS:
            continue
        img = read_image(p)
        if img is None:
            continue
        ys.append(LABELS.index(p.parent.name))
        imgs.append(letterbox(img, imgsz))
        if len(imgs) == 64:
            probs.append(predict(imgs)); imgs = []
    if imgs:
        probs.append(predict(imgs))
    return np.array(ys), (np.concatenate(probs) if probs else np.zeros((0, len(LABELS))))


def score(y, probs) -> dict:
    if len(y) == 0:
        return {"n": 0}
    pred = probs.argmax(1)
    cm = np.zeros((4, 4), int)
    for t, p in zip(y, pred):
        cm[t, p] += 1
    return {"n": int(len(y)), "acc": float((pred == y).mean()), "macro_f1": macro_f1(list(y), list(pred)),
            "confusion": cm.tolist()}


def yolo_predictor(weights: Path, device="cpu"):
    from ultralytics import YOLO
    model = YOLO(str(weights))
    names = model.names
    order = [next(i for i, n in names.items() if n == l) for l in LABELS]

    def predict(imgs):
        res = model.predict(imgs, device=device, verbose=False)
        return np.stack([r.probs.data.cpu().numpy()[order] for r in res])
    return predict


def evaluate(data: Path, predict, imgsz=224) -> dict:
    out = {}
    for name in ["val", "test", "val_blur_1", "val_blur_2", "val_blur_3", "test_blur_1", "test_blur_2", "test_blur_3"]:
        d = data / name
        if d.exists():
            out[name] = score(*collect(d, predict, imgsz))
    return out


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--weights", type=Path, required=True)
    ap.add_argument("--data", type=Path, default=ROOT / "ml" / "data" / "trashnet4")
    ap.add_argument("--imgsz", type=int, default=224)
    ap.add_argument("--device", default="cpu")
    a = ap.parse_args()
    print(json.dumps(evaluate(a.data, yolo_predictor(a.weights, a.device), a.imgsz), indent=1))
