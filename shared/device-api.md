# ESP32 device HTTP contract (v1)

Used by both the PC server (`server/`) and the Flutter app (`mobile/`).
All responses are JSON unless noted. Auth: if the device has a key stored
(set during provisioning), every **control** route requires header `X-Key: <key>`.
`GET /status` and `GET /capture` stay open.

## Discovery

| Method | Detail |
|---|---|
| mDNS | Hostname `trashnet-cam.local` / `trashnet-act.local`; service `_trashnet._tcp`, TXT `role=cam\|actuator`, `id=<mac>`, `fw=<ver>`, `api=1` |
| UDP | Broadcast to `255.255.255.255:5005` every 30 s: `TRASHNET:<CAM\|ACTUATOR>:http://<ip>` |
| Heartbeat | `POST <server>/api/v1/devices/heartbeat` every 15 s: `{role,id,ip,fw,rssi,uptime,heap,state}` |

## Provisioning (SoftAP `TrashNet-CAM-XXXX` / `TrashNet-ACT-XXXX`, IP `192.168.4.1`)

`POST /wifi` body `{"ssid":"..","pass":"..","key":"<optional device key>"}` → `{"ok":true,"id":"<mac>"}`, device reboots into STA mode.
Hold BOOT 5 s to erase WiFi credentials and return to provisioning.

## ESP32-CAM

| Route | Port | Notes |
|---|---|---|
| `GET /status` | 80 | `{role:"cam", id, fw, ip, rssi, uptime, heap, stream_port:81, flash:bool, res, quality}` |
| `GET /capture?res=svga&q=10&flash=1&warmup=2` | 80 | `image/jpeg`. `warmup` drops N frames after flash on. |
| `GET /flash?on=1` | 80 | `{flash:bool}` (control) |
| `POST /cam?res=..&q=..&sharp=..&denoise=..` | 80 | Persist sensor tuning (control) |
| `GET /stream` | 81 | `multipart/x-mixed-replace` MJPEG; separate server so `/capture` never blocks |

`res`: `qvga`, `vga`, `svga`, `xga`. `q`: 4..63 (lower is better quality).

## Actuator

| Route | Notes |
|---|---|
| `GET /status` | See below |
| `POST /action` `{bin:1..4,label,name}` | `202 {job_id}` if idle, `409 {error:"busy",job_id}` if running |
| `POST /jog` `{dir:1\|-1,ms:int}` | Raw tray jog for calibration; job: 202 `{job_id}` or 409 busy |
| `POST /home/set` | Mark current tray position as Home (bin 1) |
| `GET /calib` / `POST /calib` | Read / write calibration (below), persisted in NVS |
| `POST /calib/neutral` `{us}` | Set tray neutral pulse (µs) while tuning |
| `POST /servo` `{dump:1}` / `{lid:"open\|close"}` / `{angle:n}` / `{lid_angle:n}` | Manual servo tests; job: 202 `{job_id}` or 409 busy |
| `POST /rotate` `{bin:1..4}` | Move tray only |

`GET /status`:

```json
{"role":"actuator","id":"..","fw":"..","state":"idle|moving|dumping|lid|error",
 "job_id":"a1b2","job_done":true,"step":"home","position":1,"cycles_since_home":3,
 "dist_full_cm":12.3,"dist_lid_cm":40.1,"bin1_full":false,"lid_detected":false,
 "needs_home":false,"tray_lost":false,"fill_cm":[12.3,9.0,0,0],"full":[false,false,false,false],
 "vbus_mv":0,"rssi":-55,"uptime":123,"heap":123456,"calib_ver":1}
```

`calib` object:

```json
{"neutral_us":1500,"delta_us":250,"t90_fwd_ms":950,"t90_rev_ms":1010,
 "t180_fwd_ms":1850,"t180_rev_ms":1955,"ramp_ms":0,"brake_ms":0,"brake_us":0,"bin4_dir":1,
 "trim_ms":[0,0,0,0],"dump_rest":4,"dump_target":87,"dump_fwd_ms":600,
 "dump_hold_ms":1500,"dump_rev_ms":600,"lid_rest":0,"lid_open":10,
 "lid_settle_ms":400,"full_cm":3.0,"lid_cm":3.0,"lid_auto":true}
```

## OTA

`POST /ota` with raw firmware binary (control). Device reboots on success.

## Layout and sort job (`POST /action`)

Four bins around the dump column. Tray angle from bin 1 (`+` = forward): **bin 1 = 0 (front), bin 2 = +90 (right), bin 3 = -90 (left), bin 4 = +180 (back)**.
(`bin4_dir = -1` reaches bin 4 by -180 instead.) A move is the difference of absolute angles, so the tray never turns more than half a turn
and the way back is always the reverse of the way out; the cable is never wound.

| Bin | Job |
|---|---|
| 1 | `lid_open` -> settle -> `dump` -> `lid_close` -> `measure` |
| 2 / 3 / 4 | `rotate` out -> settle -> `dump` -> `measure` -> `rotate` back (opposite direction). The lid is **not** used. |

`step` in `/status` shows the current phase. Every motion (sort, rotate, jog, servo tests, auto lid) is a job in one queue, so `/status` always answers and jobs never overlap.
CB1 `full[n]` = distance <= `full_cm` (default 3 cm); CB2 opens the lid (bin 1 only, tray at bin 1) when distance <= `lid_cm` (default 3 cm).
`needs_home` turns true after 20 sorts (open-loop drift). `tray_lost` turns true when power dropped during a move: sort/rotate answer `409 {"error":"needs_home"}` until
the tray is jogged to bin 1 and `POST /home/set` is called.

Tray servo (continuous rotation): after every move the firmware holds the neutral pulse for 60 ms, then **cuts the PWM completely**, like the original firmware. Holding a neutral pulse that is slightly off-centre makes the tray creep forever.
