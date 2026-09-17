from pathlib import Path
from ultralytics import YOLO
import torch

BASE_DIR = Path(__file__).resolve().parent

def main():
    # 1. Chọn thiết bị train
    if torch.cuda.is_available():
        device = 0
        print("Dang su dung GPU")
    else:
        device = "cpu"
        print("Khong co GPU, dang su dung CPU")

    # 2. Load model YOLO11n-cls pretrained
    model = YOLO("yolo11n-cls.pt")

    # 3. Train model
    model.train(
        data=str(BASE_DIR / "yolo_dataset"),
        epochs=30,
        imgsz=224,
        batch=16,
        device=device
    )


if __name__ == "__main__":
    main()