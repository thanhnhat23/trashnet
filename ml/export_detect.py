"""Pick confidence thresholds on val / blurred / white-on-white val, then publish the detector."""
import argparse
import json
import shutil
from pathlib import Path

import numpy as np

from common import ROOT, iter_images, read_image
from labeling import LABELS


def _iou(a, b) -> float:
    ix, iy = max(0, min(a[2], b[2]) - max(a[0], b[0])), max(0, min(a[3], b[3]) - max(a[1], b[1]))
    inter = ix * iy
    ua = (a[2] - a[0]) * (a[3] - a[1]) + (b[2] - b[0]) * (b[3] - b[1]) - inter
    return inter / ua if ua > 0 else 0.0


def load_gt(label_file: Path, shape) -> list[tuple]:
    h, w = shape[:2]
    out = []
    if label_file.exists():
        for ln in label_file.read_text("utf-8").splitlines():
            c, cx, cy, bw, bh = ln.split()[:5]
            cx, cy, bw, bh = float(cx) * w, float(cy) * h, float(bw) * w, float(bh) * h
            out.append((int(c), (cx - bw / 2, cy - bh / 2, cx + bw / 2, cy + bh / 2)))
    return out


def score_thresholds(records, grid) -> list[dict]:
    """records: per image (gt list, preds list of (conf, cls, box)). A hit needs IoU>=0.5 and the right class."""
    rows = []
    for t in grid:
        tp = fp = fn = 0
        for gt, preds in records:
            ps = [p for p in preds if p[0] >= t]
            used = set()
            for c, box in gt:
                hit = next((i for i, p in enumerate(ps) if i not in used and p[1] == c and _iou(p[2], box) >= 0.5), None)
                if hit is None:
                    fn += 1
                else:
                    tp += 1
                    used.add(hit)
            fp += len(ps) - len(used)
        prec, rec = tp / max(1, tp + fp), tp / max(1, tp + fn)
        rows.append({"conf": float(t), "precision": prec, "recall": rec, "f1": 2 * prec * rec / max(1e-9, prec + rec)})
    return rows


def choose(rows) -> tuple[float, float]:
    best = max(rows, key=lambda r: (r["f1"], -r["conf"]))
    ok = [r for r in rows if r["precision"] >= 0.9 and r["conf"] >= best["conf"]]
    tau = ok[0]["conf"] if ok else min(0.9, best["conf"] + 0.15)
    return round(best["conf"], 2), round(tau, 2)


def collect(model_predict, data: Path, splits=("val", "val_blur", "val_hard")):
    recs = []
    for sp in splits:
        for img_path in iter_images(data / "images" / sp):
            img = read_image(img_path)
            if img is None:
                continue
            gt = load_gt(data / "labels" / sp / f"{img_path.stem}.txt", img.shape)
            recs.append((gt, model_predict(img)))
    return recs


def export(weights: Path, data: Path, imgsz=640) -> dict:
    from ultralytics import YOLO
    model = YOLO(str(weights))
    names = {int(k): v for k, v in model.names.items()}

    def predict(img):
        r = model.predict(img, imgsz=imgsz, conf=0.05, iou=0.5, verbose=False)[0]
        return [(float(c), LABELS.index(names[int(k)]), tuple(map(float, b)))
                for b, c, k in zip(r.boxes.xyxy.cpu().numpy(), r.boxes.conf.cpu().numpy(), r.boxes.cls.cpu().numpy())]

    rows = score_thresholds(collect(predict, data), np.arange(0.05, 0.9, 0.05))
    conf, tau = choose(rows)
    metrics = {"imgsz": imgsz, "conf": conf, "conf_low": 0.08, "tau": tau, "labels": LABELS, "curve": rows}
    models = ROOT / "server" / "models"
    models.mkdir(parents=True, exist_ok=True)
    shutil.copy2(weights, models / "trashnet-det.pt")
    (models / "det_metrics.json").write_text(json.dumps(metrics, indent=1), "utf-8")
    tfl = model.export(format="tflite", imgsz=imgsz)
    assets = ROOT / "mobile" / "assets" / "models"
    assets.mkdir(parents=True, exist_ok=True)
    shutil.copy2(tfl, assets / "trashnet-det.tflite")
    (assets / "det_metrics.json").write_text(json.dumps(metrics, indent=1), "utf-8")
    return metrics


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--weights", type=Path, default=ROOT / "ml" / "runs" / "det_stage2" / "weights" / "best.pt")
    ap.add_argument("--data", type=Path, default=ROOT / "ml" / "data" / "trashnet_det")
    ap.add_argument("--imgsz", type=int, default=640)
    a = ap.parse_args()
    m = export(a.weights, a.data, a.imgsz)
    print({k: m[k] for k in ("conf", "conf_low", "tau")})
