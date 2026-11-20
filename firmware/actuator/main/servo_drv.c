#include "actuator.h"
#include "driver/ledc.h"

#define BITS LEDC_TIMER_16_BIT

void servo_init(void) {
    ledc_timer_config_t t = {.speed_mode = LEDC_LOW_SPEED_MODE, .duty_resolution = BITS, .timer_num = LEDC_TIMER_0,
                             .freq_hz = 50, .clk_cfg = LEDC_AUTO_CLK};
    ledc_timer_config(&t);
    const int pins[3] = {PIN_SERVO_DUMP, PIN_SERVO_TRAY, PIN_SERVO_LID};
    for (int i = 0; i < 3; i++) {
        ledc_channel_config_t c = {.gpio_num = pins[i], .speed_mode = LEDC_LOW_SPEED_MODE, .channel = i, .timer_sel = LEDC_TIMER_0, .duty = 0};
        ledc_channel_config(&c);
    }
}

void servo_write_pulse(int ch, int us) {
    ledc_set_duty(LEDC_LOW_SPEED_MODE, ch, tn_pulse_to_duty(us, 16));
    ledc_update_duty(LEDC_LOW_SPEED_MODE, ch);
}

/* No PWM at rest: stops jitter and heating of the position servos. */
void servo_off(int ch) {
    ledc_set_duty(LEDC_LOW_SPEED_MODE, ch, 0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, ch);
}
