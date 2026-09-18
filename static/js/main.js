// TrashNet - Minimalist Monochrome Controller (IoT & Vision Hub)
let currentMode = 'upload';
let selectedFile = null;
let webcamStream = null;
let scanHistory = [];

// DOM Elements
const tabUploadBtn = document.getElementById('tabUploadBtn');
const tabWebcamBtn = document.getElementById('tabWebcamBtn');
const uploadView = document.getElementById('uploadView');
const webcamView = document.getElementById('webcamView');
const uploadActionGroup = document.getElementById('uploadActionGroup');

const dropzone = document.getElementById('dropzone');
const fileInput = document.getElementById('fileInput');
const dropzonePlaceholder = document.getElementById('dropzonePlaceholder');
const dropzonePreviewBox = document.getElementById('dropzonePreviewBox');
const imagePreview = document.getElementById('imagePreview');
const btnSubmit = document.getElementById('btnSubmit');

const webcamVideo = document.getElementById('webcamVideo');
const webcamCanvas = document.getElementById('webcamCanvas');
const webcamOffOverlay = document.getElementById('webcamOffOverlay');
const webcamHud = document.getElementById('webcamHud');
const webcamActionGroup = document.getElementById('webcamActionGroup');
const btnSnap = document.getElementById('btnSnap');

const emptyState = document.getElementById('emptyState');
const resultState = document.getElementById('resultState');
const engineStatus = document.getElementById('engineStatus');

// Result Targets
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
    // 1. Update Sidebar Active State
    const links = document.querySelectorAll('#sidebarNav .sidebar-link');
    links.forEach(link => {
        if (link.getAttribute('data-nav-tab') === tabName) {
            link.classList.add('active');
        } else {
            link.classList.remove('active');
        }
    });

    // 2. Hide all views & show target view
    const viewMap = {
        vision: { id: 'viewVision', title: 'PHÂN LOẠI RÁC QUA AI VISION' },
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

    // 3. Update Header Title
    if (currentViewTitle && viewMap[tabName]) {
        currentViewTitle.textContent = viewMap[tabName].title;
    }

    // 4. Tab-specific lifecycle callbacks
    if (tabName === 'history') {
        renderFullHistoryGrid();
    } else if (tabName === 'analytics') {
        renderAnalyticsStats();
    }
}

// ============================================================
// 2. INPUT MODE SWITCHING (Upload vs Webcam)
// ============================================================
function switchMode(mode) {
    currentMode = mode;
    if (mode === 'upload') {
        if (tabUploadBtn) tabUploadBtn.classList.add('active');
        if (tabWebcamBtn) tabWebcamBtn.classList.remove('active');
        if (uploadView) uploadView.classList.remove('d-none');
        if (webcamView) webcamView.classList.add('d-none');
        if (uploadActionGroup) uploadActionGroup.classList.remove('d-none');
        stopWebcam();
    } else {
        if (tabWebcamBtn) tabWebcamBtn.classList.add('active');
        if (tabUploadBtn) tabUploadBtn.classList.remove('active');
        if (uploadView) uploadView.classList.add('d-none');
        if (webcamView) webcamView.classList.remove('d-none');
        if (uploadActionGroup) uploadActionGroup.classList.add('d-none');
    }
}

// ============================================================
// 3. FILE UPLOAD & DRAG & DROP
// ============================================================
if (dropzone) {
    dropzone.addEventListener('click', (e) => {
        if (e.target.closest('.btn-remove-preview')) return;
        if (!selectedFile && fileInput) {
            fileInput.click();
        }
    });

    dropzone.addEventListener('dragover', (e) => {
        e.preventDefault();
        dropzone.classList.add('dragover');
    });

    dropzone.addEventListener('dragleave', () => {
        dropzone.classList.remove('dragover');
    });

    dropzone.addEventListener('drop', (e) => {
        e.preventDefault();
        dropzone.classList.remove('dragover');
        if (e.dataTransfer.files && e.dataTransfer.files.length > 0) {
            handleFile(e.dataTransfer.files[0]);
        }
    });
}

if (fileInput) {
    fileInput.addEventListener('change', (e) => {
        if (e.target.files && e.target.files.length > 0) {
            handleFile(e.target.files[0]);
        }
    });
}

// Paste from Clipboard (Ctrl + V)
window.addEventListener('paste', (e) => {
    if (e.clipboardData && e.clipboardData.items) {
        for (let item of e.clipboardData.items) {
            if (item.type.indexOf('image') !== -1) {
                const file = item.getAsFile();
                if (file) {
                    switchNavTab('vision');
                    switchMode('upload');
                    handleFile(file);
                    break;
                }
            }
        }
    }
});

function handleFile(file) {
    if (!file.type.startsWith('image/')) {
        alert('Vui lòng chọn file hình ảnh (JPG, PNG, WEBP).');
        return;
    }

    selectedFile = file;
    const reader = new FileReader();
    reader.onload = (e) => {
        if (imagePreview) imagePreview.src = e.target.result;
        if (dropzonePlaceholder) dropzonePlaceholder.classList.add('d-none');
        if (dropzonePreviewBox) dropzonePreviewBox.classList.remove('d-none');
        if (btnSubmit) btnSubmit.disabled = false;
        if (engineStatus) engineStatus.textContent = 'ĐÃ NẠP ẢNH';
    };
    reader.readAsDataURL(file);
}

function removeImage(e) {
    if (e) e.stopPropagation();
    selectedFile = null;
    if (fileInput) fileInput.value = '';
    if (imagePreview) imagePreview.src = '';
    if (dropzonePreviewBox) dropzonePreviewBox.classList.add('d-none');
    if (dropzonePlaceholder) dropzonePlaceholder.classList.remove('d-none');
    if (btnSubmit) btnSubmit.disabled = true;
    if (engineStatus) engineStatus.textContent = 'SẴN SÀNG';
}

// ============================================================
// 4. WEBCAM CONTROLS (PERMANENTLY MIRRORED)
// ============================================================
async function startWebcam() {
    try {
        const stream = await navigator.mediaDevices.getUserMedia({
            video: { width: { ideal: 1280 }, height: { ideal: 720 }, facingMode: 'user' },
            audio: false
        });

        webcamStream = stream;
        if (webcamVideo) {
            webcamVideo.srcObject = stream;
        }
        if (webcamOffOverlay) webcamOffOverlay.classList.add('d-none');
        if (webcamHud) webcamHud.classList.remove('d-none');
        if (webcamActionGroup) webcamActionGroup.classList.remove('d-none');

        if (engineStatus) engineStatus.textContent = 'WEBCAM HOẠT ĐỘNG';
    } catch (err) {
        alert('Không thể mở camera: ' + err.message);
    }
}

function stopWebcam() {
    if (webcamStream) {
        webcamStream.getTracks().forEach(t => t.stop());
        webcamStream = null;
    }
    if (webcamVideo) webcamVideo.srcObject = null;
    if (webcamOffOverlay) webcamOffOverlay.classList.remove('d-none');
    if (webcamHud) webcamHud.classList.add('d-none');
    if (webcamActionGroup) webcamActionGroup.classList.add('d-none');
}

async function captureWebcam() {
    if (!webcamVideo || !webcamVideo.videoWidth) return;

    webcamCanvas.width = webcamVideo.videoWidth;
    webcamCanvas.height = webcamVideo.videoHeight;
    const ctx = webcamCanvas.getContext('2d');

    // Permanently mirrored: flip canvas so output perfectly matches user preview
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
// 5. RUN ANALYSIS API
// ============================================================
async function runAnalysis() {
    if (!selectedFile) return;

    setLoading(true, 'ĐANG XỬ LÝ LATE FUSION...');
    const formData = new FormData();
    formData.append('image', selectedFile);

    try {
        const res = await fetch('/api/predict', { method: 'POST', body: formData });
        const data = await res.json();
        if (!res.ok) throw new Error(data.error || 'Lỗi phân tích');

        renderResults(data, imagePreview ? imagePreview.src : '');
    } catch (err) {
        alert('Lỗi: ' + err.message);
        if (engineStatus) engineStatus.textContent = 'LỖI PHÂN TÍCH';
    } finally {
        setLoading(false);
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
// 8. IOT ROTARY CAROUSEL & LEVEL SENSOR SIMULATION
// ============================================================
let currentCarouselAngle = 0;
let isTrapdoorOpen = false;

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
            badge.textContent = 'TRẠNG THÁI: MỞ (90°) - ĐANG XẢ RÁC';
            badge.className = 'badge bg-white text-black font-mono small';
        } else {
            badge.textContent = 'TRẠNG THÁI: ĐÓNG (0°) - SẴN SÀNG';
            badge.className = 'badge border border-zinc text-zinc-400 font-mono small';
        }
    }
    if (systemStatus) {
        systemStatus.textContent = isOpen ? 'SERVO: MỞ KHAY THẢ RÁC' : 'HỆ THỐNG SẴN SÀNG';
    }
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

    const recyclable = scanHistory.filter(i => (i.category || '').toLowerCase().includes('tái chế')).length;
    const hazardous = scanHistory.filter(i => (i.category || '').toLowerCase().includes('nguy hại')).length;
    const organic = scanHistory.filter(i => (i.category || '').toLowerCase().includes('hữu cơ')).length;

    const recyclablePct = Math.round((recyclable / total) * 100);

    if (statRecyclable) statRecyclable.textContent = `${recyclablePct}%`;
    if (statHazardous) statHazardous.textContent = hazardous;
    if (statOrganic) statOrganic.textContent = organic;
}

// ============================================================
// 10. UTILITIES & RESET
// ============================================================
function clearAllData() {
    removeImage();
    if (emptyState) emptyState.classList.remove('d-none');
    if (resultState) resultState.classList.add('d-none');
    if (engineStatus) engineStatus.textContent = 'SẴN SÀNG';
}

function setLoading(isLoading, text = 'ĐANG XỬ LÝ...') {
    if (isLoading) {
        if (engineStatus) engineStatus.textContent = text;
        if (btnSubmit) {
            btnSubmit.disabled = true;
            btnSubmit.innerHTML = `<span class="spinner-border spinner-border-sm me-2" role="status"></span> ${text}`;
        }
        if (btnSnap) btnSnap.disabled = true;
    } else {
        if (btnSubmit) {
            btnSubmit.disabled = !selectedFile;
            btnSubmit.innerHTML = `<i class="fa-solid fa-wand-magic-sparkles me-1"></i> PHÂN TÍCH LATE FUSION`;
        }
        if (btnSnap) btnSnap.disabled = false;
    }
}
