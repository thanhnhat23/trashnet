from ultralytics import YOLO

MODEL_PATH = "models/yolo11n_trashnet_best.pt"
IMAGE_PATH = "test_yolo/test.png"

model = YOLO(MODEL_PATH)

results = model.predict(
    source=IMAGE_PATH,
    imgsz=224,
    verbose=False
)

result = results[0]

print("=" * 50)
print("YOLO11n-cls TEST")
print("=" * 50)

print("Image:", IMAGE_PATH)
print("Predicted class:", result.names[result.probs.top1])
print("Confidence:", f"{result.probs.top1conf.item() * 100:.2f}%")

print("\nAll probabilities:")

for class_id, probability in enumerate(result.probs.data):
    class_name = result.names[class_id]
    print(f"{class_name:12s}: {probability.item() * 100:.2f}%")