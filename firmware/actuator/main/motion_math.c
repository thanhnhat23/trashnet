#include "motion_math.h"

int tn_clamp_angle(int a) { return a < TN_ANGLE_MIN ? TN_ANGLE_MIN : (a > TN_ANGLE_MAX ? TN_ANGLE_MAX : a); }

int tn_angle_to_pulse_us(int angle) { return 500 + tn_clamp_angle(angle) * 1950 / 180; }

uint32_t tn_pulse_to_duty(int pulse_us, int duty_bits) {
    uint32_t max = (1u << duty_bits) - 1u;
    return (uint32_t)((uint64_t)pulse_us * max / 20000u);
}

float tn_minjerk(float s) {
    if (s <= 0.f) return 0.f;
    if (s >= 1.f) return 1.f;
    return s * s * s * (10.f + s * (-15.f + 6.f * s));
}

float tn_angle_at(float a0, float a1, int t_ms, int total_ms) {
    if (total_ms <= 0) return a1;
    return a0 + (a1 - a0) * tn_minjerk((float)t_ms / (float)total_ms);
}

int tn_segment_total_ms(const tn_tray_cal_t *c, const tn_segment_t *s) { return s->run_ms + c->ramp_ms; }

int tn_segment_pulse_us(const tn_tray_cal_t *c, const tn_segment_t *s, int t) {
    int total = tn_segment_total_ms(c, s);
    if (t < 0 || t >= total) return c->neutral_us;
    float f = 1.f;
    if (c->ramp_ms > 0) {
        float up = (float)t / c->ramp_ms, down = (float)(total - t) / c->ramp_ms;
        f = up < down ? up : down;
        if (f > 1.f) f = 1.f;
    }
    float amp = (float)c->delta_us * s->speed_pct / 100.f;
    return c->neutral_us + (int)(s->dir * amp * f);
}

int tn_brake_pulse_us(const tn_tray_cal_t *c, const tn_segment_t *s) { return c->neutral_us - s->dir * c->brake_us; }

int tn_bin_deg(const tn_tray_cal_t *c, int bin) {
    switch (bin) {
    case 1: return 0;
    case 2: return 90;
    case 3: return -90;
    case 4: return c->bin4_dir >= 0 ? 180 : -180;
    default: return 0;
    }
}

int tn_rotation_deg(const tn_tray_cal_t *c, int from, int to) {
    if (from < 1 || from > 4 || to < 1 || to > 4) return 0;
    return tn_bin_deg(c, to) - tn_bin_deg(c, from);
}

int tn_plan_rotation(const tn_tray_cal_t *c, int from, int to, tn_segment_t out[3]) {
    if (from < 1 || from > 4 || to < 1 || to > 4) return -1;
    int deg = tn_rotation_deg(c, from, to);
    if (deg == 0) return 0;
    int dir = deg > 0 ? 1 : -1, left = deg < 0 ? -deg : deg, n = 0;
    while (left > 0 && n < 3) {   /* 180 + 90 for the 270 deg moves between bin 3 and bin 4 */
        int part = left >= 180 ? 180 : 90;
        int ms = part == 180 ? (dir > 0 ? c->t180_fwd_ms : c->t180_rev_ms) : (dir > 0 ? c->t90_fwd_ms : c->t90_rev_ms);
        out[n++] = (tn_segment_t){dir, ms < 1 ? 1 : ms, 100};
        left -= part;
    }
    int ms = out[n - 1].run_ms + c->trim_ms[to - 1];
    out[n - 1].run_ms = ms < 1 ? 1 : ms;
    return n;
}

int tn_median_int(const int *v, int n) {
    int b[15];
    if (n <= 0) return 0;
    if (n > 15) n = 15;
    for (int i = 0; i < n; i++) {
        int j = i;
        while (j > 0 && b[j - 1] > v[i]) { b[j] = b[j - 1]; j--; }
        b[j] = v[i];
    }
    return b[n / 2];
}

int tn_hyst_update(tn_hyst_t *h, int d, int on, int off, int need) {
    if (d <= 0) return h->state;            /* invalid sample: keep state */
    if (!h->state) {
        h->streak = (d <= on) ? h->streak + 1 : 0;
        if (h->streak >= need) { h->state = 1; h->streak = 0; }
    } else if (d > off) {
        h->streak++;
        if (h->streak >= need) { h->state = 0; h->streak = 0; }
    } else {
        h->streak = 0;
    }
    return h->state;
}
