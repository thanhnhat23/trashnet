"""Train YOLO11n detection on the small bounding-box set (about 150 images per class).

    python ml/train_detect.py --device 0
Stage 1 trains the head only (backbone frozen), stage 2 fine-tunes everything with strong augmentation.
"""
import argparse
from pathlib import Path

from common import ROOT

AUG = dict(hsv_h=0.02, hsv_s=0.5, hsv_v=0.5, degrees=15, translate=0.1, scale=0.5, fliplr=0.5, flipud=0.5,
           mosaic=1.0, mixup=0.1, erasing=0.2, close_mosaic=15)


def run(data: Path, imgsz=640, epochs1=30, epochs2=120, batch=16, device="0", model="yolo11n.pt") -> Path:
    from ultralytics import YOLO
    runs = ROOT / "ml" / "runs"
    yaml = str(data / "data.yaml")
    m = YOLO(model)
    m.train(data=yaml, imgsz=imgsz, epochs=epochs1, batch=batch, device=device, project=str(runs), name="det_stage1",
            exist_ok=True, freeze=10, patience=15, **AUG)
    m = YOLO(str(runs / "det_stage1" / "weights" / "best.pt"))
    m.train(data=yaml, imgsz=imgsz, epochs=epochs2, batch=batch, device=device, project=str(runs), name="det_stage2",
            exist_ok=True, lr0=0.002, lrf=0.05, patience=30, **AUG)
    return runs / "det_stage2" / "weights" / "best.pt"


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", type=Path, default=ROOT / "ml" / "data" / "trashnet_det")
    ap.add_argument("--imgsz", type=int, default=640)
    ap.add_argument("--epochs1", type=int, default=30)
    ap.add_argument("--epochs2", type=int, default=120)
    ap.add_argument("--batch", type=int, default=16)
    ap.add_argument("--device", default="0")
    a = ap.parse_args()
    print(run(a.data, a.imgsz, a.epochs1, a.epochs2, a.batch, a.device))
