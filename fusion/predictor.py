import json
from pathlib import Path

import torch
import torch.nn as nn
from torchvision import models, transforms
from ultralytics import YOLO
from PIL import Image

BASE_DIR = Path(__file__).resolve().parent.parent

RESNET_MODEL_PATH = BASE_DIR / "data" / "trashnet_resnet18_best.pth"
CLASSES_PATH = BASE_DIR / "data" / "classes.json"
YOLO_MODEL_PATH = BASE_DIR / "models" / "yolo11n_trashnet_best.pt"

device = torch.device("cuda:0" if torch.cuda.is_available() else "cpu")

def load_classes():
    global class_to_idx, idx_to_class
    if CLASSES_PATH.exists():
        with open(CLASSES_PATH, "r", encoding="utf-8") as f:
            class_to_idx = json.load(f)
    else:
        class_to_idx = {"paper": 0, "glass": 1, "other": 2, "plastic": 3}
    idx_to_class = {int(v): k for k, v in class_to_idx.items()}

class_to_idx = {}
idx_to_class = {}
load_classes()

transform = transforms.Compose([
    transforms.Resize((224, 224)),
    transforms.ToTensor(),
    transforms.Normalize(
        [0.485, 0.456, 0.406],
        [0.229, 0.224, 0.225]
    )
])

def load_resnet():
    model = models.resnet18()
    model.fc = nn.Linear(model.fc.in_features, len(idx_to_class))
    state_dict = torch.load(RESNET_MODEL_PATH, map_location=device)
    model.load_state_dict(state_dict)
    model.to(device)
    model.eval()
    return model

def load_yolo():
    return YOLO(str(YOLO_MODEL_PATH))

print(f"[AI] Khoi tao model: ResNet18 ({device}) + YOLO11n ({YOLO_MODEL_PATH.name})")
resnet_model = load_resnet()
yolo_model = load_yolo()

def reload_models():
    """Tải lại trọng số mới nhất của ResNet18 và YOLO11n sau khi train."""
    global resnet_model, yolo_model
    try:
        load_classes()
        resnet_model = load_resnet()
        yolo_model = load_yolo()
        print("[AI] Da reload model va class mapping moi nhat thanh cong!")
        return True
    except Exception as e:
        print(f"[AI] Loi reload models: {e}")
        return False

RESNET_WEIGHT = 0.70
YOLO_WEIGHT = 0.30

def predict_resnet(image):
    image = image.convert("RGB")
    tensor = transform(image).unsqueeze(0).to(device)
    with torch.no_grad():
        outputs = resnet_model(tensor)
        probs = torch.softmax(outputs, dim=1)[0]
    return probs.cpu()

def predict_yolo(image):
    results = yolo_model.predict(source=image, imgsz=224, verbose=False)
    if not results or results[0].probs is None:
        raise RuntimeError("YOLO model khong tra ve classification probabilities.")
    return results[0].probs.data.cpu()

def predict(image, model_type="fusion"):
    model_type = (model_type or "fusion").lower().strip()
    if model_type not in ("fusion", "resnet", "yolo"):
        model_type = "fusion"

    resnet_probs = predict_resnet(image)
    yolo_probs = predict_yolo(image)

    if len(resnet_probs) != len(yolo_probs):
        raise RuntimeError(f"So class khong khop: ResNet={len(resnet_probs)}, YOLO={len(yolo_probs)}")

    fusion_probs = RESNET_WEIGHT * resnet_probs + YOLO_WEIGHT * yolo_probs

    if model_type == "resnet":
        active_probs = resnet_probs
    elif model_type == "yolo":
        active_probs = yolo_probs
    else:
        active_probs = fusion_probs

    sorted_probs, indices = torch.sort(active_probs, descending=True)
    top_idx = indices[0].item()
    top_class = idx_to_class[top_idx]
    top_conf = sorted_probs[0].item()

    all_predictions = []
    for prob, idx in zip(sorted_probs, indices):
        class_id = idx.item()
        all_predictions.append({
            "class": idx_to_class[class_id],
            "confidence": round(prob.item() * 100, 2)
        })

    resnet_pred = idx_to_class[torch.argmax(resnet_probs).item()]
    yolo_pred = idx_to_class[torch.argmax(yolo_probs).item()]

    return {
        "active_model": model_type,
        "class": top_class,
        "confidence": round(top_conf * 100, 2),
        "all_predictions": all_predictions,
        "resnet_prediction": resnet_pred,
        "resnet_confidence": round(torch.max(resnet_probs).item() * 100, 2),
        "yolo_prediction": yolo_pred,
        "yolo_confidence": round(torch.max(yolo_probs).item() * 100, 2),
        "resnet_weight": RESNET_WEIGHT,
        "yolo_weight": YOLO_WEIGHT
    }
