#include <stdio.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

/* ===== ADC ===== */
#include "esp_adc/adc_continuous.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

#include "power_meter.h"

/* ===== GPIO & ADC CONFIG ===== */
#define ADC_I_CH           ADC_CHANNEL_6   // GPIO34
#define ADC_V_CH           ADC_CHANNEL_7   // GPIO35
#define ADC_UNIT_USED      ADC_UNIT_1
#define ADC_ATTEN_USED     ADC_ATTEN_DB_12
#define ADC_BITWIDTH_USED  ADC_BITWIDTH_12

/* ===== HE SO CAM BIEN ===== */
#define DIV_RATIO   2.0f       // chia áp
#define WCS_SENS    0.06f      // 60mV/A
#define NUM_TURNS   4
#define ZMPT_GAIN   170.0f

/* ===== SAMPLING ===== */
#define SAMPLE_FREQ_HZ  20000
#define RMS_SAMPLES    10000

static const char *TAG = "POWER_METER";

/* ===== HANDLE ADC ===== */
static adc_continuous_handle_t adc_handle;
static adc_cali_handle_t adc_cali;

/* ===== OFFSET ZERO ===== */
static float I_ZERO = 0.0f;
static float V_ZERO = 0.0f;

/* ========================================================= */
/* ===================== PRIVATE FUNC ====================== */
/* ========================================================= */

static void adc_continuous_init(void)
{
    adc_continuous_handle_cfg_t handle_cfg = {
        .max_store_buf_size = 4096,
        .conv_frame_size = 256,
    };
    ESP_ERROR_CHECK(adc_continuous_new_handle(&handle_cfg, &adc_handle));

    adc_digi_pattern_config_t pattern[2] = {
        {
            .atten = ADC_ATTEN_USED,
            .channel = ADC_I_CH,
            .unit = ADC_UNIT_USED,
            .bit_width = ADC_BITWIDTH_USED,
        },
        {
            .atten = ADC_ATTEN_USED,
            .channel = ADC_V_CH,
            .unit = ADC_UNIT_USED,
            .bit_width = ADC_BITWIDTH_USED,
        }
    };

    adc_continuous_config_t cfg = {
        .pattern_num = 2,
        .adc_pattern = pattern,
        .sample_freq_hz = SAMPLE_FREQ_HZ,
        .conv_mode = ADC_CONV_SINGLE_UNIT_1,
        .format = ADC_DIGI_OUTPUT_FORMAT_TYPE1,
    };

    ESP_ERROR_CHECK(adc_continuous_config(adc_handle, &cfg));
    ESP_ERROR_CHECK(adc_continuous_start(adc_handle));

    ESP_LOGI(TAG, "ADC continuous started");
}

static void adc_calibration_init(void)
{
    adc_cali_line_fitting_config_t cfg = {
        .unit_id = ADC_UNIT_USED,
        .atten = ADC_ATTEN_USED,
        .bitwidth = ADC_BITWIDTH_USED,
    };
    ESP_ERROR_CHECK(adc_cali_create_scheme_line_fitting(&cfg, &adc_cali));
    ESP_LOGI(TAG, "ADC calibration ready");
}

/* ===== CALIB ZERO ===== */
static void adc_calibrate_zero(void)
{
    uint8_t buf[256];
    uint32_t len;
    float sum_i = 0, sum_v = 0;
    int cnt_i = 0, cnt_v = 0;

    ESP_LOGI(TAG, "CALIB ZERO: DAM BAO KHONG CO DIEN & DONG");

    while (cnt_i < RMS_SAMPLES || cnt_v < RMS_SAMPLES)
    {
        if (adc_continuous_read(adc_handle, buf, sizeof(buf), &len, 100) != ESP_OK)
            continue;

        for (int i = 0; i < len; i += SOC_ADC_DIGI_RESULT_BYTES)
        {
            adc_digi_output_data_t *p = (void *)&buf[i];
            int mv;
            adc_cali_raw_to_voltage(adc_cali, p->type1.data, &mv);

            float v = (mv / 1000.0f) * DIV_RATIO;

            if (p->type1.channel == ADC_I_CH && cnt_i < RMS_SAMPLES)
            {
                sum_i += v;
                cnt_i++;
            }
            else if (p->type1.channel == ADC_V_CH && cnt_v < RMS_SAMPLES)
            {
                sum_v += v;
                cnt_v++;
            }
        }
    }

    I_ZERO = sum_i / cnt_i;
    V_ZERO = sum_v / cnt_v;

    ESP_LOGI(TAG, "ZERO DONE: I_ZERO=%.4f  V_ZERO=%.4f", I_ZERO, V_ZERO);
}

/* ===== DO RMS ===== */
static void adc_measure_rms(float *irms, float *vrms)
{
    uint8_t buf[256];
    uint32_t len;
    float sum_i2 = 0, sum_v2 = 0;
    int ci = 0, cv = 0;

    while (ci < RMS_SAMPLES || cv < RMS_SAMPLES)
    {
        if (adc_continuous_read(adc_handle, buf, sizeof(buf), &len, 100) != ESP_OK)
            continue;

        for (int i = 0; i < len; i += SOC_ADC_DIGI_RESULT_BYTES)
        {
            adc_digi_output_data_t *p = (void *)&buf[i];
            int mv;
            adc_cali_raw_to_voltage(adc_cali, p->type1.data, &mv);

            float v = (mv / 1000.0f) * DIV_RATIO;

            if (p->type1.channel == ADC_I_CH && ci < RMS_SAMPLES)
            {
                float i_inst = (v - I_ZERO) / (WCS_SENS * NUM_TURNS);
                sum_i2 += i_inst * i_inst;
                ci++;
            }
            else if (p->type1.channel == ADC_V_CH && cv < RMS_SAMPLES)
            {
                float vv = v - V_ZERO;
                sum_v2 += vv * vv;
                cv++;
            }
        }
    }

    *irms = sqrtf(sum_i2 / RMS_SAMPLES);
    *vrms = sqrtf(sum_v2 / RMS_SAMPLES) * ZMPT_GAIN;
}

/* ========================================================= */
/* ===================== PUBLIC API ======================== */
/* ========================================================= */

esp_err_t power_meter_init(void)
{
    adc_calibration_init();
    adc_continuous_init();
    adc_calibrate_zero();
    return ESP_OK;
}

esp_err_t power_meter_get(power_meter_t *meter)
{
    if (!meter) return ESP_ERR_INVALID_ARG;

    adc_measure_rms(&meter->Irms, &meter->Vrms);
    meter->P = meter->Irms * meter->Vrms;

    return ESP_OK;
}
