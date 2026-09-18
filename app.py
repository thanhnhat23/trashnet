import base64
import io
import json
import os
from pathlib import Path
from PIL import Image
from flask import Flask, render_template, request, jsonify, send_from_directory

# Import Late Fusion Predictor (ResNet18 + YOLO11n-cls)
from fusion.predictor import predict, RESNET_WEIGHT, YOLO_WEIGHT

app = Flask(__name__)
app.config['MAX_CONTENT_LENGTH'] = 16 * 1024 * 1024  # 16 MB max
app.config['TEMPLATES_AUTO_RELOAD'] = True
app.jinja_env.auto_reload = True

@app.after_request
def add_header(response):
    response.headers['Cache-Control'] = 'no-store, no-cache, must-revalidate, max-age=0'
    response.headers['Pragma'] = 'no-cache'
    response.headers['Expires'] = '-1'
    return response

BASE_DIR = Path(__file__).resolve().parent

# Waste category metadata with FontAwesome 6 icons and professional color schemes
WASTE_META = {
    'battery': {
        'name_vi': 'Pin & Ắc quy',
        'category': 'Rác nguy hại',
        'badge_color': '#ef4444',
        'badge_bg': 'rgba(239, 68, 68, 0.15)',
        'bin': 'Thùng rác nguy hại (Màu Đỏ)',
        'bin_color': '#dc2626',
        'icon': 'fa-solid fa-car-battery',
        'color': '#ef4444',
        'tips': 'Tuyệt đối không vứt chung với rác sinh hoạt. Thu gom và đem đến các điểm tái chế rác điện tử để tránh rò rỉ hóa chất độc hại vào đất và nguồn nước.'
    },
    'biological': {
        'name_vi': 'Rác hữu cơ sinh hoạt',
        'category': 'Rác hữu cơ',
        'badge_color': '#22c55e',
        'badge_bg': 'rgba(34, 197, 94, 0.15)',
        'bin': 'Thùng rác hữu cơ (Màu Xanh lá)',
        'bin_color': '#16a34a',
        'icon': 'fa-solid fa-apple-whole',
        'color': '#22c55e',
        'tips': 'Bao gồm thức ăn thừa, cuống rau củ quả, vỏ trứng, bã cà phê. Có thể ủ làm phân bón sinh học hữu cơ cho cây trồng.'
    },
    'cardboard': {
        'name_vi': 'Bìa carton & Hộp giấy',
        'category': 'Rác tái chế',
        'badge_color': '#3b82f6',
        'badge_bg': 'rgba(59, 130, 246, 0.15)',
        'bin': 'Thùng rác tái chế (Màu Xanh dương)',
        'bin_color': '#2563eb',
        'icon': 'fa-solid fa-box-open',
        'color': '#3b82f6',
        'tips': 'Gấp phẳng hộp để tiết kiệm diện tích. Giữ khô ráo và bỏ vào thùng rác tái chế giấy.'
    },
    'glass': {
        'name_vi': 'Thủy tinh & Chai lọ',
        'category': 'Rác tái chế',
        'badge_color': '#06b6d4',
        'badge_bg': 'rgba(6, 182, 212, 0.15)',
        'bin': 'Thùng rác tái chế (Màu Xanh dương)',
        'bin_color': '#0891b2',
        'icon': 'fa-solid fa-wine-glass-empty',
        'color': '#06b6d4',
        'tips': 'Rửa sạch cặn đồ uống. Nếu chai lọ bị nứt vỡ, hãy bọc giấy báo cẩn thận để bảo vệ an toàn cho người thu gom rác.'
    },
    'metal': {
        'name_vi': 'Kim loại & Vỏ lon',
        'category': 'Rác tái chế',
        'badge_color': '#f59e0b',
        'badge_bg': 'rgba(245, 158, 11, 0.15)',
        'bin': 'Thùng rác tái chế (Màu Xanh dương)',
        'bin_color': '#d97706',
        'icon': 'fa-solid fa-cube',
        'color': '#f59e0b',
        'tips': 'Vỏ lon nhôm, đồ hộp sắt có giá trị tái chế rất cao. Nên súc rửa sạch và dẹp phẳng trước khi bỏ vào thùng rác.'
    },
    'paper': {
        'name_vi': 'Giấy vụn & Sách báo',
        'category': 'Rác tái chế',
        'badge_color': '#60a5fa',
        'badge_bg': 'rgba(96, 165, 250, 0.15)',
        'bin': 'Thùng rác tái chế (Màu Xanh dương)',
        'bin_color': '#2563eb',
        'icon': 'fa-solid fa-newspaper',
        'color': '#60a5fa',
        'tips': 'Giữ giấy sạch và khô ráo. Tránh để dính dầu mỡ hoặc thực phẩm vì sẽ làm giảm khả năng tái chế sợi bột giấy.'
    },
    'plastic': {
        'name_vi': 'Nhựa & Chai nhựa PET',
        'category': 'Rác tái chế',
        'badge_color': '#10b981',
        'badge_bg': 'rgba(16, 185, 129, 0.15)',
        'bin': 'Thùng rác tái chế (Màu Vàng / Xanh)',
        'bin_color': '#eab308',
        'icon': 'fa-solid fa-bottle-water',
        'color': '#10b981',
        'tips': 'Đổ sạch chất lỏng bên trong, ép dẹp chai để giảm thể tích và bỏ vào thùng rác tái chế nhựa.'
    },
    'trash': {
        'name_vi': 'Rác vô cơ khác',
        'category': 'Rác không tái chế',
        'badge_color': '#94a3b8',
        'badge_bg': 'rgba(148, 163, 184, 0.15)',
        'bin': 'Thùng rác thông thường (Màu Xám / Đen)',
        'bin_color': '#475569',
        'icon': 'fa-solid fa-trash-can',
        'color': '#94a3b8',
        'tips': 'Bao gồm túi nilon bẩn, khăn ướt, tã lót, rác sinh hoạt tổng hợp không tái chế được. Cần buộc chặt túi để đưa đi xử lý hợp vệ sinh.'
    }
}

def format_prediction_result(raw_result):
    """
    Enriches raw fusion prediction result with metadata, modern FontAwesome styling, and multi-model comparison.
    """
    top_class = raw_result["class"]
    top_meta = WASTE_META.get(top_class, {})

    # Enrich all prediction items
    enriched_predictions = []
    for item in raw_result["all_predictions"]:
        c_name = item["class"]
        meta = WASTE_META.get(c_name, {})
        enriched_predictions.append({
            "class": c_name,
            "name_vi": meta.get("name_vi", c_name),
            "category": meta.get("category", "Chưa phân loại"),
            "badge_color": meta.get("badge_color", "#94a3b8"),
            "badge_bg": meta.get("badge_bg", "rgba(148, 163, 184, 0.15)"),
            "icon": meta.get("icon", "fa-solid fa-recycle"),
            "color": meta.get("color", "#10b981"),
            "confidence": item["confidence"]
        })

    # Model agreement check
    resnet_pred = raw_result["resnet_prediction"]
    yolo_pred = raw_result["yolo_prediction"]
    models_agree = (resnet_pred == yolo_pred)

    resnet_meta = WASTE_META.get(resnet_pred, {})
    yolo_meta = WASTE_META.get(yolo_pred, {})

    return {
        "class": top_class,
        "name_vi": top_meta.get("name_vi", top_class),
        "category": top_meta.get("category", "Chưa xác định"),
        "badge_color": top_meta.get("badge_color", "#10b981"),
        "badge_bg": top_meta.get("badge_bg", "rgba(16, 185, 129, 0.15)"),
        "bin": top_meta.get("bin", "Thùng rác thông thường"),
        "bin_color": top_meta.get("bin_color", "#10b981"),
        "icon": top_meta.get("icon", "fa-solid fa-recycle"),
        "color": top_meta.get("color", "#10b981"),
        "tips": top_meta.get("tips", ""),
        "confidence": raw_result["confidence"],
        "all_predictions": enriched_predictions,

        # Multi-model detailed breakdown
        "fusion_info": {
            "method": "Late Fusion (Soft Voting)",
            "accuracy": "99.37%",
            "models_agree": models_agree,
            "resnet": {
                "name": "ResNet-18",
                "weight": RESNET_WEIGHT,
                "weight_percent": int(RESNET_WEIGHT * 100),
                "prediction": resnet_pred,
                "name_vi": resnet_meta.get("name_vi", resnet_pred),
                "icon": "fa-solid fa-network-wired",
                "confidence": raw_result["resnet_confidence"]
            },
            "yolo": {
                "name": "YOLO11n-cls",
                "weight": YOLO_WEIGHT,
                "weight_percent": int(YOLO_WEIGHT * 100),
                "prediction": yolo_pred,
                "name_vi": yolo_meta.get("name_vi", yolo_pred),
                "icon": "fa-solid fa-bolt-lightning",
                "confidence": raw_result["yolo_confidence"]
            }
        }
    }


@app.route('/')
def index():
    return render_template('index.html')


@app.route('/test_yolo/<path:filename>')
def serve_sample(filename):
    return send_from_directory(BASE_DIR / 'test_yolo', filename)



@app.route('/api/predict', methods=['POST'])
def handle_predict():
    try:
        # Handle file upload
        if 'image' in request.files:
            file = request.files['image']
            if file.filename == '':
                return jsonify({'error': 'Không tìm thấy file ảnh được chọn.'}), 400
            image = Image.open(file.stream)
            raw_res = predict(image)
            return jsonify(format_prediction_result(raw_res))

        # Handle base64 from webcam
        data = request.get_json(silent=True)
        if data and 'image_base64' in data:
            raw_base64 = data['image_base64']
            if ',' in raw_base64:
                raw_base64 = raw_base64.split(',')[1]
            image_bytes = base64.b64decode(raw_base64)
            image = Image.open(io.BytesIO(image_bytes))
            raw_res = predict(image)
            return jsonify(format_prediction_result(raw_res))

        return jsonify({'error': 'Không có dữ liệu ảnh hợp lệ được cung cấp.'}), 400
    except Exception as e:
        return jsonify({'error': str(e)}), 500


@app.route('/api/info', methods=['GET'])
def get_model_info():
    return jsonify({
        "system": "TrashNet Multi-Model AI Fusion",
        "fusion_method": "Late Fusion (Weighted Soft Voting)",
        "fusion_formula": "P_final = 0.70 * P_ResNet18 + 0.30 * P_YOLO11n",
        "models": [
            {
                "name": "ResNet-18",
                "role": "Deep Residual Feature Extractor",
                "weight": 0.70,
                "accuracy": "99.27%",
                "status": "Active"
            },
            {
                "name": "YOLO11n-cls",
                "role": "Ultra-Fast Lightweight Classifier",
                "weight": 0.30,
                "accuracy": "96.01%",
                "status": "Active"
            },
            {
                "name": "Ensemble Late Fusion",
                "role": "Weighted Soft Voting Strategy",
                "weight": 1.00,
                "accuracy": "99.37%",
                "status": "Optimal Best"
            }
        ]
    })


if __name__ == '__main__':
    import sys
    if hasattr(sys.stdout, 'reconfigure'):
        sys.stdout.reconfigure(encoding='utf-8')
    print("=" * 60)
    print("TrashNet Smart AI")
    print("Open your browser at: http://127.0.0.1:5001")
    print("=" * 60)
    app.run(host='0.0.0.0', port=5001, debug=False)
