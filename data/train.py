import json
import os
from pathlib import Path
import torch
import torch.nn as nn
import torch.optim as optim
from torchvision import datasets, models, transforms
from torch.utils.data import DataLoader, random_split, Subset

def main():
    device = torch.device("cuda:0" if torch.cuda.is_available() else "cpu")
    print(f"Using device: {device}")

    # Separate transforms for training and validation
    train_transforms = transforms.Compose([
        transforms.Resize((224, 224)),
        transforms.RandomHorizontalFlip(),
        transforms.RandomRotation(15),
        transforms.ToTensor(),
        transforms.Normalize([0.485, 0.456, 0.406], [0.229, 0.224, 0.225])
    ])

    val_transforms = transforms.Compose([
        transforms.Resize((224, 224)),
        transforms.ToTensor(),
        transforms.Normalize([0.485, 0.456, 0.406], [0.229, 0.224, 0.225])
    ])

    base_dir = Path(__file__).resolve().parent
    data_dir = base_dir / 'dataset-resized'

    # Load two dataset instances to apply independent transformations
    full_dataset_train = datasets.ImageFolder(str(data_dir), transform=train_transforms)
    full_dataset_val = datasets.ImageFolder(str(data_dir), transform=val_transforms)

    # Save class mapping for inference
    with open(base_dir / 'classes.json', 'w') as f:
        json.dump(full_dataset_train.class_to_idx, f, indent=4)
    print(f"Class mapping: {full_dataset_train.class_to_idx}")

    # Split dataset into train and validation sets
    total_size = len(full_dataset_train)
    train_size = int(0.8 * total_size)
    val_size = total_size - train_size
    generator = torch.Generator().manual_seed(42)  # Fixed seed for split reproducibility
    train_indices, val_indices = random_split(range(total_size), [train_size, val_size], generator=generator)

    train_data = Subset(full_dataset_train, train_indices)
    val_data = Subset(full_dataset_val, val_indices)

    # pin_memory speeds up host-to-device tensor transfers
    is_cuda = device.type == 'cuda'
    train_loader = DataLoader(train_data, batch_size=32, shuffle=True, num_workers=2 if is_cuda else 0, pin_memory=is_cuda)
    val_loader = DataLoader(val_data, batch_size=32, shuffle=False, num_workers=2 if is_cuda else 0, pin_memory=is_cuda)

    model = models.resnet18(weights=models.ResNet18_Weights.DEFAULT)
    num_ftrs = model.fc.in_features
    model.fc = nn.Linear(num_ftrs, len(full_dataset_train.classes))
    model = model.to(device)

    # Cross Entropy Loss, Label Smoothing
    criterion = nn.CrossEntropyLoss(label_smoothing=0.05)
    optimizer = optim.AdamW(model.parameters(), lr=1e-4, weight_decay=1e-2)

    num_epochs = 15
    scheduler = optim.lr_scheduler.CosineAnnealingLR(optimizer, T_max=num_epochs, eta_min=1e-6)
    best_val_acc = 0.0

    for epoch in range(num_epochs):
        model.train()
        running_loss = 0.0
        corrects = 0

        for inputs, labels in train_loader:
            inputs, labels = inputs.to(device, non_blocking=is_cuda), labels.to(device, non_blocking=is_cuda)

            optimizer.zero_grad()
            outputs = model(inputs)
            loss = criterion(outputs, labels)
            loss.backward()
            optimizer.step()

            _, preds = torch.max(outputs, 1)
            running_loss += loss.item() * inputs.size(0)
            corrects += torch.sum(preds == labels.data)

        epoch_loss = running_loss / train_size
        epoch_acc = corrects.double() / train_size

        model.eval()
        val_running_loss = 0.0
        val_corrects = 0
        with torch.no_grad():
            for inputs, labels in val_loader:
                inputs, labels = inputs.to(device, non_blocking=is_cuda), labels.to(device, non_blocking=is_cuda)
                outputs = model(inputs)
                loss = criterion(outputs, labels)
                val_running_loss += loss.item() * inputs.size(0)
                _, preds = torch.max(outputs, 1)
                val_corrects += torch.sum(preds == labels.data)

        epoch_val_loss = val_running_loss / val_size
        val_acc = val_corrects.double() / val_size
        current_lr = optimizer.param_groups[0]['lr']

        print(f"Epoch {epoch+1:02d}/{num_epochs:02d} [LR: {current_lr:.6f}] - Train Loss: {epoch_loss:.4f} Acc: {epoch_acc:.4f} | Val Loss: {epoch_val_loss:.4f} Acc: {val_acc:.4f}")

        # Optimize checkpoint saving: only save the best model based on validation accuracy
        if val_acc > best_val_acc:
            best_val_acc = val_acc
            torch.save(model.state_dict(), base_dir / 'trashnet_resnet18_best.pth')

        scheduler.step()

    print(f"Training complete. Best checkpoint Val Acc: {best_val_acc:.4f}")

if __name__ == '__main__':
    main()