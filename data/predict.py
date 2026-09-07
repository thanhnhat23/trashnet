import json
from pathlib import Path
from PIL import Image
import torch
import torch.nn as nn
from torchvision import models, transforms

def predict(image_path):
    device = torch.device("cuda:0" if torch.cuda.is_available() else "cpu")
    base_dir = Path(__file__).resolve().parent

    # Load label mapping saved during training
    with open(base_dir / 'classes.json', 'r') as f:
        class_to_idx = json.load(f)
    idx_to_class = {v: k for k, v in class_to_idx.items()}

    # Preprocess test image identically to the validation set
    transform = transforms.Compose([
        transforms.Resize((224, 224)),
        transforms.ToTensor(),
        transforms.Normalize([0.485, 0.456, 0.406], [0.229, 0.224, 0.225])
    ])

    image = Image.open(image_path).convert('RGB')
    input_tensor = transform(image).unsqueeze(0).to(device)

    # Load the best model weights
    model = models.resnet18()
    model.fc = nn.Linear(model.fc.in_features, len(idx_to_class))
    model.load_state_dict(torch.load(base_dir / 'trashnet_resnet18_best.pth', map_location=device))
    model = model.to(device)
    model.eval()

    # Predict class label and confidence
    with torch.no_grad():
        outputs = model(input_tensor)
        probabilities = torch.softmax(outputs, dim=1)[0]
        confidence, predicted_idx = torch.max(probabilities, dim=0)

    label = idx_to_class[predicted_idx.item()]
    print(f"File: {Path(image_path).name}")
    print(f"Result: {label.upper()} (Confidence: {confidence.item() * 100:.2f}%)")

if __name__ == '__main__':
    test_img = r"C:\Users\admin\Pictures\test-3.jpg"
    predict(test_img)