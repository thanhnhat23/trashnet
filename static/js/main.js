// TrashNet - Minimalist Monochrome Controller (IoT & Camera Vision Hub)
let currentCameraSource = 'webcam';
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
        history: { id: 'viewHistory', title: 'LỊCH SỬ PHÂN LOẠI TRONG PHIÊN' },
        iot: { id: 'viewIot', title: 'KẾT NỐI IOT & ĐIỀU KHIỂN THÙNG RÁC' },
        analytics: { id: 'viewAnalytics', title: 'THỐNG KÊ RÁC THẢI & BÁO CÁO' },
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
        renderFullHistoryGrid();
    } else if (tabName === 'analytics') {
        renderAnalyticsStats();
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
            body: JSON.stringify({ image_base64: base64Img })
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
    let url = (el ? el.value : '').trim() || saved || 'http://172.16.3.205';
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
                // Chờ đúng 3 giây cho người dùng đặt rác tiếp theo rồi MỚI chụp tiếp!
                if (esp32AutoScanCheck && esp32AutoScanCheck.checked) {
                    if (engineStatus) engineStatus.textContent = 'SẴN SÀNG: ĐẶT RÁC TIẾP THEO (3s)...';
                    esp32AutoScanTimer = setTimeout(() => {
                        if (esp32AutoScanCheck && esp32AutoScanCheck.checked && !isActuatorCurrentlyBusy) {
                            captureEsp32Cam();
                        }
                    }, 3000);
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
                body: JSON.stringify({ image_base64: base64Img })
            });
            data = await res.json();
            if (!res.ok) throw new Error(data.error || 'Lỗi nhận diện AI');
            data.captured_image = base64Img;
        } else {
            // DỰ PHÒNG: Nếu chưa mở stream, gọi server proxy sang /capture của ESP32
            const res = await fetch(`/api/esp32/capture?ip=${encodeURIComponent(baseUrl)}`);
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
    if (engineStatus) engineStatus.textContent = 'HOÀN TẤT (99.37%)';

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

    // Add to history
    if (imageThumb) {
        saveToHistory({
            time: new Date().toLocaleTimeString(),
            thumb: imageThumb,
            name_vi: data.name_vi,
            class_name: data.class,
            category: data.category,
            confidence: data.confidence
        });
    }
}

// ============================================================
// 7. HISTORY MANAGEMENT
// ============================================================
function saveToHistory(entry) {
    scanHistory.unshift(entry);
    if (scanHistory.length > 50) scanHistory.pop();
    try {
        localStorage.setItem('trashnet_history', JSON.stringify(scanHistory));
    } catch (e) {}
    updateHistoryCounters();
}

function updateHistoryCounters() {
    if (sidebarHistoryCount) sidebarHistoryCount.textContent = scanHistory.length;
}

function renderFullHistoryGrid() {
    updateHistoryCounters();
    if (!fullHistoryEmpty || !fullHistoryGrid) return;

    if (scanHistory.length === 0) {
        fullHistoryEmpty.classList.remove('d-none');
        fullHistoryGrid.classList.add('d-none');
        fullHistoryGrid.innerHTML = '';
        return;
    }

    fullHistoryEmpty.classList.add('d-none');
    fullHistoryGrid.classList.remove('d-none');
    fullHistoryGrid.innerHTML = '';

    scanHistory.forEach(item => {
        const col = document.createElement('div');
        col.className = 'col-12 col-sm-6 col-md-4 col-xl-3';
        col.innerHTML = `
            <div class="p-2 border border-zinc bg-zinc-900 rounded-1 d-flex gap-2 align-items-center">
                <img src="${item.thumb}" alt="thumb" style="width: 56px; height: 56px; object-fit: cover; border-radius: 4px; border: 1px solid #27272a; flex-shrink: 0;">
                <div class="overflow-hidden flex-grow-1">
                    <div class="fw-semibold text-white text-truncate small">${item.name_vi}</div>
                    <div class="text-zinc-500 font-mono" style="font-size: 0.72rem;">${item.category}</div>
                    <div class="d-flex align-items-center justify-content-between mt-1">
                        <span class="badge border border-zinc-700 text-zinc-400 font-mono" style="font-size: 0.65rem;">${item.time}</span>
                        <span class="font-mono text-white fw-bold small">${item.confidence}%</span>
                    </div>
                </div>
            </div>
        `;
        fullHistoryGrid.appendChild(col);
    });
}

function clearHistory() {
    if (!confirm('Bạn có chắc muốn xóa toàn bộ lịch sử phân loại trong phiên này?')) return;
    scanHistory = [];
    try {
        localStorage.removeItem('trashnet_history');
    } catch (e) {}
    updateHistoryCounters();
    renderFullHistoryGrid();
    renderAnalyticsStats();
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
    } catch (e) {
        console.warn('Lỗi gửi lệnh Actuator:', e);
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

// Tự động khởi tạo kết nối ESP32-CAM và ESP32 Thùng Rác khi tải trang
document.addEventListener('DOMContentLoaded', () => {
    // Tải IP đã lưu nếu có
    try {
        const savedCam = localStorage.getItem('trashnet_esp32_ip');
        if (savedCam && esp32IpInput) esp32IpInput.value = savedCam;
        const savedAct = localStorage.getItem('trashnet_actuator_ip');
        const actInput = document.getElementById('actuatorIpInputMain');
        if (savedAct && actInput) actInput.value = savedAct;
    } catch (e) {}

    setTimeout(() => {
        connectEsp32Cam();
        connectActuatorFromMain();
    }, 400);
});
