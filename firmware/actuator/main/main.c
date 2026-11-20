/* TrashNet actuator: tray (MG996R), dump arm (MG995), lid, two HC-SR04. */
#include "actuator.h"
#include "esp_ota_ops.h"
#include "tn_common.h"

int actuator_status_extra(char *b, size_t n);
void http_api_start(void);
void console_start(void);

#ifdef CONFIG_TN_DEBUG_CONSOLE
#include <stdio.h>
#include "esp_log.h"
static void debug_banner(void) { ESP_LOGI("dbg", "debug console enabled"); }
#else
static void debug_banner(void) {}
#endif

void app_main(void) {
    static const tn_cfg_t cfg = {.role = "actuator", .role_upper = "ACTUATOR", .reset_gpio = 0, .status_extra = actuator_status_extra};
    tn_common_start(&cfg);
    if (tn_is_provisioning()) return;
    act_start();
    http_api_start();
    console_start();
    debug_banner();
    esp_ota_mark_app_valid_cancel_rollback();
}
