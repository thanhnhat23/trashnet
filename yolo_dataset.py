import os
import random
import shutil

# 1. Đường dẫn

SOURCE_DIR = r"C:\PBL4\trashnet\data\dataset-resized"
OUTPUT_DIR = r"C:\PBL4\trashnet\yolo_dataset"

# Tỷ lệ train / validation
TRAIN_RATIO = 0.8
random.seed(42)


# 2. Mapping 8 class → 4 class

CLASS_MAPPING = {
    "battery": "hazardous",
    "biological": "organic",
    "cardboard": "recyclable",
    "glass": "recyclable",
    "metal": "recyclable",
    "paper": "recyclable",
    "plastic": "recyclable",
    "trash": "non_recyclable"
}


# 3. Số ảnh muốn lấy

TARGET_IMAGES = {
    "battery": 945,
    "biological": 985,
    "cardboard": 240,
    "glass": 240,
    "metal": 240,
    "paper": 240,
    "plastic": 240,
    "trash": 1196
}


# 4. Tạo thư mục

for split in ["train", "val"]:
    for class_name in [
        "hazardous",
        "organic",
        "recyclable",
        "non_recyclable"
    ]:
        folder = os.path.join(
            OUTPUT_DIR,
            split,
            class_name
        )

        os.makedirs(folder, exist_ok=True)


# 5. Copy ảnh

for source_class, target_class in CLASS_MAPPING.items():

    source_folder = os.path.join(
        SOURCE_DIR,
        source_class
    )

    if not os.path.exists(source_folder):
        print(f"KHONG TIM THAY: {source_folder}")
        continue

    images = [
        file for file in os.listdir(source_folder)
        if file.lower().endswith(
            (".jpg", ".jpeg", ".png", ".bmp", ".webp")
        )
    ]

    random.shuffle(images)

    target_number = TARGET_IMAGES[source_class]

    if len(images) < target_number:
        print(
            f"CANH BAO: {source_class} "
            f"chi co {len(images)} anh"
        )
        selected_images = images
    else:
        selected_images = images[:target_number]

    train_count = int(
        len(selected_images) * TRAIN_RATIO
    )

    train_images = selected_images[:train_count]
    val_images = selected_images[train_count:]

    # Copy train
    for image in train_images:

        src = os.path.join(
            source_folder,
            image
        )

        dst = os.path.join(
            OUTPUT_DIR,
            "train",
            target_class,
            image
        )

        shutil.copy2(src, dst)

    # Copy validation
    for image in val_images:

        src = os.path.join(
            source_folder,
            image
        )

        dst = os.path.join(
            OUTPUT_DIR,
            "val",
            target_class,
            image
        )

        shutil.copy2(src, dst)

    print(
        f"{source_class:12} -> "
        f"{target_class:15} | "
        f"train: {len(train_images):4} | "
        f"val: {len(val_images):4}"
    )


print("\n================================")
print("HOAN THANH!")
print("Dataset YOLO:")
print(OUTPUT_DIR)
print("================================")