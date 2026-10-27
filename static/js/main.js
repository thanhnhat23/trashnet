// TrashNet - Minimalist Monochrome Controller (IoT & Camera Vision Hub)
let currentCameraSource = 'webcam';
let currentModelType = 'fusion';
try {
    const savedModel = localStorage.getItem('trashnet_model_type');
    if (savedModel && ['fusion', 'resnet', 'yolo'].includes(savedModel)) {
        currentModelType = savedModel;
    }
} catch (e) {}

let webcamStream = null;
let webcamAutoScanTimer = null;
let esp32AutoScanTimer = null;
let esp32Connected = false;
let esp32FlashState = false;
let scanHistory = [];

// DOM Elements - Camera
const tabWebcamBtn = document.getElementById('tabWebcamBtn');
const tabEsp32Btn = document.getElementById('tabEsp32Btn');
const webcamView = document.getElementById('webcamView');
const esp32View = document.getElementById('esp32View');

// Webcam DOM
const webcamVideo = document.getElementById('webcamVideo');
const webcamCanvas = document.getElementById('webcamCanvas');
const webcamOffOverlay = document.getElementById('webcamOffOverlay');
const webcamHud = document.getElementById('webcamHud');
const webcamActionGroup = document.getElementById('webcamActionGroup');
const btnSnap = document.getElementById('btnSnap');
const webcamAutoScanCheck = document.getElementById('webcamAutoScanCheck');

// ESP32 DOM
const esp32IpInput = document.getElementById('esp32IpInput');
const btnConnectEsp32 = document.getElementById('btnConnectEsp32');
const esp32StatusText = document.getElementById('esp32StatusText');
const esp32StreamImg = document.getElementById('esp32StreamImg');
const esp32Canvas = document.getElementById('esp32Canvas');
const esp32OffOverlay = document.getElementById('esp32OffOverlay');
const esp32Hud = document.getElementById('esp32Hud');
const btnSnapEsp32 = document.getElementById('btnSnapEsp32');
const esp32AutoScanCheck = document.getElementById('esp32AutoScanCheck');
const btnToggleFlash = document.getElementById('btnToggleFlash');

// Result Targets
const emptyState = document.getElementById('emptyState');
const resultState = document.getElementById('resultState');
const engineStatus = document.getElementById('engineStatus');
const resCategoryTag = document.getElementById('resCategoryTag');
const resClassTitle = document.getElementById('resClassTitle');
const resClassEn = document.getElementById('resClassEn');
const resConfidenceNum = document.getElementById('resConfidenceNum');
const resProgressBar = document.getElementById('resProgressBar');

const resNetPredVal = document.getElementById('resNetPredVal');
const resNetConfVal = document.getElementById('resNetConfVal');
const yoloPredVal = document.getElementById('yoloPredVal');
const yoloConfVal = document.getElementById('yoloConfVal');

const resBinName = document.getElementById('resBinName');
const resTipsContent = document.getElementById('resTipsContent');
const probabilityTableBody = document.getElementById('probabilityTableBody');

const sidebarHistoryCount = document.getElementById('sidebarHistoryCount');
const fullHistoryEmpty = document.getElementById('fullHistoryEmpty');
const fullHistoryGrid = document.getElementById('fullHistoryGrid');
const currentViewTitle = document.getElementById('currentViewTitle');

// Init saved ESP32 IP
try {
    const savedIp = localStorage.getItem('trashnet_esp32_ip');
    if (savedIp && esp32IpInput) {
        esp32IpInput.value = savedIp;
    }
} catch (e) {}

// Init history from localStorage
try {
    const saved = localStorage.getItem('trashnet_history');
    if (saved) {
        scanHistory = JSON.parse(saved);
        updateHistoryCounters();
    }
} catch (e) {
    console.warn('Could not read history from localStorage');
}

// ============================================================
// 0. AI MODEL SWITCHER (FUSION, RESNET-18, YOLO11n)
// ============================================================
function switchAiModel(modelType) {
    if (!['fusion', 'resnet', 'yolo'].includes(modelType)) {
        modelType = 'fusion';
    }
    currentModelType = modelType;
    try {
        localStorage.setItem('trashnet_model_type', currentModelType);
    } catch (e) {}

    const btnFusion = document.getElementById('btnModelFusion');
    const btnResnet = document.getElementById('btnModelResnet');
    const btnYolo = document.getElementById('btnModelYolo');

    if (btnFusion) btnFusion.classList.toggle('active', modelType === 'fusion');
    if (btnResnet) btnResnet.classList.toggle('active', modelType === 'resnet');
    if (btnYolo) btnYolo.classList.toggle('active', modelType === 'yolo');

    updateActiveModelUi(modelType);
}

function updateActiveModelUi(modelType) {
    const sidebarActiveModel = document.getElementById('sidebarActiveModel');
    const probTableTitle = document.getElementById('probTableTitle');
    const resNetBoxTitle = document.getElementById('resNetBoxTitle');
    const yoloBoxTitle = document.getElementById('yoloBoxTitle');
    const boxResNet = document.getElementById('boxResNetMeta');
    const boxYolo = document.getElementById('boxYoloMeta');

    const meta = {
        fusion: { name: 'Fusion' },
        resnet: { name: 'ResNet-18' },
        yolo: { name: 'YOLO11n' }
    };

    const info = meta[modelType] || meta.fusion;

    if (sidebarActiveModel) sidebarActiveModel.textContent = info.name;
    if (probTableTitle) probTableTitle.textContent = 'Phân phối xác suất';
    if (resNetBoxTitle) resNetBoxTitle.textContent = 'ResNet-18';
    if (yoloBoxTitle) yoloBoxTitle.textContent = 'YOLO11n';

    if (boxResNet) {
        if (modelType === 'resnet') {
            boxResNet.style.borderColor = '#ffffff';
            boxResNet.style.boxShadow = '0 0 10px rgba(255, 255, 255, 0.12)';
        } else {
            boxResNet.style.borderColor = '';
            boxResNet.style.boxShadow = '';
        }
    }
    if (boxYolo) {
        if (modelType === 'yolo') {
            boxYolo.style.borderColor = '#ffffff';
            boxYolo.style.boxShadow = '0 0 10px rgba(255, 255, 255, 0.12)';
        } else {
            boxYolo.style.borderColor = '';
            boxYolo.style.boxShadow = '';
        }
    }
}

// ============================================================
// 1. NAVIGATION TAB SWITCHER (Sidebar Menu)
// ============================================================
function switchNavTab(tabName) {
    const links = document.querySelectorAll('#sidebarNav .sidebar-link');
    links.forEach(link => {
        if (link.getAttribute('data-nav-tab') === tabName) {
            link.classList.add('active');
        } else {
            link.classList.remove('active');
        }
    });

    const viewMap = {
        vision: { id: 'viewVision', title: 'CAMERA NHẬN DIỆN RÁC THẢI AI' },
        history: { id: 'viewHistory', title: 'LỊCH SỬ PHÂN LOẠI TOÀN HỆ THỐNG' },
        iot: { id: 'viewIot', title: 'KẾT NỐI IOT & ĐIỀU KHIỂN THÙNG RÁC' },
        analytics: { id: 'viewAnalytics', title: 'ĐỘ CHÍNH XÁC TRÊN TẬP DỮ LIỆU & HUẤN LUYỆN AI' },
        settings: { id: 'viewSettings', title: 'CẤU HÌNH HỆ THỐNG & AI' }
    };

    Object.keys(viewMap).forEach(key => {
        const el = document.getElementById(viewMap[key].id);
        if (el) {
            if (key === tabName) {
                el.classList.remove('d-none');
                el.classList.add('active');
            } else {
                el.classList.add('d-none');
                el.classList.remove('active');
            }
        }
    });

    if (currentViewTitle && viewMap[tabName]) {
        currentViewTitle.textContent = viewMap[tabName].title;
    }

    if (tabName === 'history') {
        loadAndRenderHistory();
    } else if (tabName === 'settings') {
        loadActuatorSettings();
    } else if (tabName === 'analytics') {
        setTimeout(() => {
            loadDatasetAndTrainStats();
        }, 60);
    }
}


// ============================================================
// 2. CAMERA SOURCE SWITCHER (Webcam vs ESP32-CAM)
// ============================================================
function switchCameraSource(source) {
    currentCameraSource = source;
    if (source === 'webcam') {
        if (tabWebcamBtn) tabWebcamBtn.classList.add('active');
        if (tabEsp32Btn) tabEsp32Btn.classList.remove('active');
        if (webcamView) webcamView.classList.remove('d-none');
        if (esp32View) esp32View.classList.add('d-none');
        // Stop ESP32 auto-scan if active
        if (esp32AutoScanCheck && esp32AutoScanCheck.checked) {
            esp32AutoScanCheck.checked = false;
            toggleEsp32AutoScan(false);
        }
    } else {
        if (tabEsp32Btn) tabEsp32Btn.classList.add('active');
        if (tabWebcamBtn) tabWebcamBtn.classList.remove('active');
        if (esp32View) esp32View.classList.remove('d-none');
        if (webcamView) webcamView.classList.add('d-none');
        // Stop webcam auto-scan if active
        if (webcamAutoScanCheck && webcamAutoScanCheck.checked) {
            webcamAutoScanCheck.checked = false;
            toggleWebcamAutoScan(false);
        }
    }
}

// ============================================================
// 3. WEBCAM CONTROLS
// ============================================================
async function startWebcam() {
    try {
        const stream = await navigator.mediaDevices.getUserMedia({
            video: { width: { ideal: 1280 }, height: { ideal: 720 }, facingMode: 'user' },
            audio: false
        });

        webcamStream = stream;
        if (webcamVideo) webcamVideo.srcObject = stream;
        if (webcamOffOverlay) webcamOffOverlay.classList.add('d-none');
        if (webcamHud) webcamHud.classList.remove('d-none');
        if (webcamActionGroup) webcamActionGroup.classList.remove('d-none');

        if (engineStatus) engineStatus.textContent = 'WEBCAM HOẠT ĐỘNG';
    } catch (err) {
        alert('Không thể mở camera webcam: ' + err.message);
    }
}

function stopWebcam() {
    if (webcamAutoScanTimer) {
        clearInterval(webcamAutoScanTimer);
        webcamAutoScanTimer = null;
        if (webcamAutoScanCheck) webcamAutoScanCheck.checked = false;
    }
    if (webcamStream) {
        webcamStream.getTracks().forEach(t => t.stop());
        webcamStream = null;
    }
    if (webcamVideo) webcamVideo.srcObject = null;
    if (webcamOffOverlay) webcamOffOverlay.classList.remove('d-none');
    if (webcamHud) webcamHud.classList.add('d-none');
    if (webcamActionGroup) webcamActionGroup.classList.add('d-none');
}

function toggleWebcamAutoScan(enabled) {
    if (webcamAutoScanTimer) {
        clearInterval(webcamAutoScanTimer);
        webcamAutoScanTimer = null;
    }
    if (enabled) {
        if (!webcamStream) {
            startWebcam().then(() => {
                webcamAutoScanTimer = setInterval(captureWebcam, 3200);
            });
        } else {
            captureWebcam();
            webcamAutoScanTimer = setInterval(captureWebcam, 3200);
        }
    }
}

async function captureWebcam() {
    if (!webcamVideo || !webcamVideo.videoWidth) return;

    webcamCanvas.width = webcamVideo.videoWidth;
    webcamCanvas.height = webcamVideo.videoHeight;
    const ctx = webcamCanvas.getContext('2d');

    // Mirrored display matching user preview
    ctx.translate(webcamCanvas.width, 0);
    ctx.scale(-1, 1);
    ctx.drawImage(webcamVideo, 0, 0);

    const base64Img = webcamCanvas.toDataURL('image/jpeg', 0.9);
    setLoading(true, 'ĐANG PHÂN TÍCH...');

    try {
        const res = await fetch('/api/predict', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ 
                image_base64: base64Img,
                model_type: currentModelType
            })
        });
        const data = await res.json();
        if (!res.ok) throw new Error(data.error || 'Lỗi nhận diện');

        renderResults(data, base64Img);
    } catch (err) {
        alert('Lỗi: ' + err.message);
        if (engineStatus) engineStatus.textContent = 'LỖI PHÂN TÍCH';
    } finally {
        setLoading(false);
    }
}

// ============================================================
// 4. ESP32-CAM (ESP-IDF) CONTROLS
// ============================================================
function getCleanEsp32Url() {
    let saved = '';
    try { saved = localStorage.getItem('trashnet_esp32_ip') || ''; } catch (e) {}
    let url = (esp32IpInput ? esp32IpInput.value : '').trim() || saved || 'http://172.16.3.132';
    if (!url.startsWith('http://') && !url.startsWith('https://')) {
        url = 'http://' + url;
    }
    return url.replace(/\/+$/, '');
}

function getCleanActuatorUrl() {
    const el = document.getElementById('actuatorIpInputMain') || document.getElementById('actuatorIpInput');
    let saved = '';
    try { saved = localStorage.getItem('trashnet_actuator_ip') || ''; } catch (e) {}
    let url = (el ? el.value : '').trim() || saved || 'http://172.16.0.116';
    if (!url.startsWith('http://') && !url.startsWith('https://')) {
        url = 'http://' + url;
    }
    return url.replace(/\/+$/, '');
}

async function connectEsp32Cam() {
    const baseUrl = getCleanEsp32Url();
    if (esp32IpInput) esp32IpInput.value = baseUrl;

    try {
        localStorage.setItem('trashnet_esp32_ip', baseUrl);
    } catch (e) {}

    if (esp32StatusText) {
        esp32StatusText.innerHTML = '<span class="spinner-border spinner-border-sm me-1"></span> Đang kiểm tra CAM...';
    }

    try {
        // Ping ESP32 via backend to avoid CORS / Mixed Content issues
        const res = await fetch(`/api/esp32/ping?ip=${encodeURIComponent(baseUrl)}`);
        const info = await res.json();

        if (info.online) {
            esp32Connected = true;
            if (esp32StatusText) {
                esp32StatusText.innerHTML = `<i class="fa-solid fa-circle-check text-success me-1"></i> CAM: Online (${baseUrl})`;
            }

            // Set stream source
            const streamUrl = `${baseUrl}/stream`;
            if (esp32StreamImg) {
                esp32StreamImg.crossOrigin = "anonymous";
                esp32StreamImg.src = streamUrl;
                esp32StreamImg.classList.remove('d-none');
            }
            if (esp32OffOverlay) esp32OffOverlay.classList.add('d-none');
            if (esp32Hud) esp32Hud.classList.remove('d-none');

            if (engineStatus) engineStatus.textContent = 'ESP32-CAM TRỰC TUYẾN';
        } else {
            throw new Error(info.error || 'ESP32 không phản hồi');
        }
    } catch (err) {
        esp32Connected = false;
        if (esp32StatusText) {
            esp32StatusText.innerHTML = `<i class="fa-solid fa-triangle-exclamation text-warning me-1"></i> Stream trực tiếp...`;
        }

        // Try direct stream connection in case server ping timeout
        const streamUrl = `${baseUrl}/stream`;
        if (esp32StreamImg) {
            esp32StreamImg.crossOrigin = "anonymous";
            esp32StreamImg.src = streamUrl;
            esp32StreamImg.classList.remove('d-none');
        }
        if (esp32OffOverlay) esp32OffOverlay.classList.add('d-none');
        if (esp32Hud) esp32Hud.classList.remove('d-none');
    }
}

async function connectActuatorFromMain() {
    const ip = getCleanActuatorUrl();
    const statusText = document.getElementById('actuatorStatusTextMain');
    const input = document.getElementById('actuatorIpInputMain');
    if (input) input.value = ip;

    try {
        localStorage.setItem('trashnet_actuator_ip', ip);
    } catch (e) {}

    if (statusText) statusText.innerHTML = '<span class="spinner-border spinner-border-sm me-1"></span> Đang kết nối...';

    try {
        const res = await fetch('/api/actuator/config', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ ip: ip, enabled: true })
        });
        const data = await res.json();

        // Kiểm tra ping status
        const statusRes = await fetch('/api/actuator/status');
        const statusData = await statusRes.json();

        if (statusData.online) {
            if (statusText) {
                statusText.innerHTML = `<i class="fa-solid fa-circle-check text-success me-1"></i> Thùng: Online (${data.ip})`;
            }
            const badge = document.getElementById('actuatorStatusBadge');
            if (badge) badge.innerHTML = '<span class="status-dot online"></span> TRỰC TUYẾN';
        } else {
            if (statusText) {
                statusText.innerHTML = `<i class="fa-solid fa-triangle-exclamation text-warning me-1"></i> Thùng: Đã lưu (${data.ip})`;
            }
        }
    } catch (err) {
        if (statusText) {
            statusText.innerHTML = `<i class="fa-solid fa-circle-xmark text-danger me-1"></i> Lỗi: ${err.message}`;
        }
    }
}

function disconnectEsp32() {
    if (esp32AutoScanTimer) {
        clearTimeout(esp32AutoScanTimer);
        clearInterval(esp32AutoScanTimer);
        esp32AutoScanTimer = null;
        if (esp32AutoScanCheck) esp32AutoScanCheck.checked = false;
    }
    if (esp32StreamImg) {
        esp32StreamImg.src = '';
        esp32StreamImg.classList.add('d-none');
    }
    if (esp32OffOverlay) esp32OffOverlay.classList.remove('d-none');
    if (esp32Hud) esp32Hud.classList.add('d-none');
    if (esp32StatusText) {
        esp32StatusText.innerHTML = '<i class="fa-solid fa-circle-xmark text-zinc-500 me-1"></i> Đã ngắt kết nối';
    }
    esp32Connected = false;
}

// Biến trạng thái đồng bộ khóa bận giữa Camera và Cơ cấu chấp hành
let isActuatorCurrentlyBusy = false;
let actuatorPollInterval = null;

function toggleEsp32AutoScan(enabled) {
    if (esp32AutoScanTimer) {
        clearTimeout(esp32AutoScanTimer);
        clearInterval(esp32AutoScanTimer);
        esp32AutoScanTimer = null;
    }
    if (enabled) {
        if (!isActuatorCurrentlyBusy) {
            captureEsp32Cam();
        } else {
            if (engineStatus) engineStatus.textContent = 'AUTO-SCAN: ĐỢI THÙNG XỬ LÝ XONG...';
        }
    }
}

// Giám sát khi nào ESP32 Thường hoàn thành xoay và mở/đóng nắp xong
function pollActuatorCompletion() {
    isActuatorCurrentlyBusy = true;
    const btn = document.getElementById('btnSnapEsp32');
    if (btn) {
        btn.disabled = true;
        btn.innerHTML = `<span class="spinner-border spinner-border-sm me-1"></span> Đang xoay mâm & mở nắp...`;
    }

    let pollCount = 0;
    const maxPolls = 30; // 30 * 500ms = 15s max guard

    if (actuatorPollInterval) clearInterval(actuatorPollInterval);

    actuatorPollInterval = setInterval(async () => {
        pollCount++;
        try {
            const res = await fetch('/api/actuator/status');
            const data = await res.json();

            // Nếu mạch đã xử lý xong (is_busy === false sau ít nhất 2 giây bắt đầu)
            if (pollCount >= 4 && (!data.online || !data.is_busy || pollCount >= maxPolls)) {
                clearInterval(actuatorPollInterval);
                actuatorPollInterval = null;
                isActuatorCurrentlyBusy = false;

                if (btn) {
                    btn.disabled = false;
                    btn.innerHTML = `<i class="fa-solid fa-camera-retro me-1"></i> Chụp & Nhận diện`;
                }
                if (engineStatus) engineStatus.textContent = 'HOÀN TẤT - SẴN SÀNG NHẬN RÁC';

                // Trả màn hình ảo LCD về trạng thái sẵn sàng
                const lcd1 = document.getElementById('lcdLine1');
                const lcd2 = document.getElementById('lcdLine2');
                if (lcd1) lcd1.textContent = 'THUNG RAC 4 NGAN';
                if (lcd2) lcd2.textContent = 'SAN SANG NHAN RAC';

                // NẾU ĐANG BẬT TỰ ĐỘNG QUÉT (AUTO-SCAN):
                // Sử dụng thời gian chờ đã cấu hình trong settings
                if (esp32AutoScanCheck && esp32AutoScanCheck.checked) {
                    const delaySec = (typeof currentActuatorSettings !== 'undefined' && currentActuatorSettings.auto_scan_interval_sec) ? currentActuatorSettings.auto_scan_interval_sec : 3;
                    if (engineStatus) engineStatus.textContent = `SẴN SÀNG: ĐẶT RÁC TIẾP THEO (${delaySec}s)...`;
                    esp32AutoScanTimer = setTimeout(() => {
                        if (esp32AutoScanCheck && esp32AutoScanCheck.checked && !isActuatorCurrentlyBusy) {
                            captureEsp32Cam();
                        }
                    }, delaySec * 1000);
                }
            } else if (data.is_busy) {
                if (btn) {
                    btn.innerHTML = `<span class="spinner-border spinner-border-sm me-1"></span> Đang thả rác vào ngăn ${data.current_bin || ''}...`;
                }
            }
        } catch (e) {
            if (pollCount >= 10) {
                clearInterval(actuatorPollInterval);
                actuatorPollInterval = null;
                isActuatorCurrentlyBusy = false;
                if (btn) {
                    btn.disabled = false;
                    btn.innerHTML = `<i class="fa-solid fa-camera-retro me-1"></i> Chụp & Nhận diện`;
                }
            }
        }
    }, 500);
}

async function captureEsp32Cam() {
    if (isActuatorCurrentlyBusy) {
        console.log('Thùng rác đang hoạt động, tạm dừng chụp để tránh xung đột!');
        return;
    }

    const baseUrl = getCleanEsp32Url();
    setLoading(true, 'ĐANG PHÂN TÍCH KHUNG HÌNH ESP32...');

    try {
        let base64Img = null;

        // ƯU TIÊN 1: Chụp tức thì từ khung hình Live Stream đang hiển thị (0ms độ trễ, không xung đột socket ESP32)
        if (esp32StreamImg && !esp32StreamImg.classList.contains('d-none')) {
            try {
                const w = esp32StreamImg.naturalWidth || esp32StreamImg.clientWidth || 640;
                const h = esp32StreamImg.naturalHeight || esp32StreamImg.clientHeight || 480;
                if (w > 20 && h > 20 && esp32Canvas) {
                    esp32Canvas.width = w;
                    esp32Canvas.height = h;
                    const ctx = esp32Canvas.getContext('2d');
                    ctx.drawImage(esp32StreamImg, 0, 0, w, h);
                    base64Img = esp32Canvas.toDataURL('image/jpeg', 0.92);
                }
            } catch (canvasErr) {
                console.warn('Canvas stream capture error (CORS hoặc buffer):', canvasErr);
            }
        }

        let data = null;

        if (base64Img && base64Img.length > 200) {
            // Gửi trực tiếp frame Base64 lên Server Flask /api/predict
            const res = await fetch('/api/predict', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ 
                    image_base64: base64Img,
                    model_type: currentModelType
                })
            });
            data = await res.json();
            if (!res.ok) throw new Error(data.error || 'Lỗi nhận diện AI');
            data.captured_image = base64Img;
        } else {
            // DỰ PHÒNG: Nếu chưa mở stream, gọi server proxy sang /capture của ESP32
            const res = await fetch(`/api/esp32/capture?ip=${encodeURIComponent(baseUrl)}&model_type=${encodeURIComponent(currentModelType)}`);
            data = await res.json();
            if (!res.ok) throw new Error(data.error || 'Không thể lấy ảnh từ ESP32-CAM');
        }

        renderResults(data, data.captured_image || '');
        if (esp32StatusText) {
            esp32StatusText.innerHTML = `<i class="fa-solid fa-circle-check text-success me-1"></i> Đã nhận diện thành công`;
        }
        if (engineStatus) engineStatus.textContent = 'ĐANG ĐIỀU KHIỂN THÙNG RÁC...';

        // Bắt đầu khóa đồng bộ và theo dõi khi nào thùng rác xử lý xong hoàn toàn
        pollActuatorCompletion();
    } catch (err) {
        alert('Lỗi ESP32-CAM: ' + err.message);
        if (engineStatus) engineStatus.textContent = 'LỖI NHẬN DIỆN ESP32';
    } finally {
        setLoading(false);
    }
}

async function toggleEsp32Flash() {
    const baseUrl = getCleanEsp32Url();
    esp32FlashState = !esp32FlashState;
    try {
        await fetch(`${baseUrl}/flash?state=${esp32FlashState ? 1 : 0}`, { mode: 'no-cors' });
        if (btnToggleFlash) {
            btnToggleFlash.classList.toggle('btn-white', esp32FlashState);
            btnToggleFlash.classList.toggle('btn-outline-mono', !esp32FlashState);
        }
    } catch (e) {
        console.warn('Could not toggle flash:', e);
    }
}

// ============================================================
// 6. RENDER RESULTS
// ============================================================
function renderResults(data, imageThumb) {
    if (emptyState) emptyState.classList.add('d-none');
    if (resultState) resultState.classList.remove('d-none');
    if (engineStatus) engineStatus.textContent = 'HOÀN TẤT';

    const activeM = data.active_model || currentModelType;
    updateActiveModelUi(activeM);

    // Main Info
    if (resCategoryTag) resCategoryTag.textContent = (data.category || '').toUpperCase();
    if (resClassTitle) resClassTitle.textContent = data.name_vi;
    if (resClassEn) resClassEn.textContent = (data.class || '').toUpperCase();
    if (resConfidenceNum) resConfidenceNum.textContent = `${data.confidence}%`;
    if (resProgressBar) resProgressBar.style.width = `${data.confidence}%`;

    // Multi-model metrics
    const fusion = data.fusion_info;
    if (fusion) {
        if (resNetPredVal) resNetPredVal.textContent = fusion.resnet.name_vi;
        if (resNetConfVal) resNetConfVal.textContent = `${fusion.resnet.confidence}%`;

        if (yoloPredVal) yoloPredVal.textContent = fusion.yolo.name_vi;
        if (yoloConfVal) yoloConfVal.textContent = `${fusion.yolo.confidence}%`;
    }

    // Bin & Tips
    if (resBinName) resBinName.textContent = data.bin;
    if (resTipsContent) resTipsContent.textContent = data.tips;

    // Cập nhật Cơ cấu chấp hành thực tế (ESP32 Actuator: Màn hình LCD 1602, 4 Ngăn xoay, Servo mở nắp 180°)
    if (data.actuator) {
        updateActuatorHardwareUi(data.actuator, data.name_vi);
    }

    // Table of 8 classes
    if (probabilityTableBody) {
        probabilityTableBody.innerHTML = '';
        data.all_predictions.forEach(item => {
            const isTop = (item.class === data.class);
            const tr = document.createElement('tr');
            if (isTop) tr.className = 'table-active-row';

            tr.innerHTML = `
                <td style="width: 32px;" class="text-center">
                    <i class="${item.icon} text-zinc-400"></i>
                </td>
                <td>
                    <span class="${isTop ? 'fw-bold text-white' : 'text-zinc-300'}">${item.name_vi}</span>
                    <span class="text-zinc-500 small ms-1">(${item.class})</span>
                </td>
                <td class="text-zinc-400">${item.category}</td>
                <td style="width: 80px;" class="text-end font-mono ${isTop ? 'fw-bold text-white' : 'text-zinc-400'}">
                    ${item.confidence}%
                </td>
                <td style="width: 70px;">
                    <div class="table-progress ms-auto">
                        <div class="table-progress-fill" style="width: ${item.confidence}%;"></div>
                    </div>
                </td>
            `;
            probabilityTableBody.appendChild(tr);
        });
    }

    // Cập nhật nhật ký phân loại trực tiếp từ file JSON trên Server
    loadAndRenderHistory();
}

// ============================================================
// 7. PERSISTENT CLASSIFICATION HISTORY (LƯU VÀO FILE JSON TRÊN SERVER)
// ============================================================
let serverClassificationHistory = [];
let currentHistoryFilter = 'all';
let currentHistorySearchQuery = '';

async function loadAndRenderHistory() {
    try {
        const res = await fetch('/api/history');
        const data = await res.json();
        if (data.status === 'ok') {
            serverClassificationHistory = data.records || [];
            
            // Cập nhật các ô thống kê ở đầu trang lịch sử
            const stats = data.stats || {};
            const totalEl = document.getElementById('histStatTotal');
            const burnableEl = document.getElementById('histStatBurnable');
            const glassEl = document.getElementById('histStatGlass');
            const metalEl = document.getElementById('histStatMetal');
            const counterBadge = document.getElementById('historyCounterBadge');
            
            if (totalEl) totalEl.textContent = stats.total || 0;
            if (burnableEl) burnableEl.textContent = stats.burnable || 0;
            if (glassEl) glassEl.textContent = stats.glass || 0;
            if (metalEl) metalEl.textContent = (stats.metal || 0) + ' | ' + (stats.plastic || 0) + ' (PET)';
            if (counterBadge) counterBadge.textContent = `${stats.total || 0} MỤC`;
            if (sidebarHistoryCount) sidebarHistoryCount.textContent = stats.total || 0;
            
            applyHistoryFiltersAndRender();
        }
    } catch (e) {
        console.warn('Lỗi tải lịch sử từ server:', e);
    }
}

function filterHistory(category, btnEl) {
    currentHistoryFilter = category;
    const group = document.getElementById('historyFilterGroup');
    if (group) {
        group.querySelectorAll('button').forEach(b => {
            b.classList.remove('btn-white', 'active');
            b.classList.add('btn-outline-mono');
        });
    }
    if (btnEl) {
        btnEl.classList.remove('btn-outline-mono');
        btnEl.classList.add('btn-white', 'active');
    }
    applyHistoryFiltersAndRender();
}

function handleHistorySearch(keyword) {
    currentHistorySearchQuery = (keyword || '').toLowerCase().trim();
    applyHistoryFiltersAndRender();
}

function applyHistoryFiltersAndRender() {
    let filtered = serverClassificationHistory;
    
    // 1. Lọc theo danh mục
    if (currentHistoryFilter !== 'all') {
        filtered = filtered.filter(item => {
            const cat = (item.category || '').toLowerCase();
            const name = (item.name_vi || '').toLowerCase();
            const cls = (item.class || '').toLowerCase();
            const bin = item.bin_number;
            
            if (currentHistoryFilter === 'burnable') {
                return cat.includes('cháy') || cat.includes('hữu cơ') || bin === 1 || cls === 'burnable' || cls === 'biological' || cls === 'paper' || cls === 'cardboard';
            } else if (currentHistoryFilter === 'glass') {
                return name.includes('thủy tinh') || bin === 2 || cls === 'glass';
            } else if (currentHistoryFilter === 'metal') {
                return name.includes('kim loại') || bin === 3 || cls === 'metal' || cls === 'battery';
            } else if (currentHistoryFilter === 'plastic') {
                return name.includes('nhựa') || bin === 4 || cls === 'plastic';
            }
            return true;
        });
    }
    
    // 2. Lọc theo ô tìm kiếm
    if (currentHistorySearchQuery) {
        filtered = filtered.filter(item => {
            const txt = `${item.name_vi} ${item.class} ${item.category} ${item.bin}`.toLowerCase();
            return txt.includes(currentHistorySearchQuery);
        });
    }
    
    renderFullHistoryGrid(filtered);
}

function renderFullHistoryGrid(items = null) {
    const records = items !== null ? items : serverClassificationHistory;
    if (!fullHistoryEmpty || !fullHistoryGrid) return;

    if (!records || records.length === 0) {
        fullHistoryEmpty.classList.remove('d-none');
        fullHistoryGrid.classList.add('d-none');
        fullHistoryGrid.innerHTML = '';
        return;
    }

    fullHistoryEmpty.classList.add('d-none');
    fullHistoryGrid.classList.remove('d-none');
    fullHistoryGrid.innerHTML = '';

    records.forEach(item => {
        const col = document.createElement('div');
        col.className = 'col-12 col-sm-6 col-md-4 col-xl-3';
        
        const thumbSrc = item.thumb || 'data:image/svg+xml,<svg xmlns="http://www.w3.org/2000/svg" width="60" height="60" viewBox="0 0 24 24" fill="%2327272a"><rect width="24" height="24"/></svg>';
        const agreeIcon = item.models_agree 
            ? '<i class="fa-solid fa-circle-check text-success" title="2 mô hình ResNet-18 & YOLO11n đồng thuận tuyệt đối"></i>' 
            : '<i class="fa-solid fa-code-compare text-warning" title="Phân loại qua Soft Voting Late Fusion"></i>';
            
        const binBadgeColor = item.bin_color || '#22c55e';
        
        col.innerHTML = `
            <div class="p-2 border border-zinc bg-zinc-900 rounded-1 d-flex flex-column justify-content-between h-100 position-relative">
                <div class="d-flex gap-2 align-items-start">
                    <img src="${thumbSrc}" alt="thumb" style="width: 64px; height: 64px; object-fit: cover; border-radius: 4px; border: 1px solid #27272a; flex-shrink: 0;">
                    <div class="overflow-hidden flex-grow-1">
                        <div class="d-flex align-items-center justify-content-between">
                            <span class="badge border border-zinc-700 font-mono text-zinc-400" style="font-size: 0.65rem;">${item.time_display || item.date_display || ''}</span>
                            <button type="button" class="btn btn-link text-zinc-500 p-0 text-decoration-none" title="Xóa bản ghi này" onclick="deleteHistoryItem('${item.id}')" style="font-size: 0.8rem;">
                                <i class="fa-solid fa-xmark"></i>
                            </button>
                        </div>
                        <div class="fw-semibold text-white text-truncate small mt-1" title="${item.name_vi}">${item.name_vi}</div>
                        <div class="text-zinc-500 font-mono text-truncate" style="font-size: 0.72rem;">${item.category} (${item.class})</div>
                    </div>
                </div>
                
                <div class="mt-2 pt-2 border-top border-zinc d-flex align-items-center justify-content-between" style="font-size: 0.72rem;">
                    <div class="d-flex align-items-center gap-1 font-mono">
                        <span style="display:inline-block; width:8px; height:8px; border-radius:50%; background-color:${binBadgeColor};"></span>
                        <span class="text-zinc-300">Ngăn ${item.bin_number}</span>
                        ${agreeIcon}
                        <span class="badge border border-zinc-700 font-mono text-zinc-400 ms-1" style="font-size: 0.58rem;">${(item.active_model || 'fusion').toUpperCase()}</span>
                    </div>
                    <span class="font-mono text-white fw-bold">${item.confidence}%</span>
                </div>
            </div>
        `;
        fullHistoryGrid.appendChild(col);
    });
}

async function deleteHistoryItem(id) {
    try {
        await fetch(`/api/history?id=${encodeURIComponent(id)}`, { method: 'DELETE' });
        loadAndRenderHistory();
    } catch (e) {
        console.warn('Lỗi xóa mục lịch sử:', e);
    }
}

async function clearHistory() {
    if (!confirm('Bạn có chắc chắn muốn xóa TOÀN BỘ lịch sử phân loại đã lưu trong file classification_history.json?')) return;
    try {
        await fetch('/api/history', { method: 'DELETE' });
        loadAndRenderHistory();
    } catch (e) {
        console.warn('Lỗi xóa tất cả lịch sử:', e);
    }
}

// ============================================================
// 8. IOT & ESP32 ACTUATOR (SERVO 180° + STEPPER 4 HƯỚNG + LCD 1602A)
// ============================================================
let currentCarouselAngle = 0;
let isTrapdoorOpen = false;
let actuatorTimer = null;

function updateActuatorHardwareUi(actuator, viName = '') {
    const lcdLine1 = document.getElementById('lcdLine1');
    const lcdLine2 = document.getElementById('lcdLine2');
    const stepperAngleDisplay = document.getElementById('stepperAngleDisplay');
    const servoAngleDisplay = document.getElementById('servoAngleDisplay');
    const servoLidCard = document.getElementById('servoLidCard');
    const servoLidText = document.getElementById('servoLidText');
    const servoSubText = document.getElementById('servoSubText');
    const servoLidIcon = document.getElementById('servoLidIcon');
    const actuatorStatusBadge = document.getElementById('actuatorStatusBadge');

    if (actuatorStatusBadge) {
        actuatorStatusBadge.innerHTML = '<span class="status-dot online"></span> ĐANG ĐIỀU KHIỂN';
    }

    // 1. Màn hình ảo LCD 1602A
    const catMap = { 1: 'CHAY DUOC', 2: 'THUY TINH', 3: 'KIM LOAI', 4: 'NHUA PET' };
    const cat = catMap[actuator.bin] || 'CHAY DUOC';
    if (lcdLine1) lcdLine1.textContent = `NGAN ${actuator.bin}: ${cat}`;
    if (lcdLine2) lcdLine2.textContent = `${actuator.lcd_title || ''}`;

    // 2. Động cơ bước 4 ngăn (28BYJ-48)
    if (stepperAngleDisplay) stepperAngleDisplay.textContent = `${actuator.angle}°`;
    [1, 2, 3, 4].forEach(b => {
        const card = document.getElementById(`binCard${b}`);
        if (card) {
            if (b === actuator.bin) {
                card.classList.add('active');
            } else {
                card.classList.remove('active');
            }
        }
    });

    // 3. Servo MG995 mở nắp 180°
    if (servoAngleDisplay) servoAngleDisplay.textContent = '180°';
    if (servoLidCard) servoLidCard.classList.add('lid-open');
    if (servoLidText) servoLidText.textContent = 'MỞ NẮP 180°';
    if (servoSubText) servoSubText.textContent = 'Đang thả rác (2.5s)...';
    if (servoLidIcon) servoLidIcon.className = 'fa-solid fa-door-open fs-4 text-warning mb-1';

    // Sau 3.5 giây tự động đóng nắp về 0° và trả màn hình LCD về trạng thái chờ
    if (actuatorTimer) clearTimeout(actuatorTimer);
    actuatorTimer = setTimeout(() => {
        if (servoAngleDisplay) servoAngleDisplay.textContent = '0°';
        if (servoLidCard) servoLidCard.classList.remove('lid-open');
        if (servoLidText) servoLidText.textContent = 'ĐÓNG NẮP 0°';
        if (servoSubText) servoSubText.textContent = 'Sẵn sàng nhận rác';
        if (servoLidIcon) servoLidIcon.className = 'fa-solid fa-lock fs-4 text-zinc-400 mb-1';

        if (lcdLine1) lcdLine1.textContent = 'THUNG RAC 4 NGAN';
        if (lcdLine2) lcdLine2.textContent = 'SAN SANG CHO RAC';
        if (actuatorStatusBadge) {
            actuatorStatusBadge.innerHTML = '<span class="status-dot online"></span> TRỰC TUYẾN';
        }
    }, 3500);
}

async function triggerManualBin(binNum, binTitle) {
    if (isActuatorCurrentlyBusy) {
        console.warn('ESP32 Actuator đang bận chu trình trước, vui lòng đợi xong');
        return;
    }
    isActuatorCurrentlyBusy = true;
    updateActuatorHardwareUi({
        bin: binNum,
        lcd_title: binTitle || `Rac NGAN ${binNum}`,
        angle: (binNum - 1) * 90
    });

    try {
        await fetch('/api/actuator/control', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({
                action: 'bin',
                bin: binNum,
                name: binTitle
            })
        });
        pollActuatorCompletion();
    } catch (e) {
        console.warn('Lỗi gửi lệnh Actuator:', e);
        isActuatorCurrentlyBusy = false;
    }
}

async function connectActuator() {
    const ipInput = document.getElementById('actuatorIpInput');
    const statusText = document.getElementById('actuatorConnectionStatus');
    let ip = (ipInput ? ipInput.value : '').trim();
    if (!ip) ip = 'http://192.168.1.10';

    if (statusText) statusText.innerHTML = '<span class="spinner-border spinner-border-sm me-1"></span> Đang kết nối ESP32 Actuator...';

    try {
        const res = await fetch('/api/actuator/config', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ ip: ip, enabled: true })
        });
        const data = await res.json();

        // Kiểm tra ping status
        const statusRes = await fetch('/api/actuator/status');
        const statusData = await statusRes.json();

        if (statusData.online) {
            if (statusText) {
                statusText.innerHTML = `<i class="fa-solid fa-circle-check text-success me-1"></i> Trực tuyến (${data.ip}) - Đã sẵn sàng điều khiển!`;
            }
        } else {
            if (statusText) {
                statusText.innerHTML = `<i class="fa-solid fa-triangle-exclamation text-warning me-1"></i> Đã lưu IP (${data.ip}) - Đang đợi mạch khởi động`;
            }
        }
    } catch (err) {
        if (statusText) {
            statusText.innerHTML = `<i class="fa-solid fa-circle-xmark text-danger me-1"></i> Lỗi: ${err.message}`;
        }
    }
}

function rotateToAngle(angle, compartmentName) {
    currentCarouselAngle = angle;
    const badge = document.getElementById('carouselAngleBadge');
    const systemStatus = document.getElementById('iotSystemStatus');

    if (badge) {
        badge.textContent = `ĐANG XOAY ${angle}° (${compartmentName})...`;
        badge.className = 'badge bg-white text-black font-mono small';
    }
    if (systemStatus) {
        systemStatus.textContent = `ĐỘNG CƠ BƯỚC: QUAY ${angle}°`;
        systemStatus.className = 'badge bg-white text-black font-mono';
    }

    const binNum = (angle === 0) ? 1 : (angle === 90) ? 2 : (angle === 180) ? 3 : 4;
    fetch('/api/actuator/control', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ action: 'rotate', bin: binNum })
    }).catch(e => console.warn(e));

    setTimeout(() => {
        if (badge) {
            badge.textContent = `GÓC HIỆN TẠI: ${angle}° (${compartmentName})`;
            badge.className = 'badge border border-zinc text-zinc-300 font-mono small';
        }
        if (systemStatus) {
            systemStatus.textContent = 'MÂM XOAY ĐÃ ĐẾN VỊ TRÍ';
            systemStatus.className = 'badge border border-zinc-700 text-zinc-400 font-mono';
        }
    }, 800);
}

function toggleTrapdoor(isOpen) {
    isTrapdoorOpen = isOpen;
    const badge = document.getElementById('trapdoorStatusBadge');
    const systemStatus = document.getElementById('iotSystemStatus');

    if (badge) {
        if (isOpen) {
            badge.textContent = 'TRẠNG THÁI: MỞ NẮP (180°) - ĐANG XẢ RÁC';
            badge.className = 'badge bg-white text-black font-mono small';
        } else {
            badge.textContent = 'TRẠNG THÁI: ĐÓNG NẮP (0°) - AN TOÀN';
            badge.className = 'badge border border-zinc text-zinc-400 font-mono small';
        }
    }
    if (systemStatus) {
        systemStatus.textContent = isOpen ? 'SERVO: MỞ NẮP 180° THẢ RÁC' : 'HỆ THỐNG SẴN SÀNG';
    }

    // Gửi lệnh điều khiển thực tế sang ESP32
    fetch('/api/actuator/control', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ action: 'servo', angle: isOpen ? 180 : 0 })
    }).catch(e => console.warn(e));
}

async function simulateRotaryCycle(trashType = 'plastic') {
    const s1 = document.getElementById('stepIndicator1');
    const s2 = document.getElementById('stepIndicator2');
    const s3 = document.getElementById('stepIndicator3');
    const s4 = document.getElementById('stepIndicator4');
    const systemStatus = document.getElementById('iotSystemStatus');

    const resetSteps = () => {
        [s1, s2, s3, s4].forEach(el => {
            if (el) {
                el.classList.remove('border-white', 'bg-zinc-800');
                el.classList.add('border-zinc', 'bg-black');
            }
        });
    };

    const activateStep = (el) => {
        resetSteps();
        if (el) {
            el.classList.remove('border-zinc', 'bg-black');
            el.classList.add('border-white', 'bg-zinc-800');
        }
    };

    // Step 1: AI Inspection
    activateStep(s1);
    if (systemStatus) systemStatus.textContent = 'BƯỚC 1: CAMERA ĐỈNH NHẬN DIỆN TRÊN NỀN ĐƠN SẮC...';
    await new Promise(r => setTimeout(r, 1200));

    // Step 2: Rotate Compartment
    activateStep(s2);
    rotateToAngle(0, 'Rác tái chế');
    await new Promise(r => setTimeout(r, 1500));

    // Step 3: Open Trapdoor to drop trash
    activateStep(s3);
    toggleTrapdoor(true);
    await new Promise(r => setTimeout(r, 1400));
    toggleTrapdoor(false);
    await new Promise(r => setTimeout(r, 800));

    // Step 4: Top Sensor Level Check
    activateStep(s4);
    if (systemStatus) systemStatus.textContent = 'BƯỚC 4: CẢM BIẾN ĐỈNH QUÉT MỨC ĐẦY NGĂN TÁI CHẾ (25% - AN TOÀN)';
    await new Promise(r => setTimeout(r, 2000));

    resetSteps();
    if (systemStatus) {
        systemStatus.textContent = 'CHU TRÌNH HOÀN TẤT - SẴN SÀNG ĐỢI RÁC TIẾP THEO';
        systemStatus.className = 'badge border border-zinc-700 text-zinc-400 font-mono';
    }
}

// ============================================================
// 9. ANALYTICS & STATS CALCULATIONS
// ============================================================
function renderAnalyticsStats() {
    const total = scanHistory.length;
    const statTotalScans = document.getElementById('statTotalScans');
    const statRecyclable = document.getElementById('statRecyclable');
    const statHazardous = document.getElementById('statHazardous');
    const statOrganic = document.getElementById('statOrganic');

    if (statTotalScans) statTotalScans.textContent = total;

    if (total === 0) {
        if (statRecyclable) statRecyclable.textContent = '0%';
        if (statHazardous) statHazardous.textContent = '0';
        if (statOrganic) statOrganic.textContent = '0';
        return;
    }

    const burnable = scanHistory.filter(i => (i.category || '').toLowerCase().includes('cháy') || (i.category || '').toLowerCase().includes('hữu cơ')).length;
    const metalGlass = scanHistory.filter(i => (i.category || '').toLowerCase().includes('kim loại') || (i.category || '').toLowerCase().includes('thủy tinh')).length;
    const plastic = scanHistory.filter(i => (i.category || '').toLowerCase().includes('nhựa')).length;

    if (statRecyclable) statRecyclable.textContent = metalGlass;
    if (statHazardous) statHazardous.textContent = plastic;
    if (statOrganic) statOrganic.textContent = burnable;
}

// ============================================================
// 10. UTILITIES & RESET
// ============================================================
function clearAllData() {
    if (emptyState) emptyState.classList.remove('d-none');
    if (resultState) resultState.classList.add('d-none');
    if (engineStatus) engineStatus.textContent = 'SẴN SÀNG';
}

function setLoading(isLoading, text = 'ĐANG XỬ LÝ...') {
    if (isLoading) {
        if (engineStatus) engineStatus.textContent = text;
        if (btnSnap) {
            btnSnap.disabled = true;
            btnSnap.innerHTML = `<span class="spinner-border spinner-border-sm me-1"></span> Đang nhận diện...`;
        }
        if (btnSnapEsp32) {
            btnSnapEsp32.disabled = true;
            btnSnapEsp32.innerHTML = `<span class="spinner-border spinner-border-sm me-1"></span> Đang chụp ESP32...`;
        }
    } else {
        if (btnSnap) {
            btnSnap.disabled = false;
            btnSnap.innerHTML = `<i class="fa-solid fa-camera-retro me-1"></i> Chụp & Nhận diện`;
        }
        if (btnSnapEsp32) {
            btnSnapEsp32.disabled = false;
            btnSnapEsp32.innerHTML = `<i class="fa-solid fa-camera-retro me-1"></i> Chụp & Nhận diện`;
        }
    }
}

// ============================================================
// 11. HARDWARE TIMING CALIBRATION (BƯỚC ±5ms) & ESP32-CAM SYNC
// ============================================================
let currentActuatorSettings = {
    enabled: true,
    actuator_ip: "http://172.16.0.116",
    esp32_cam_ip: "http://172.16.0.100",
    auto_scan_interval_sec: 3,
    rotate_time_90_ms: 950,
    rotate_time_180_fwd_ms: 1850,
    rotate_time_180_rev_ms: 1955,
    dump_fwd_time_ms: 500,
    dump_rev_time_ms: 500,
    dump_wait_ms: 1500,
    dump_inverted: false
};

async function loadActuatorSettings() {
    try {
        const res = await fetch('/api/actuator/settings');
        const data = await res.json();
        if (data.status === 'ok' && data.settings) {
            currentActuatorSettings = data.settings;
            populateSettingsUi(data.settings);
            
            // Cập nhật IP sang các input ở tab khác
            const actMain = document.getElementById('actuatorIpInputMain');
            const actTab = document.getElementById('actuatorIpInput');
            if (actMain) actMain.value = data.settings.actuator_ip || 'http://172.16.0.116';
            if (actTab) actTab.value = data.settings.actuator_ip || 'http://172.16.0.116';
            if (esp32IpInput) esp32IpInput.value = data.settings.esp32_cam_ip || 'http://172.16.0.100';
        }
    } catch (e) {
        console.warn('Lỗi tải cài đặt:', e);
    }
}

function populateSettingsUi(s) {
    const setVal = (id, val) => {
        const el = document.getElementById(id);
        if (el && val !== undefined) el.value = val;
    };

    setVal('cfg_rotate_time_90_ms', s.rotate_time_90_ms);
    setVal('cfg_rotate_time_90_rev_ms', s.rotate_time_90_rev_ms !== undefined ? s.rotate_time_90_rev_ms : 1010);
    setVal('cfg_rotate_time_180_fwd_ms', s.rotate_time_180_fwd_ms);
    setVal('cfg_rotate_time_180_rev_ms', s.rotate_time_180_rev_ms);
    setVal('cfg_dump_fwd_time_ms', s.dump_fwd_time_ms);
    setVal('cfg_dump_rev_time_ms', s.dump_rev_time_ms);
    setVal('cfg_dump_rest_angle', s.dump_rest_angle !== undefined ? s.dump_rest_angle : 0);
    setVal('cfg_dump_wait_ms', s.dump_wait_ms);
    setVal('cfg_esp32_cam_ip', s.esp32_cam_ip);
    setVal('cfg_auto_scan_interval_sec', s.auto_scan_interval_sec);

    const invLabel = document.getElementById('cfg_dump_inverted_label');
    if (invLabel) {
        invLabel.textContent = s.dump_inverted 
            ? 'Chiều Nghịch (180° -> 90° -> 180°)' 
            : 'Chiều Thuận (0° -> 90° -> 0°)';
    }
}

function stepTiming(key, delta) {
    const input = document.getElementById(`cfg_${key}`);
    if (!input) return;
    let val = parseInt(input.value) || 0;
    val += delta;
    if (val < parseInt(input.min || 50)) val = parseInt(input.min || 50);
    if (val > parseInt(input.max || 10000)) val = parseInt(input.max || 10000);
    input.value = val;
    currentActuatorSettings[key] = val;
}

function stepCamInterval(delta) {
    const input = document.getElementById('cfg_auto_scan_interval_sec');
    if (!input) return;
    let val = parseInt(input.value) || 3;
    val += delta;
    if (val < 1) val = 1;
    if (val > 30) val = 30;
    input.value = val;
    currentActuatorSettings['auto_scan_interval_sec'] = val;
}

async function toggleServoDirectionFromUi() {
    currentActuatorSettings.dump_inverted = !currentActuatorSettings.dump_inverted;
    const invLabel = document.getElementById('cfg_dump_inverted_label');
    if (invLabel) {
        invLabel.textContent = currentActuatorSettings.dump_inverted 
            ? 'Chiều Nghịch (180° -> 90° -> 180°)' 
            : 'Chiều Thuận (0° -> 90° -> 0°)';
    }
    try {
        await fetch('/api/actuator/control', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ action: 'invert' })
        });
    } catch (e) {}
}

async function saveActuatorSettingsFromUi() {
    const badge = document.getElementById('settingsSaveStatus');
    if (badge) {
        badge.innerHTML = '<span class="spinner-border spinner-border-sm me-1"></span> ĐANG LƯU VÀO FILE & MẠCH...';
        badge.className = 'badge bg-white text-black font-mono';
    }

    const getVal = (id, fallback) => {
        const el = document.getElementById(id);
        return el ? (parseInt(el.value) || fallback) : fallback;
    };

    currentActuatorSettings.rotate_time_90_ms = getVal('cfg_rotate_time_90_ms', 950);
    currentActuatorSettings.rotate_time_90_rev_ms = getVal('cfg_rotate_time_90_rev_ms', 1010);
    currentActuatorSettings.rotate_time_180_fwd_ms = getVal('cfg_rotate_time_180_fwd_ms', 1850);
    currentActuatorSettings.rotate_time_180_rev_ms = getVal('cfg_rotate_time_180_rev_ms', 1955);
    currentActuatorSettings.dump_fwd_time_ms = getVal('cfg_dump_fwd_time_ms', 500);
    currentActuatorSettings.dump_rev_time_ms = getVal('cfg_dump_rev_time_ms', 500);
    currentActuatorSettings.dump_rest_angle = getVal('cfg_dump_rest_angle', 0);
    currentActuatorSettings.dump_wait_ms = getVal('cfg_dump_wait_ms', 1500);

    try {
        const res = await fetch('/api/actuator/settings', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(currentActuatorSettings)
        });
        const data = await res.json();

        if (badge) {
            if (data.esp_sync && data.esp_sync.synced) {
                badge.innerHTML = '<i class="fa-solid fa-check me-1"></i> ĐÃ LƯU VÀO FILE & FLASH NVS ESP32';
                badge.className = 'badge bg-success text-white font-mono';
            } else {
                badge.innerHTML = '<i class="fa-solid fa-check me-1"></i> ĐÃ LƯU VÀO FILE JSON';
                badge.className = 'badge border border-success text-success font-mono';
            }
            setTimeout(() => {
                badge.innerHTML = 'LƯU TRỰC TIẾP VÀO FILE';
                badge.className = 'badge border border-zinc-700 text-zinc-400 font-mono';
            }, 3000);
        }
    } catch (err) {
        if (badge) {
            badge.innerHTML = '<i class="fa-solid fa-triangle-exclamation me-1"></i> LỖI LƯU CÀI ĐẶT';
            badge.className = 'badge bg-danger text-white font-mono';
        }
    }
}

async function saveCamSettingsFromUi() {
    const camIpInput = document.getElementById('cfg_esp32_cam_ip');
    const intervalInput = document.getElementById('cfg_auto_scan_interval_sec');
    
    let camIp = (camIpInput ? camIpInput.value : '').trim();
    if (!camIp.startsWith('http://') && !camIp.startsWith('https://')) {
        camIp = 'http://' + camIp;
    }
    
    currentActuatorSettings.esp32_cam_ip = camIp;
    currentActuatorSettings.auto_scan_interval_sec = parseInt(intervalInput ? intervalInput.value : 3) || 3;
    
    // Đồng bộ sang localStorage và input camera
    try {
        localStorage.setItem('trashnet_esp32_ip', camIp);
    } catch (e) {}
    if (esp32IpInput) esp32IpInput.value = camIp;

    await saveActuatorSettingsFromUi();
    connectEsp32Cam();
}

// ============================================================
// 12. MANUAL ACTUATOR CONTROL (Phím bấm & Phím tắt 1, 2, 3, 4, D)
// ============================================================
async function connectActuatorFromMain() {
    const input = document.getElementById('actuatorIpInputMain');
    const statusText = document.getElementById('actuatorStatusTextMain');
    const quickBadge = document.getElementById('quickActuatorStatusBadge');
    if (!input) return;

    let ip = input.value.trim();
    if (!ip) ip = 'http://172.16.0.116';
    if (!ip.startsWith('http://') && !ip.startsWith('https://')) {
        ip = 'http://' + ip;
    }
    input.value = ip;

    if (statusText) statusText.innerHTML = '<i class="fa-solid fa-spinner fa-spin me-1"></i> Đang nối thùng...';

    try {
        await fetch('/api/actuator/config', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ ip: ip, enabled: true })
        });

        const sRes = await fetch('/api/actuator/status');
        const sData = await sRes.json();
        const displayIp = ip.replace(/^https?:\/\//, '');

        if (sData.online) {
            if (statusText) statusText.innerHTML = `<i class="fa-solid fa-circle-check text-success me-1"></i> Thùng: ${displayIp} (Online)`;
            if (quickBadge) {
                quickBadge.textContent = 'ONLINE';
                quickBadge.className = 'badge bg-success font-mono';
            }
        } else {
            if (statusText) statusText.innerHTML = `<i class="fa-solid fa-circle-info text-zinc-400 me-1"></i> Thùng: ${displayIp}`;
            if (quickBadge) {
                quickBadge.textContent = 'SẴN SÀNG';
                quickBadge.className = 'badge border border-zinc-700 text-zinc-400 font-mono';
            }
        }
    } catch (e) {
        const displayIp = ip.replace(/^https?:\/\//, '');
        if (statusText) statusText.innerHTML = `<i class="fa-solid fa-triangle-exclamation text-warning me-1"></i> Nối thùng: ${displayIp}`;
    }
}

async function triggerManualBin(binNum) {
    if (isActuatorCurrentlyBusy) {
        const badge = document.getElementById('quickActuatorStatusBadge');
        if (badge) {
            const oldText = badge.textContent;
            badge.textContent = 'ĐANG BẬN...';
            badge.className = 'badge bg-warning text-dark font-mono';
            setTimeout(() => {
                badge.textContent = oldText;
                badge.className = 'badge border border-zinc-700 text-zinc-400 font-mono';
            }, 800);
        }
        return;
    }

    const btn = document.getElementById(`btnQuickBin${binNum}`);
    const badge = document.getElementById('quickActuatorStatusBadge');
    const modeRotateOnly = document.getElementById('modeRotateOnly');
    const isRotateOnly = modeRotateOnly && modeRotateOnly.checked;

    const action = isRotateOnly ? 'rotate' : 'bin';
    const actionLabel = isRotateOnly ? `QUAY NGĂN ${binNum}...` : `CHU TRÌNH NGĂN ${binNum}...`;

    isActuatorCurrentlyBusy = true;
    if (btn) btn.classList.add('border-primary', 'bg-zinc-800');
    if (badge) {
        badge.textContent = actionLabel;
        badge.className = 'badge bg-warning text-dark font-mono';
    }

    try {
        const res = await fetch('/api/actuator/control', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({
                action: action,
                bin: binNum,
                name: `Ngăn ${binNum}`
            })
        });
        const data = await res.json();
        if (data.error) {
            if (badge) {
                badge.textContent = `LỖI: ${data.error}`;
                badge.className = 'badge bg-danger text-white font-mono';
            }
        } else {
            if (badge) {
                badge.textContent = isRotateOnly ? `ĐÃ ĐẾN NGĂN ${binNum}` : `XONG NGĂN ${binNum}`;
                badge.className = 'badge bg-success text-white font-mono';
            }
        }
    } catch (e) {
        if (badge) {
            badge.textContent = 'LỖI KẾT NỐI';
            badge.className = 'badge bg-danger text-white font-mono';
        }
    } finally {
        setTimeout(() => {
            isActuatorCurrentlyBusy = false;
            if (btn) btn.classList.remove('border-primary', 'bg-zinc-800');
            if (badge) {
                badge.textContent = 'SẴN SÀNG';
                badge.className = 'badge border border-zinc-700 text-zinc-400 font-mono';
            }
        }, 1800);
    }
}

async function triggerManualDump() {
    if (isActuatorCurrentlyBusy) {
        return;
    }
    const btn = document.getElementById('btnQuickDump');
    const badge = document.getElementById('quickActuatorStatusBadge');

    isActuatorCurrentlyBusy = true;
    if (btn) btn.classList.add('btn-warning', 'text-dark');
    if (badge) {
        badge.textContent = 'ĐANG HẤT RÁC...';
        badge.className = 'badge bg-warning text-dark font-mono';
    }

    try {
        const res = await fetch('/api/actuator/control', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ action: 'dump' })
        });
        const data = await res.json();
        if (data.error) {
            if (badge) {
                badge.textContent = `LỖI: ${data.error}`;
                badge.className = 'badge bg-danger text-white font-mono';
            }
        } else {
            if (badge) {
                badge.textContent = 'HẤT RÁC XONG';
                badge.className = 'badge bg-success text-white font-mono';
            }
        }
    } catch (e) {
        if (badge) {
            badge.textContent = 'LỖI KẾT NỐI';
            badge.className = 'badge bg-danger text-white font-mono';
        }
    } finally {
        setTimeout(() => {
            isActuatorCurrentlyBusy = false;
            if (btn) btn.classList.remove('btn-warning', 'text-dark');
            if (badge) {
                badge.textContent = 'SẴN SÀNG';
                badge.className = 'badge border border-zinc-700 text-zinc-400 font-mono';
            }
        }, 1500);
    }
}

async function testActuatorCmd(cmd) {
    if (cmd === 'dump') {
        triggerManualDump();
    } else if (cmd.startsWith('bin')) {
        const bin = parseInt(cmd.replace('bin', '')) || 1;
        triggerManualBin(bin);
    } else if (cmd === 'rotate_3_fwd') {
        fetch('/api/actuator/control', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ action: 'rotate_test', step: '3_fwd' })
        }).catch(e => console.warn(e));
    } else if (cmd === 'rotate_3_rev') {
        fetch('/api/actuator/control', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ action: 'rotate_test', step: '3_rev' })
        }).catch(e => console.warn(e));
    } else if (cmd === 'rotate_2_fwd') {
        fetch('/api/actuator/control', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ action: 'rotate_test', step: '2_fwd' })
        }).catch(e => console.warn(e));
    } else if (cmd === 'rotate_2_rev') {
        fetch('/api/actuator/control', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ action: 'rotate_test', step: '2_rev' })
        }).catch(e => console.warn(e));
    } else if (cmd === 'rotate_4_fwd') {
        fetch('/api/actuator/control', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ action: 'rotate_test', step: '4_fwd' })
        }).catch(e => console.warn(e));
    } else if (cmd === 'rotate_4_rev') {
        fetch('/api/actuator/control', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ action: 'rotate_test', step: '4_rev' })
        }).catch(e => console.warn(e));
    }
}

// Bắt phím tắt 1, 2, 3, 4 hoặc D trên bàn phím (khi không gõ ô input)
document.addEventListener('keydown', (e) => {
    const tag = document.activeElement ? document.activeElement.tagName.toLowerCase() : '';
    if (tag === 'input' || tag === 'textarea' || tag === 'select') {
        return;
    }

    if (e.key === '1') {
        e.preventDefault();
        triggerManualBin(1);
    } else if (e.key === '2') {
        e.preventDefault();
        triggerManualBin(2);
    } else if (e.key === '3') {
        e.preventDefault();
        triggerManualBin(3);
    } else if (e.key === '4') {
        e.preventDefault();
        triggerManualBin(4);
    } else if (e.key === 'd' || e.key === 'D') {
        e.preventDefault();
        triggerManualDump();
    }
});

// ============================================================
// 13. DATASET & AUTOMATED TRAINING HUB
// ============================================================
let selectedDatasetFiles = [];
let trainStatusPollInterval = null;
let shadcnClassChart = null;
let shadcnModelChart = null;

async function loadDatasetAndTrainStats() {
    try {
        const res = await fetch('/api/dataset/stats');
        const data = await res.json();
        if (data.status === 'ok') {
            const bms = data.benchmarks || {};
            const bmFusion = document.getElementById('bmFusionAcc');
            const bmResNet = document.getElementById('bmResNetAcc');
            const bmYolo = document.getElementById('bmYoloAcc');
            const footerAcc = document.getElementById('footerAccNum');

            if (bmFusion) bmFusion.textContent = bms.fusion || '99.37%';
            if (bmResNet) bmResNet.textContent = bms.resnet || '99.27%';
            if (bmYolo) bmYolo.textContent = bms.yolo || '96.01%';
            if (footerAcc) footerAcc.textContent = bms.fusion || '99.37%';

            const totalEl = document.getElementById('totalDatasetSamples');
            if (totalEl) totalEl.textContent = `Tổng cộng: ${data.total_dataset.toLocaleString()} ảnh (${data.total_train} train / ${data.total_val} val)`;

            const c = data.classes || {};
            const burnableEl = document.getElementById('dsBurnableCount');
            const glassEl = document.getElementById('dsGlassCount');
            const metalEl = document.getElementById('dsMetalCount');
            const plasticEl = document.getElementById('dsPlasticCount');

            if (burnableEl && c.burnable) burnableEl.textContent = `${c.burnable.total} ảnh`;
            if (glassEl && c.glass) glassEl.textContent = `${c.glass.total} ảnh`;
            if (metalEl && c.metal) metalEl.textContent = `${c.metal.total} ảnh`;
            if (plasticEl && c.plastic) plasticEl.textContent = `${c.plastic.total} ảnh`;

            // Chi tiết số lượng ảnh trên tập kiểm nghiệm Validation (2,318 ảnh)
            const det = data.benchmark_details || {
                val_total: 2318,
                fusion_correct: 2303,
                fusion_wrong: 15,
                resnet_correct: 2301,
                resnet_wrong: 17,
                yolo_correct: 2226,
                yolo_wrong: 92,
                by_class: {
                    burnable: { name: 'Rác cháy được', total: 594, correct: 591, wrong: 3, acc: '99.49%' },
                    glass: { name: 'Chai lọ thủy tinh', total: 600, correct: 597, wrong: 3, acc: '99.50%' },
                    metal: { name: 'Kim loại & vỏ lon', total: 567, correct: 564, wrong: 3, acc: '99.47%' },
                    plastic: { name: 'Nhựa (PET)', total: 557, correct: 551, wrong: 6, acc: '98.92%' }
                }
            };

            const bmValSummaryText = document.getElementById('bmValSummaryText');
            const bmFusionCount = document.getElementById('bmFusionCount');
            const bmResNetCount = document.getElementById('bmResNetCount');
            const bmYoloCount = document.getElementById('bmYoloCount');
            const bmErrorRate = document.getElementById('bmErrorRate');
            const bmErrorCount = document.getElementById('bmErrorCount');

            if (bmValSummaryText) {
                bmValSummaryText.textContent = `Tập kiểm nghiệm: ${det.val_total.toLocaleString()} ảnh (${det.fusion_correct.toLocaleString()} đúng / ${det.fusion_wrong} sai)`;
            }
            if (bmFusionCount) {
                bmFusionCount.textContent = `${det.fusion_correct.toLocaleString()} / ${det.val_total.toLocaleString()} ảnh đúng`;
            }
            if (bmResNetCount) {
                bmResNetCount.textContent = `${det.resnet_correct.toLocaleString()} / ${det.val_total.toLocaleString()} ảnh đúng`;
            }
            if (bmYoloCount) {
                bmYoloCount.textContent = `${det.yolo_correct.toLocaleString()} / ${det.val_total.toLocaleString()} ảnh đúng`;
            }
            if (bmErrorRate) {
                const fusionPct = parseFloat(bms.fusion) || 99.37;
                const errPct = (100 - fusionPct).toFixed(2);
                bmErrorRate.textContent = `${errPct}%`;
            }
            if (bmErrorCount) {
                bmErrorCount.textContent = `Chỉ ${det.fusion_wrong} ảnh sai / ${det.val_total.toLocaleString()} ảnh`;
            }

            // Render bảng thống kê chi tiết từng ngăn rác
            renderDetailedClassTable(det);

            // Render 2 biểu đồ phong cách Shadcn/UI (Chart.js)
            renderShadcnCharts(det);
        }
    } catch (e) {
        console.warn('Lỗi tải thống kê dataset:', e);
    }
}

function renderDetailedClassTable(det) {
    const tbody = document.getElementById('detailedClassTableBody');
    if (!tbody || !det.by_class) return;

    const classMeta = {
        burnable: { icon: 'fa-fire-flame-curved', bin: 'Ngăn 1' },
        glass: { icon: 'fa-wine-bottle', bin: 'Ngăn 2' },
        metal: { icon: 'fa-can-food', bin: 'Ngăn 3' },
        plastic: { icon: 'fa-bottle-water', bin: 'Ngăn 4' }
    };

    tbody.innerHTML = Object.keys(det.by_class).map(key => {
        const item = det.by_class[key];
        const meta = classMeta[key] || { icon: 'fa-recycle', bin: 'Ngăn ?' };
        const accVal = parseFloat(item.acc) || 0;
        return `
            <tr>
                <td>
                    <div class="d-flex align-items-center gap-2">
                        <i class="fa-solid ${meta.icon} text-secondary"></i>
                        <div>
                            <div class="fw-semibold text-white">${meta.bin}: ${item.name}</div>
                            <div class="font-mono text-muted" style="font-size: 0.7rem;">${key.toUpperCase()}</div>
                        </div>
                    </div>
                </td>
                <td class="text-center font-mono text-white fw-semibold">${item.total.toLocaleString()} ảnh</td>
                <td class="text-center font-mono"><span class="badge bg-white text-black font-mono px-2 py-1">${item.correct.toLocaleString()}</span></td>
                <td class="text-center font-mono"><span class="badge border border-subtle text-muted font-mono px-2 py-1">${item.wrong}</span></td>
                <td class="text-end font-mono text-white fw-bold fs-6">${item.acc}</td>
                <td>
                    <div class="progress" style="height: 6px; background: #27272a; border-radius: 999px;">
                        <div class="progress-bar bg-white" style="width: ${accVal}%; border-radius: 999px;"></div>
                    </div>
                </td>
            </tr>
        `;
    }).join('');
}

function renderShadcnCharts(det) {
    if (typeof Chart === 'undefined') {
        console.warn('Chart.js chưa sẵn sàng.');
        return;
    }

    // Chart 1: Per-Class Correct vs Wrong (Monochrome Shadcn Style)
    const classCanvas = document.getElementById('shadcnClassBarChart');
    if (classCanvas && det.by_class) {
        if (shadcnClassChart) {
            shadcnClassChart.destroy();
        }

        const labels = ['Cháy được', 'Thủy tinh', 'Kim loại', 'Nhựa (PET)'];
        const correctData = [
            det.by_class.burnable.correct,
            det.by_class.glass.correct,
            det.by_class.metal.correct,
            det.by_class.plastic.correct
        ];
        const wrongData = [
            det.by_class.burnable.wrong,
            det.by_class.glass.wrong,
            det.by_class.metal.wrong,
            det.by_class.plastic.wrong
        ];

        const ctx1 = classCanvas.getContext('2d');
        shadcnClassChart = new Chart(ctx1, {
            type: 'bar',
            data: {
                labels: labels,
                datasets: [
                    {
                        label: 'Ảnh đúng (Correct)',
                        data: correctData,
                        backgroundColor: '#ffffff',
                        borderRadius: 4,
                        borderSkipped: false,
                        barPercentage: 0.65,
                        categoryPercentage: 0.7
                    },
                    {
                        label: 'Ảnh sai (Wrong)',
                        data: wrongData,
                        backgroundColor: '#52525b',
                        borderRadius: 4,
                        borderSkipped: false,
                        barPercentage: 0.65,
                        categoryPercentage: 0.7
                    }
                ]
            },
            options: {
                responsive: true,
                maintainAspectRatio: false,
                animation: { duration: 500, easing: 'easeOutQuart' },
                interaction: {
                    mode: 'index',
                    intersect: false
                },
                plugins: {
                    legend: {
                        display: false
                    },
                    tooltip: {
                        backgroundColor: '#09090b',
                        borderColor: '#27272a',
                        borderWidth: 1,
                        titleColor: '#ffffff',
                        bodyColor: '#a1a1aa',
                        padding: 10,
                        cornerRadius: 6,
                        titleFont: { family: 'Inter', size: 12, weight: '600' },
                        bodyFont: { family: 'JetBrains Mono', size: 11 },
                        callbacks: {
                            label: function(context) {
                                return ` ${context.dataset.label}: ${context.raw.toLocaleString()} ảnh`;
                            }
                        }
                    }
                },
                scales: {
                    x: {
                        grid: {
                            display: false
                        },
                        ticks: {
                            color: '#a1a1aa',
                            font: { family: 'Inter', size: 11 }
                        },
                        border: {
                            color: '#27272a'
                        }
                    },
                    y: {
                        grid: {
                            color: 'rgba(255, 255, 255, 0.05)',
                            borderDash: [3, 3]
                        },
                        ticks: {
                            color: '#71717a',
                            font: { family: 'JetBrains Mono', size: 10 },
                            callback: function(val) {
                                return val.toLocaleString();
                            }
                        },
                        border: {
                            color: '#27272a'
                        }
                    }
                }
            }
        });
    }

    // Chart 2: Model Benchmark Comparison (Monochrome Shadcn Style)
    const modelCanvas = document.getElementById('shadcnModelBarChart');
    if (modelCanvas) {
        if (shadcnModelChart) {
            shadcnModelChart.destroy();
        }

        const ctx2 = modelCanvas.getContext('2d');
        shadcnModelChart = new Chart(ctx2, {
            type: 'bar',
            data: {
                labels: ['Late Fusion', 'ResNet-18', 'YOLO11n-cls'],
                datasets: [
                    {
                        label: 'Số ảnh phân loại chính xác',
                        data: [det.fusion_correct, det.resnet_correct, det.yolo_correct],
                        backgroundColor: [
                            '#ffffff', // Highlight Late Fusion in pure white
                            '#a1a1aa', // ResNet-18 in light zinc
                            '#52525b'  // YOLO11n-cls in dark zinc
                        ],
                        borderRadius: 4,
                        borderSkipped: false,
                        barPercentage: 0.55
                    }
                ]
            },
            options: {
                responsive: true,
                maintainAspectRatio: false,
                animation: { duration: 500, easing: 'easeOutQuart' },
                plugins: {
                    legend: {
                        display: false
                    },
                    tooltip: {
                        backgroundColor: '#09090b',
                        borderColor: '#27272a',
                        borderWidth: 1,
                        titleColor: '#ffffff',
                        bodyColor: '#a1a1aa',
                        padding: 10,
                        cornerRadius: 6,
                        titleFont: { family: 'Inter', size: 12, weight: '600' },
                        bodyFont: { family: 'JetBrains Mono', size: 11 },
                        callbacks: {
                            label: function(context) {
                                const val = context.raw;
                                const pct = ((val / det.val_total) * 100).toFixed(2);
                                return ` Chính xác: ${val.toLocaleString()} / ${det.val_total.toLocaleString()} ảnh (${pct}%)`;
                            }
                        }
                    }
                },
                scales: {
                    x: {
                        grid: {
                            display: false
                        },
                        ticks: {
                            color: '#a1a1aa',
                            font: { family: 'Inter', size: 11 }
                        },
                        border: {
                            color: '#27272a'
                        }
                    },
                    y: {
                        min: 2150,
                        max: 2350,
                        grid: {
                            color: 'rgba(255, 255, 255, 0.05)',
                            borderDash: [3, 3]
                        },
                        ticks: {
                            color: '#71717a',
                            font: { family: 'JetBrains Mono', size: 10 },
                            stepSize: 50,
                            callback: function(val) {
                                return val.toLocaleString();
                            }
                        },
                        border: {
                            color: '#27272a'
                        }
                    }
                }
            }
        });
    }
}

function handleDatasetFileSelect(files) {
    if (!files || files.length === 0) return;
    selectedDatasetFiles = Array.from(files);

    const statusEl = document.getElementById('uploadPreviewStatus');
    const uploadBtn = document.getElementById('btnUploadDataset');

    if (statusEl) {
        statusEl.innerHTML = `<i class="fa-solid fa-check me-1"></i> Đã chọn <strong>${selectedDatasetFiles.length}</strong> ảnh sẵn sàng nạp vào dataset.`;
    }
    if (uploadBtn) {
        uploadBtn.disabled = false;
    }
}

async function submitDatasetUpload() {
    if (selectedDatasetFiles.length === 0) return;

    const categorySelect = document.getElementById('uploadDatasetCategory');
    const category = categorySelect ? categorySelect.value : 'plastic';
    const uploadBtn = document.getElementById('btnUploadDataset');
    const statusEl = document.getElementById('uploadPreviewStatus');

    if (uploadBtn) {
        uploadBtn.disabled = true;
        uploadBtn.innerHTML = '<span class="spinner-border spinner-border-sm me-1"></span> Đang tải lên...';
    }

    let successCount = 0;
    for (const file of selectedDatasetFiles) {
        try {
            const base64 = await fileToBase64(file);
            const res = await fetch('/api/dataset/upload', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({
                    category: category,
                    image_base64: base64
                })
            });
            const data = await res.json();
            if (data.status === 'ok') successCount++;
        } catch (e) {
            console.warn('Lỗi upload file:', e);
        }
    }

    if (statusEl) {
        statusEl.innerHTML = `<span class="text-white fw-bold"><i class="fa-solid fa-circle-check me-1"></i> Đã nạp thành công ${successCount}/${selectedDatasetFiles.length} ảnh vào ngăn [${category.toUpperCase()}]!</span>`;
    }
    if (uploadBtn) {
        uploadBtn.disabled = true;
        uploadBtn.innerHTML = '<i class="fa-solid fa-plus me-1"></i> Bổ Sung Vào Dataset';
    }
    selectedDatasetFiles = [];
    loadDatasetAndTrainStats();
}

function fileToBase64(file) {
    return new Promise((resolve, reject) => {
        const reader = new FileReader();
        reader.readAsDataURL(file);
        reader.onload = () => resolve(reader.result);
        reader.onerror = error => reject(error);
    });
}

async function startAutoTraining() {
    const epochsSelect = document.getElementById('trainEpochsInput');
    const lrSelect = document.getElementById('trainLrInput');
    const epochs = epochsSelect ? parseInt(epochsSelect.value) : 5;
    const lr = lrSelect ? parseFloat(lrSelect.value) : 0.0001;

    const startBtn = document.getElementById('btnStartTrain');
    const stopBtn = document.getElementById('btnStopTrain');
    const badge = document.getElementById('trainStatusBadge');

    if (startBtn) startBtn.disabled = true;
    if (stopBtn) stopBtn.classList.remove('d-none');
    if (badge) {
        badge.textContent = 'ĐANG HUẤN LUYỆN...';
        badge.className = 'badge bg-white text-black font-mono small';
    }

    try {
        const res = await fetch('/api/train/start', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ epochs: epochs, lr: lr, batch_size: 16 })
        });
        const data = await res.json();
        if (data.error) {
            alert('Lỗi: ' + data.error);
            if (startBtn) startBtn.disabled = false;
            if (stopBtn) stopBtn.classList.add('d-none');
            return;
        }

        if (trainStatusPollInterval) clearInterval(trainStatusPollInterval);
        trainStatusPollInterval = setInterval(pollTrainStatus, 1000);
    } catch (e) {
        alert('Lỗi khởi động huấn luyện: ' + e.message);
        if (startBtn) startBtn.disabled = false;
        if (stopBtn) stopBtn.classList.add('d-none');
    }
}

async function pollTrainStatus() {
    try {
        const res = await fetch('/api/train/status');
        const state = await res.json();

        const pBar = document.getElementById('trainProgressBar');
        const pPct = document.getElementById('trainProgressPct');
        const pLbl = document.getElementById('trainProgressLabel');
        const counter = document.getElementById('trainEpochCounter');
        const term = document.getElementById('trainTerminalBox');
        const badge = document.getElementById('trainStatusBadge');
        const startBtn = document.getElementById('btnStartTrain');
        const stopBtn = document.getElementById('btnStopTrain');

        if (pBar) pBar.style.width = `${state.progress}%`;
        if (pPct) pPct.textContent = `${state.progress}%`;
        if (pLbl && state.status_text) pLbl.textContent = state.status_text;
        if (counter && state.total_epochs) counter.textContent = `Epoch ${state.current_epoch}/${state.total_epochs}`;

        if (term && state.logs && state.logs.length > 0) {
            term.textContent = state.logs.join('\n');
            term.scrollTop = term.scrollHeight;
        }

        if (!state.is_training) {
            if (trainStatusPollInterval) {
                clearInterval(trainStatusPollInterval);
                trainStatusPollInterval = null;
            }
            if (startBtn) startBtn.disabled = false;
            if (stopBtn) stopBtn.classList.add('d-none');
            if (badge) {
                badge.textContent = state.status_text.includes('Hoàn tất') ? 'HOÀN TẤT' : 'SẴN SÀNG';
                badge.className = 'badge border border-subtle text-secondary font-mono small';
            }
            loadDatasetAndTrainStats();
        }
    } catch (e) {
        console.warn('Lỗi polling training status:', e);
    }
}

async function stopAutoTraining() {
    if (!confirm('Bạn có chắc chắn muốn dừng tiến trình huấn luyện hiện tại?')) return;
    try {
        await fetch('/api/train/stop', { method: 'POST' });
    } catch (e) {
        console.warn('Lỗi dừng huấn luyện:', e);
    }
}

// Tự động khởi tạo kết nối ESP32-CAM và ESP32 Thùng Rác khi tải trang
document.addEventListener('DOMContentLoaded', () => {
    // 0. Khởi tạo mô hình AI đã chọn
    switchAiModel(currentModelType);

    // 1. Tải cài đặt từ server (file actuator_settings.json)
    loadActuatorSettings();

    // 2. Tải lịch sử phân loại từ file classification_history.json
    loadAndRenderHistory();

    // 3. Tải thông số dataset và benchmark ban đầu
    loadDatasetAndTrainStats();

    // 4. Kéo thả file vào dropzone
    const dropzone = document.getElementById('datasetDropzone');
    if (dropzone) {
        ['dragenter', 'dragover'].forEach(eventName => {
            dropzone.addEventListener(eventName, (e) => {
                e.preventDefault();
                dropzone.classList.add('dragover');
            }, false);
        });
        ['dragleave', 'drop'].forEach(eventName => {
            dropzone.addEventListener(eventName, (e) => {
                e.preventDefault();
                dropzone.classList.remove('dragover');
            }, false);
        });
        dropzone.addEventListener('drop', (e) => {
            e.preventDefault();
            dropzone.classList.remove('dragover');
            const dt = e.dataTransfer;
            if (dt && dt.files) {
                handleDatasetFileSelect(dt.files);
            }
        }, false);
    }

    // 5. Kết nối ESP32-CAM và Actuator
    setTimeout(() => {
        connectEsp32Cam();
        connectActuatorFromMain();
    }, 400);
});

