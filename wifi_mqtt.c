#include "wifi_mqtt.h"

#include <string.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "sdkconfig.h"
#include "mqtt_client.h"

/* ================= DEFINE ================= */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static const char *TAG = "wifi_sta";
static const char *MQTT_TAG = "mqtt";

static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num = 0;
static const int MAX_RETRY = 5;

/* topic nhận lệnh */
static const char *MQTT_TOPIC_ORDER =
    "iot/charge_station/v1/orders";

/* MQTT handle */
static esp_mqtt_client_handle_t s_mqtt_client = NULL;

/* ===== MQTT DATA BUFFER (THÊM DUY NHẤT) ===== */
char g_mqtt_last_msg[128] = {0};
int  g_mqtt_last_len = 0;

/* ================= WIFI EVENT HANDLER ================= */
static void wifi_event_handler(void *arg,
                               esp_event_base_t event_base,
                               int32_t event_id,
                               void *event_data)
{
    if (event_base == WIFI_EVENT &&
        event_id == WIFI_EVENT_STA_START) {

        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_STA_DISCONNECTED) {

        if (s_retry_num < MAX_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGW(TAG, "retry to connect AP");
        } else {
            xEventGroupSetBits(
                s_wifi_event_group,
                WIFI_FAIL_BIT
            );
        }
    }
    else if (event_base == IP_EVENT &&
             event_id == IP_EVENT_STA_GOT_IP) {

        ip_event_got_ip_t *event =
            (ip_event_got_ip_t *)event_data;

        ESP_LOGI(TAG, "got ip: " IPSTR,
                 IP2STR(&event->ip_info.ip));

        s_retry_num = 0;
        xEventGroupSetBits(
            s_wifi_event_group,
            WIFI_CONNECTED_BIT
        );
    }
}

/* ================= WIFI INIT ================= */
void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(
        esp_event_loop_create_default()
    );
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg =
        WIFI_INIT_CONFIG_DEFAULT();

    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(
        esp_event_handler_instance_register(
            WIFI_EVENT,
            ESP_EVENT_ANY_ID,
            &wifi_event_handler,
            NULL,
            NULL));

    ESP_ERROR_CHECK(
        esp_event_handler_instance_register(
            IP_EVENT,
            IP_EVENT_STA_GOT_IP,
            &wifi_event_handler,
            NULL,
            NULL));

    wifi_config_t wifi_config = {0};

    strncpy(
        (char *)wifi_config.sta.ssid,
        CONFIG_ESP_WIFI_SSID,
        sizeof(wifi_config.sta.ssid)
    );

    strncpy(
        (char *)wifi_config.sta.password,
        CONFIG_ESP_WIFI_PASSWORD,
        sizeof(wifi_config.sta.password)
    );

    wifi_config.sta.threshold.authmode =
        WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(
        esp_wifi_set_mode(WIFI_MODE_STA)
    );
    ESP_ERROR_CHECK(
        esp_wifi_set_config(
            WIFI_IF_STA,
            &wifi_config)
    );
    ESP_ERROR_CHECK(esp_wifi_start());

    EventBits_t bits =
        xEventGroupWaitBits(
            s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "WiFi connected");
    } else {
        ESP_LOGE(TAG, "WiFi failed");
    }
}

/* ================= MQTT EVENT HANDLER ================= */
static void mqtt_event_handler(void *handler_args,
                               esp_event_base_t base,
                               int32_t event_id,
                               void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;

    switch (event->event_id) {

    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(MQTT_TAG, "MQTT connected");
        esp_mqtt_client_subscribe(
            client,
            MQTT_TOPIC_ORDER,
            1
        );
        break;

    case MQTT_EVENT_DATA:
        ESP_LOGI(MQTT_TAG,
                 "DATA=%.*s",
                 event->data_len,
                 event->data);

        /* ===== CHỈ THÊM PHẦN NÀY ===== */
        int len = event->data_len;
        if (len >= sizeof(g_mqtt_last_msg))
            len = sizeof(g_mqtt_last_msg) - 1;

        memcpy(g_mqtt_last_msg, event->data, len);
        g_mqtt_last_msg[len] = '\0';
        g_mqtt_last_len = len;
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(MQTT_TAG, "MQTT disconnected");
        break;

    default:
        break;
    }
}

/* ================= MQTT START ================= */
void mqtt_app_start(void)
{
    const esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri =
            "mqtt://broker.hivemq.com:1883",
    };

    s_mqtt_client =
        esp_mqtt_client_init(&mqtt_cfg);

    esp_mqtt_client_register_event(
        s_mqtt_client,
        ESP_EVENT_ANY_ID,
        mqtt_event_handler,
        NULL
    );

    esp_mqtt_client_start(s_mqtt_client);
}

/* ================= OPTIONAL ================= */
esp_mqtt_client_handle_t wifi_mqtt_get_client(void)
{
    return s_mqtt_client;
}
