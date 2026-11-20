#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "../main/motion_math.h"

static tn_tray_cal_t cal(void) {
    tn_tray_cal_t c = {.neutral_us = 1500, .delta_us = 250, .t90_fwd_ms = 950, .t90_rev_ms = 1010, .t180_fwd_ms = 1850, .t180_rev_ms = 1955,
                       .ramp_ms = 100, .brake_ms = 30, .brake_us = 60, .bin4_dir = 1, .trim_ms = {0, 0, 0, 0}};
    return c;
}

/* Integrate speed (fraction of full) over a segment, 1 ms steps. */
static double travel(const tn_tray_cal_t *c, const tn_segment_t *s) {
    double d = 0; int total = tn_segment_total_ms(c, s);
    for (int t = 0; t < total; t++) d += (tn_segment_pulse_us(c, s, t) - c->neutral_us) / (double)c->delta_us;
    return d;  /* in full-speed-ms, signed */
}

int main(void) {
    tn_tray_cal_t c = cal();
    assert(tn_angle_to_pulse_us(0) == 500);                            /* full 0..180 range */
    assert(tn_angle_to_pulse_us(90) == 500 + 90 * 1950 / 180);
    assert(tn_angle_to_pulse_us(999) == tn_angle_to_pulse_us(180));
    assert(tn_pulse_to_duty(20000, 16) == 65535);
    assert(tn_pulse_to_duty(1500, 16) == 4915);

    assert(tn_minjerk(0) == 0 && tn_minjerk(1) == 1 && fabsf(tn_minjerk(0.5f) - 0.5f) < 1e-6);
    float prev = 0; for (int i = 1; i <= 100; i++) { float v = tn_minjerk(i / 100.f); assert(v >= prev); prev = v; }
    assert(fabsf(tn_angle_at(4, 87, 0, 600) - 4) < 1e-4 && fabsf(tn_angle_at(4, 87, 600, 600) - 87) < 1e-4);

    /* ramp: starts and ends at neutral, travel equals run_ms (ramps compensated) */
    tn_segment_t s = {+1, 950, 100};
    assert(tn_segment_pulse_us(&c, &s, 0) == c.neutral_us);
    assert(tn_segment_pulse_us(&c, &s, tn_segment_total_ms(&c, &s)) == c.neutral_us);
    assert(fabs(travel(&c, &s) - 950) < 2.0);
    assert(tn_brake_pulse_us(&c, &s) == 1440);
    c.ramp_ms = 0;                                                     /* constant speed, default */
    tn_segment_t k = {-1, 1010, 100};
    assert(tn_segment_pulse_us(&c, &k, 0) == 1250 && tn_segment_pulse_us(&c, &k, 1009) == 1250);
    c.ramp_ms = 100;

    /* layout: bin1 0, bin2 +90, bin3 -90, bin4 +180 */
    tn_segment_t p[3];
    assert(tn_plan_rotation(&c, 1, 1, p) == 0);
    assert(tn_plan_rotation(&c, 1, 5, p) < 0);
    assert(tn_plan_rotation(&c, 1, 2, p) == 1 && p[0].dir == +1 && p[0].run_ms == 950);
    assert(tn_plan_rotation(&c, 1, 3, p) == 1 && p[0].dir == -1 && p[0].run_ms == 1010);   /* bin 3 = -90, one short reverse move */
    assert(tn_plan_rotation(&c, 1, 4, p) == 1 && p[0].dir == +1 && p[0].run_ms == 1850);
    assert(tn_plan_rotation(&c, 2, 1, p) == 1 && p[0].dir == -1 && p[0].run_ms == 1010);   /* back = reverse of out */
    assert(tn_plan_rotation(&c, 3, 1, p) == 1 && p[0].dir == +1 && p[0].run_ms == 950);
    assert(tn_plan_rotation(&c, 4, 1, p) == 1 && p[0].dir == -1 && p[0].run_ms == 1955);
    assert(tn_plan_rotation(&c, 3, 4, p) == 2 && p[0].dir == +1 && p[1].dir == +1);        /* 270 forward through home */

    /* every pair: segments add up to the absolute angle difference; out-and-back is always zero net turn */
    for (int dirsel = 1; dirsel >= -1; dirsel -= 2) {
        c.bin4_dir = dirsel;
        for (int a = 1; a <= 4; a++) for (int b = 1; b <= 4; b++) {
            int n2 = tn_plan_rotation(&c, a, b, p), deg = 0;
            for (int i2 = 0; i2 < n2; i2++) {
                int part = (p[i2].run_ms == c.t180_fwd_ms || p[i2].run_ms == c.t180_rev_ms) ? 180 : 90;
                deg += p[i2].dir * part;
            }
            assert(deg == tn_rotation_deg(&c, a, b));
            assert(deg == tn_bin_deg(&c, b) - tn_bin_deg(&c, a));
        }
        for (int b = 2; b <= 4; b++) {
            int out_deg = tn_rotation_deg(&c, 1, b), back_deg = tn_rotation_deg(&c, b, 1);
            assert(out_deg + back_deg == 0 && out_deg * back_deg < 0);                   /* opposite directions */
            assert(tn_bin_deg(&c, b) >= -180 && tn_bin_deg(&c, b) <= 180);               /* never beyond half a turn */
        }
    }
    c.bin4_dir = 1;
    assert(tn_bin_deg(&c, 3) == -90 && tn_bin_deg(&c, 2) == 90 && tn_bin_deg(&c, 4) == 180);
    c.trim_ms[1] = 25;
    tn_plan_rotation(&c, 1, 2, p); assert(p[0].run_ms == 975);

    int v[5] = {40, 41, 300, 39, 42};
    assert(tn_median_int(v, 5) == 41);                                  /* outlier rejected */
    tn_hyst_t h = {0, 0};
    assert(tn_hyst_update(&h, 30, 40, 60, 3) == 0);                     /* need 3 hits */
    tn_hyst_update(&h, 30, 40, 60, 3); assert(tn_hyst_update(&h, 30, 40, 60, 3) == 1);
    assert(tn_hyst_update(&h, 50, 40, 60, 3) == 1);                     /* inside band: stays on */
    assert(tn_hyst_update(&h, 0, 40, 60, 3) == 1);                      /* invalid sample ignored */
    tn_hyst_update(&h, 80, 40, 60, 3); tn_hyst_update(&h, 80, 40, 60, 3);
    assert(tn_hyst_update(&h, 80, 40, 60, 3) == 0);
    puts("motion_math: all host tests passed");
    return 0;
}
