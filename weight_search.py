import pandas as pd
import numpy as np
from pathlib import Path


# ============================================================
# CONFIG
# ============================================================

BASE_DIR = Path(__file__).resolve().parent

YOLO_FILE = BASE_DIR / "yolo_predictions.csv"
RESNET_FILE = BASE_DIR / "resnet_predictions.csv"

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

print("=" * 70)
print("SOFT VOTING WEIGHT SEARCH")
print("=" * 70)

yolo_df = pd.read_csv(YOLO_FILE)
resnet_df = pd.read_csv(RESNET_FILE)

print(f"YOLO images   : {len(yolo_df)}")
print(f"ResNet images : {len(resnet_df)}")
print()


# ============================================================
# MERGE
# ============================================================

df = pd.merge(
    yolo_df,
    resnet_df,
    on="image",
    suffixes=("_yolo", "_resnet"),
    how="inner"
)

print(f"Matched images: {len(df)}")
print()


# ============================================================
# TRUE LABEL
# ============================================================

y_true = df["true_class_yolo"].values


# ============================================================
# PROBABILITY MATRICES
# ============================================================

yolo_probs = np.column_stack([
    df[f"prob_{cls}_yolo"].values
    for cls in CLASSES
])

resnet_probs = np.column_stack([
    df[f"prob_{cls}_resnet"].values
    for cls in CLASSES
])


# ============================================================
# SEARCH WEIGHTS
# ============================================================

results = []

print("=" * 70)
print("TESTING WEIGHTS")
print("=" * 70)

for resnet_weight in np.arange(0.0, 1.01, 0.05):

    yolo_weight = 1.0 - resnet_weight

    # Soft Voting
    fusion_probs = (
        resnet_weight * resnet_probs
        + yolo_weight * yolo_probs
    )

    predictions = np.array([
        CLASSES[i]
        for i in np.argmax(fusion_probs, axis=1)
    ])

    correct = np.sum(
        predictions == y_true
    )

    total = len(y_true)

    accuracy = correct / total * 100

    results.append({
        "ResNet Weight": resnet_weight,
        "YOLO Weight": yolo_weight,
        "Correct": correct,
        "Wrong": total - correct,
        "Accuracy": accuracy
    })

    print(
        f"ResNet {resnet_weight:.2f} | "
        f"YOLO {yolo_weight:.2f} | "
        f"{correct}/{total} | "
        f"Accuracy: {accuracy:.2f}%"
    )


# ============================================================
# RESULTS DATAFRAME
# ============================================================

results_df = pd.DataFrame(results)

print()
print("=" * 70)
print("ALL RESULTS")
print("=" * 70)

print(
    results_df.to_string(
        index=False,
        formatters={
            "ResNet Weight": "{:.2f}".format,
            "YOLO Weight": "{:.2f}".format,
            "Accuracy": "{:.2f}%".format
        }
    )
)

print()


# ============================================================
# FIND BEST
# ============================================================

max_accuracy = results_df["Accuracy"].max()

best_results = results_df[
    results_df["Accuracy"] == max_accuracy
]

print("=" * 70)
print("BEST WEIGHT(S)")
print("=" * 70)

for _, row in best_results.iterrows():

    print(
        f"ResNet = {row['ResNet Weight']:.2f} | "
        f"YOLO = {row['YOLO Weight']:.2f} | "
        f"Accuracy = {row['Accuracy']:.2f}% | "
        f"Correct = {int(row['Correct'])}/{len(y_true)}"
    )

print()


# ============================================================
# SAVE CSV
# ============================================================

output_file = BASE_DIR / "weight_search_results.csv"

results_df.to_csv(
    output_file,
    index=False
)

print(f"Saved: {output_file}")
print()


# ============================================================
# OPTIONAL GRAPH
# ============================================================

try:

    import matplotlib.pyplot as plt

    plt.figure(figsize=(10, 6))

    plt.plot(
        results_df["ResNet Weight"],
        results_df["Accuracy"],
        marker="o"
    )

    plt.xlabel("ResNet Weight")
    plt.ylabel("Accuracy (%)")

    plt.title(
        "Soft Voting Accuracy vs ResNet Weight"
    )

    plt.xticks(
        np.arange(0, 1.01, 0.05),
        rotation=45
    )

    plt.grid(True)

    plt.tight_layout()

    graph_file = (
        BASE_DIR
        / "soft_voting_weight_accuracy.png"
    )

    plt.savefig(
        graph_file,
        dpi=300,
        bbox_inches="tight"
    )

    plt.close()

    print(f"Saved graph: {graph_file}")

except ImportError:

    print(
        "matplotlib chưa được cài, "
        "bỏ qua biểu đồ."
    )


# ============================================================
# DONE
# ============================================================

print()
print("=" * 70)
print("WEIGHT SEARCH COMPLETED!")
print("=" * 70)