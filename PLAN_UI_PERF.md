# PLAN_UI_PERF — Kết quả hiện sớm, độ chênh lệch giữa các ngăn, hiệu năng web ↔ IoT, Cài đặt, stream camera

Ngày: 2026-11-20. Trạng thái: **đã làm** các mục đánh dấu ✅, còn lại là đề xuất chờ thử trên phần cứng.

---

## 1. Lỗi: kết quả chỉ hiện sau khi mâm quay xong

### Nguyên nhân
Trong `orchestrator._cycle_detect()` thứ tự là:

```
capture → detect → _dispatch() (gửi lệnh + CHỜ job xong, 3–10 s) → history.add → publish("result")
```

Sự kiện `result` chỉ được bắn ra **sau** `_dispatch`, mà `_dispatch` chờ actuator quay ra, hất, quay về.
Trong lúc đó web chỉ nhận `sort.state = dispatch/wait`, ô kết quả vẫn giữ kết quả cũ → nhìn như "đang chụp" mãi.

### Sửa ✅
1. Ngay khi có quyết định: `publish("result", {..., dispatch: {state: "pending"}})` → web hiện loại rác + % ngay.
2. Trong lúc chờ job: mỗi lần actuator đổi `step` thì gửi `result_update` nhỏ `{dispatch: {state: "sent", step}}` → ô kết quả hiện "Đang quay tới ngăn 4… / Đang hất…".
3. Khi xong: publish lần nữa với `dispatch.state = done|timeout|error` và `id` lịch sử.
4. Web/mobile so `ts` của kết quả: cùng `ts` thì cập nhật tại chỗ, không nháy.

Thời gian từ lúc bấm đến lúc thấy kết quả giảm từ ~(chụp + suy luận + quay) ≈ 5–12 s xuống ~(chụp + suy luận) ≈ 0.6–1.5 s.

---

## 2. Ô kết quả: thấy độ chênh lệch giữa các ngăn ✅

### Vấn đề
`probs` cũ trong chế độ YOLO là **phiếu bầu theo khung hình** (3 ảnh cùng ra Nhựa → Nhựa 100%), không phản ánh model phân vân giữa các loại.

### Cách lấy điểm từng loại
- Chạy YOLO một lần ở ngưỡng thấp (`conf = 0.02`) trên mỗi ảnh (không tốn thêm lần suy luận nào: dùng chung kết quả với bước tìm vật).
- Với mỗi loại rác, lấy **điểm cao nhất** của các khung loại đó **trùng vị trí** với vật đã chọn (IoU ≥ 0.3).
- Trung bình theo các ảnh (trọng số theo độ nét) → `result.scores = {paper, glass, other, plastic}` trong khoảng 0–1.
- `result.margin = top1 − top2` (điểm phần trăm).

Lưu ý: YOLO cho điểm **độc lập từng loại** (không cộng lại thành 100%). Vì vậy web hiển thị đúng điểm của model, không chuẩn hóa.

### Thiết kế
```
┌───────────────────────────────────────┐
│ Nhựa · Ngăn 4                    69%  │
│ Chắc chắn · hơn Giấy 41 điểm          │  ← margin, màu theo mức: ≥30 xanh, 10–30 vàng, <10 đỏ "phân vân"
│                                       │
│ 4 Nhựa      ████████████████░░░  69%  │  ← thanh màu theo ngăn, ngăn thắng đậm
│ 1 Giấy      ███████░░░░░░░░░░░░  28%  │
│ 2 Thủy tinh ██░░░░░░░░░░░░░░░░░   7%  │
│ 3 Rác khác  ░░░░░░░░░░░░░░░░░░░  <1%  │
│                                       │
│ ⟳ Đang quay tới ngăn 4…   · ảnh hơi mờ│
└───────────────────────────────────────┘
```
- Thanh xếp theo điểm giảm dần, luôn đủ 4 ngăn, nhãn trực tiếp (không phụ thuộc màu).
- Dòng trạng thái dưới cùng thay cho "Đã hất xong · 9.4 s" + "Sẵn sàng" (gộp một dòng).

---

## 3. Hiệu năng web ↔ server ↔ ESP32

### Hiện trạng
| Nguồn gọi | Tần suất | Ghi chú |
|---|---|---|
| `status_loop` → `GET /status` actuator | 1 s | publish SSE mỗi giây dù không đổi gì |
| `_wait_job` → `GET /status` | 0.3 s | **song song** với status_loop → 4 req/s lên ESP32 lúc đang quay |
| `_bin_full` trước mỗi lần gửi | 1 lần | thêm một round-trip |
| Web `live.js` | mỗi sự kiện SSE | dựng lại toàn bộ cột phải mỗi giây → nút/switch nháy, mất hover |

ESP32 httpd xử lý 1 request mỗi lúc; nhiều request chồng nhau làm `/status` chậm 200–800 ms và đôi khi timeout → web báo "offline" giả.

### Sửa ✅
1. **Một poller duy nhất** (`ActuatorMonitor`): chỉ nó gọi `/status`. 1 s khi rảnh, 250 ms khi có job. `_wait_job` và `_bin_full` đọc từ cache (`await monitor.wait_job(id)`), không gọi thêm.
2. **SSE chỉ gửi khi đổi**: so sánh JSON với lần trước, bỏ trường dao động (khoảng cách cảm biến làm tròn 0.5 cm); heartbeat 5 s.
3. **Web render có điều kiện**: mỗi view tính "chữ ký" phần state nó dùng, không đổi thì không dựng lại DOM.
4. Nút điều khiển phản hồi ngay (trạng thái "đang chạy" bật lên trước khi có phản hồi HTTP).

### Đề xuất thêm (chưa làm)
- Actuator đẩy sự kiện (WebSocket/UDP) khi đổi `step` thay vì server poll — cần sửa firmware, làm sau khi bản hiện tại ổn định.
- Gộp `/status` của actuator thành bản gọn (bỏ `last_move`, chỉ gửi khi có).

---

## 4. Stream camera mượt hơn

### Nguyên nhân giật
1. Mỗi lần phân loại, server gọi `GET /capture` 3 lần; ESP32 giữ `g_cam_lock` + bỏ 2 khung warmup → stream **đứng 0.5–1.5 s**.
2. Firmware stream có `vTaskDelay(60 ms)` cố định sau mỗi khung → trần ~10 fps kể cả khi WiFi còn dư.
3. Hàng đợi hub `maxsize=2` → trễ 1 khung; client chậm nhận khung cũ.
4. Web không biết stream đã chết (thẻ `<img>` đứng hình im lặng).

### Sửa ✅
1. **Chụp từ stream**: khi độ phân giải chụp = độ phân giải stream và không bật flash riêng cho lúc chụp, `_grab` lấy 3 khung mới nhất từ hub (cách nhau ≥ 1 khung) → không gọi `/capture`, stream không đứng, chụp nhanh hơn ~300 ms. Nếu hub không có khung mới trong 1.5 s thì quay về `/capture` như cũ.
2. Firmware cam: bỏ delay 60 ms cố định, thay bằng giới hạn fps cấu hình (`stream_fps`, mặc định 15) và nhả lock ngay sau `fb_get`.
3. Hub: hàng đợi 1 khung (luôn khung mới nhất), đo fps/kbps; `GET /camera/stats`.
4. Web: watchdog — nếu không có khung mới > 3 s thì tự kết nối lại stream; góc ảnh hiện `12 fps` nhỏ, đổi đỏ khi chậm.

### Đề xuất thêm
- Stream VGA q12 (≈25 KB/khung) thay SVGA q10 (≈60 KB) nếu WiFi yếu; chụp vẫn SVGA bằng `/capture` (chấp nhận đứng hình ngắn).
- Đặt ESP32-CAM gần router hoặc dùng anten ngoài (AI-Thinker có chân IPEX, cần chuyển điện trở 0 Ω).

---

## 5. Thiết kế lại Cài đặt ✅

### Vấn đề
Một danh sách dài ô số, tên kỹ thuật (τ, δ, Laplacian), không biết điền gì.

### Thiết kế mới
- Chia **thẻ** theo mục đích: *Nhận diện*, *Tự động*, *Camera*, *Giao diện & bảo mật*. Mục nâng cao thu gọn trong "Nâng cao".
- Mỗi dòng: **tên dễ hiểu** + nút **ⓘ** (tooltip: ý nghĩa, tăng/giảm thì sao, giá trị gợi ý) + ô nhập dạng stepper/thanh trượt + giá trị mặc định mờ.
- Ngưỡng dạng 0–1 hiển thị bằng **%** và thanh trượt.
- Nút "Khôi phục mặc định" từng mục; nút Lưu dính dưới, chỉ sáng khi có thay đổi.

| Trường | Tên mới | Tooltip (tóm tắt) |
|---|---|---|
| `det_tau` | Độ chắc chắn tối thiểu | Dưới mức này rác được đưa vào *Rác khác*. Tăng → ít phân loại sai, nhiều rác vào ngăn 3 hơn. Gợi ý 40–55%. |
| `det_conf` | Ngưỡng nhìn thấy vật | Model phải tự tin tới mức này mới coi là có vật. Thấp → nhạy, dễ báo nhầm bóng/vết bẩn. Gợi ý 25–35%. |
| `presence_min_diff` | Độ nhạy khay trắng | So với ảnh khay trống, chênh bao nhiêu mức xám thì coi là có vật. Nhỏ → nhạy với giấy trắng, dễ nhầm do đổi ánh sáng. Gợi ý 12–25. |
| `blur_min` | Độ nét tối thiểu | Ảnh mờ hơn mức này sẽ chụp lại (tối đa 3 lần). Gợi ý 40–80. |
| `min_area_frac` | Kích thước vật tối thiểu | Phần trăm khung hình; vật nhỏ hơn bị bỏ qua. Gợi ý 1–3%. |
| `vote_frames` | Số ảnh mỗi lần | Chụp nhiều ảnh rồi bầu chọn. Nhiều → chắc hơn nhưng chậm hơn. Gợi ý 3. |
| `tau`, `delta` | (Nâng cao) ngưỡng model phân loại cũ | Chỉ dùng khi không có model YOLO. |
| `auto_interval_sec` | Kiểm tra khay mỗi | Chế độ tự động nhìn khay bao lâu một lần. |
| `cooldown_sec` | Nghỉ sau mỗi lần | Chờ sau khi hất để rác rơi hẳn. |
| `capture.res` | Độ phân giải chụp | SVGA cân bằng; XGA nét hơn nhưng chậm và stream giật. |

---

## 6. Kiểm thử
- `pytest`: kết quả được publish trước khi job xong (fake actuator chậm 2 s); `scores` đủ 4 loại; `_wait_job` không gọi `/status` riêng; `_grab` lấy khung từ hub.
- Playwright: ô kết quả có 4 thanh; Cài đặt có tooltip; ảnh chụp màn hình.
- Phần cứng (cần bạn thử): stream fps khi rảnh và khi đang phân loại; thời gian từ bấm → thấy kết quả.
