from ultralytics import YOLO

# ============================================================
# YOLO11n-cls - PRETRAINED / TRANSFER LEARNING
# ============================================================

model = YOLO("yolo11n-cls.pt")

# TRAIN

results = model.train(
    data="data/yolo-dataset",

    # Training
    epochs=50,
    imgsz=224,

    # Batch
    batch=16,

    # Mac Intel
    device="cpu",
    workers=0,

    # Optimizer
    optimizer="SGD",

    # Output
    project="runs",
    name="yolo11n_trashnet",

    # Save best model
    save=True,

    # Validation
    val=True
)

print("\n====================================")
print("YOLO TRAINING COMPLETED!")
print("====================================")