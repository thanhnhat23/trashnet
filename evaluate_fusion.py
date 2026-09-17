import pandas as pd
import numpy as np
from pathlib import Path

from sklearn.metrics import (
    accuracy_score,
    precision_score,
    recall_score,
    f1_score,
    confusion_matrix,
    classification_report
)

import matplotlib.pyplot as plt


# ============================================================
# CONFIG
# ============================================================

BASE_DIR = Path(__file__).resolve().parent

YOLO_FILE = BASE_DIR / "yolo_predictions.csv"
RESNET_FILE = BASE_DIR / "resnet_predictions.csv"
FUSION_FILE = BASE_DIR / "fusion_predictions.csv"

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
# LOAD FILES
# ============================================================

print("=" * 70)
print("EVALUATE YOLO vs RESNET vs LATE FUSION")
print("=" * 70)

print()

for file in [YOLO_FILE, RESNET_FILE, FUSION_FILE]:
    if not file.exists():
        raise FileNotFoundError(
            f"Không tìm thấy file:\n{file}"
        )

yolo_df = pd.read_csv(YOLO_FILE)
resnet_df = pd.read_csv(RESNET_FILE)
fusion_df = pd.read_csv(FUSION_FILE)

print(f"YOLO   : {len(yolo_df)} images")
print(f"ResNet : {len(resnet_df)} images")
print(f"Fusion : {len(fusion_df)} images")

print()


# ============================================================
# MERGE DATA
# ============================================================

df = pd.merge(
    yolo_df[
        [
            "image",
            "true_class",
            "predicted_class"
        ]
    ],
    resnet_df[
        [
            "image",
            "predicted_class"
        ]
    ],
    on="image",
    suffixes=("_yolo", "_resnet")
)

df = pd.merge(
    df,
    fusion_df[
        [
            "image",
            "fusion_prediction"
        ]
    ],
    on="image",
    how="inner"
)

print(f"Matched images: {len(df)}")
print()


# ============================================================
# CHECK TRUE CLASS
# ============================================================

# ============================================================
# CHECK TRUE CLASS
# ============================================================

# ============================================================
# CHECK TRUE CLASS
# ============================================================

# true_class chỉ có trong YOLO CSV
# nên sau merge tên vẫn là "true_class"

if "true_class" not in df.columns:
    raise ValueError(
        "Không tìm thấy column 'true_class'"
    )

y_true = df["true_class"]

y_pred_yolo = df["predicted_class_yolo"]

y_pred_resnet = df["predicted_class_resnet"]

y_pred_fusion = df["fusion_prediction"]

# ============================================================
# FUNCTION: EVALUATE MODEL
# ============================================================

def evaluate_model(name, y_true, y_pred):

    accuracy = accuracy_score(
        y_true,
        y_pred
    )

    precision = precision_score(
        y_true,
        y_pred,
        labels=CLASSES,
        average="macro",
        zero_division=0
    )

    recall = recall_score(
        y_true,
        y_pred,
        labels=CLASSES,
        average="macro",
        zero_division=0
    )

    f1 = f1_score(
        y_true,
        y_pred,
        labels=CLASSES,
        average="macro",
        zero_division=0
    )

    print("=" * 70)
    print(name)
    print("=" * 70)

    print(f"Accuracy  : {accuracy * 100:.2f}%")
    print(f"Precision : {precision * 100:.2f}%")
    print(f"Recall    : {recall * 100:.2f}%")
    print(f"F1-score  : {f1 * 100:.2f}%")

    print()

    print("Classification Report:")
    print(
        classification_report(
            y_true,
            y_pred,
            labels=CLASSES,
            target_names=CLASSES,
            digits=4,
            zero_division=0
        )
    )

    return {
        "Model": name,
        "Accuracy": accuracy * 100,
        "Precision": precision * 100,
        "Recall": recall * 100,
        "F1-score": f1 * 100
    }


# ============================================================
# EVALUATE 3 MODELS
# ============================================================

yolo_result = evaluate_model(
    "YOLO11n-cls",
    y_true,
    y_pred_yolo
)

resnet_result = evaluate_model(
    "ResNet18",
    y_true,
    y_pred_resnet
)

fusion_result = evaluate_model(
    "Late Fusion - Soft Voting",
    y_true,
    y_pred_fusion
)


# ============================================================
# SUMMARY TABLE
# ============================================================

results = pd.DataFrame(
    [
        yolo_result,
        resnet_result,
        fusion_result
    ]
)

print("=" * 70)
print("FINAL COMPARISON")
print("=" * 70)

print(
    results.to_string(
        index=False,
        formatters={
            "Accuracy": "{:.2f}%".format,
            "Precision": "{:.2f}%".format,
            "Recall": "{:.2f}%".format,
            "F1-score": "{:.2f}%".format
        }
    )
)

print()


# ============================================================
# SAVE SUMMARY
# ============================================================

summary_file = BASE_DIR / "evaluation_summary.csv"

results.to_csv(
    summary_file,
    index=False
)

print(f"Saved summary: {summary_file}")
print()


# ============================================================
# CONFUSION MATRIX FUNCTION
# ============================================================

def plot_confusion_matrix(
    name,
    y_true,
    y_pred,
    filename
):

    cm = confusion_matrix(
        y_true,
        y_pred,
        labels=CLASSES
    )

    fig, ax = plt.subplots(
        figsize=(10, 8)
    )

    image = ax.imshow(cm)

    ax.set_xticks(
        np.arange(len(CLASSES))
    )

    ax.set_yticks(
        np.arange(len(CLASSES))
    )

    ax.set_xticklabels(
        CLASSES,
        rotation=45,
        ha="right"
    )

    ax.set_yticklabels(
        CLASSES
    )

    ax.set_xlabel(
        "Predicted Class"
    )

    ax.set_ylabel(
        "True Class"
    )

    ax.set_title(
        f"Confusion Matrix - {name}"
    )

    # Hiển thị số lượng trong từng ô
    for i in range(len(CLASSES)):
        for j in range(len(CLASSES)):

            ax.text(
                j,
                i,
                cm[i, j],
                ha="center",
                va="center"
            )

    plt.tight_layout()

    output_path = BASE_DIR / filename

    plt.savefig(
        output_path,
        dpi=300,
        bbox_inches="tight"
    )

    plt.close()

    print(
        f"Saved confusion matrix: {output_path}"
    )


# ============================================================
# CONFUSION MATRICES
# ============================================================

print("=" * 70)
print("CONFUSION MATRICES")
print("=" * 70)

plot_confusion_matrix(
    "YOLO11n-cls",
    y_true,
    y_pred_yolo,
    "confusion_matrix_yolo.png"
)

plot_confusion_matrix(
    "ResNet18",
    y_true,
    y_pred_resnet,
    "confusion_matrix_resnet.png"
)

plot_confusion_matrix(
    "Late Fusion - Soft Voting",
    y_true,
    y_pred_fusion,
    "confusion_matrix_fusion.png"
)

print()


# ============================================================
# SAVE WRONG PREDICTIONS
# ============================================================

wrong_fusion = df[
    df["true_class"]
    != df["fusion_prediction"]
].copy()

wrong_file = BASE_DIR / "fusion_wrong_predictions.csv"

wrong_fusion.to_csv(
    wrong_file,
    index=False
)

print("=" * 70)
print("WRONG FUSION PREDICTIONS")
print("=" * 70)

print(
    f"Wrong: {len(wrong_fusion)} / {len(df)}"
)

print()

if len(wrong_fusion) > 0:

    print(
        wrong_fusion[
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

print("=" * 70)
print("EVALUATION COMPLETED!")
print("=" * 70)