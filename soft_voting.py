import pandas as pd
import numpy as np
from pathlib import Path


# ============================================================
# CONFIG
# ============================================================

BASE_DIR = Path(__file__).resolve().parent

YOLO_FILE = BASE_DIR / "yolo_predictions.csv"
RESNET_FILE = BASE_DIR / "resnet_predictions.csv"
OUTPUT_FILE = BASE_DIR / "fusion_predictions.csv"

# Weight
# 70% ResNet + 30% YOLO
RESNET_WEIGHT = 0.7
YOLO_WEIGHT = 0.3

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
# LOAD DATA
# ============================================================

print("=" * 60)
print("LATE FUSION - SOFT VOTING")
print("=" * 60)

print(f"YOLO file   : {YOLO_FILE}")
print(f"ResNet file : {RESNET_FILE}")
print()

if not YOLO_FILE.exists():
    raise FileNotFoundError(
        f"Không tìm thấy file YOLO:\n{YOLO_FILE}"
    )

if not RESNET_FILE.exists():
    raise FileNotFoundError(
        f"Không tìm thấy file ResNet:\n{RESNET_FILE}"
    )


yolo_df = pd.read_csv(YOLO_FILE)
resnet_df = pd.read_csv(RESNET_FILE)

print(f"YOLO images   : {len(yolo_df)}")
print(f"ResNet images : {len(resnet_df)}")
print()


# ============================================================
# CHECK COLUMNS
# ============================================================

print("Checking CSV columns...")

for cls in CLASSES:
    yolo_col = f"prob_{cls}"
    resnet_col = f"prob_{cls}"

    if yolo_col not in yolo_df.columns:
        raise ValueError(
            f"YOLO CSV thiếu column: {yolo_col}"
        )

    if resnet_col not in resnet_df.columns:
        raise ValueError(
            f"ResNet CSV thiếu column: {resnet_col}"
        )

if "image" not in yolo_df.columns:
    raise ValueError("YOLO CSV không có column 'image'")

if "image" not in resnet_df.columns:
    raise ValueError("ResNet CSV không có column 'image'")

if "true_class" not in yolo_df.columns:
    raise ValueError("YOLO CSV không có column 'true_class'")

if "true_class" not in resnet_df.columns:
    raise ValueError("ResNet CSV không có column 'true_class'")

print("Columns OK!")
print()


# ============================================================
# MERGE
# ============================================================

print("Merging YOLO + ResNet predictions...")

merged = pd.merge(
    yolo_df,
    resnet_df,
    on="image",
    suffixes=("_yolo", "_resnet"),
    how="inner"
)

print(f"Matched images: {len(merged)}")

if len(merged) == 0:
    raise ValueError("Không có ảnh nào khớp giữa YOLO và ResNet.")

if len(merged) != len(yolo_df):
    print(
        "WARNING: Số ảnh sau khi merge khác số ảnh YOLO."
    )

print()


# ============================================================
# CHECK TRUE CLASS
# ============================================================

print("Checking true classes...")

true_class_yolo = merged["true_class_yolo"].values
true_class_resnet = merged["true_class_resnet"].values

if not np.array_equal(true_class_yolo, true_class_resnet):
    mismatch = np.sum(true_class_yolo != true_class_resnet)

    raise ValueError(
        f"true_class không khớp giữa YOLO và ResNet: "
        f"{mismatch} ảnh."
    )

merged["true_class"] = merged["true_class_yolo"]

print("True classes match!")
print()


# ============================================================
# SOFT VOTING
# ============================================================

print("Performing Soft Voting...")
print()
print(f"ResNet weight : {RESNET_WEIGHT}")
print(f"YOLO weight   : {YOLO_WEIGHT}")
print()

if abs(RESNET_WEIGHT + YOLO_WEIGHT - 1.0) > 1e-9:
    raise ValueError(
        "Tổng weight phải bằng 1.0"
    )


fusion_probabilities = []

for cls in CLASSES:

    yolo_prob = merged[f"prob_{cls}_yolo"].astype(float)

    resnet_prob = merged[f"prob_{cls}_resnet"].astype(float)

    fusion_prob = (
        RESNET_WEIGHT * resnet_prob
        + YOLO_WEIGHT * yolo_prob
    )

    merged[f"prob_{cls}_fusion"] = fusion_prob

    fusion_probabilities.append(
        fusion_prob.values
    )


# ============================================================
# FINAL PREDICTION
# ============================================================

fusion_matrix = np.column_stack(
    fusion_probabilities
)

best_indices = np.argmax(
    fusion_matrix,
    axis=1
)

merged["fusion_prediction"] = [
    CLASSES[index]
    for index in best_indices
]


# ============================================================
# ACCURACY
# ============================================================

merged["fusion_correct"] = (
    merged["fusion_prediction"]
    == merged["true_class"]
)

correct = merged["fusion_correct"].sum()
total = len(merged)

accuracy = correct / total * 100


print("=" * 60)
print("RESULT")
print("=" * 60)

print(f"Total images : {total}")
print(f"Correct      : {correct}")
print(f"Wrong        : {total - correct}")
print(f"Accuracy     : {accuracy:.2f}%")
print()


# ============================================================
# YOLO ACCURACY
# ============================================================

yolo_correct = (
    merged["predicted_class_yolo"]
    == merged["true_class"]
).sum()

yolo_accuracy = (
    yolo_correct / total * 100
)


# ============================================================
# RESNET ACCURACY
# ============================================================

resnet_correct = (
    merged["predicted_class_resnet"]
    == merged["true_class"]
).sum()

resnet_accuracy = (
    resnet_correct / total * 100
)


print("=" * 60)
print("MODEL COMPARISON")
print("=" * 60)

print(
    f"YOLO11n-cls : "
    f"{yolo_correct}/{total} "
    f"({yolo_accuracy:.2f}%)"
)

print(
    f"ResNet18    : "
    f"{resnet_correct}/{total} "
    f"({resnet_accuracy:.2f}%)"
)

print(
    f"Fusion      : "
    f"{correct}/{total} "
    f"({accuracy:.2f}%)"
)

print()


# ============================================================
# IMPROVEMENT
# ============================================================

improvement_vs_resnet = accuracy - resnet_accuracy
improvement_vs_yolo = accuracy - yolo_accuracy

print("=" * 60)
print("IMPROVEMENT")
print("=" * 60)

print(
    f"Fusion vs ResNet : "
    f"{improvement_vs_resnet:+.2f} percentage points"
)

print(
    f"Fusion vs YOLO   : "
    f"{improvement_vs_yolo:+.2f} percentage points"
)

print()


# ============================================================
# SAVE RESULT
# ============================================================

# Chỉ lưu các column quan trọng + probability fusion
output_columns = [
    "image",
    "true_class",
    "predicted_class_yolo",
    "predicted_class_resnet",
    "fusion_prediction",
]

for cls in CLASSES:
    output_columns.append(
        f"prob_{cls}_fusion"
    )

output_columns.append("fusion_correct")

result_df = merged[output_columns]

result_df.to_csv(
    OUTPUT_FILE,
    index=False
)


# ============================================================
# WRONG PREDICTIONS
# ============================================================

wrong_df = result_df[
    result_df["fusion_correct"] == False
]

print("=" * 60)
print("WRONG PREDICTIONS")
print("=" * 60)

print(f"Wrong images: {len(wrong_df)}")
print()

if len(wrong_df) > 0:
    print(
        wrong_df[
            [
                "image",
                "true_class",
                "predicted_class_yolo",
                "predicted_class_resnet",
                "fusion_prediction"
            ]
        ].to_string(index=False)
    )

print()


# ============================================================
# DONE
# ============================================================

print("=" * 60)
print("DONE!")
print("=" * 60)

print(f"Output: {OUTPUT_FILE}")
print("=" * 60)