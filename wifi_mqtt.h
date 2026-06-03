#pragma once

#include "mqtt_client.h"

/* ===== WIFI ===== */
void wifi_init_sta(void);

/* ===== MQTT ===== */
void mqtt_app_start(void);

/* ===== MQTT DATA (SUBSCRIBE) ===== */
/* main.c chỉ đọc, không ghi */
extern char g_mqtt_last_msg[128];
extern int  g_mqtt_last_len;

/* (nếu cần publish ở main) */
esp_mqtt_client_handle_t wifi_mqtt_get_client(void);
