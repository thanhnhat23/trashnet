import torch
import torch.nn as nn
from torchvision import models, datasets, transforms
from torch.utils.data import DataLoader
from pathlib import Path
import copy

# 1. Cấu hình

BASE_DIR = Path(__file__).resolve().parent
DATA_DIR = BASE_DIR / "yolo_dataset"

BATCH_SIZE = 32
EPOCHS = 30
LEARNING_RATE = 1e-4

device = torch.device("cuda" if torch.cuda.is_available() else "cpu")

print("Device:", device)

# 2. Preprocessing

train_transform = transforms.Compose([
    transforms.Resize((224, 224)),
    transforms.RandomHorizontalFlip(),
    transforms.RandomRotation(15),
    transforms.ColorJitter(
        brightness=0.2,
        contrast=0.2,
        saturation=0.2
    ),
    transforms.ToTensor(),
    transforms.Normalize(
        [0.485, 0.456, 0.406],
        [0.229, 0.224, 0.225]
    )
])

val_transform = transforms.Compose([
    transforms.Resize((224, 224)),
    transforms.ToTensor(),
    transforms.Normalize(
        [0.485, 0.456, 0.406],
        [0.229, 0.224, 0.225]
    )
])

# 3. Load dataset

train_dataset = datasets.ImageFolder(
    DATA_DIR / "train",
    transform=train_transform
)

val_dataset = datasets.ImageFolder(
    DATA_DIR / "val",
    transform=val_transform
)

train_loader = DataLoader(
    train_dataset,
    batch_size=BATCH_SIZE,
    shuffle=True,
    num_workers=0
)

val_loader = DataLoader(
    val_dataset,
    batch_size=BATCH_SIZE,
    shuffle=False,
    num_workers=0
)

print("\nClasses:")
print(train_dataset.class_to_idx)

print("\nTrain images:", len(train_dataset))
print("Validation images:", len(val_dataset))

# 4. Load ResNet-18

model = models.resnet18(weights=models.ResNet18_Weights.DEFAULT)

# Đổi lớp cuối thành 4 class
model.fc = nn.Linear(
    model.fc.in_features,
    4
)

model = model.to(device)

# 5. Loss + Optimizer

criterion = nn.CrossEntropyLoss()

optimizer = torch.optim.AdamW(
    model.parameters(),
    lr=LEARNING_RATE,
    weight_decay=1e-3
)

scheduler = torch.optim.lr_scheduler.CosineAnnealingLR(
    optimizer,
    T_max=EPOCHS
)

# 6. Training

best_accuracy = 0.0
best_model = copy.deepcopy(model.state_dict())

for epoch in range(EPOCHS):

    print(f"\nEpoch {epoch + 1}/{EPOCHS}")

    # TRAIN

    model.train()

    running_loss = 0.0
    correct = 0
    total = 0

    for images, labels in train_loader:

        images = images.to(device)
        labels = labels.to(device)

        optimizer.zero_grad()

        outputs = model(images)

        loss = criterion(outputs, labels)

        loss.backward()

        optimizer.step()

        running_loss += loss.item()

        _, predicted = torch.max(outputs, 1)

        total += labels.size(0)
        correct += (predicted == labels).sum().item()

    train_accuracy = 100 * correct / total
    train_loss = running_loss / len(train_loader)

    # VALIDATION

    model.eval()

    correct = 0
    total = 0
    val_loss = 0.0

    with torch.no_grad():

        for images, labels in val_loader:

            images = images.to(device)
            labels = labels.to(device)

            outputs = model(images)

            loss = criterion(outputs, labels)

            val_loss += loss.item()

            _, predicted = torch.max(outputs, 1)

            total += labels.size(0)
            correct += (predicted == labels).sum().item()

    val_accuracy = 100 * correct / total
    val_loss = val_loss / len(val_loader)

    scheduler.step()

    print(f"Train Loss: {train_loss:.4f}")
    print(f"Train Accuracy: {train_accuracy:.2f}%")
    print(f"Val Loss: {val_loss:.4f}")
    print(f"Val Accuracy: {val_accuracy:.2f}%")

    # Save best model
    if val_accuracy > best_accuracy:

        best_accuracy = val_accuracy
        best_model = copy.deepcopy(model.state_dict())

        print("→ New best model!")

# 7. Save best model

model.load_state_dict(best_model)

OUTPUT_DIR = BASE_DIR / "weights"
OUTPUT_DIR.mkdir(exist_ok=True)

output_path = OUTPUT_DIR / "resnet18_4class_best.pth"

torch.save(
    model.state_dict(),
    output_path
)

print("\n==============================")
print("TRAINING HOAN THANH")
print("==============================")
print("Best validation accuracy:",
      f"{best_accuracy:.2f}%")
print("Model saved at:")
print(output_path)