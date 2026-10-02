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
    'burnable': {
        'name_vi': 'Rác cháy được (Hữu cơ & Giấy)',
        'category': 'Rác cháy được',
        'badge_color': '#22c55e',
        'badge_bg': 'rgba(34, 197, 94, 0.15)',
        'bin': 'Thùng rác hữu cơ / cháy được (Màu Xanh lá)',
        'bin_color': '#16a34a',
        'icon': 'fa-solid fa-fire',
        'color': '#22c55e',
        'tips': 'Bao gồm thức ăn thừa, cuống rau củ quả, giấy báo, bã hữu cơ dễ cháy hoặc phân hủy sinh học.'
    },
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
# ==============================================================================
# BẢNG QUY HOẠCH 4 NGĂN RÁC CHUẨN XÁC CHO ĐỘNG CƠ BƯỚC + SERVO 180° + LCD 1602
# ==============================================================================
BIN_MAPPING = {
    # 1. Ngăn 1: Rác Cháy Được (burnable) - Góc 0°
    'burnable': {
        'bin': 1,
        'bin_name': 'Rác Cháy Được (Hữu cơ & Giấy)',
        'lcd_title': 'Rac Chay Duoc',
        'angle': 0,
        'color': '#22c55e',
        'icon': 'fa-solid fa-fire'
    },

    # 2. Ngăn 2: Chai Lọ Thủy Tinh (glass) - Góc 90°
    'glass': {
        'bin': 2,
        'bin_name': 'Rác Thủy Tinh (Chai lọ)',
        'lcd_title': 'Chai Thuy Tinh',
        'angle': 90,
        'color': '#06b6d4',
        'icon': 'fa-solid fa-wine-glass-empty'
    },

    # 3. Ngăn 3: Kim Loại & Vỏ Lon (metal) - Góc 180°
    'metal': {
        'bin': 3,
        'bin_name': 'Rác Kim Loại (Vỏ lon)',
        'lcd_title': 'Vo Lon Kim Loai',
        'angle': 180,
        'color': '#f59e0b',
        'icon': 'fa-solid fa-cube'
    },

    # 4. Ngăn 4: Nhựa & Chai PET (plastic) - Góc 270°
    'plastic': {
        'bin': 4,
        'bin_name': 'Rác Nhựa (Chai PET)',
        'lcd_title': 'Chai Nhua (PET)',
        'angle': 270,
        'color': '#10b981',
        'icon': 'fa-solid fa-bottle-water'
    },

    # Lớp ánh xạ dự phòng:
    'biological': {
        'bin': 1,
        'bin_name': 'Rác Hữu Cơ Sinh Hoạt',
        'lcd_title': 'Huu Co (Do an)',
        'angle': 0,
        'color': '#22c55e',
        'icon': 'fa-solid fa-apple-whole'
    },
    'cardboard': {
        'bin': 1,
        'bin_name': 'Rác Cháy Được (Bìa Carton)',
        'lcd_title': 'Bia Carton',
        'angle': 0,
        'color': '#3b82f6',
        'icon': 'fa-solid fa-box-open'
    },
    'paper': {
        'bin': 1,
        'bin_name': 'Rác Cháy Được (Giấy báo)',
        'lcd_title': 'Giay Bao / Tap',
        'angle': 0,
        'color': '#60a5fa',
        'icon': 'fa-solid fa-newspaper'
    },
    'trash': {
        'bin': 1,
        'bin_name': 'Rác Vô Cơ Sinh Hoạt',
        'lcd_title': 'Rac Vo Co Khac',
        'angle': 0,
        'color': '#94a3b8',
        'icon': 'fa-solid fa-trash-can'
    },
    'battery': {
        'bin': 3,
        'bin_name': 'Rác Thải Nguy Hại',
        'lcd_title': 'Pin / Ac Quy',
        'angle': 180,
        'color': '#ef4444',
        'icon': 'fa-solid fa-car-battery'
    }
}

def get_bin_info_for_label(label):
    lbl = (label or '').strip().lower()
    return BIN_MAPPING.get(lbl, {
        'bin': 1,
        'bin_name': 'Rác Cháy Được (Mặc định)',
        'lcd_title': 'Rac Chay Duoc',
        'angle': 0,
        'color': '#22c55e',
        'icon': 'fa-solid fa-fire'
    })

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

    bin_info = get_bin_info_for_label(top_class)

    return {
        "class": top_class,
        "name_vi": top_meta.get("name_vi", top_class),
        "category": top_meta.get("category", "Chưa xác định"),
        "badge_color": top_meta.get("badge_color", "#10b981"),
        "badge_bg": top_meta.get("badge_bg", "rgba(16, 185, 129, 0.15)"),
        "bin": f"Ngăn {bin_info['bin']}: {bin_info['bin_name']}",
        "bin_number": bin_info["bin"],
        "bin_color": bin_info["color"],
        "actuator": bin_info,
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


@app.route('/firmware_esp32_cam/<path:filename>')
def serve_firmware(filename):
    return send_from_directory(BASE_DIR / 'firmware_esp32_cam', filename)



import urllib.request
import urllib.error
import urllib.parse
import threading

# CẤU HÌNH GỬI TÍN HIỆU ĐIỀU KHIỂN SANG ESP32 ACTUATOR (SERVO 180° + STEPPER 4 HƯỚNG + LCD 1602A)
# ==============================================================================
ENABLE_ESP32_ACTUATOR = True  # Kích hoạt điều khiển ESP32 Thường
ESP32_ACTUATOR_IP = "http://172.16.3.205"  # IP thực tế của ESP32 Actuator tại cafe/wifi hiện tại

def _dispatch_esp32_actuator_req(url):
    try:
        req = urllib.request.Request(url, headers={'User-Agent': 'TrashNet-Server/1.0'})
        with urllib.request.urlopen(req, timeout=3.0) as resp:
            print(f"[ESP32-ACTUATOR] >>> Gửi lệnh thành công ({url}) - HTTP {resp.getcode()}")
    except Exception as err:
        print(f"[ESP32-ACTUATOR CẢNH BÁO] Không thể kết nối tới ESP32 Actuator ({url}): {err}")

def send_command_to_esp32_actuator(label, display_name=None):
    """
    Gửi tín hiệu điều khiển bất đồng bộ sang ESP32 Thường:
    1. Hiển thị loại rác và số ngăn lên màn hình LCD 1602
    2. Động cơ bước 28BYJ-48 xoay thùng rác về ngăn tương ứng (0°, 90°, 180°, 270°)
    3. Servo MG995 mở nắp 180° để rác rơi vào, sau đó đóng nắp về 0°
    """
    if not ENABLE_ESP32_ACTUATOR:
        return None

    bin_info = get_bin_info_for_label(label)
    bin_num = bin_info['bin']
    lcd_name = bin_info.get('lcd_title', label)

    # Đảm bảo chuỗi tối đa 16 ký tự, chuẩn ASCII cho LCD 1602
    clean_name = str(lcd_name)[:16]
    url = f"{ESP32_ACTUATOR_IP.rstrip('/')}/action?bin={bin_num}&label={label}&name={urllib.parse.quote(clean_name)}"
    
    # Chạy trên Thread riêng biệt để không làm chậm phản hồi AI của Web
    th = threading.Thread(target=_dispatch_esp32_actuator_req, args=(url,), daemon=True)
    th.start()
    return bin_info


@app.route('/api/predict', methods=['POST'])
def handle_predict():
    try:
        raw_res = None

        # 1. Handle file upload
        if 'image' in request.files:
            file = request.files['image']
            if file.filename != '':
                image = Image.open(file.stream)
                raw_res = predict(image)

        # 2. Handle base64 from webcam (JSON body)
        if raw_res is None:
            data = request.get_json(silent=True)
            if data and 'image_base64' in data:
                raw_base64 = data['image_base64']
                if ',' in raw_base64:
                    raw_base64 = raw_base64.split(',')[1]
                image_bytes = base64.b64decode(raw_base64)
                image = Image.open(io.BytesIO(image_bytes))
                raw_res = predict(image)

        # 3. Handle raw binary JPEG sent directly from ESP32-CAM HTTP POST
        if raw_res is None and request.data and len(request.data) > 0:
            try:
                image = Image.open(io.BytesIO(request.data))
                raw_res = predict(image)
            except Exception:
                pass

        if raw_res is None:
            return jsonify({'error': 'Không có dữ liệu ảnh hợp lệ được cung cấp.'}), 400

        formatted = format_prediction_result(raw_res)

        # Trích xuất nhãn dự đoán tốt nhất và gửi tín hiệu sang ESP32 Actuator (4 ngăn + Servo 180° + LCD)
        best_label = formatted.get('class')
        if best_label:
            send_command_to_esp32_actuator(best_label, formatted.get('name_vi'))

        return jsonify(formatted)
    except Exception as e:
        return jsonify({'error': str(e)}), 500


@app.route('/api/esp32/ping', methods=['GET', 'POST'])
def esp32_ping():
    """Check connectivity to an ESP32-CAM IP or URL."""
    target = request.args.get('ip') or request.args.get('url')
    if not target and request.is_json:
        data = request.get_json(silent=True) or {}
        target = data.get('ip') or data.get('url')

    if not target:
        return jsonify({'error': 'Vui lòng cung cấp IP hoặc URL của ESP32-CAM'}), 400

    target = target.strip()
    if not target.startswith('http://') and not target.startswith('https://'):
        target = 'http://' + target

    try:
        req = urllib.request.Request(target, headers={'User-Agent': 'TrashNet-Server/1.0'})
        with urllib.request.urlopen(req, timeout=2.5) as resp:
            status_code = resp.getcode()
            return jsonify({'online': True, 'status': status_code, 'target': target})
    except Exception as e:
        return jsonify({'online': False, 'error': str(e), 'target': target}), 200


@app.route('/api/esp32/capture', methods=['GET', 'POST'])
def esp32_capture():
    """Fetch frame from ESP32-CAM /capture endpoint and run AI classification."""
    target = request.args.get('ip') or request.args.get('url')
    if not target and request.is_json:
        data = request.get_json(silent=True) or {}
        target = data.get('ip') or data.get('url')

    if not target:
        return jsonify({'error': 'Vui lòng cung cấp IP hoặc URL của ESP32-CAM'}), 400

    target = target.strip()
    if not target.startswith('http://') and not target.startswith('https://'):
        target = 'http://' + target

    # Ensure target points to capture endpoint
    if not target.endswith('/capture') and not target.endswith('/jpg') and not target.endswith('.jpg'):
        target = target.rstrip('/') + '/capture'

    try:
        req = urllib.request.Request(target, headers={'User-Agent': 'TrashNet-Server/1.0'})
        with urllib.request.urlopen(req, timeout=10.0) as resp:
            image_data = resp.read()

        if not image_data or len(image_data) < 100:
            return jsonify({'error': 'Không nhận được dữ liệu ảnh từ ESP32-CAM'}), 502

        image = Image.open(io.BytesIO(image_data))
        raw_res = predict(image)
        result = format_prediction_result(raw_res)

        # Trích xuất nhãn và gửi lệnh sang ESP32 Actuator
        best_label = result.get('fusion', {}).get('predicted_label') or result.get('resnet', {}).get('predicted_label') or result.get('class')
        if best_label:
            actuator_res = send_command_to_esp32_actuator(best_label, result.get('name_vi'))
            if actuator_res:
                result['actuator'] = actuator_res

        # Include base64 of captured frame so UI can preview it
        result['captured_image'] = 'data:image/jpeg;base64,' + base64.b64encode(image_data).decode('ascii')
        return jsonify(result)
    except urllib.error.URLError as e:
        return jsonify({'error': f'Không thể kết nối đến ESP32-CAM ({target}): {e.reason}'}), 504
    except Exception as e:
        return jsonify({'error': f'Lỗi khi xử lý ảnh từ ESP32-CAM: {str(e)}'}), 500


@app.route('/api/actuator/config', methods=['GET', 'POST'])
def actuator_config():
    """Get or update ESP32 Actuator configuration."""
    global ENABLE_ESP32_ACTUATOR, ESP32_ACTUATOR_IP
    if request.method == 'POST':
        data = request.get_json(silent=True) or {}
        if 'enabled' in data:
            ENABLE_ESP32_ACTUATOR = bool(data['enabled'])
        if 'ip' in data and data['ip'].strip():
            ip = data['ip'].strip()
            if not ip.startswith('http://') and not ip.startswith('https://'):
                ip = 'http://' + ip
            ESP32_ACTUATOR_IP = ip
        return jsonify({
            'status': 'ok',
            'enabled': ENABLE_ESP32_ACTUATOR,
            'ip': ESP32_ACTUATOR_IP
        })
    return jsonify({
        'enabled': ENABLE_ESP32_ACTUATOR,
        'ip': ESP32_ACTUATOR_IP,
        'bins': BIN_MAPPING
    })


@app.route('/api/actuator/status', methods=['GET'])
def actuator_status():
    """Fetch status from ESP32 Actuator board."""
    if not ENABLE_ESP32_ACTUATOR:
        return jsonify({'online': False, 'enabled': False, 'message': 'Actuator disabled'})
    try:
        url = f"{ESP32_ACTUATOR_IP.rstrip('/')}/status"
        req = urllib.request.Request(url, headers={'User-Agent': 'TrashNet-Server/1.0'})
        with urllib.request.urlopen(req, timeout=2.0) as resp:
            data = json.loads(resp.read().decode('utf-8'))
            data['enabled'] = True
            data['ip'] = ESP32_ACTUATOR_IP
            return jsonify(data)
    except Exception as e:
        return jsonify({
            'online': False,
            'enabled': True,
            'ip': ESP32_ACTUATOR_IP,
            'error': str(e)
        })


@app.route('/api/actuator/control', methods=['POST'])
def actuator_control():
    """Manually test bin rotation or servo lid from Web UI."""
    data = request.get_json(silent=True) or {}
    action = data.get('action')  # 'bin', 'full', 'servo', or 'rotate'
    try:
        if action in ('bin', 'full'):
            bin_num = int(data.get('bin', 1))
            name = data.get('name', '')
            label = data.get('label', 'manual')
            url = f"{ESP32_ACTUATOR_IP.rstrip('/')}/action?bin={bin_num}&label={label}&name={urllib.parse.quote(name)}"
        elif action == 'servo':
            angle = int(data.get('angle', 0))
            url = f"{ESP32_ACTUATOR_IP.rstrip('/')}/servo?angle={angle}"
        elif action == 'rotate':
            bin_num = int(data.get('bin', 1))
            url = f"{ESP32_ACTUATOR_IP.rstrip('/')}/rotate?bin={bin_num}"
        else:
            return jsonify({'error': 'Hành động không hợp lệ'}), 400

        req = urllib.request.Request(url, headers={'User-Agent': 'TrashNet-Server/1.0'})
        with urllib.request.urlopen(req, timeout=2.5) as resp:
            raw = resp.read().decode('utf-8')
            return jsonify({'status': 'ok', 'response': json.loads(raw) if raw.startswith('{') else raw})
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
