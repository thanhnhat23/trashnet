import json
import csv
from pathlib import Path

import torch
import torch.nn as nn
from torchvision import models, transforms
from PIL import Image


# ============================================================
# 1. ĐƯỜNG DẪN
# ============================================================

BASE_DIR = Path(__file__).resolve().parent

MODEL_PATH = BASE_DIR / "data" / "trashnet_resnet18_best.pth"

# File YOLO predictions chứa danh sách 953 ảnh test
YOLO_CSV_PATH = BASE_DIR / "yolo_predictions.csv"

# Thư mục chứa dataset test của bạn
# Nếu dataset test của bạn nằm ở vị trí khác thì sửa dòng này.
TEST_DIR = BASE_DIR / "data" / "yolo-dataset" / "test"

# File kết quả ResNet
OUTPUT_CSV = BASE_DIR / "resnet_predictions.csv"


# ============================================================
# 2. CLASS
# ============================================================

CLASSES = [
    "battery",
    "biological",
    "cardboard",
    "glass",
    "metal",
    "paper",
    "plastic",
    "trash"
]


# ============================================================
# 3. DEVICE
# ============================================================

device = torch.device("cuda" if torch.cuda.is_available() else "cpu")

print("=" * 60)
print("RESNET18 PREDICTION")
print("=" * 60)
print(f"Device: {device}")
print(f"Model : {MODEL_PATH}")
print()


# ============================================================
# 4. LOAD RESNET18
# ============================================================

print("Loading ResNet18...")

model = models.resnet18(weights=None)

model.fc = nn.Linear(
    model.fc.in_features,
    len(CLASSES)
)

state_dict = torch.load(
    MODEL_PATH,
    map_location=device
)

model.load_state_dict(state_dict)

model = model.to(device)
model.eval()

print("ResNet18 loaded successfully!")
print()


# ============================================================
# 5. PREPROCESSING
# ============================================================
# Giữ đúng preprocessing của code ResNet hiện tại:
#
# Resize 224x224
# ToTensor
# Normalize theo ImageNet
# ============================================================

transform = transforms.Compose([
    transforms.Resize((224, 224)),
    transforms.ToTensor(),
    transforms.Normalize(
        mean=[0.485, 0.456, 0.406],
        std=[0.229, 0.224, 0.225]
    )
])


# ============================================================
# 6. LẤY DANH SÁCH ẢNH TỪ YOLO CSV
# ============================================================
# Làm như vậy để đảm bảo ResNet dự đoán ĐÚNG 953 ảnh
# mà YOLO đã dự đoán.
# ============================================================

if not YOLO_CSV_PATH.exists():
    raise FileNotFoundError(
        f"Không tìm thấy: {YOLO_CSV_PATH}"
    )

image_records = []

with open(YOLO_CSV_PATH, "r", encoding="utf-8") as f:
    reader = csv.DictReader(f)

    for row in reader:
        image_records.append(row)


print(f"Images from YOLO CSV: {len(image_records)}")


# ============================================================
# 7. PREDICT
# ============================================================

results = []

correct = 0
total = 0

with torch.no_grad():

    for i, row in enumerate(image_records, start=1):

        image_name = row["image"]
        true_class = row["true_class"]

        # ----------------------------------------------------
        # Tìm ảnh trong thư mục test
        # ----------------------------------------------------

        image_path = TEST_DIR / true_class / image_name

        if not image_path.exists():

            # Trường hợp dataset test không được chia folder
            # theo class thì thử tìm trực tiếp.
            alternative_path = TEST_DIR / image_name

            if alternative_path.exists():
                image_path = alternative_path

            else:
                print(
                    f"[WARNING] Không tìm thấy ảnh: "
                    f"{true_class}/{image_name}"
                )
                continue

        # ----------------------------------------------------
        # Load ảnh
        # ----------------------------------------------------

        try:
            image = Image.open(image_path).convert("RGB")

        except Exception as e:

            print(
                f"[WARNING] Không đọc được {image_path}: {e}"
            )

            continue

        # ----------------------------------------------------
        # Preprocess
        # ----------------------------------------------------

        input_tensor = transform(image)

        input_tensor = input_tensor.unsqueeze(0)

        input_tensor = input_tensor.to(device)

        # ----------------------------------------------------
        # ResNet prediction
        # ----------------------------------------------------

        output = model(input_tensor)

        probabilities = torch.softmax(
            output,
            dim=1
        )[0]

        predicted_index = torch.argmax(
            probabilities
        ).item()

        predicted_class = CLASSES[predicted_index]

        # ----------------------------------------------------
        # Accuracy
        # ----------------------------------------------------

        if predicted_class == true_class:
            correct += 1

        total += 1

        # ----------------------------------------------------
        # Lưu kết quả
        # ----------------------------------------------------

        result = {
            "image": image_name,
            "true_class": true_class,
            "predicted_class": predicted_class
        }

        for index, class_name in enumerate(CLASSES):

            result[f"prob_{class_name}"] = (
                probabilities[index].item()
            )

        results.append(result)

        # ----------------------------------------------------
        # Progress
        # ----------------------------------------------------

        if i % 50 == 0 or i == len(image_records):

            current_accuracy = (
                correct / total * 100
                if total > 0
                else 0
            )

            print(
                f"[{i}/{len(image_records)}] "
                f"Accuracy: {current_accuracy:.2f}%"
            )


# ============================================================
# 8. SAVE CSV
# ============================================================

fieldnames = [
    "image",
    "true_class",
    "predicted_class"
]

fieldnames += [
    f"prob_{class_name}"
    for class_name in CLASSES
]


with open(
    OUTPUT_CSV,
    "w",
    newline="",
    encoding="utf-8"
) as f:

    writer = csv.DictWriter(
        f,
        fieldnames=fieldnames
    )

    writer.writeheader()

    writer.writerows(results)


# ============================================================
# 9. FINAL RESULT
# ============================================================

accuracy = (
    correct / total * 100
    if total > 0
    else 0
)

print()
print("=" * 60)
print("DONE!")
print("=" * 60)

print(f"Total images : {total}")
print(f"Correct      : {correct}")
print(f"Accuracy     : {accuracy:.2f}%")
print(f"Output       : {OUTPUT_CSV}")
print("=" * 60)