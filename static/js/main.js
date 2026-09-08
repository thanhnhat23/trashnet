// State management
let currentMode = 'upload';
let selectedFile = null;
let webcamStream = null;

// DOM Elements
const tabUploadBtn = document.getElementById('tabUploadBtn');
const tabWebcamBtn = document.getElementById('tabWebcamBtn');
const uploadTab = document.getElementById('uploadTab');
const webcamTab = document.getElementById('webcamTab');
const uploadActions = document.getElementById('uploadActions');

const dropzone = document.getElementById('dropzone');
const fileInput = document.getElementById('fileInput');
const dropzoneDefault = document.getElementById('dropzoneDefault');
const previewWrapper = document.getElementById('previewWrapper');
const imagePreview = document.getElementById('imagePreview');
const btnAnalyze = document.getElementById('btnAnalyze');

const webcamVideo = document.getElementById('webcamVideo');
const webcamCanvas = document.getElementById('webcamCanvas');
const webcamOverlay = document.getElementById('webcamOverlay');
const scanFrame = document.getElementById('scanFrame');
const webcamControls = document.getElementById('webcamControls');

// Result Elements
const resultPlaceholder = document.getElementById('resultPlaceholder');
const resultContent = document.getElementById('resultContent');
const analysisStatus = document.getElementById('analysisStatus');

const resIcon = document.getElementById('resIcon');
const resCategory = document.getElementById('resCategory');
const resClassEn = document.getElementById('resClassEn');
const resTitle = document.getElementById('resTitle');
const resConfidence = document.getElementById('resConfidence');
const resConfidenceBar = document.getElementById('resConfidenceBar');
const resBinBanner = document.getElementById('resBinBanner');
const resBinText = document.getElementById('resBinText');
const resTips = document.getElementById('resTips');
const resBreakdownList = document.getElementById('resBreakdownList');
const cardHighlight = document.getElementById('cardHighlight');

// 1. Tab Switching
function switchTab(mode) {
    currentMode = mode;
    if (mode === 'upload') {
        tabUploadBtn.classList.add('active');
        tabWebcamBtn.classList.remove('active');
        uploadTab.style.display = 'block';
        webcamTab.style.display = 'none';
        uploadActions.style.display = 'block';
        stopWebcam();
    } else {
        tabWebcamBtn.classList.add('active');
        tabUploadBtn.classList.remove('active');
        uploadTab.style.display = 'none';
        webcamTab.style.display = 'block';
        uploadActions.style.display = 'none';
    }
}

// 2. Drag & Drop File Upload Handling
dropzone.addEventListener('click', (e) => {
    if (e.target.closest('#btnRemoveImage')) return;
    if (!selectedFile) {
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
        handleFileSelect(e.dataTransfer.files[0]);
    }
});

fileInput.addEventListener('change', (e) => {
    if (e.target.files && e.target.files.length > 0) {
        handleFileSelect(e.target.files[0]);
    }
});

function handleFileSelect(file) {
    if (!file.type.startsWith('image/')) {
        alert('Vui lòng chọn một file hình ảnh (JPG, PNG, WEBP).');
        return;
    }

    selectedFile = file;
    const reader = new FileReader();
    reader.onload = (e) => {
        imagePreview.src = e.target.result;
        dropzoneDefault.style.display = 'none';
        previewWrapper.style.display = 'flex';
        btnAnalyze.disabled = false;
    };
    reader.readAsDataURL(file);
}

function resetUpload(e) {
    if (e) e.stopPropagation();
    selectedFile = null;
    fileInput.value = '';
    imagePreview.src = '';
    previewWrapper.style.display = 'none';
    dropzoneDefault.style.display = 'block';
    btnAnalyze.disabled = true;
    previewWrapper.classList.remove('scanning');
}

// 3. Webcam Controls
async function startWebcam() {
    try {
        webcamStream = await navigator.mediaDevices.getUserMedia({
            video: {
                width: { ideal: 640 },
                height: { ideal: 480 },
                facingMode: 'environment'
            },
            audio: false
        });
        webcamVideo.srcObject = webcamStream;
        webcamOverlay.style.display = 'none';
        scanFrame.style.display = 'block';
        webcamControls.style.display = 'flex';
    } catch (err) {
        alert('Không thể truy cập camera. Vui lòng cấp quyền truy cập camera trong trình duyệt: ' + err.message);
    }
}

function stopWebcam() {
    if (webcamStream) {
        webcamStream.getTracks().forEach(track => track.stop());
        webcamStream = null;
    }
    webcamVideo.srcObject = null;
    webcamOverlay.style.display = 'flex';
    scanFrame.style.display = 'none';
    webcamControls.style.display = 'none';
}

// 4. API Predictions
async function analyzeUploadedImage() {
    if (!selectedFile) return;

    btnAnalyze.disabled = true;
    previewWrapper.classList.add('scanning');
    analysisStatus.innerText = 'Đang nhận diện AI...';
    analysisStatus.style.color = 'var(--primary)';

    const formData = new FormData();
    formData.append('image', selectedFile);

    try {
        const response = await fetch('/api/predict', {
            method: 'POST',
            body: formData
        });

        const data = await response.json();
        if (response.ok) {
            displayResults(data);
            analysisStatus.innerText = 'Hoàn tất!';
            analysisStatus.style.color = 'var(--primary)';
        } else {
            alert('Lỗi: ' + (data.error || 'Không thể phân tích ảnh'));
            analysisStatus.innerText = 'Lỗi phân tích';
            analysisStatus.style.color = 'var(--accent-red)';
        }
    } catch (err) {
        alert('Lỗi kết nối máy chủ: ' + err.message);
        analysisStatus.innerText = 'Lỗi kết nối';
    } finally {
        previewWrapper.classList.remove('scanning');
        btnAnalyze.disabled = false;
    }
}

async function captureAndPredict() {
    if (!webcamStream) return;

    const btnCapture = document.getElementById('btnCapture');
    btnCapture.disabled = true;
    analysisStatus.innerText = 'Đang chụp & phân tích...';

    // Draw video frame to hidden canvas
    webcamCanvas.width = webcamVideo.videoWidth || 640;
    webcamCanvas.height = webcamVideo.videoHeight || 480;
    const ctx = webcamCanvas.getContext('2d');
    ctx.drawImage(webcamVideo, 0, 0, webcamCanvas.width, webcamCanvas.height);

    const base64Data = webcamCanvas.toDataURL('image/jpeg', 0.9);

    try {
        const response = await fetch('/api/predict', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ image_base64: base64Data })
        });

        const data = await response.json();
        if (response.ok) {
            displayResults(data);
            analysisStatus.innerText = 'Hoàn tất!';
            analysisStatus.style.color = 'var(--primary)';
        } else {
            alert('Lỗi: ' + (data.error || 'Không thể phân tích'));
        }
    } catch (err) {
        alert('Lỗi kết nối máy chủ: ' + err.message);
    } finally {
        btnCapture.disabled = false;
    }
}

// 5. Render Results to UI
function displayResults(data) {
    resultPlaceholder.style.display = 'none';
    resultContent.style.display = 'flex';

    // Highlight card
    resIcon.innerText = data.icon;
    resCategory.innerText = data.category;
    resClassEn.innerText = data.class.toUpperCase();
    resTitle.innerText = data.name_vi;
    
    // Set custom theme color based on trash type
    cardHighlight.style.setProperty('--primary', data.badge_color);
    resCategory.style.background = `${data.badge_color}22`;
    resCategory.style.color = data.badge_color;

    // Confidence counter animation
    animateCounter(resConfidence, data.confidence, '%');
    resConfidence.style.color = data.badge_color;
    resConfidenceBar.style.width = `${data.confidence}%`;
    resConfidenceBar.style.background = `linear-gradient(90deg, ${data.badge_color}, ${data.badge_color}dd)`;

    // Bin recommendation & tips
    resBinBanner.style.borderColor = `${data.badge_color}55`;
    resBinBanner.style.background = `${data.badge_color}18`;
    resBinText.innerText = data.bin;
    resTips.innerText = data.tips;

    // Detailed breakdown list
    resBreakdownList.innerHTML = '';
    data.all_predictions.forEach(item => {
        const row = document.createElement('div');
        row.className = 'breakdown-row';
        row.innerHTML = `
            <div class="b-name">
                <span>${item.icon}</span>
                <span>${item.name_vi}</span>
            </div>
            <div class="b-bar-bg">
                <div class="b-bar-fill" style="width: ${item.confidence}%; background-color: ${item.badge_color};"></div>
            </div>
            <div class="b-conf">${item.confidence}%</div>
        `;
        resBreakdownList.appendChild(row);
    });
}

function animateCounter(element, targetValue, suffix = '') {
    let current = 0;
    const step = targetValue / 25;
    const timer = setInterval(() => {
        current += step;
        if (current >= targetValue) {
            current = targetValue;
            clearInterval(timer);
        }
        element.innerText = `${current.toFixed(1)}${suffix}`;
    }, 20);
}
