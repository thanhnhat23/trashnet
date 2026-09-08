import base64
import io
import json
import os
from pathlib import Path
from PIL import Image
import torch
import torch.nn as nn
from torchvision import models, transforms
from flask import Flask, render_template, request, jsonify

app = Flask(__name__)
app.config['MAX_CONTENT_LENGTH'] = 16 * 1024 * 1024  # 16 MB max

BASE_DIR = Path(__file__).resolve().parent
DATA_DIR = BASE_DIR / 'data'
MODEL_PATH = DATA_DIR / 'trashnet_resnet18_best.pth'
CLASSES_PATH = DATA_DIR / 'classes.json'

device = torch.device('cuda:0' if torch.cuda.is_available() else 'cpu')

# Load classes mapping
with open(CLASSES_PATH, 'r', encoding='utf-8') as f:
    class_to_idx = json.load(f)
idx_to_class = {v: k for k, v in class_to_idx.items()}

# Waste category metadata (bilingual information & environmental guidelines)
WASTE_META = {
    'battery': {
        'name_vi': 'Pin & Ắc quy',
        'category': 'Rác nguy hại',
        'badge_color': '#ef4444',
        'bin': 'Thùng rác nguy hại (Màu Đỏ)',
        'icon': '🔋',
        'tips': 'Tuyệt đối không vứt chung với rác sinh hoạt. Thu gom và đem đến các điểm tái chế rác điện tử để tránh rò rỉ hóa chất độc hại.'
    },
    'biological': {
        'name_vi': 'Rác hữu cơ',
        'category': 'Rác hữu cơ',
        'badge_color': '#22c55e',
        'bin': 'Thùng rác hữu cơ (Màu Xanh lá)',
        'icon': '🍎',
        'tips': 'Bao gồm thức ăn thừa, cuống rau củ quả. Có thể ủ làm phân bón sinh học hoặc thức ăn gia súc.'
    },
    'cardboard': {
        'name_vi': 'Bìa carton & Hộp giấy',
        'category': 'Rác tái chế',
        'badge_color': '#3b82f6',
        'bin': 'Thùng rác tái chế (Màu Xanh dương)',
        'icon': '📦',
        'tips': 'Gấp phẳng hộp để tiết kiệm diện tích. Giữ khô ráo và bỏ vào thùng rác tái chế giấy.'
    },
    'glass': {
        'name_vi': 'Thủy tinh & Chai lọ',
        'category': 'Rác tái chế',
        'badge_color': '#06b6d4',
        'bin': 'Thùng rác tái chế (Màu Xanh dương)',
        'icon': '🥛',
        'tips': 'Rửa sạch cặn đồ uống. Nếu chai lọ bị nứt vỡ, hãy bọc giấy báo cẩn thận để bảo vệ người thu gom rác.'
    },
    'metal': {
        'name_vi': 'Kim loại & Vỏ lon',
        'category': 'Rác tái chế',
        'badge_color': '#f59e0b',
        'bin': 'Thùng rác tái chế (Màu Xanh dương)',
        'icon': '🥫',
        'tips': 'Vỏ lon nhôm, đồ hộp sắt có giá trị tái chế cao. Nên súc rửa sạch và dẹp phẳng trước khi vứt.'
    },
    'paper': {
        'name_vi': 'Giấy vụn & Sách báo',
        'category': 'Rác tái chế',
        'badge_color': '#60a5fa',
        'bin': 'Thùng rác tái chế (Màu Xanh dương)',
        'icon': '📄',
        'tips': 'Giữ giấy sạch và khô. Tránh để dính dầu mỡ hoặc thực phẩm vì sẽ không tái chế được.'
    },
    'plastic': {
        'name_vi': 'Nhựa & Chai nhựa',
        'category': 'Rác tái chế',
        'badge_color': '#10b981',
        'bin': 'Thùng rác tái chế (Màu Vàng / Xanh)',
        'icon': '🧴',
        'tips': 'Đổ sạch chất lỏng bên trong, ép dẹp chai để giảm thể tích và bỏ vào thùng rác tái chế nhựa.'
    },
    'trash': {
        'name_vi': 'Rác vô cơ khác',
        'category': 'Rác không thể tái chế',
        'badge_color': '#6b7280',
        'bin': 'Thùng rác thông thường (Màu Xám)',
        'icon': '🗑️',
        'tips': 'Bao gồm rác sinh hoạt tổng hợp không tái chế được. Cần buộc chặt túi rác để đưa đi chôn lấp hợp vệ sinh.'
    }
}

# Image transformation identical to training/validation pipeline
transform = transforms.Compose([
    transforms.Resize((224, 224)),
    transforms.ToTensor(),
    transforms.Normalize([0.485, 0.456, 0.406], [0.229, 0.224, 0.225])
])

# Initialize model
def load_model():
    model = models.resnet18()
    model.fc = nn.Linear(model.fc.in_features, len(idx_to_class))
    state_dict = torch.load(MODEL_PATH, map_location=device)
    model.load_state_dict(state_dict)
    model.to(device)
    model.eval()
    return model

model = load_model()
print(f"Model successfully loaded on {device} with {len(idx_to_class)} classes.")

def predict_pil_image(image: Image.Image):
    image = image.convert('RGB')
    tensor = transform(image).unsqueeze(0).to(device)
    with torch.no_grad():
        outputs = model(tensor)
        probs = torch.softmax(outputs, dim=1)[0]
    
    # Sort all classes by probability descending
    sorted_probs, indices = torch.sort(probs, descending=True)
    
    top_idx = indices[0].item()
    top_class = idx_to_class[top_idx]
    top_conf = sorted_probs[0].item()

    all_predictions = []
    for prob, idx in zip(sorted_probs, indices):
        c_name = idx_to_class[idx.item()]
        meta = WASTE_META.get(c_name, {})
        all_predictions.append({
            'class': c_name,
            'name_vi': meta.get('name_vi', c_name),
            'category': meta.get('category', ''),
            'badge_color': meta.get('badge_color', '#6b7280'),
            'icon': meta.get('icon', '♻️'),
            'confidence': round(prob.item() * 100, 2)
        })

    top_meta = WASTE_META.get(top_class, {})
    return {
        'class': top_class,
        'name_vi': top_meta.get('name_vi', top_class),
        'category': top_meta.get('category', 'Không xác định'),
        'badge_color': top_meta.get('badge_color', '#10b981'),
        'bin': top_meta.get('bin', 'Thùng rác thông thường'),
        'icon': top_meta.get('icon', '♻️'),
        'tips': top_meta.get('tips', ''),
        'confidence': round(top_conf * 100, 2),
        'all_predictions': all_predictions
    }

@app.route('/')
def index():
    return render_template('index.html')

@app.route('/api/predict', methods=['POST'])
def predict():
    try:
        # Handle file upload
        if 'image' in request.files:
            file = request.files['image']
            if file.filename == '':
                return jsonify({'error': 'Không tìm thấy file ảnh'}), 400
            image = Image.open(file.stream)
            result = predict_pil_image(image)
            return jsonify(result)

        # Handle base64 from webcam
        data = request.get_json(silent=True)
        if data and 'image_base64' in data:
            raw_base64 = data['image_base64']
            if ',' in raw_base64:
                raw_base64 = raw_base64.split(',')[1]
            image_bytes = base64.b64decode(raw_base64)
            image = Image.open(io.BytesIO(image_bytes))
            result = predict_pil_image(image)
            return jsonify(result)

        return jsonify({'error': 'Không có dữ liệu ảnh hợp lệ'}), 400
    except Exception as e:
        return jsonify({'error': str(e)}), 500

if __name__ == '__main__':
    import sys
    if hasattr(sys.stdout, 'reconfigure'):
        sys.stdout.reconfigure(encoding='utf-8')
    print("=" * 60)
    print("TrashNet Smart AI Web Server is running!")
    print("Open your browser at: http://127.0.0.1:5000")
    print("=" * 60)
    app.run(host='0.0.0.0', port=5000, debug=False)
