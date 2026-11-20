#pragma once
/* Pure motion maths (no ESP-IDF dependency) so it can be unit-tested on the host. */
#include <stdint.h>

#define TN_ANGLE_MIN 0
#define TN_ANGLE_MAX 180

typedef struct {
    int neutral_us, delta_us;
    int t90_fwd_ms, t90_rev_ms, t180_fwd_ms, t180_rev_ms;   /* measured run times at full speed */
    int ramp_ms, brake_ms, brake_us;
    int bin4_dir;     /* +1: bin 4 is reached by +180 (via bin 2), -1: by -180 (via bin 3) */
    int trim_ms[4];   /* added to the last segment of a move that ends at that bin */
} tn_tray_cal_t;

typedef struct {
    int dir;          /* +1 forward, -1 reverse */
    int run_ms;       /* equivalent full-speed time; ramps add on top */
    int speed_pct;    /* 1..100 of delta_us */
} tn_segment_t;

/* Servo angle -> pulse width (us) for the position servos (MG995 / lid). */
int tn_angle_to_pulse_us(int angle);
int tn_clamp_angle(int angle);
uint32_t tn_pulse_to_duty(int pulse_us, int duty_bits);

/* Min-jerk interpolation, s in [0,1] -> [0,1]. */
float tn_minjerk(float s);
float tn_angle_at(float a0, float a1, int t_ms, int total_ms);

/* Tray (continuous-rotation servo): pulse at time t of a segment. total = run_ms + ramp_ms. */
int tn_segment_total_ms(const tn_tray_cal_t *c, const tn_segment_t *s);
int tn_segment_pulse_us(const tn_tray_cal_t *c, const tn_segment_t *s, int t_ms);
/* Brake pulse applied for brake_ms after the segment ends. */
int tn_brake_pulse_us(const tn_tray_cal_t *c, const tn_segment_t *s);

/* Layout around the dump column (angle from bin 1, + = forward): bin1 0, bin2 +90, bin3 -90, bin4 +/-180.
 * The tray never wraps: a move is the difference of absolute angles, so the way back is always the reverse of the way out
 * (no full turns, the cable is never wound). */
int tn_bin_deg(const tn_tray_cal_t *c, int bin);                 /* 0 on bad bin */
int tn_rotation_deg(const tn_tray_cal_t *c, int from_bin, int to_bin);   /* signed degrees, 0 = none */
/* Returns segment count (0 = no move, <0 = bad args). */
int tn_plan_rotation(const tn_tray_cal_t *c, int from_bin, int to_bin, tn_segment_t out[3]);

/* Ultrasonic filtering. */
int tn_median_int(const int *v, int n);
typedef struct { int state; int streak; } tn_hyst_t;
/* state=1 when dist_cm <= on_cm for `need` consecutive samples; releases above off_cm. Returns state. */
int tn_hyst_update(tn_hyst_t *h, int dist_cm10, int on_cm10, int off_cm10, int need);
