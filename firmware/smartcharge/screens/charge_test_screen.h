#pragma once

#include "lvgl.h"

typedef enum {
    CHARGE_SCREEN_PAYMENT = 0,
    CHARGE_SCREEN_CHARGING,
    CHARGE_SCREEN_DONE,
} charge_screen_mode_t;

void charge_test_screen_create(void);
void charge_test_screen_set_mode(charge_screen_mode_t mode);
void charge_test_screen_set_power(float I, float V, float P);
void charge_test_screen_set_energy(float energy_Wh, float target_Wh);
void charge_test_screen_set_charging(int money, float target_Wh);
void charge_test_screen_set_done(void);
