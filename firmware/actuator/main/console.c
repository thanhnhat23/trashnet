#include "actuator.h"
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static void print_menu(void) {
    printf("\n");
    printf("=================================================================\n");
    printf("=== ESP32 ACTUATOR - MENU PHIM DIEU KHIEN SERIAL (MONITOR) ===\n");
    printf("=================================================================\n");
    printf("CHU TRINH PHAN LOAI:\n  Ngan 1: Mo nap -> Hat -> Dong nap | Ngan 2/3/4: Xoay toi -> Hat -> Xoay ve nguoc chieu (khong mo nap)\n");
    printf("  Phim 1, 2, 3, 4 : Chay tron chu trinh Ngan 1, 2, 3, 4\n");
    printf("-----------------------------------------------------------------\n");
    printf("TEST SERVO HAT RAC (G25) & SERVO MO NAP (G27):\n");
    printf("  Phim t          : Test chu trinh hat rac (Mo -> Doi -> Dong)\n");
    printf("  Phim o          : Test quay MO hat rac\n");
    printf("  Phim c          : Test quay DONG ve nghi hat rac\n");
    printf("  Phim p          : Test MO NAP\n");
    printf("  Phim q          : Test DONG NAP\n");
    printf("-----------------------------------------------------------------\n");
    printf("TEST QUAY MAM 360 (G26):\n");
    printf("  Bo tri: Ngan 1 truoc (0), Ngan 2 phai (+90), Ngan 3 trai (-90), Ngan 4 sau (180)\n");
    printf("  Phim y / h / g  : Quay toi Ngan 2 (+90) / Ngan 3 (-90) / Ngan 4 (180)\n");
    printf("  Phim b          : Quay VE Ngan 1 (nguoc chieu luc di)\n");
    printf("  Phim u / j      : Nhich tho mam -90 / +90 theo thoi gian (chi de chinh, khong doi vi tri luu)\n");
    printf("  Phim 0 (so 0)   : Dat vi tri hien tai lam Home (Ngan 1)\n");
    printf("  Phim x          : DUNG KHAN CAP / THA LONG (Tat PWM tat ca servo)\n");
    printf("-----------------------------------------------------------------\n");
    printf("CAN CHINH GOC & THOI GIAN (Tu dong luu Flash NVS):\n");
    printf("  Phim , hoac <   : GIAM goc dong nghi hat rac (-1 do / -5 do)\n");
    printf("  Phim . hoac >   : TANG goc dong nghi hat rac (+1 do / +5 do)\n");
    printf("  Phim k hoac K   : GIAM goc mo hat rac (-1 do / -5 do)\n");
    printf("  Phim l hoac L   : TANG goc mo hat rac (+1 do / +5 do)\n");
    printf("  Phim v hoac V   : TANG goc mo nap (+1 do / +5 do)\n");
    printf("  Phim z hoac Z   : GIAM goc mo nap (-1 do / -5 do)\n");
    printf("  Phim + hoac =   : TANG thoi gian quay mam 90 do (+10 ms)\n");
    printf("  Phim - hoac _   : GIAM thoi gian quay mam 90 do (-10 ms)\n");
    printf("  Phim [ hoac {   : GIAM thoi gian quay mo hat (-50 ms)\n");
    printf("  Phim ] hoac }   : TANG thoi gian quay mo hat (+50 ms)\n");
    printf("  Phim ; hoac :   : GIAM thoi gian quay dong hat (-50 ms)\n");
    printf("  Phim ' hoac \"   : TANG thoi gian quay dong hat (+50 ms)\n");
    printf("-----------------------------------------------------------------\n");
    printf("TIEN ICH:\n");
    printf("  Phim a          : BAT/TAT tu dong mo nap bang HC-SR04\n");
    printf("  Phim d          : BAT/TAT in log lien tuc 2 cam bien HC-SR04\n");
    printf("  Phim s          : In trang thai hien tai (Status / Sensors)\n");
    printf("  Phim m hoac ?   : Hien thi lai Menu tro giup nay\n");
    printf("=================================================================\n\n");
}

static void console_task(void *arg) {
    vTaskDelay(pdMS_TO_TICKS(1500));
    print_menu();
    while (1) {
        int c = getchar();
        if (c == '\r' || c == '\n' || c == EOF) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        char id[9];
        tn_calib_t cal = *act_calib();

        if (c >= '1' && c <= '4') {
            int bin = c - '0';
            printf("\n>>> [KEY %d] Kich hoat chu trinh Ngan %d\n", bin, bin);
            int rc = act_submit_sort(bin, id);
            if (rc != 0) printf("    [CANH BAO] He thong dang ban (job: %s)\n", id);
        } else if (c == 't' || c == 'T') {
            printf("\n>>> [KEY t] Test chu trinh hat rac (Mo -> Doi -> Dong)\n");
            int rc = act_submit_dump(id);
            if (rc != 0) printf("    [CANH BAO] He thong dang ban (job: %s)\n", id);
        } else if (c == 'o' || c == 'O') {
            printf("\n>>> [KEY o] Test quay MO goc hat rac (%d do)\n", cal.dump_target);
            act_submit_angle(CH_DUMP, cal.dump_target, id);
        } else if (c == 'c' || c == 'C') {
            printf("\n>>> [KEY c] Test quay DONG goc hat rac (%d do)\n", cal.dump_rest);
            act_submit_angle(CH_DUMP, cal.dump_rest, id);
        } else if (c == 'p' || c == 'P') {
            printf("\n>>> [KEY p] Test MO NAP (%d do)\n", cal.lid_open);
            int rc = act_submit_lid(true, id);
            if (rc != 0) printf("    [CANH BAO] He thong dang ban\n");
        } else if (c == 'q' || c == 'Q') {
            printf("\n>>> [KEY q] Test DONG NAP (%d do)\n", cal.lid_rest);
            int rc = act_submit_lid(false, id);
            if (rc != 0) printf("    [CANH BAO] He thong dang ban\n");
        } else if (c == 'y' || c == 'Y') {
            printf("\n>>> [KEY y] Quay toi Ngan 2 (+90 do)\n");
            act_submit_rotate(2, id);
        } else if (c == 'h' || c == 'H') {
            printf("\n>>> [KEY h] Quay toi Ngan 3 (-90 do)\n");
            act_submit_rotate(3, id);
        } else if (c == 'g' || c == 'G') {
            printf("\n>>> [KEY g] Quay toi Ngan 4 (180 do)\n");
            act_submit_rotate(4, id);
        } else if (c == 'b' || c == 'B') {
            printf("\n>>> [KEY b] Quay VE Ngan 1\n");
            act_submit_rotate(1, id);
        } else if (c == 'u' || c == 'U') {
            printf("\n>>> [KEY u] Nhich tho -90 do (-%d ms)\n", cal.tray.t90_rev_ms);
            act_submit_jog(-1, cal.tray.t90_rev_ms, id);
        } else if (c == 'j' || c == 'J') {
            printf("\n>>> [KEY j] Nhich tho +90 do (+%d ms)\n", cal.tray.t90_fwd_ms);
            act_submit_jog(1, cal.tray.t90_fwd_ms, id);
        } else if (c == '0') {
            act_set_home();
            printf("\n>>> [KEY 0] DA DAT VI TRI HIEN TAI LAM HOME (Ngan 1)\n");
        } else if (c == 'x' || c == 'X') {
            printf("\n>>> [KEY x] DUNG KHAN CAP / THA LONG TAT CA SERVO (Tat PWM)\n");
            servo_off(CH_DUMP); servo_off(CH_TRAY); servo_off(CH_LID);
        } else if (c == 's' || c == 'S') {
            tn_snapshot_t snap;
            act_snapshot(&snap);
            printf("\n>>> [KEY s] TRANG THAI ACTUATOR:\n");
            printf("    Vi tri: Ngan %d | Buoc: %s | Trang thai: %d | Mat vi tri: %s\n", snap.position, snap.step, snap.state, snap.tray_lost ? "CO (can Home)" : "khong");
            printf("    Lan quay cuoi: %d -> %d (%d do, %d ms)\n", snap.last_from, snap.last_to, snap.last_deg, snap.last_ms);
            printf("    CB1 Bao day (G19/G23): %.1f cm (%s) | CB2 Mo nap (G21/G22): %.1f cm (%s)\n",
                   snap.dist_full_cm10 / 10.0f, snap.bin1_full ? "DAY!" : "Chua day",
                   snap.dist_lid_cm10 / 10.0f, snap.lid_detected ? "PHAT HIEN" : "Trong");
        } else if (c == 'd' || c == 'D') {
            g_log_sr04 = !g_log_sr04;
            printf("\n>>> [KEY d] In log HC-SR04: %s\n", g_log_sr04 ? "BAT (DANG IN)" : "TAM DUNG (OFF)");
        } else if (c == 'a' || c == 'A') {
            cal.lid_auto = !cal.lid_auto;
            act_calib_set(&cal);
            printf("\n>>> [KEY a] Che do tu dong mo nap HC-SR04: %s (Da luu NVS)\n",
                   cal.lid_auto ? "BAT (ENABLED)" : "TAT (DISABLED)");
        } else if (c == 'v' || c == 'V') {
            int step = (c == 'V') ? 5 : 1;
            cal.lid_open += step;
            if (cal.lid_open > 180) cal.lid_open = 180;
            act_calib_set(&cal);
            printf("\n>>> [KEY v] TANG goc mo nap: %d do (Da luu NVS)\n", cal.lid_open);
            act_submit_angle(CH_LID, cal.lid_open, id);
        } else if (c == 'z' || c == 'Z') {
            int step = (c == 'Z') ? 5 : 1;
            cal.lid_open -= step;
            if (cal.lid_open < 0) cal.lid_open = 0;
            act_calib_set(&cal);
            printf("\n>>> [KEY z] GIAM goc mo nap: %d do (Da luu NVS)\n", cal.lid_open);
            act_submit_angle(CH_LID, cal.lid_open, id);
        } else if (c == ',' || c == '<') {
            int step = (c == '<') ? 5 : 1;
            cal.dump_rest -= step;
            if (cal.dump_rest < 0) cal.dump_rest = 0;
            act_calib_set(&cal);
            printf("\n>>> [KEY ,] GIAM goc dong nghi hat: %d do (Da luu NVS)\n", cal.dump_rest);
            act_submit_angle(CH_DUMP, cal.dump_rest, id);
        } else if (c == '.' || c == '>') {
            int step = (c == '>') ? 5 : 1;
            cal.dump_rest += step;
            if (cal.dump_rest > 180) cal.dump_rest = 180;
            act_calib_set(&cal);
            printf("\n>>> [KEY .] TANG goc dong nghi hat: %d do (Da luu NVS)\n", cal.dump_rest);
            act_submit_angle(CH_DUMP, cal.dump_rest, id);
        } else if (c == 'k' || c == 'K') {
            int step = (c == 'K') ? 5 : 1;
            cal.dump_target -= step;
            if (cal.dump_target < 0) cal.dump_target = 0;
            act_calib_set(&cal);
            printf("\n>>> [KEY k] GIAM goc mo hat rac: %d do (Da luu NVS)\n", cal.dump_target);
            act_submit_angle(CH_DUMP, cal.dump_target, id);
        } else if (c == 'l' || c == 'L') {
            int step = (c == 'L') ? 5 : 1;
            cal.dump_target += step;
            if (cal.dump_target > 180) cal.dump_target = 180;
            act_calib_set(&cal);
            printf("\n>>> [KEY l] TANG goc mo hat rac: %d do (Da luu NVS)\n", cal.dump_target);
            act_submit_angle(CH_DUMP, cal.dump_target, id);
        } else if (c == '+' || c == '=') {
            cal.tray.t90_fwd_ms += 10;
            cal.tray.t90_rev_ms += 10;
            act_calib_set(&cal);
            printf("\n>>> [KEY +] TANG thoi gian quay 90 do: %d ms (Da luu NVS)\n", cal.tray.t90_fwd_ms);
        } else if (c == '-' || c == '_') {
            cal.tray.t90_fwd_ms -= 10;
            cal.tray.t90_rev_ms -= 10;
            if (cal.tray.t90_fwd_ms < 100) cal.tray.t90_fwd_ms = 100;
            if (cal.tray.t90_rev_ms < 100) cal.tray.t90_rev_ms = 100;
            act_calib_set(&cal);
            printf("\n>>> [KEY -] GIAM thoi gian quay 90 do: %d ms (Da luu NVS)\n", cal.tray.t90_fwd_ms);
        } else if (c == '[' || c == '{') {
            cal.dump_fwd_ms -= 50;
            if (cal.dump_fwd_ms < 100) cal.dump_fwd_ms = 100;
            act_calib_set(&cal);
            printf("\n>>> [KEY [] GIAM thoi gian mo hat: %d ms (Da luu NVS)\n", cal.dump_fwd_ms);
        } else if (c == ']' || c == '}') {
            cal.dump_fwd_ms += 50;
            act_calib_set(&cal);
            printf("\n>>> [KEY ]] TANG thoi gian mo hat: %d ms (Da luu NVS)\n", cal.dump_fwd_ms);
        } else if (c == ';' || c == ':') {
            cal.dump_rev_ms -= 50;
            if (cal.dump_rev_ms < 100) cal.dump_rev_ms = 100;
            act_calib_set(&cal);
            printf("\n>>> [KEY ;] GIAM thoi gian dong hat: %d ms (Da luu NVS)\n", cal.dump_rev_ms);
        } else if (c == '\'' || c == '\"') {
            cal.dump_rev_ms += 50;
            act_calib_set(&cal);
            printf("\n>>> [KEY '] TANG thoi gian dong hat: %d ms (Da luu NVS)\n", cal.dump_rev_ms);
        } else if (c == 'm' || c == 'M' || c == '?') {
            print_menu();
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void console_start(void) {
    xTaskCreate(console_task, "act_console", 4096, NULL, 2, NULL);
}
