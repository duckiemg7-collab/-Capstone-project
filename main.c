#include "power_meter.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "display_lvgl.h"
#include "nvs_flash.h"
#include "wifi_mqtt.h" 
#include "driver/gpio.h"

#define CHARGE_RELAY_GPIO GPIO_NUM_15   // D15

static float energy_Wh = 0.0f;
static float energy_target_Wh = 0.0f;
static int charging_enable = 0;

#define COS_PHI 0.9f

void handle_payment(const char *msg)
{
    char dev[32];
    int port;
    int money;

    // msg dạng: "ESP32_001 1 10000"
    if (sscanf(msg, "%s %d %d", dev, &port, &money) != 3) {
        ESP_LOGE("PAY", "Sai dinh dang: %s", msg);
        return;
    }

    // Quy đổi tiền → Wh
    // 10k = 0.13 kWh = 130 Wh
    energy_target_Wh = (money / 10000.0f) * 0.9f;

    energy_Wh = 0;
    charging_enable = 1;
    display_show_charging(money, energy_target_Wh);
    // 🔌 BẬT SẠC
	gpio_set_level(CHARGE_RELAY_GPIO, 0);
	ESP_LOGI("PAY", "BAT SAC - GPIO15 = 0");
    ESP_LOGI("PAY",
             "Nhan %d VND -> muc tieu %.1f Wh",
             money, energy_target_Wh);
}
void update_energy(power_meter_t *m)
{
    if (!charging_enable)
        return;

    float P = m->Vrms * m->Irms * COS_PHI;

    // loop 1s → Wh = P / 3600
    energy_Wh += P / 3600.0f;
    display_update_energy(energy_Wh, energy_target_Wh);

    ESP_LOGI("ENERGY",
             "P=%.1f W | %.2f / %.2f Wh",
             P, energy_Wh, energy_target_Wh);

    if (energy_Wh >= energy_target_Wh) {
        charging_enable = 0;
        display_show_done();
	    gpio_set_level(CHARGE_RELAY_GPIO, 1);
	    ESP_LOGI("ENERGY", "SAC XONG - GPIO15 = 1");
        ESP_LOGI("ENERGY", "SAC XONG");
        
        // PUB DONE
        char msg[128];
        snprintf(msg, sizeof(msg),
                 "{\"device\":\"ESP32_001\",\"port\":1,\"status\":\"DONE\"}");

        esp_mqtt_client_publish(
            wifi_mqtt_get_client(),
            "iot/charge_station/v1/orders",
            msg,
            0,
            1,
            0
        );
    }
}

void app_main(void)
{
    power_meter_t meter;
    display_lvgl_init();
    power_meter_init();
    /* ===== BẮT BUỘC PHẢI CÓ ===== */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    wifi_init_sta();       // kết nối WiFi
    mqtt_app_start();      // MQTT tự SUB trong event
     // ==== INIT GPIO CHARGE RELAY ====
	// ==== INIT GPIO CHARGE RELAY ====
	gpio_config_t io_conf = {
    	.pin_bit_mask = 1ULL << CHARGE_RELAY_GPIO,
    	.mode = GPIO_MODE_OUTPUT,
    	.pull_down_en = 0,
    	.pull_up_en = 0,
    	.intr_type = GPIO_INTR_DISABLE
    };
	gpio_config(&io_conf);
	
	// Mặc định KHÔNG sạc (OFF) → xuất 1
	gpio_set_level(CHARGE_RELAY_GPIO, 1);

    while (1)
    {
        power_meter_get(&meter);
        ESP_LOGI("MAIN",
                 "Irms=%.3f A | Vrms=%.1f V | P=%.1f W",
                 meter.Irms, meter.Vrms, meter.P);
        /* GỌI HÀM – không có void */
        if (g_mqtt_last_len > 0) {
            handle_payment(g_mqtt_last_msg);
            g_mqtt_last_len = 0; // clear sau khi xử lý
        }
        // Cập nhật Wh nếu đang sạc
        update_energy(&meter);
        // Hiển thị
        float P = meter.Vrms * meter.Irms * COS_PHI;
        display_update_power(
            meter.Irms,
            meter.Vrms,
            P
        );

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
