#pragma once

#include "esp_err.h"

esp_err_t display_lvgl_init(void);

void display_update_power(float I, float V, float P);
void display_update_energy(float energy_Wh, float target_Wh);
void display_show_payment(void);
void display_show_charging(int money, float target_Wh);
void display_show_done(void);
