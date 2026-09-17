from ultralytics import YOLO
import torch

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
    data=r"C:\PBL4\trashnet\yolo_dataset",
    epochs=30,
    imgsz=224,
    batch=16,
    device=device
)