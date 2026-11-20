#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "actuator.h"
#include "cJSON.h"
#include "esp_http_server.h"
#include "tn_common.h"

static const char *state_name(tn_state_t s) {
    static const char *n[] = {"idle", "moving", "dumping", "lid", "error"};
    return n[s];
}

int actuator_status_extra(char *b, size_t n) {
    tn_snapshot_t s; act_snapshot(&s);
    return snprintf(b, n, ",\"state\":\"%s\"", state_name(s.state));
}

static esp_err_t status_get(httpd_req_t *r) {
    tn_snapshot_t s; act_snapshot(&s);
    char c[200], b[1100];
    tn_status_common(c, sizeof c);
    snprintf(b, sizeof b,
        "{%s,\"state\":\"%s\",\"job_id\":\"%s\",\"job_done\":%s,\"step\":\"%s\",\"position\":%d,\"cycles_since_home\":%d,"
        "\"dist_full_cm\":%.1f,\"dist_lid_cm\":%.1f,\"bin1_full\":%s,\"lid_detected\":%s,\"needs_home\":%s,\"tray_lost\":%s,\"last_move\":{\"from\":%d,\"to\":%d,\"deg\":%d,\"ms\":%d},\"fill_cm\":[%.1f,%.1f,%.1f,%.1f],\"full\":[%s,%s,%s,%s],\"vbus_mv\":0,\"calib_ver\":%d}",
        c, state_name(s.state), s.job_id, s.job_done ? "true" : "false", s.step, s.position, s.cycles_since_home,
        s.dist_full_cm10 / 10.0, s.dist_lid_cm10 / 10.0, s.bin1_full ? "true" : "false", s.lid_detected ? "true" : "false", s.needs_home ? "true" : "false", s.tray_lost ? "true" : "false",
        s.last_from, s.last_to, s.last_deg, s.last_ms,
        s.fill_cm10[1] / 10.0, s.fill_cm10[2] / 10.0, s.fill_cm10[3] / 10.0, s.fill_cm10[4] / 10.0,
        s.full[1] ? "true" : "false", s.full[2] ? "true" : "false", s.full[3] ? "true" : "false", s.full[4] ? "true" : "false",
        act_calib()->version);
    return tn_send_json(r, b);
}

static cJSON *body_json(httpd_req_t *r) {
    char b[400];
    int n = tn_read_body(r, b, sizeof b);
    return n > 0 ? cJSON_Parse(b) : NULL;
}

static int jint(cJSON *j, const char *k, int def) {
    const cJSON *v = j ? cJSON_GetObjectItem(j, k) : NULL;
    return cJSON_IsNumber(v) ? (int)v->valuedouble : def;
}

static esp_err_t job_reply(httpd_req_t *r, int rc, const char *id) {
    char b[64];
    if (rc == 0) { snprintf(b, sizeof b, "{\"job_id\":\"%s\"}", id); httpd_resp_set_status(r, "202 Accepted"); return tn_send_json(r, b); }
    if (rc == -1) { snprintf(b, sizeof b, "{\"error\":\"busy\",\"job_id\":\"%s\"}", id); httpd_resp_set_status(r, "409 Conflict"); return tn_send_json(r, b); }
    if (rc == -3) { httpd_resp_set_status(r, "409 Conflict"); return tn_send_json(r, "{\"error\":\"needs_home\"}"); }
    return tn_send_err(r, "400 Bad Request", "args");
}

static esp_err_t action_post(httpd_req_t *r) {
    if (!tn_auth_ok(r)) return ESP_OK;
    cJSON *j = body_json(r);
    char id[9] = "";
    int rc = act_submit_sort(jint(j, "bin", 0), id);
    cJSON_Delete(j);
    return job_reply(r, rc, id);
}

static esp_err_t rotate_post(httpd_req_t *r) {
    if (!tn_auth_ok(r)) return ESP_OK;
    cJSON *j = body_json(r);
    char id[9] = "";
    int rc = act_submit_rotate(jint(j, "bin", 0), id);
    cJSON_Delete(j);
    return job_reply(r, rc, id);
}

static esp_err_t jog_post(httpd_req_t *r) {
    if (!tn_auth_ok(r)) return ESP_OK;
    cJSON *j = body_json(r);
    char id[9] = "";
    int rc = act_submit_jog(jint(j, "dir", 1), jint(j, "ms", 100), id);
    cJSON_Delete(j);
    return job_reply(r, rc, id);
}

static esp_err_t home_post(httpd_req_t *r) { if (!tn_auth_ok(r)) return ESP_OK; act_set_home(); return tn_send_json(r, "{\"ok\":true}"); }

static esp_err_t calib_get(httpd_req_t *r) { char b[768]; act_calib_to_json(b, sizeof b); return tn_send_json(r, b); }

static esp_err_t calib_post(httpd_req_t *r) {
    if (!tn_auth_ok(r)) return ESP_OK;
    char b[768];
    if (tn_read_body(r, b, sizeof b) <= 0 || !act_calib_from_json(b)) return tn_send_err(r, "400 Bad Request", "json");
    return calib_get(r);
}

static esp_err_t neutral_post(httpd_req_t *r) {
    if (!tn_auth_ok(r)) return ESP_OK;
    cJSON *j = body_json(r);
    act_set_neutral(jint(j, "us", 1500));
    cJSON_Delete(j);
    return tn_send_json(r, "{\"ok\":true}");
}

static esp_err_t servo_post(httpd_req_t *r) {
    if (!tn_auth_ok(r)) return ESP_OK;
    cJSON *j = body_json(r);
    char id[9] = "";
    int rc = -2;
    const cJSON *lid = j ? cJSON_GetObjectItem(j, "lid") : NULL;
    if (jint(j, "dump", 0)) rc = act_submit_dump(id);
    else if (cJSON_IsString(lid)) rc = act_submit_lid(!strcmp(lid->valuestring, "open"), id);
    else if (j && cJSON_GetObjectItem(j, "angle")) rc = act_submit_angle(CH_DUMP, jint(j, "angle", 4), id);
    else if (j && cJSON_GetObjectItem(j, "lid_angle")) rc = act_submit_angle(CH_LID, jint(j, "lid_angle", 10), id);
    cJSON_Delete(j);
    return job_reply(r, rc, id);
}

void http_api_start(void) {
    httpd_config_t c = HTTPD_DEFAULT_CONFIG();
    c.max_uri_handlers = 14; c.stack_size = 6144; c.lru_purge_enable = true;
    httpd_handle_t h;
    if (httpd_start(&h, &c) != ESP_OK) return;
    httpd_uri_t u[] = {
        {"/status", HTTP_GET, status_get}, {"/action", HTTP_POST, action_post}, {"/rotate", HTTP_POST, rotate_post},
        {"/jog", HTTP_POST, jog_post}, {"/home/set", HTTP_POST, home_post}, {"/calib", HTTP_GET, calib_get},
        {"/calib", HTTP_POST, calib_post}, {"/calib/neutral", HTTP_POST, neutral_post}, {"/servo", HTTP_POST, servo_post},
        {"/ota", HTTP_POST, tn_ota_handler},
    };
    for (size_t i = 0; i < sizeof u / sizeof u[0]; i++) httpd_register_uri_handler(h, &u[i]);
}
