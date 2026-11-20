"""Two-stage YOLO11n-cls training with blur curriculum (YOLO only, local data only).

    python ml/train.py --data ml/data/trashnet4 --device 0
"""
import argparse
import shutil
from pathlib import Path

from common import ROOT

AUG = dict(hsv_h=0.02, hsv_s=0.5, hsv_v=0.4, degrees=15, translate=0.1, scale=0.4,
           fliplr=0.5, erasing=0.2, auto_augment="randaugment")


def mix_train(data: Path, work: Path, levels: list[int]) -> Path:
    """Build a train dir = clean + selected degraded levels (symlink/copy)."""
    dst = work / "mix"
    if dst.exists():
        shutil.rmtree(dst)
    for src in [data / "train"] + [data / "train_deg" / str(l) for l in levels]:
        for p in src.rglob("*.jpg"):
            out = dst / "train" / p.parent.name / p.name
            out.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(p, out)
    for name in ("val", "test"):
        shutil.copytree(data / name, dst / name, dirs_exist_ok=True)
    for lvl in (1, 2, 3):  # blurred val doubles as the model-selection set
        shutil.copytree(data / f"val_blur_{lvl}", dst / f"val_blur_{lvl}", dirs_exist_ok=True)
    return dst


def run(data: Path, imgsz=224, epochs1=15, epochs2=40, batch=64, device="0", model="yolo11n-cls.pt"):
    from ultralytics import YOLO
    runs = ROOT / "ml" / "runs"
    # stage 1: clean + mild blur, transfer learning
    d1 = mix_train(data, runs / "s1", [1])
    m = YOLO(model)
    m.train(data=str(d1), imgsz=imgsz, epochs=epochs1, batch=batch, device=device, project=str(runs),
            name="stage1", exist_ok=True, patience=10, **AUG)
    # stage 2: all degradation levels, lower LR, start from stage-1 best
    d2 = mix_train(data, runs / "s2", [1, 2, 3])
    best1 = runs / "stage1" / "weights" / "best.pt"
    m = YOLO(str(best1))
    m.train(data=str(d2), imgsz=imgsz, epochs=epochs2, batch=batch, device=device, project=str(runs),
            name="stage2", exist_ok=True, lr0=0.002, lrf=0.05, patience=15, close_mosaic=0, **AUG)
    return runs / "stage2" / "weights" / "best.pt"


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", type=Path, default=ROOT / "ml" / "data" / "trashnet4")
    ap.add_argument("--imgsz", type=int, default=224)
    ap.add_argument("--epochs1", type=int, default=15)
    ap.add_argument("--epochs2", type=int, default=40)
    ap.add_argument("--batch", type=int, default=64)
    ap.add_argument("--device", default="0")
    a = ap.parse_args()
    print(run(a.data, a.imgsz, a.epochs1, a.epochs2, a.batch, a.device))
