#include "actuator.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "cJSON.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "tn_common.h"

static const char *TAG = "act";
#define HOME_EVERY_CYCLES 20
#define HYST_RELEASE_CM10 15   /* release 1.5 cm above the trigger distance */
#define AUTO_LID_QUIET_MS 2000
enum { K_SORT, K_ROTATE, K_DUMP, K_LID, K_JOG, K_ANGLE, K_AUTO_LID };

static tn_calib_t g_cal = {
    .tray = {.neutral_us = 1500, .delta_us = 250, .t90_fwd_ms = 950, .t90_rev_ms = 1010, .t180_fwd_ms = 1850, .t180_rev_ms = 1955,
             .ramp_ms = 0, .brake_ms = 0, .brake_us = 0, .bin4_dir = 1, .trim_ms = {0, 0, 0, 0}},
    .dump_rest = 4, .dump_target = 87, .dump_fwd_ms = 600, .dump_hold_ms = 1500, .dump_rev_ms = 600,
    .lid_rest = 10, .lid_open = 0,   /* closed 10 deg, open 0 deg: as measured on the original firmware */ .full_cm10 = 30, .lid_cm10 = 30, .lid_settle_ms = 400, .lid_auto = true, .version = 1,
};
static SemaphoreHandle_t g_lock;
static tn_snapshot_t g_snap = {.position = 1, .step = "home", .job_done = true};
static QueueHandle_t g_q;
typedef struct { int kind, bin, a, b; char id[9]; } job_t;
static int64_t g_last_end_ms;

/* ---------- calibration persistence ---------- */
int act_calib_to_json(char *b, size_t n) {
    const tn_calib_t *c = &g_cal;
    return snprintf(b, n,
        "{\"neutral_us\":%d,\"delta_us\":%d,\"t90_fwd_ms\":%d,\"t90_rev_ms\":%d,\"t180_fwd_ms\":%d,\"t180_rev_ms\":%d,"
        "\"ramp_ms\":%d,\"brake_ms\":%d,\"brake_us\":%d,\"bin4_dir\":%d,\"trim_ms\":[%d,%d,%d,%d],\"dump_rest\":%d,\"dump_target\":%d,\"dump_fwd_ms\":%d,\"dump_hold_ms\":%d,"
        "\"dump_rev_ms\":%d,\"lid_rest\":%d,\"lid_open\":%d,\"full_cm\":%.1f,\"lid_cm\":%.1f,\"lid_settle_ms\":%d,\"lid_auto\":%s}",
        c->tray.neutral_us, c->tray.delta_us, c->tray.t90_fwd_ms, c->tray.t90_rev_ms, c->tray.t180_fwd_ms, c->tray.t180_rev_ms,
        c->tray.ramp_ms, c->tray.brake_ms, c->tray.brake_us, c->tray.bin4_dir, c->tray.trim_ms[0], c->tray.trim_ms[1], c->tray.trim_ms[2], c->tray.trim_ms[3],
        c->dump_rest, c->dump_target, c->dump_fwd_ms, c->dump_hold_ms, c->dump_rev_ms, c->lid_rest, c->lid_open,
        c->full_cm10 / 10.0, c->lid_cm10 / 10.0, c->lid_settle_ms, c->lid_auto ? "true" : "false");
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

static void geti(const cJSON *j, const char *k, int *dst, int lo, int hi) {
    const cJSON *v = cJSON_GetObjectItem(j, k);
    if (cJSON_IsNumber(v)) *dst = clampi((int)v->valuedouble, lo, hi);
}

bool act_calib_from_json(const char *json) {
    cJSON *j = cJSON_Parse(json);
    if (!j) return false;
    tn_calib_t c = g_cal;
    geti(j, "neutral_us", &c.tray.neutral_us, 1300, 1700);
    geti(j, "delta_us", &c.tray.delta_us, 50, 500);
    geti(j, "t90_fwd_ms", &c.tray.t90_fwd_ms, 100, 5000);
    geti(j, "t90_rev_ms", &c.tray.t90_rev_ms, 100, 5000);
    geti(j, "t180_fwd_ms", &c.tray.t180_fwd_ms, 200, 9000);
    geti(j, "t180_rev_ms", &c.tray.t180_rev_ms, 200, 9000);
    geti(j, "ramp_ms", &c.tray.ramp_ms, 0, 500);
    geti(j, "brake_ms", &c.tray.brake_ms, 0, 200);
    geti(j, "brake_us", &c.tray.brake_us, 0, 200);
    geti(j, "bin4_dir", &c.tray.bin4_dir, -1, 1);
    if (c.tray.bin4_dir == 0) c.tray.bin4_dir = 1;
    const cJSON *tr = cJSON_GetObjectItem(j, "trim_ms");
    for (int i = 0; cJSON_IsArray(tr) && i < 4 && i < cJSON_GetArraySize(tr); i++) c.tray.trim_ms[i] = clampi((int)cJSON_GetArrayItem(tr, i)->valuedouble, -400, 400);
    geti(j, "dump_rest", &c.dump_rest, TN_ANGLE_MIN, TN_ANGLE_MAX);
    geti(j, "dump_target", &c.dump_target, TN_ANGLE_MIN, TN_ANGLE_MAX);
    geti(j, "dump_fwd_ms", &c.dump_fwd_ms, 100, 3000);
    geti(j, "dump_hold_ms", &c.dump_hold_ms, 100, 8000);
    geti(j, "dump_rev_ms", &c.dump_rev_ms, 100, 3000);
    geti(j, "lid_settle_ms", &c.lid_settle_ms, 0, 2000);
    geti(j, "lid_rest", &c.lid_rest, TN_ANGLE_MIN, TN_ANGLE_MAX);
    geti(j, "lid_open", &c.lid_open, TN_ANGLE_MIN, TN_ANGLE_MAX);
    const cJSON *f = cJSON_GetObjectItem(j, "full_cm"), *l = cJSON_GetObjectItem(j, "lid_cm"), *a = cJSON_GetObjectItem(j, "lid_auto");
    if (cJSON_IsNumber(f)) c.full_cm10 = clampi((int)(f->valuedouble * 10), 25, 4000);   /* HC-SR04 blind zone ~2 cm */
    if (cJSON_IsNumber(l)) c.lid_cm10 = clampi((int)(l->valuedouble * 10), 25, 4000);
    if (cJSON_IsBool(a)) c.lid_auto = cJSON_IsTrue(a);
    cJSON_Delete(j);
    act_calib_set(&c);
    return true;
}

void act_calib_set(const tn_calib_t *c) {
    xSemaphoreTake(g_lock, portMAX_DELAY);
    g_cal = *c;
    g_cal.version++;
    xSemaphoreGive(g_lock);
    char b[768];
    act_calib_to_json(b, sizeof b);
    tn_nvs_set_str("calib2", b);   /* v2 layout: old "calib" values (other delta/ramp/times) are ignored */
}

const tn_calib_t *act_calib(void) { return &g_cal; }

static void load_calib(void) {
    char b[768];
    if (tn_nvs_get_str("calib2", b, sizeof b) == ESP_OK) act_calib_from_json(b);
}

/* ---------- state helpers ---------- */
static void set_state(tn_state_t s, const char *step) {
    xSemaphoreTake(g_lock, portMAX_DELAY);
    g_snap.state = s;
    g_snap.step = step;
    xSemaphoreGive(g_lock);
}

void act_snapshot(tn_snapshot_t *o) {
    xSemaphoreTake(g_lock, portMAX_DELAY);
    *o = g_snap;
    xSemaphoreGive(g_lock);
}

static void new_id(char id[9]) { snprintf(id, 9, "%08x", (unsigned)esp_random()); }

/* ---------- servo motions ---------- */
static void run_segment(const tn_segment_t *s) {
    const tn_tray_cal_t *c = &g_cal.tray;
    int total = tn_segment_total_ms(c, s);
    if (c->ramp_ms <= 0) {   /* constant speed: time it with the hardware timer, no tick rounding */
        servo_write_pulse(CH_TRAY, tn_segment_pulse_us(c, s, 0));
        int64_t start = esp_timer_get_time(), target = (int64_t)total * 1000;
        for (;;) {
            int64_t left = target - (esp_timer_get_time() - start);
            if (left <= 0) break;
            if (left > 25000) vTaskDelay(pdMS_TO_TICKS(15));
            else { esp_rom_delay_us((uint32_t)left); break; }
        }
    } else {
        for (int t = 0; t < total; t += 10) {
            servo_write_pulse(CH_TRAY, tn_segment_pulse_us(c, s, t));
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
    servo_write_pulse(CH_TRAY, tn_brake_pulse_us(c, s));
    vTaskDelay(pdMS_TO_TICKS(c->brake_ms));
    servo_write_pulse(CH_TRAY, c->neutral_us);
    vTaskDelay(pdMS_TO_TICKS(60));  /* settle */
    servo_off(CH_TRAY);             /* no PWM at rest: a continuous servo that is slightly off-centre would creep forever */
}

/* Smooth move in pulse width (not whole degrees): ~1 us resolution, 20 ms = one PWM frame per step. */
static void move_pos(int a0, int a1, int ms, int ch) {
    int p0 = tn_angle_to_pulse_us(a0), p1 = tn_angle_to_pulse_us(a1);
    servo_write_pulse(ch, p0);
    vTaskDelay(pdMS_TO_TICKS(40));   /* PWM was off: let the servo lock on before moving */
    for (int t = 20; t < ms; t += 20) {
        servo_write_pulse(ch, p0 + (int)((p1 - p0) * tn_minjerk((float)t / (float)ms) + (p1 >= p0 ? 0.5f : -0.5f)));
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    servo_write_pulse(ch, p1);
}

static void dump_cycle(void) {
    const tn_calib_t *c = &g_cal;
    set_state(ST_DUMPING, "dump");
    move_pos(c->dump_rest, c->dump_target, c->dump_fwd_ms, CH_DUMP);
    vTaskDelay(pdMS_TO_TICKS(c->dump_hold_ms));
    move_pos(c->dump_target, c->dump_rest, c->dump_rev_ms, CH_DUMP);
    vTaskDelay(pdMS_TO_TICKS(150));
    servo_off(CH_DUMP);
}

static void lid_move(bool open) {
    const tn_calib_t *c = &g_cal;
    set_state(ST_LID, open ? "lid_open" : "lid_close");
    move_pos(open ? c->lid_rest : c->lid_open, open ? c->lid_open : c->lid_rest, 500, CH_LID);
    vTaskDelay(pdMS_TO_TICKS(150));
    servo_off(CH_LID);
}

static void goto_bin(int bin) {
    tn_segment_t seg[3];
    int from = g_snap.position;
    int n = tn_plan_rotation(&g_cal.tray, from, bin, seg);
    if (n <= 0) return;
    set_state(ST_MOVING, "rotate");
    tn_nvs_set_str("tray_pos", "?");   /* if power drops mid-move the position is unknown at next boot */
    int64_t t0 = esp_timer_get_time();
    for (int i = 0; i < n; i++) run_segment(&seg[i]);
    xSemaphoreTake(g_lock, portMAX_DELAY);
    g_snap.last_from = from; g_snap.last_to = bin; g_snap.last_deg = tn_rotation_deg(&g_cal.tray, from, bin);
    g_snap.last_ms = (int)((esp_timer_get_time() - t0) / 1000);
    g_snap.position = bin;
    xSemaphoreGive(g_lock);
    char p[2] = {(char)('0' + bin), 0};
    tn_nvs_set_str("tray_pos", p);
}

/* ---------- ultrasonic ---------- */
static int read_cm10(int trig, int echo) {
    gpio_set_level(trig, 0); esp_rom_delay_us(2);
    gpio_set_level(trig, 1); esp_rom_delay_us(10);
    gpio_set_level(trig, 0);
    int64_t t0 = esp_timer_get_time();
    while (gpio_get_level(echo) == 0) if (esp_timer_get_time() - t0 > 20000) return 0;
    int64_t s = esp_timer_get_time();
    while (gpio_get_level(echo) == 1) if (esp_timer_get_time() - s > 25000) return 0;
    return (int)((esp_timer_get_time() - s) * 10 / 58);
}

static int median_cm10(int trig, int echo) {
    int v[5], n = 0;
    for (int i = 0; i < 5; i++) {
        int d = read_cm10(trig, echo);
        if (d >= 20) v[n++] = d;
        vTaskDelay(pdMS_TO_TICKS(30));
    }
    return n >= 3 ? tn_median_int(v, n) : 0;
}

volatile bool g_log_sr04 = false;
static tn_hyst_t g_full_h[5], g_lid_h;
static int submit(int kind, int bin, int a, int b, char id[9]);

/* Idle-time sensing: CB1 refreshes the fill state of the bin under it, CB2 asks for the lid. */
static void sensor_task(void *arg) {
    gpio_config_t out = {.pin_bit_mask = (1ULL << PIN_TRIG_FULL) | (1ULL << PIN_TRIG_LID), .mode = GPIO_MODE_OUTPUT};
    gpio_config_t in = {.pin_bit_mask = (1ULL << PIN_ECHO_FULL) | (1ULL << PIN_ECHO_LID), .mode = GPIO_MODE_INPUT};
    gpio_config(&out); gpio_config(&in);
    for (;;) {
        if (g_snap.state == ST_IDLE) {   /* servo PWM noise corrupts echoes while anything moves */
            int df = median_cm10(PIN_TRIG_FULL, PIN_ECHO_FULL);
            int dl = median_cm10(PIN_TRIG_LID, PIN_ECHO_LID);
            int pos = g_snap.position;
            if (g_log_sr04) printf("[HC-SR04] CB1 (day) %.1f cm | CB2 (nap) %.1f cm | ngan %d\n", df / 10.0f, dl / 10.0f, pos);
            int f = tn_hyst_update(&g_full_h[pos], df, g_cal.full_cm10, g_cal.full_cm10 + HYST_RELEASE_CM10, 3);
            int l = tn_hyst_update(&g_lid_h, dl, g_cal.lid_cm10, g_cal.lid_cm10 + HYST_RELEASE_CM10, 2);
            xSemaphoreTake(g_lock, portMAX_DELAY);
            g_snap.dist_full_cm10 = df; g_snap.dist_lid_cm10 = dl;
            if (df > 0) g_snap.fill_cm10[pos] = df;
            g_snap.full[pos] = f;
            g_snap.bin1_full = g_snap.full[1];
            g_snap.lid_detected = l;
            xSemaphoreGive(g_lock);
            int64_t now = esp_timer_get_time() / 1000;
            if (l && g_cal.lid_auto && pos == 1 && now - g_last_end_ms > AUTO_LID_QUIET_MS) {   /* the lid belongs to bin 1 */
                char id[9];
                submit(K_AUTO_LID, 1, 0, 0, id);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}

/* ---------- job worker ---------- */
static void auto_lid(void) {
    lid_move(true);
    vTaskDelay(pdMS_TO_TICKS(2500));
    int clear = 0;
    for (int i = 0; i < 20 && clear < 3; i++) {   /* wait until the hand is gone, max ~10 s */
        int d = median_cm10(PIN_TRIG_LID, PIN_ECHO_LID);
        clear = (d == 0 || d > g_cal.lid_cm10 + HYST_RELEASE_CM10) ? clear + 1 : 0;
        vTaskDelay(pdMS_TO_TICKS(300));
    }
    lid_move(false);
    g_lid_h.state = 0; g_lid_h.streak = 0;
}

/* Re-measure the bin that is under CB1 right after it was emptied/filled. */
static void measure_here(void) {
    set_state(ST_MOVING, "measure");
    vTaskDelay(pdMS_TO_TICKS(200));
    int d = median_cm10(PIN_TRIG_FULL, PIN_ECHO_FULL);
    int pos = g_snap.position;
    xSemaphoreTake(g_lock, portMAX_DELAY);
    if (d > 0) g_snap.fill_cm10[pos] = d;
    g_snap.full[pos] = tn_hyst_update(&g_full_h[pos], d, g_cal.full_cm10, g_cal.full_cm10 + HYST_RELEASE_CM10, 1);
    g_snap.bin1_full = g_snap.full[1];
    xSemaphoreGive(g_lock);
}

/* Bin 1 (front): lid open -> dump -> lid close. Bins 2/3/4: turn out, dump, turn back the opposite way (cable never winds). */
static void run_sort(int bin) {
    if (bin == 1) {
        lid_move(true);
        vTaskDelay(pdMS_TO_TICKS(g_cal.lid_settle_ms));
        dump_cycle();
        lid_move(false);
        measure_here();
    } else {
        goto_bin(bin);
        vTaskDelay(pdMS_TO_TICKS(g_cal.lid_settle_ms));
        dump_cycle();
        measure_here();
        goto_bin(1);
    }
    xSemaphoreTake(g_lock, portMAX_DELAY);
    if (++g_snap.cycles_since_home >= HOME_EVERY_CYCLES) g_snap.needs_home = true;   /* open-loop drift: ask for a re-home */
    xSemaphoreGive(g_lock);
}

static void worker(void *arg) {
    job_t j;
    for (;;) {
        if (xQueueReceive(g_q, &j, portMAX_DELAY) != pdTRUE) continue;
        switch (j.kind) {
        case K_SORT: run_sort(j.bin); break;
        case K_ROTATE: goto_bin(j.bin); break;
        case K_DUMP: dump_cycle(); break;
        case K_LID: lid_move(j.a); break;
        case K_JOG: {
            tn_segment_t s = {j.a >= 0 ? 1 : -1, clampi(j.b, 10, 1500), 100};
            set_state(ST_MOVING, "jog");
            run_segment(&s);
            break;
        }
        case K_ANGLE:
            set_state(ST_MOVING, "servo");
            servo_write_pulse(j.a, tn_angle_to_pulse_us(j.b));
            vTaskDelay(pdMS_TO_TICKS(600));
            servo_off(j.a);
            break;
        case K_AUTO_LID: auto_lid(); break;
        }
        xSemaphoreTake(g_lock, portMAX_DELAY);
        g_snap.job_done = true;
        xSemaphoreGive(g_lock);
        g_last_end_ms = esp_timer_get_time() / 1000;
        set_state(ST_IDLE, "idle");
    }
}

static int submit(int kind, int bin, int a, int b, char id[9]) {
    if (kind <= K_ROTATE && (bin < 1 || bin > 4)) return -2;
    if (kind <= K_ROTATE && g_snap.tray_lost) return -3;
    xSemaphoreTake(g_lock, portMAX_DELAY);
    if (!g_snap.job_done || g_snap.state != ST_IDLE) {
        strcpy(id, g_snap.job_id);
        xSemaphoreGive(g_lock);
        return -1;
    }
    job_t j = {.kind = kind, .bin = bin, .a = a, .b = b};
    new_id(j.id);
    strcpy(g_snap.job_id, j.id);
    g_snap.job_done = false;
    g_snap.state = ST_MOVING;
    xSemaphoreGive(g_lock);
    strcpy(id, j.id);
    xQueueSend(g_q, &j, 0);
    return 0;
}

int act_submit_sort(int bin, char id[9]) { return submit(K_SORT, bin, 0, 0, id); }
int act_submit_rotate(int bin, char id[9]) { return submit(K_ROTATE, bin, 0, 0, id); }
int act_submit_dump(char id[9]) { return submit(K_DUMP, 1, 0, 0, id); }
int act_submit_lid(bool open, char id[9]) { return submit(K_LID, 1, open, 0, id); }
int act_submit_jog(int dir, int ms, char id[9]) { return submit(K_JOG, 1, dir, ms, id); }
int act_submit_angle(int ch, int angle, char id[9]) { return submit(K_ANGLE, 1, ch, angle, id); }

/* ---------- calibration helpers ---------- */
void act_set_home(void) {
    xSemaphoreTake(g_lock, portMAX_DELAY);
    g_snap.position = 1; g_snap.cycles_since_home = 0; g_snap.needs_home = false; g_snap.tray_lost = false;
    xSemaphoreGive(g_lock);
    tn_nvs_set_str("tray_pos", "1");
}
/* Calibration aid: hold the pulse ~1.2 s so the drift can be seen, then cut the PWM so the tray cannot creep. */
void act_set_neutral(int us) {
    if (g_snap.state != ST_IDLE) return;
    servo_write_pulse(CH_TRAY, clampi(us, 1300, 1700));
    vTaskDelay(pdMS_TO_TICKS(1200));
    servo_off(CH_TRAY);
}

void act_start(void) {
    g_lock = xSemaphoreCreateMutex();
    g_q = xQueueCreate(2, sizeof(job_t));
    servo_init();
    servo_write_pulse(CH_TRAY, 1500);
    load_calib();
    char tp[8];
    if (tn_nvs_get_str("tray_pos", tp, sizeof tp) != ESP_OK || tp[0] == '?') {
        g_snap.tray_lost = g_snap.needs_home = true;   /* first boot or reset during a move: the user must align the tray and set Home */
    }
    servo_write_pulse(CH_TRAY, g_cal.tray.neutral_us);
    servo_write_pulse(CH_DUMP, tn_angle_to_pulse_us(g_cal.dump_rest));
    servo_write_pulse(CH_LID, tn_angle_to_pulse_us(g_cal.lid_rest));
    vTaskDelay(pdMS_TO_TICKS(600));
    servo_off(CH_DUMP); servo_off(CH_LID); servo_off(CH_TRAY);
    xTaskCreate(worker, "act_job", 4096, NULL, 5, NULL);
    xTaskCreate(sensor_task, "act_us", 4096, NULL, 3, NULL);
    ESP_LOGI(TAG, "actuator ready");
}
