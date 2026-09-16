from ultralytics import YOLO

# Load model đã train
model = YOLO("models/yolo/yolo11n_trashnet_best.pt")

# Ảnh cần dự đoán
image_path = "test_yolo/test.jpg"

# Predict
results = model.predict(
    source=image_path,
    imgsz=224
)

# Lấy kết quả
result = results[0]

# Class có xác suất cao nhất
top1_index = result.probs.top1
top1_name = result.names[top1_index]
top1_conf = result.probs.top1conf.item()

print("=" * 50)
print("YOLO11n-CLS PREDICTION")
print("=" * 50)

print(f"Class      : {top1_name}")
print(f"Confidence : {top1_conf * 100:.2f}%")

print("\nAll probabilities:")

for index, probability in enumerate(result.probs.data):
    class_name = result.names[index]
    print(f"{class_name:12s}: {probability.item() * 100:.2f}%")