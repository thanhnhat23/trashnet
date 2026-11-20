"""Publish trained weights: server .pt, mobile .tflite, metrics.json (T, tau, delta, scores)."""
import argparse
import json
import shutil
from pathlib import Path

from calibrate import calibrate
from common import ROOT
from evaluate import evaluate, yolo_predictor
from labeling import LABELS


def export(weights: Path, data: Path, imgsz=224) -> dict:
    pred = yolo_predictor(weights)
    cal = calibrate(data, pred, imgsz)
    metrics = {"labels": LABELS, "imgsz": imgsz, **cal, "scores": evaluate(data, pred, imgsz)}
    models = ROOT / "server" / "models"
    models.mkdir(parents=True, exist_ok=True)
    shutil.copy2(weights, models / "trashnet-cls.pt")
    (models / "metrics.json").write_text(json.dumps(metrics, indent=1), "utf-8")
    from ultralytics import YOLO
    tfl = YOLO(str(weights)).export(format="tflite", imgsz=imgsz, int8=False)
    assets = ROOT / "mobile" / "assets" / "models"
    assets.mkdir(parents=True, exist_ok=True)
    shutil.copy2(tfl, assets / "trashnet-cls.tflite")
    (assets / "metrics.json").write_text(json.dumps(metrics, indent=1), "utf-8")
    return metrics


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--weights", type=Path, default=ROOT / "ml" / "runs" / "stage2" / "weights" / "best.pt")
    ap.add_argument("--data", type=Path, default=ROOT / "ml" / "data" / "trashnet4")
    ap.add_argument("--imgsz", type=int, default=224)
    a = ap.parse_args()
    print(json.dumps(export(a.weights, a.data, a.imgsz), indent=1)[:1500])
