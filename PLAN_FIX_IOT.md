# PLAN_FIX_IOT: rà soát luồng IoT và các lỗi cần fix

Ngày rà soát: 20/11/2026. Phạm vi: firmware actuator (`firmware/actuator/main/*`), server (`server/services/orchestrator.py`, `server/api/actuator.py`), nút trên web/mobile (tab Thiết bị, Live).

> Chỉ là kế hoạch, chưa sửa code.

---

## 1. Kịch bản mong muốn

| Rác | Trình tự |
|---|---|
| Ngăn 1 | mở nắp → hất rác → đóng nắp (mâm đã ở ngăn 1, không quay) |
| Ngăn 2/3/4 | quay tới ngăn N → mở nắp → hất rác → đóng nắp → quay về ngăn 1 |

Cảm biến:
- **CB1** (`TRIG 19 / ECHO 23`): khoảng cách ≤ **3 cm** thì ngăn đầy.
- **CB2** (`TRIG 21 / ECHO 22`): khoảng cách ≤ **3 cm** thì mở nắp.

> Giả định: CB2 là cảm biến tiệm cận (đưa tay/vật lại gần thì mở nắp), đúng với cách code hiện tại đang dùng. Nếu CB2 thực ra dùng để *xác nhận nắp đã mở* thì mục F4 phải đổi logic.

## 2. Luồng hiện tại (đọc từ code)

`worker()` trong `actuator.c`, job loại sort (`POST /action`):

```
goto_bin(N) → dump_cycle() → goto_bin(1) → rehome_if_due()
```

So với kịch bản:

| Bước | Kịch bản | Code hiện tại | Kết luận |
|---|---|---|---|
| Quay tới ngăn N | có | có (`goto_bin`) | ✅ |
| Mở nắp trước khi hất | **có** | **không có** | ❌ thiếu |
| Hất rác | có | có (`dump_cycle`) | ✅ |
| Đóng nắp | có | không có | ❌ thiếu |
| Về ngăn 1 | có | có | ✅ |
| Ngăn 1 không quay | có | `tn_plan_rotation` trả 0 bước | ✅ |
| Ngưỡng đầy 3 cm | 3 cm | mặc định 5 cm, **không cho đặt dưới 4 cm** | ❌ |
| Ngưỡng nắp 3 cm | 3 cm | mặc định 5 cm, **không cho đặt dưới 4 cm** | ❌ |
| Ngăn đầy thì xử lý | (cần) | chỉ báo `bin1_full`, server vẫn hất vào | ❌ |

Nắp hiện chỉ mở trong một trường hợp: chế độ tự động `lid_auto`, khi CB2 thấy vật trong khoảng `lid_cm`.

---

## 3. Danh sách lỗi

Mức độ: 🔴 sai kịch bản / có thể hỏng cơ khí · 🟠 sai chức năng · 🟡 cần cải thiện.

### Firmware actuator

| # | Mức | Lỗi | Vị trí |
|---|---|---|---|
| F1 | 🔴 | Job phân loại **không mở nắp trước khi hất** và không đóng nắp sau đó. Tay hất có thể đập vào nắp đang đóng. | `actuator.c` `worker()` |
| F2 | 🔴 | Ngưỡng `full_cm` / `lid_cm` bị kẹp tối thiểu 4 cm (`clampi(..., 40, 4000)`), nên **không thể đặt 3 cm**. Mặc định đang là 5 cm. | `act_calib_from_json`, `g_cal` |
| F3 | 🟠 | Nút test "Hất rác", "Mở/Đóng nắp" và jog chạy **chặn luồng HTTP** (~3 s). Trong lúc đó `/status` không trả lời, server tưởng actuator offline. Các nút này cũng kiểm tra `state` không khóa mutex, nên có thể chạy chồng với một job đang chạy. | `act_test_dump`, `act_test_lid`, `act_jog`, `servo_post` |
| F4 | 🟠 | Auto-nắp có thể tự kích hoạt trong/ngay sau job (khi nắp vừa mở, CB2 có thể đọc ≤ 3 cm vì chính cánh nắp). Vòng chờ "người còn đứng đó" gọi `median_cm10` **2 lần mỗi vòng** (tốn gấp đôi thời gian, và hai lần đọc khác nhau). | `sensor_task` |
| F5 | 🟠 | Khi đang auto-mở nắp (tối đa ~13 s), `/action` trả 409 busy. Server chỉ thử lại 1 lần sau 3 s nên rác bị bỏ, báo lỗi "busy". | `sensor_task` ↔ `orchestrator._dispatch` |
| F6 | 🟠 | `rehome_if_due()` gọi `goto_bin(1)` khi mâm **đã ở ngăn 1**, nên không làm gì. Sai số quay open-loop vẫn tích lũy, bộ đếm bị reset như thể đã về home. | `rehome_if_due` |
| F7 | 🟡 | Các nút test trả `{"ok":true}` ngay cả khi bị bỏ qua vì đang bận, nên web báo thành công giả. | `servo_post`, `jog_post` |
| F8 | 🟡 | CB1 đặt cố định, nên đo ngăn nào đang nằm dưới nó. Sau nút "Ngăn N" (rotate), mâm **không về 1**, nên `bin1_full` thực ra là trạng thái của ngăn N. | `worker` kind=1 |
| F9 | 🟡 | HC-SR04 có vùng mù khoảng 2 cm, `median_cm10` bỏ mọi giá trị < 2 cm. Ở 3 cm sát giới hạn: cần gắn cảm biến lùi vào ≥ 1 cm hoặc đổi ngưỡng tương ứng. Ngoài ra trễ nhả (off) hiện là +2 cm, nên với ngưỡng 3 cm thì phải lên 5 cm mới hết "đầy". | `sensor_task` |

### Server / web / mobile

| # | Mức | Lỗi | Vị trí |
|---|---|---|---|
| S1 | 🔴 | Không chặn phân loại khi ngăn đầy: server không đọc `bin1_full`, cứ thế gửi `/action`. | `orchestrator._dispatch` |
| S2 | 🟠 | Không có cảnh báo "Ngăn đầy" trên Live (web và mobile); chỉ thấy số cm ở tab Thiết bị. | `views/live.js`, `live_page.dart` |
| S3 | 🟠 | Nút "Ngăn 1–4" ở tab Thiết bị chỉ **quay** (rotate), không có cách test **trọn kịch bản** cho một ngăn. Action `bin` có sẵn ở server nhưng không có nút nào gọi. | `views/devices.js`, `devices_page.dart` |
| S4 | 🟠 | "Quay mâm tự động" quay 1→2→3→4 rồi để mâm ở ngăn 4 khi tắt, vì không về ngăn 1. | `orchestrator._cycle_loop`, standalone `setCycle` |
| S5 | 🟡 | `job_timeout_sec` = 20 s. Kịch bản mới (quay ~2 s + nắp 0,5 s + hất ~2,7 s + nắp 0,5 s + về ~2 s ≈ 8–9 s) vẫn đủ, nhưng không còn dư nếu đang auto-nắp (F5). | `config.py` |
| S6 | 🟡 | Dòng trạng thái actuator chưa hiện bước đang chạy (`step`), nên khó biết đang kẹt ở bước nào. | `views/devices.js` |

---

## 4. Đối chiếu từng nút trên web (tab Thiết bị)

| Nút | Gọi | Thực tế làm | Đúng chưa |
|---|---|---|---|
| Ngăn 1–4 | `rotate` → `/rotate` | Quay mâm tới ngăn N rồi **đứng đó** | ✅ nếu chỉ để test quay. Nên ghi rõ "Quay tới N" và thêm nút test trọn kịch bản (S3) |
| Hất rác | `dump` → `/servo {dump:1}` | Hất tại vị trí hiện tại, **không mở nắp**, chặn HTTP ~3 s | ⚠️ F1, F3, F7 |
| Mở nắp / Đóng nắp | `/servo {lid}` | Đúng, nhưng chặn HTTP ~0,6 s | ✅ (sửa theo F3) |
| −30/−10/+10/+30 ms | `jog` | Nhích mâm, đúng | ✅ (chặn HTTP, F3) |
| Đặt làm Home | `/home/set` | Đặt vị trí hiện tại là ngăn 1, đúng | ✅ |
| Hiệu chuẩn servo | `/calib` | Lưu NVS, đúng; nhưng `full_cm`/`lid_cm` < 4 bị kẹp về 4 | ⚠️ F2 |
| Live: Chụp & phân loại / Auto | `/sort/*` → `/action` | Chạy kịch bản hiện tại (thiếu nắp) | ⚠️ F1, S1 |
| Live: Quay mâm tự động | `/sort/cycle` | Quay vòng 1→4 | ⚠️ S4 |
| Live: Kết nối lại | `/devices/rescan` | Đúng sau khi khởi động lại server | ✅ |

---

## 5. Cách sửa

### 5.1 Firmware: job phân loại đúng kịch bản (F1, F6, F8)

`worker()` kind = sort:

```
set step "rotate"     goto_bin(N)                    (bỏ qua nếu N = 1)
set step "lid_open"   lid_move(true);  chờ lid_settle_ms (mặc định 300)
set step "dump"       dump_cycle()
set step "lid_close"  lid_move(false)
set step "measure"    đo CB1 cho ngăn N, lưu fill[N]   (tận dụng lúc ngăn N đang dưới cảm biến)
set step "return"     goto_bin(1)
job_done = true, state = idle
```

- Thêm `lid_settle_ms` vào calib (JSON và web).
- Thêm `fill_cm[4]` và `full[4]` vào `/status`, thay cho một giá trị `bin1_full` duy nhất.
- Job rotate (kind = 1) giữ nguyên để test, nhưng ghi `position` rõ ràng. `bin1_full` chỉ cập nhật khi `position == 1`.
- `rehome_if_due`: bỏ, hoặc đổi thành "báo cần căn lại": sau 20 lần thì `/status` trả `needs_home: true`, web hiện nhắc bấm jog + "Đặt làm Home". Không có công tắc home thì không tự sửa trôi được; nếu muốn tự động, có thể gắn thêm 1 cảm biến hall/công tắc hành trình ở ngăn 1 (servo giữ nguyên).

### 5.2 Firmware: ngưỡng 3 cm (F2, F9)

- Kẹp mới: `full_cm`, `lid_cm` cho phép từ **2,5 cm** (`clampi(..., 25, 4000)`).
- Mặc định `full_cm10 = 30`, `lid_cm10 = 30`.
- Lọc mẫu: giữ mẫu ≥ 2,0 cm; mẫu 0 (timeout) bỏ qua, như hiện tại.
- Trễ nhả: off = on + 1,5 cm (thay vì +2 cm), để "hết đầy" ở 4,5 cm.
- Số mẫu liên tiếp: đầy cần 3 lần, nắp cần 2 lần (giữ nguyên).
- Ghi chú lắp đặt: CB1 cách mặt rác tối đa ≥ 3 cm khi ngăn đầy đúng mức mong muốn.

### 5.3 Firmware: các nút test không chặn và không chồng job (F3, F7)

- Đưa `dump`, `lid_open`, `lid_close`, `jog` vào **cùng hàng đợi job** với `kind` mới (2 = dump, 3 = lid, 4 = jog). HTTP trả 202 + `job_id` ngay, hoặc 409 busy.
- Như vậy mọi chuyển động đi qua một worker, không chạy chồng nhau, `/status` luôn trả lời.
- `servo_post` / `jog_post` trả đúng mã 202/409 thay vì luôn `ok:true`.
- Server `api/actuator.py`: chuyển 409 thành lỗi "Đang bận" cho web.

### 5.4 Firmware: auto-nắp (F4, F5)

- Auto-nắp chỉ chạy khi `state == IDLE` **và** đã qua ≥ 2 s kể từ job cuối, để cánh nắp vừa đóng không kích hoạt lại.
- Vòng chờ: đọc 1 lần mỗi 500 ms, đóng khi 3 lần liên tiếp > ngưỡng + 1,5 cm, tối đa 10 s.
- Khi có `/action` tới trong lúc auto-nắp: nhận job vào hàng đợi (không trả 409). Worker đợi nắp đóng xong rồi chạy kịch bản. Hàng đợi chỉ nhận 1 job chờ.

### 5.5 Server (S1, S4, S5)

- `_dispatch`: đọc `/status` trước. Nếu `full[N]` (hoặc `bin1_full` với firmware cũ) thì **không hất**: kết quả có `error: "bin_full"`, phát event `alert`, Auto tạm dừng đến khi hết đầy.
- 409 busy: thử lại tới khi hết `job_timeout_sec`, mỗi 1 s (thay vì 1 lần sau 3 s).
- `_cycle_loop`: khi tắt, gửi `/rotate {bin:1}` để mâm về ngăn 1.
- `job_timeout_sec` mặc định 30.

### 5.6 Web + mobile (S2, S3, S6)

- **Live**: hiện cảnh báo đỏ "Ngăn N đầy" khi `full[N]` (SSE `actuator`).
- **Thiết bị → Kiểm tra nhanh**, chia hai hàng:
  - "Quay tới: 1 2 3 4" (rotate, như hiện tại)
  - "Chạy thử kịch bản: 1 2 3 4" (action `bin`: quay → mở nắp → hất → đóng nắp → về 1)
- Dòng trạng thái: `idle · vị trí 1 · bước: lid_open · đầy: 1✕ 2✓ 3✓ 4✓ · nắp 78 cm`.
- Ô hiệu chuẩn: `full_cm`, `lid_cm` cho phép 2,5; thêm `lid_settle_ms`.
- Áp dụng giống nhau cho `views/devices.js`, `views/live.js`, `devices_page.dart`, `live_page.dart`, và `standalone_backend.dart` (chặn ngăn đầy ở chế độ không server).

### 5.7 Cập nhật tài liệu và test

- `shared/device-api.md`: thêm `step` mới, `fill_cm[]`, `full[]`, `needs_home`, mã 202/409 cho `/servo` và `/jog`.
- `firmware/actuator/test/test_motion_math.c`: test ngưỡng 3 cm với hysteresis mới.
- `scripts/fake_devices.py`: mô phỏng trình tự bước mới và trạng thái đầy.
- `server/tests`: thêm test "ngăn đầy thì không dispatch", "409 thì thử lại", "tắt cycle thì về 1".

---

## 6. Thứ tự làm

1. F1 + F2 (đúng kịch bản và ngưỡng 3 cm). Đây là phần tối thiểu để chạy demo.
2. F3 + F7 (đưa nút test vào hàng đợi).
3. S1 + S2 (chặn ngăn đầy, cảnh báo).
4. F4 + F5 (auto-nắp không phá job).
5. S3 + S4 + S6 (nút test kịch bản, cycle về 1, hiển thị bước).
6. F6, F8, F9 và tài liệu/test.

## 7. Kiểm thử trên phần cứng (sau khi flash)

| # | Thao tác | Kỳ vọng |
|---|---|---|
| 1 | "Chạy thử kịch bản: 1" | Không quay; nắp mở → hất → nắp đóng; `job_done` |
| 2 | "Chạy thử kịch bản: 3" | Quay 180° → nắp mở → hất → nắp đóng → về 1 |
| 3 | "Chạy thử kịch bản: 2" | Về 1 bằng đường lùi + bù khe hở; vị trí cuối đúng ngăn 1 |
| 4 | Lặp lại bước 2 10 lần | Sai lệch mắt thường < 5°; nếu lớn hơn thì chỉnh `trim_ms` |
| 5 | Đặt vật cách CB1 3 cm | `full` bật sau ~1 s; Live báo đầy; Chụp & phân loại bị chặn |
| 6 | Bỏ vật ra (> 4,5 cm) | Hết đầy, Auto chạy lại |
| 7 | Đưa tay cách CB2 3 cm khi idle | Nắp mở, rút tay thì đóng sau ~1,5 s |
| 8 | Đưa tay vào CB2 trong lúc đang chạy kịch bản | Nắp không bị đóng/mở chen ngang |
| 9 | Bấm "Hất rác" liên tục 3 lần | Lần 2–3 báo "Đang bận"; chấm act không chuyển đỏ |
| 10 | Bật rồi tắt "Quay mâm tự động" | Mâm trở về ngăn 1 |

---

# Đợt 2: sau khi chạy thử lần đầu (20/11/2026, 18:00)

Phản hồi từ chạy thực tế:
1. Giao diện nút xấu, cần thiết kế lại.
2. Bỏ "Quay mâm tự động" vì đã có Auto.
3. Chụp xong, server phân loại ra "Giấy · Ngăn 1" nhưng servo hình như không chạy.
4. **Sửa kịch bản:** chỉ ngăn 1 mới mở nắp; ngăn 2/3/4 không mở nắp.
5. Mở/đóng nắp đang ngược: bấm "Đóng" thì nắp mở và ngược lại.

## 8. Kịch bản đúng (đối chiếu firmware gốc `_legacy/firmware_esp32_actuator/main/main.c`)

Firmware gốc ghi rõ: nắp (servo GPIO 27) và CB2 thuộc **Thùng 1**; ngăn 2/3/4 không mở nắp.

| Rác | Trình tự |
|---|---|
| Ngăn 1 | (không quay) → **mở nắp** → chờ ~400 ms → hất rác → **đóng nắp** |
| Ngăn 2/3/4 | quay tới ngăn N → chờ ~400 ms → hất rác → quay về ngăn 1 |

Bản hotfix đợt 1 đang mở/đóng nắp cho **mọi** ngăn, nên phải sửa lại.

## 9. Lỗi mới

| # | Mức | Lỗi | Nguyên nhân / vị trí |
|---|---|---|---|
| G1 | 🔴 | Mở nắp cho cả ngăn 2/3/4 | `run_sort()` trong `actuator.c` (hotfix đợt 1) gọi `lid_move` vô điều kiện |
| G2 | 🔴 | Mở/đóng nắp ngược chiều | Mặc định `lid_rest = 10`, `lid_open = 0`, nhưng code kẹp góc tối thiểu 5° (`TN_ANGLE_MIN`), nên "mở" chỉ còn 10°→5°. Firmware gốc có cờ `lid_inverted` (lưu NVS `lidinv`); khi tắt cờ thì đóng = 0°, mở = 10°. Phần cứng của nhóm khả năng đang lắp theo chiều này, nên bản mới chạy ngược. |
| G3 | 🟠 | Không kéo được góc nắp về 0° | `tn_clamp_angle` kẹp 5..175 cho mọi servo vị trí. Firmware gốc cho phép 0..180. |
| G4 | 🟠 | "Servo không chạy" sau khi phân loại | Xem mục 10. Ngăn 1 thì **mâm không quay là đúng**, chỉ servo hất (và nắp) chạy. Thẻ kết quả không có lỗi, tức server đã nhận `job_done`. Chưa rõ servo hất có thật sự chạy không. |
| G5 | 🟠 | Thẻ kết quả không cho biết đã gửi actuator hay chưa | `ResultCard` chỉ hiện lỗi; không có "đã gửi → đang chạy → xong" |
| G6 | 🟡 | Thẻ kết quả gây khó hiểu: "73%" nhưng ngay dưới ghi "Giấy 100%" | 73% là độ tin của detector (đã nhân 0,85 vì ảnh mờ); dòng dưới là phân phối đã chuẩn hóa sau vote. Nên chỉ hiện một con số. |
| G7 | 🟡 | "Quay mâm tự động" thừa | Có Auto rồi; chức năng test quay vòng chỉ cần ở tab Thiết bị (nút Ngăn 1–4) |
| G8 | 🟡 | Nút xấu, rời rạc | Mỗi nút một hàng có gạch dưới (`.row` có border), không nhóm theo chức năng, nút bật/tắt hiển thị dạng chữ "Auto: tắt" |

## 10. Chẩn đoán "servo không chạy" (G4)

Làm theo thứ tự, dừng ở bước đầu tiên thấy sai:

1. Tab Thiết bị: bấm **Hất rác**. Nếu servo GPIO 25 không động thì lỗi nằm ở nguồn/dây/servo, không phải phần mềm. MG995 + MG996R cần nguồn 5–6 V riêng ≥ 3 A, nối chung GND với ESP32.
2. Bấm **Chạy thử kịch bản: Ngăn 3**. Mâm phải quay 180°, hất, rồi quay về.
3. Mở `.\monitor.bat actuator COM7`, bấm lại "Chụp & phân loại". Log phải có `job` và các bước `lid_open → dump → lid_close`. Nếu log có `Brownout detector was triggered` hoặc ESP32 khởi động lại thì nguồn servo đang kéo sụt nguồn ESP32.
4. Kiểm tra `/status` có `calib_ver` và `step`. Nếu không có `step` thì actuator vẫn chạy firmware cũ, chưa flash bản hotfix.

Phần mềm sẽ hỗ trợ chẩn đoán qua G5: thẻ kết quả hiện "Đã gửi actuator · job abc123 · xong sau 4,2 s", hoặc lỗi cụ thể.

## 11. Cách sửa

### 11.1 Firmware actuator (G1, G2, G3)

- `run_sort(bin)`:
  ```
  if bin != 1: goto_bin(bin)
  if bin == 1: lid_move(open); delay(lid_settle_ms = 400)
  dump_cycle()
  if bin == 1: lid_move(close)
  measure_here()            // CB1 cho ngăn đang ở dưới cảm biến
  if bin != 1: goto_bin(1)
  ```
- Bỏ kẹp 5..175 cho servo nắp và servo hất: dùng 0..180 như firmware gốc. Giữ `TN_ANGLE_MIN/MAX` chỉ cho thanh trượt web để tránh kéo nhầm quá giới hạn cơ khí.
- Thêm `lid_invert` (bool) vào calib và đọc thêm khóa NVS cũ `lidinv`/`lidrest`/`lidopen` nếu chưa có `calib`, để giữ đúng cấu hình nhóm đã chỉnh trên firmware gốc.
- Mặc định mới: `lid_rest = 0`, `lid_open = 10`, `lid_invert = false`. Kèm nút **Đảo chiều nắp** (đổi chỗ hai góc rồi lưu).
- Tự mở nắp bằng CB2: chỉ khi mâm đang ở ngăn 1 (nắp thuộc Thùng 1).
- `lid_settle_ms` mặc định 400 ms (như firmware gốc).

### 11.2 Server (G5, G6, G7)

- Bỏ `/sort/cycle`, `set_cycle`, `_cycle_loop`, `_return_to_bin1`, trường `cycle` trong event `sort`; xóa test `test_rescan_finds_devices_and_cycle_rotates` phần cycle.
- Thêm vào kết quả: `dispatch: {state: sent|done|timeout|busy|bin_full|error, job_id, ms}`. Event `sort` phát `step` của actuator trong lúc chờ (đọc từ `/status`), để Live hiện "Đang hất rác…".
- Thẻ kết quả: một con số độ tin duy nhất (của detector); thanh phân phối nhỏ chỉ hiện khi có từ 2 lớp trở lên với xác suất > 5%.

### 11.3 Thiết kế lại giao diện nút (G8), web và mobile như nhau

Nguyên tắc: tối giản, chữ là chính, nhóm theo việc, không gạch dưới từng nút.

**Live, cột phải:**

```
KẾT QUẢ
  Giấy · Ngăn 1                       73%
  ảnh mờ · Đã gửi actuator · xong 4,2 s

ĐIỀU KHIỂN
  [        Chụp & phân loại        ]      ← nút chính, rộng hết cột, cao 48 px
  Tự động phân loại            ( ●○ )     ← công tắc (switch), không dùng chữ "Auto: tắt"
  Đèn flash                    ( ○● )
  [ Đặt khay trống ]                      ← nút phụ, viền mảnh

MÂM                                       ← chỉ mobile (hàng nút to dễ bấm); web giữ ở tab Thiết bị
  [ 1 ][ 2 ][ 3 ][ 4 ]   [ Hất ]          ← segmented control, ô đang ở vị trí hiện tại được tô đậm
```

**Tab Thiết bị:** chia thành các khối có tiêu đề nhỏ, mỗi khối là một lưới nút đều cột:

| Khối | Nút |
|---|---|
| Mâm | segmented `1 2 3 4` (chỉ quay) · `Chạy thử kịch bản` + chọn ngăn |
| Nắp & hất | `Mở nắp` `Đóng nắp` `Đảo chiều nắp` · `Hất rác` |
| Căn chỉnh | `−30` `−10` `+10` `+30` ms · `Đặt làm Home` |
| Hiệu chuẩn | form 2 cột, nhóm: Mâm / Servo hất / Nắp / Cảm biến |

CSS (`base.css`, `tokens.css`):
- `.btn-group` (segmented): nút dính liền, bo góc ngoài, ô đang chọn có nền `--text`.
- `.switch`: công tắc 36×20 px thay cho nút "bật/tắt".
- `.btn-block`: nút chính rộng 100 %.
- `.panel`: khối có tiêu đề nhỏ + khoảng cách, bỏ border-bottom của `.row` trong khối điều khiển.
- Chiều cao nút đồng nhất 36 px (web) / 48 px (mobile); khoảng cách 8 px.

Mobile (Flutter): `SegmentedButton` cho chọn ngăn, `SwitchListTile` cho Auto/Flash, `FilledButton` rộng hết cho Chụp & phân loại. Tokens vẫn sinh từ `shared/design-tokens.json`.

### 11.4 Tài liệu và test

- `shared/device-api.md`: kịch bản mới (nắp chỉ ngăn 1), `lid_invert`, bỏ `/sort/cycle`.
- `fake_devices.py`: `step` mô phỏng theo kịch bản mới.
- Test server: kết quả có `dispatch.state == "done"`; `/sort/cycle` trả 404.
- Smoke test UI: Live không còn nút "Quay mâm tự động"; có công tắc Auto.

## 12. Thứ tự làm đợt 2

1. G1 + G2 + G3 (đúng kịch bản, đúng chiều nắp). Đây là phần quan trọng nhất trước khi test tiếp.
2. G4: chạy 4 bước chẩn đoán ở mục 10 trên phần cứng (nhóm làm, gửi log).
3. G7 (bỏ quay mâm tự động) + G5 + G6 (thẻ kết quả rõ ràng).
4. G8: thiết kế lại nút web + mobile.
5. Cập nhật tài liệu và test, flash lại actuator, chạy lại bảng kiểm thử mục 7 (bước 1: ngăn 1 có mở/đóng nắp; bước 2–3: ngăn 3, 2 không mở nắp).
