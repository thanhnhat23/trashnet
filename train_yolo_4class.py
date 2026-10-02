import shutil
from pathlib import Path
from ultralytics import YOLO
import torch

BASE_DIR = Path(__file__).resolve().parent


def main():
    print("=" * 60)
    print("TRASHNET - YOLO11n-cls TRAINING (4 CLASSES)")
    print("=" * 60)

    # 1. Chọn thiết bị train
    if torch.cuda.is_available():
        device = 0
        print(f"Dang su dung GPU: {torch.cuda.get_device_name(0)}")
    else:
        device = "cpu"
        print("Khong co GPU, dang su dung CPU")

    last_weight = BASE_DIR / "runs" / "classify" / "yolo11n_pbl4" / "weights" / "last.pt"
    if last_weight.exists():
        print(f"Tim thay checkpoint gan nhat: {last_weight}, tiep tuc huan luyen (resume)...")
        model = YOLO(str(last_weight))
        results = model.train(resume=True, workers=0)
    else:
        # 2. Load model YOLO11n-cls pretrained
        model = YOLO("yolo11n-cls.pt")

        # 3. Train model
        results = model.train(
            data=str(BASE_DIR / "data" / "pbl4_split"),
            epochs=30,
            imgsz=224,
            batch=32,
            workers=0,
            device=device,
            project=str(BASE_DIR / "runs" / "classify"),
            name="yolo11n_pbl4",
            exist_ok=True,
            save=True,
            val=True
        )

    # 4. Sao chép checkpoint tốt nhất vào models/yolo11n_trashnet_best.pt
    best_weight = BASE_DIR / "runs" / "classify" / "yolo11n_pbl4" / "weights" / "best.pt"
    target_model_path = BASE_DIR / "models" / "yolo11n_trashnet_best.pt"

    if best_weight.exists():
        target_model_path.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy(best_weight, target_model_path)
        print(f"\n[THANH CONG] Da luu model YOLO tot nhat vao: {target_model_path}")
    else:
        print(f"\n[CANH BAO] Khong tim thay file {best_weight}")

    print("\n====================================")
    print("YOLO TRAINING COMPLETED!")
    print("====================================")


if __name__ == "__main__":
    main()