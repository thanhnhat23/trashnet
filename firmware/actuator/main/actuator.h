#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "motion_math.h"

/* Pins (unchanged from the original wiring). */
#define PIN_SERVO_DUMP 25   /* MG995, 0..180 deg */
#define PIN_SERVO_TRAY 26   /* MG996R, 360 deg continuous */
#define PIN_SERVO_LID  27
#define PIN_TRIG_FULL 19
#define PIN_ECHO_FULL 23
#define PIN_TRIG_LID  21
#define PIN_ECHO_LID  22

typedef struct {
    tn_tray_cal_t tray;
    int dump_rest, dump_target, dump_fwd_ms, dump_hold_ms, dump_rev_ms;
    int lid_rest, lid_open;
    int full_cm10, lid_cm10;   /* thresholds in 0.1 cm */
    int lid_settle_ms;         /* pause after the lid opens, before dumping */
    bool lid_auto;
    int version;
} tn_calib_t;

typedef enum { ST_IDLE, ST_MOVING, ST_DUMPING, ST_LID, ST_ERROR } tn_state_t;

typedef struct {
    tn_state_t state;
    char job_id[9];
    bool job_done;
    const char *step;
    int position, cycles_since_home;
    int last_from, last_to, last_deg, last_ms;   /* last tray move: what the firmware planned and how long it ran */
    int dist_full_cm10, dist_lid_cm10;
    int fill_cm10[5];          /* last CB1 reading per bin (index 1..4), 0 = unknown */
    bool full[5];              /* bin full flags (index 1..4) */
    bool bin1_full, lid_detected, needs_home, tray_lost;   /* tray_lost: position unknown, sorting blocked until Home is set */
} tn_snapshot_t;

/* Set from the serial console (key d): print both HC-SR04 readings every cycle. */
extern volatile bool g_log_sr04;

/* servo_drv */
void servo_init(void);
void servo_write_pulse(int ch, int us);
void servo_off(int ch);
enum { CH_DUMP = 0, CH_TRAY = 1, CH_LID = 2 };

/* actuator */
void act_start(void);
void act_snapshot(tn_snapshot_t *out);
const tn_calib_t *act_calib(void);
void act_calib_set(const tn_calib_t *c);      /* validates + persists */
int act_calib_to_json(char *buf, size_t n);
bool act_calib_from_json(const char *json);
/* -3 = tray position unknown (needs Home). All motions run in the job worker. Return 0 and fill job_id when accepted; -1 when busy (job_id = running job); -2 bad args. */
int act_submit_sort(int bin, char job_id[9]);    /* rotate -> lid open -> dump -> lid close -> measure -> back to bin 1 */
int act_submit_rotate(int bin, char job_id[9]);
int act_submit_dump(char job_id[9]);
int act_submit_lid(bool open, char job_id[9]);
int act_submit_jog(int dir, int ms, char job_id[9]);
int act_submit_angle(int ch, int angle, char job_id[9]);
void act_set_home(void);
void act_set_neutral(int us);
