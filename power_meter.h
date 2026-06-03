#ifndef POWER_METER_H
#define POWER_METER_H

#include "esp_err.h"

/* ===== STRUCT KET QUA DO ===== */
typedef struct
{
    float Irms;     // A
    float Vrms;     // V
    float P;        // W
} power_meter_t;

/* ===== API PUBLIC ===== */

/**
 * @brief  Khởi tạo ADC + calibration + calibrate ZERO
 * @note   PHẢI gọi 1 lần trước khi dùng power_meter_get()
 */
esp_err_t power_meter_init(void);

/**
 * @brief  Đo Irms, Vrms, P
 * @param  meter  con trỏ struct nhận kết quả
 */
esp_err_t power_meter_get(power_meter_t *meter);

#endif /* POWER_METER_H */
