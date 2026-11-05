import json
import os
import time
import warnings
from pathlib import Path
from PIL import Image

# Tắt các cảnh báo Palette Transparency của PIL để terminal sạch sẽ
warnings.filterwarnings("ignore", category=UserWarning)
warnings.filterwarnings("ignore", module="PIL")

import torch
import torch.nn as nn
import torch.optim as optim
from torchvision import datasets, models, transforms
from torch.utils.data import DataLoader

BASE_DIR = Path(__file__).resolve().parent
DATA_DIR = BASE_DIR / "data" / "pbl4_split"
CLASSES_JSON = BASE_DIR / "data" / "classes.json"
MODEL_SAVE_PATH = BASE_DIR / "data" / "trashnet_resnet18_best.pth"


def safe_pil_loader(path):
    """Đọc ảnh và tự động chuyển về RGB, tránh warning bảng màu Palette."""
    with open(path, "rb") as f:
        img = Image.open(f)
        return img.convert("RGB")


def main():
    print("=" * 60, flush=True)
    print("TRASHNET - RESNET-18 TRAINING (4 CLASSES)", flush=True)
    print("=" * 60, flush=True)

    device = torch.device("cuda:0" if torch.cuda.is_available() else "cpu")
    print(f"Device: {device}", flush=True)
    if torch.cuda.is_available():
        print(f"GPU: {torch.cuda.get_device_name(0)}", flush=True)

    # Transforms
    train_transforms = transforms.Compose([
        transforms.Resize((224, 224)),
        transforms.RandomHorizontalFlip(),
        transforms.RandomRotation(15),
        transforms.ColorJitter(brightness=0.1, contrast=0.1),
        transforms.ToTensor(),
        transforms.Normalize([0.485, 0.456, 0.406], [0.229, 0.224, 0.225])
    ])

    val_transforms = transforms.Compose([
        transforms.Resize((224, 224)),
        transforms.Resize((224, 224)),
        transforms.ToTensor(),
        transforms.Normalize([0.485, 0.456, 0.406], [0.229, 0.224, 0.225])
    ])

    train_dir = DATA_DIR / "train"
    val_dir = DATA_DIR / "val"

    assert train_dir.exists(), f"Train directory not found: {train_dir}"
    assert val_dir.exists(), f"Val directory not found: {val_dir}"

    train_dataset = datasets.ImageFolder(str(train_dir), transform=train_transforms, loader=safe_pil_loader)
    val_dataset = datasets.ImageFolder(str(val_dir), transform=val_transforms, loader=safe_pil_loader)

    num_classes = len(train_dataset.classes)
    print(f"Classes ({num_classes}): {train_dataset.class_to_idx}", flush=True)
    print(f"Train samples: {len(train_dataset)} | Val samples: {len(val_dataset)}", flush=True)

    # Lưu classes.json
    with open(CLASSES_JSON, "w", encoding="utf-8") as f:
        json.dump(train_dataset.class_to_idx, f, indent=4)
    print(f"Saved class mapping to: {CLASSES_JSON}", flush=True)

    is_cuda = device.type == "cuda"
    train_loader = DataLoader(
        train_dataset,
        batch_size=32,
        shuffle=True,
        num_workers=0,  # Dùng 0 worker trên Windows để tránh lỗi paging/multiprocessing
        pin_memory=is_cuda
    )
    val_loader = DataLoader(
        val_dataset,
        batch_size=32,
        shuffle=False,
        num_workers=0,
        pin_memory=is_cuda
    )

    # Initialize model
    print("Loading pretrained ResNet-18...", flush=True)
    model = models.resnet18(weights=models.ResNet18_Weights.DEFAULT)
    num_ftrs = model.fc.in_features
    model.fc = nn.Linear(num_ftrs, num_classes)
    model = model.to(device)

    criterion = nn.CrossEntropyLoss(label_smoothing=0.05)
    optimizer = optim.AdamW(model.parameters(), lr=1e-4, weight_decay=1e-2)
    num_epochs = 15
    scheduler = optim.lr_scheduler.CosineAnnealingLR(optimizer, T_max=num_epochs, eta_min=1e-6)

    best_val_acc = 0.0
    start_time = time.time()

    print("\nStarting training...", flush=True)
    for epoch in range(num_epochs):
        model.train()
        running_loss = 0.0
        corrects = 0

        for inputs, labels in train_loader:
            inputs = inputs.to(device, non_blocking=is_cuda)
            labels = labels.to(device, non_blocking=is_cuda)

            optimizer.zero_grad()
            outputs = model(inputs)
            loss = criterion(outputs, labels)
            loss.backward()
            optimizer.step()

            _, preds = torch.max(outputs, 1)
            running_loss += loss.item() * inputs.size(0)
            corrects += torch.sum(preds == labels.data)

        epoch_loss = running_loss / len(train_dataset)
        epoch_acc = corrects.double() / len(train_dataset)

        # Validation
        model.eval()
        val_running_loss = 0.0
        val_corrects = 0

        with torch.no_grad():
            for inputs, labels in val_loader:
                inputs = inputs.to(device, non_blocking=is_cuda)
                labels = labels.to(device, non_blocking=is_cuda)

                outputs = model(inputs)
                loss = criterion(outputs, labels)

                val_running_loss += loss.item() * inputs.size(0)
                _, preds = torch.max(outputs, 1)
                val_corrects += torch.sum(preds == labels.data)

        val_loss = val_running_loss / len(val_dataset)
        val_acc = val_corrects.double() / len(val_dataset)
        current_lr = optimizer.param_groups[0]["lr"]

        print(
            f"Epoch {epoch+1:02d}/{num_epochs:02d} [LR: {current_lr:.6f}] "
            f"- Train Loss: {epoch_loss:.4f} Acc: {epoch_acc:.4f} "
            f"| Val Loss: {val_loss:.4f} Acc: {val_acc:.4f}",
            flush=True
        )

        if val_acc > best_val_acc:
            best_val_acc = val_acc
            torch.save(model.state_dict(), MODEL_SAVE_PATH)
            print(f"  --> Saved new best checkpoint to {MODEL_SAVE_PATH} (Val Acc: {best_val_acc:.4f})", flush=True)

        scheduler.step()

    elapsed = time.time() - start_time
    print(f"\nResNet-18 Training Completed in {elapsed/60:.2f} mins!", flush=True)
    print(f"Best Val Accuracy: {best_val_acc:.4f}", flush=True)
    print(f"Model saved: {MODEL_SAVE_PATH}", flush=True)


if __name__ == "__main__":
    main()
