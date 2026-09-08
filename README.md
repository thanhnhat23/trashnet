# TrashNet: AI-Powered Smart Waste Classification & Web App

[![PyTorch](https://img.shields.io/badge/PyTorch-2.0+-ee4c2c.svg)](https://pytorch.org)
[![Flask](https://img.shields.io/badge/Flask-Web%20UI-black.svg)](https://flask.palletsprojects.com/)
[![Accuracy](https://img.shields.io/badge/Val%20Accuracy-96.25%25-brightgreen.svg)]()
[![Classes](https://img.shields.io/badge/Classes-8%20Categories-blue.svg)]()

TrashNet is an intelligent deep learning system and web application designed to automatically classify household and recyclable waste into proper disposal categories. Originally based on [Gary Thung & Mindy Yang's Stanford CS229 Project](https://cs229.stanford.edu/proj2016/report/ThungYang-ClassificationOfTrashForRecyclabilityStatus-report.pdf), this repository has been modernized to PyTorch with an optimized ResNet-18 pipeline achieving **96.25% validation accuracy** across **8 waste categories**, along with a responsive modern Web UI for live webcam and image classification.

---

## ✨ Features

- 🧠 **High-Accuracy PyTorch Pipeline**: Transfer learning using ResNet-18 fine-tuned with AdamW optimizer, Cosine Annealing learning rate schedule, data augmentation (RandomResizedCrop, Flip, Rotation, ColorJitter), and Label Smoothing.
- ♻️ **Expanded 8 Waste Categories**:
  1. 🔋 **Battery (Pin & Ắc quy)** - Hazardous waste / Red bin
  2. 🍎 **Biological (Rác hữu cơ)** - Organic waste / Green bin
  3. 📦 **Cardboard (Bìa carton)** - Recyclable / Blue bin
  4. 🥛 **Glass (Thủy tinh & Chai lọ)** - Recyclable / Blue bin
  5. 🥫 **Metal (Kim loại & Vỏ lon)** - Recyclable / Blue bin
  6. 📄 **Paper (Giấy vụn & Sách báo)** - Recyclable / Blue bin
  7. 🧴 **Plastic (Nhựa & Chai nhựa)** - Recyclable / Yellow-Blue bin
  8. 🗑️ **Trash (Rác vô cơ khác)** - Non-recyclable landfill / Gray bin
- 🌐 **Interactive Web Interface**:
  - Drag-and-drop or browse image upload
  - Real-time webcam capture & instant AI inference
  - Environmental guidance with recommended trash bin colors and recycling tips (bilingual Vietnamese / English)
  - Detailed probability breakdown bar chart for all 8 categories
  - Glassmorphic dark/light responsive layout

---

## 🚀 Quick Start: Web Application

### 1. Install Dependencies

Make sure you have Python 3.8+ installed:

```bash
pip install torch torchvision pillow flask
```

*(CUDA-enabled GPU is automatically used if available, otherwise runs smoothly on CPU).*

### 2. Launch the Web Server

From the repository root:

```bash
python app.py
```

### 3. Open in Browser

Visit **`http://127.0.0.1:5000`** in your browser to start classifying waste images or streaming from your webcam!

---

## 🛠️ CLI Usage & Model Training

### 1. Data Structure

Images are placed in `data/dataset-resized/` organized by class folders:

```text
data/dataset-resized/
  ├── battery/
  ├── biological/
  ├── cardboard/
  ├── glass/
  ├── metal/
  ├── paper/
  ├── plastic/
  └── trash/
```

### 2. Split Dataset

To regenerate the 80/20 train/validation splits:

```bash
python data/split_data.py
```

This creates:
- `data/one-indexed-files-notrash_train.txt`
- `data/one-indexed-files-notrash_val.txt`
- `data/classes.json`

### 3. Train the Model

To train or fine-tune ResNet-18:

```bash
python data/train.py
```

Key training optimizations configured in `train.py`:
- **Optimizer**: AdamW (`lr=1e-4`, `weight_decay=1e-3`)
- **Scheduler**: `CosineAnnealingLR` (smooth decay down to $10^{-6}$)
- **Criterion**: Cross-Entropy with `label_smoothing=0.05`
- **Data Augmentation**: `RandomResizedCrop(224)`, `RandomHorizontalFlip`, `RandomRotation(15)`, `ColorJitter`
- Checkpoints are saved to `data/trashnet_resnet18_best.pth`.

### 4. Command-Line Inference

To predict the class of a single image via CLI:

```bash
python data/predict.py
```

Or configure the target file in `predict.py`:
```python
test_img = r"path/to/your/image.jpg"
```

Output:
```text
File: sample.jpg
Result: PLASTIC (Confidence: 96.42%)
```

---

## 📊 Dataset & Original Paper

- **Paper**: [Thung & Yang - Classification of Trash for Recyclability Status (Stanford CS 229)](https://cs229.stanford.edu/proj2016/report/ThungYang-ClassificationOfTrashForRecyclabilityStatus-report.pdf)
- **HuggingFace Dataset**: [garythung/trashnet](https://huggingface.co/datasets/garythung/trashnet)
- Original 6-class dataset photos were captured on white posterboard backgrounds under controlled lighting with Apple iPhone devices and resized to 512x384.

---

## 📜 Legacy Torch / Lua Codebase

The root directory retains the original Torch7 Lua implementation for historical reference and research reproduction:

### Setup (Lua/Torch)
```bash
luarocks install torch
luarocks install nn
luarocks install optim
luarocks install image
luarocks install gnuplot
# For CUDA acceleration:
luarocks install cutorch
luarocks install cunn
```

### Running Legacy Scripts
- `train.lua` - Train Torch7 CNN models
- `test.lua` - Evaluate accuracy on test splits
- `plot.lua` - Visualize training history of legacy `.t7` checkpoints

---

## 🤝 Contributing

1. Fork the Project
2. Create your Feature Branch (`git checkout -b feat/AmazingFeature`)
3. Commit your Changes (`git commit -m 'Add some AmazingFeature'`)
4. Push to the Branch (`git push origin feat/AmazingFeature`)
5. Open a Pull Request

---

## 📝 License & Acknowledgments

- Distributed under the MIT License. See `LICENSE` for more information.
- Original authors: **Gary Thung** and **Mindy Yang** (Stanford CS 229).
- Torch weight initialization module provided by [@e-lab](http://github.com/e-lab).
