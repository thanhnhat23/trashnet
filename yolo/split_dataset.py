import random
import shutil
from pathlib import Path

# Dataset gốc
SOURCE_DIR = Path("data/dataset-resized")

# Dataset mới dành cho YOLO classification
OUTPUT_DIR = Path("data/yolo-dataset")

# Tỷ lệ chia
TRAIN_RATIO = 0.8
VAL_RATIO = 0.1
TEST_RATIO = 0.1

# Seed để lần nào chạy cũng chia giống nhau
random.seed(42)

# Kiểm tra tỷ lệ
assert TRAIN_RATIO + VAL_RATIO + TEST_RATIO == 1.0

# Các class
classes = [
    "battery",
    "biological",
    "cardboard",
    "glass",
    "metal",
    "paper",
    "plastic",
    "trash",
]

for class_name in classes:

    source_class_dir = SOURCE_DIR / class_name

    if not source_class_dir.exists():
        print(f"Không tìm thấy: {source_class_dir}")
        continue

    # Lấy tất cả file ảnh
    images = [
        f for f in source_class_dir.iterdir()
        if f.is_file() and f.suffix.lower() in [".jpg", ".jpeg", ".png"]
    ]

    random.shuffle(images)

    total = len(images)

    train_end = int(total * TRAIN_RATIO)
    val_end = train_end + int(total * VAL_RATIO)

    train_images = images[:train_end]
    val_images = images[train_end:val_end]
    test_images = images[val_end:]

    splits = {
        "train": train_images,
        "val": val_images,
        "test": test_images,
    }

    print(f"\n{class_name}:")
    print(f"  Total: {total}")
    print(f"  Train: {len(train_images)}")
    print(f"  Val:   {len(val_images)}")
    print(f"  Test:  {len(test_images)}")

    for split_name, split_images in splits.items():

        destination_dir = OUTPUT_DIR / split_name / class_name
        destination_dir.mkdir(parents=True, exist_ok=True)

        for image_path in split_images:

            destination_path = destination_dir / image_path.name

            shutil.copy2(
                image_path,
                destination_path
            )

print("\n===================================")
print("Đã chia dataset thành công!")
print(f"Dataset mới: {OUTPUT_DIR}")
print("===================================")