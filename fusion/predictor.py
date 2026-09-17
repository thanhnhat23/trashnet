import json
from pathlib import Path

import torch
import torch.nn as nn
from torchvision import models, transforms
from ultralytics import YOLO
from PIL import Image


# ============================================================
# PATH
# ============================================================

BASE_DIR = Path(__file__).resolve().parent.parent

RESNET_MODEL_PATH = BASE_DIR / "data" / "trashnet_resnet18_best.pth"
CLASSES_PATH = BASE_DIR / "data" / "classes.json"
YOLO_MODEL_PATH = BASE_DIR / "models" / "yolo11n_trashnet_best.pt"


# ============================================================
# DEVICE
# ============================================================

device = torch.device(
    "cuda:0" if torch.cuda.is_available() else "cpu"
)


# ============================================================
# CLASSES
# ============================================================

with open(CLASSES_PATH, "r", encoding="utf-8") as f:
    class_to_idx = json.load(f)

idx_to_class = {
    int(v): k for k, v in class_to_idx.items()
}


# ============================================================
# IMAGE TRANSFORM - SAME AS RESNET TRAINING
# ============================================================

transform = transforms.Compose([
    transforms.Resize((224, 224)),
    transforms.ToTensor(),
    transforms.Normalize(
        [0.485, 0.456, 0.406],
        [0.229, 0.224, 0.225]
    )
])


# ============================================================
# LOAD RESNET18
# ============================================================

def load_resnet():
    model = models.resnet18()

    model.fc = nn.Linear(
        model.fc.in_features,
        len(idx_to_class)
    )

    state_dict = torch.load(
        RESNET_MODEL_PATH,
        map_location=device
    )

    model.load_state_dict(state_dict)

    model.to(device)
    model.eval()

    return model


# ============================================================
# LOAD YOLO11n-CLS
# ============================================================

def load_yolo():
    model = YOLO(str(YOLO_MODEL_PATH))
    return model


# ============================================================
# LOAD BOTH MODELS
# ============================================================

print("=" * 60)
print("Loading AI models...")
print("=" * 60)

resnet_model = load_resnet()
yolo_model = load_yolo()

print(f"ResNet18 loaded on: {device}")
print(f"YOLO11n-cls loaded from: {YOLO_MODEL_PATH}")

print("=" * 60)


# ============================================================
# SOFT VOTING WEIGHTS
# ============================================================

RESNET_WEIGHT = 0.70
YOLO_WEIGHT = 0.30


# ============================================================
# PREDICT RESNET
# ============================================================

def predict_resnet(image):
    """
    Return probability vector of ResNet18.
    """

    image = image.convert("RGB")

    tensor = transform(image)
    tensor = tensor.unsqueeze(0)
    tensor = tensor.to(device)

    with torch.no_grad():
        outputs = resnet_model(tensor)
        probs = torch.softmax(outputs, dim=1)[0]

    return probs.cpu()


# ============================================================
# PREDICT YOLO
# ============================================================

def predict_yolo(image):
    """
    Return probability vector of YOLO11n-cls.
    """

    results = yolo_model.predict(
        source=image,
        imgsz=224,
        verbose=False
    )

    if not results:
        raise RuntimeError("YOLO không trả về kết quả.")

    result = results[0]

    if result.probs is None:
        raise RuntimeError(
            "YOLO model không trả về classification probabilities."
        )

    # YOLO probability vector
    yolo_probs = result.probs.data.cpu()

    return yolo_probs


# ============================================================
# SOFT VOTING
# ============================================================

def predict(image):
    """
    Predict using:

        Final = 0.7 * ResNet + 0.3 * YOLO

    Return final classification result.
    """

    # --------------------------------------------------------
    # 1. ResNet probabilities
    # --------------------------------------------------------

    resnet_probs = predict_resnet(image)

    # --------------------------------------------------------
    # 2. YOLO probabilities
    # --------------------------------------------------------

    yolo_probs = predict_yolo(image)

    # --------------------------------------------------------
    # 3. Safety check
    # --------------------------------------------------------

    if len(resnet_probs) != len(yolo_probs):
        raise RuntimeError(
            f"Số class không giống nhau: "
            f"ResNet={len(resnet_probs)}, "
            f"YOLO={len(yolo_probs)}"
        )

    # --------------------------------------------------------
    # 4. Soft Voting
    # --------------------------------------------------------

    fusion_probs = (
        RESNET_WEIGHT * resnet_probs
        + YOLO_WEIGHT * yolo_probs
    )

    # --------------------------------------------------------
    # 5. Find final class
    # --------------------------------------------------------

    sorted_probs, indices = torch.sort(
        fusion_probs,
        descending=True
    )

    top_idx = indices[0].item()
    top_class = idx_to_class[top_idx]
    top_conf = sorted_probs[0].item()

    # --------------------------------------------------------
    # 6. All predictions
    # --------------------------------------------------------

    all_predictions = []

    for prob, idx in zip(sorted_probs, indices):

        class_id = idx.item()
        class_name = idx_to_class[class_id]

        all_predictions.append({
            "class": class_name,
            "confidence": round(
                prob.item() * 100,
                2
            )
        })

    # --------------------------------------------------------
    # 7. Return result
    # --------------------------------------------------------

    return {
        "class": top_class,
        "confidence": round(
            top_conf * 100,
            2
        ),
        "all_predictions": all_predictions,

        # Debug information
        "resnet_prediction": idx_to_class[
            torch.argmax(resnet_probs).item()
        ],

        "resnet_confidence": round(
            torch.max(resnet_probs).item() * 100,
            2
        ),

        "yolo_prediction": idx_to_class[
            torch.argmax(yolo_probs).item()
        ],

        "yolo_confidence": round(
            torch.max(yolo_probs).item() * 100,
            2
        ),

        "resnet_weight": RESNET_WEIGHT,
        "yolo_weight": YOLO_WEIGHT
    }