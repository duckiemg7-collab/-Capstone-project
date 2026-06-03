#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_lcd_ili9341.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/spi_types.h"
#include "lvgl.h"
#include "sys/lock.h"
#include "sys/param.h"

#include "charge_test_screen.h"
#include "display_lvgl.h"

#define LCD_HOST VSPI_HOST
#define PIN_NUM_SCLK 18
#define PIN_NUM_MOSI 23
#define PIN_NUM_MISO 19
#define PIN_NUM_LCD_CS 5
#define PIN_NUM_BKL 4
#define PIN_NUM_RST 22
#define PIN_NUM_LCD_DC 21

#define LCD_H_RES 320
#define LCD_V_RES 240
#define LVGL_DRAW_BUF_LINES 16

#define LCD_PIXEL_CLOCK_HZ (20 * 1000 * 1000)
#define LCD_CMD_BITS 8
#define LCD_PARAM_BITS 8
#define LVGL_TICK_PERIOD_MS 2

static esp_lcd_panel_io_handle_t io_handle;
static esp_lcd_panel_handle_t lcd_panel_handle;
static _lock_t lvgl_api_lock;

static void display_init(void);
static void lvgl_task(void *arg);
static void lvgl_tick(void *arg);

static void display_init(void)
{
    gpio_set_direction(PIN_NUM_BKL, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_NUM_BKL, 1);

    spi_bus_config_t buscfg = {
        .sclk_io_num = PIN_NUM_SCLK,
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = PIN_NUM_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_H_RES * 40 * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = PIN_NUM_LCD_DC,
        .cs_gpio_num = PIN_NUM_LCD_CS,
        .pclk_hz = LCD_PIXEL_CLOCK_HZ,
        .lcd_cmd_bits = LCD_CMD_BITS,
        .lcd_param_bits = LCD_PARAM_BITS,
        .spi_mode = 0,
        .trans_queue_depth = 4,
    };
    ESP_ERROR_CHECK(
        esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &io_handle));

    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = PIN_NUM_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(io_handle, &panel_config, &lcd_panel_handle));

    ESP_ERROR_CHECK(esp_lcd_panel_reset(lcd_panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(lcd_panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(lcd_panel_handle, true));
}

static bool notify_lvgl_flush_ready(
    esp_lcd_panel_io_handle_t panel_io,
    esp_lcd_panel_io_event_data_t *edata,
    void *user_ctx)
{
    lv_display_flush_ready((lv_display_t *)user_ctx);
    return false;
}

static void lvgl_port_update_callback(lv_display_t *disp)
{
    esp_lcd_panel_handle_t panel = lv_display_get_user_data(disp);
    lv_display_rotation_t rotation = lv_display_get_rotation(disp);

    switch (rotation) {
    case LV_DISPLAY_ROTATION_0:
        esp_lcd_panel_swap_xy(panel, false);
        esp_lcd_panel_mirror(panel, true, false);
        break;
    case LV_DISPLAY_ROTATION_90:
        esp_lcd_panel_swap_xy(panel, true);
        esp_lcd_panel_mirror(panel, true, true);
        break;
    case LV_DISPLAY_ROTATION_180:
        esp_lcd_panel_swap_xy(panel, false);
        esp_lcd_panel_mirror(panel, false, true);
        break;
    case LV_DISPLAY_ROTATION_270:
        esp_lcd_panel_swap_xy(panel, true);
        esp_lcd_panel_mirror(panel, false, false);
        break;
    }
}

static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    lvgl_port_update_callback(disp);

    esp_lcd_panel_handle_t panel = lv_display_get_user_data(disp);
    lv_draw_sw_rgb565_swap(px_map,
        (area->x2 - area->x1 + 1) * (area->y2 - area->y1 + 1));

    esp_lcd_panel_draw_bitmap(panel,
        area->x1, area->y1,
        area->x2 + 1, area->y2 + 1,
        px_map);
}

static void lvgl_tick(void *arg)
{
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

static void lvgl_task(void *arg)
{
    uint32_t wait_ms;
    while (1) {
        _lock_acquire(&lvgl_api_lock);
        wait_ms = lv_timer_handler();
        _lock_release(&lvgl_api_lock);
        vTaskDelay(pdMS_TO_TICKS(MAX(wait_ms, 10)));
    }
}

esp_err_t display_lvgl_init(void)
{
    display_init();
    lv_init();

    lv_display_t *disp = lv_display_create(LCD_H_RES, LCD_V_RES);

    static uint8_t buf[LCD_H_RES * LVGL_DRAW_BUF_LINES * sizeof(uint16_t)];
    lv_display_set_buffers(disp, buf, NULL, sizeof(buf), LV_DISPLAY_RENDER_MODE_PARTIAL);

    lv_display_set_user_data(disp, lcd_panel_handle);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(disp, lvgl_flush_cb);
    lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_0);

    const esp_lcd_panel_io_callbacks_t cbs = {
        .on_color_trans_done = notify_lvgl_flush_ready,
    };
    ESP_ERROR_CHECK(esp_lcd_panel_io_register_event_callbacks(io_handle, &cbs, disp));

    const esp_timer_create_args_t tick_args = {
        .callback = lvgl_tick,
        .name = "lvgl_tick",
    };
    esp_timer_handle_t tick_timer;
    ESP_ERROR_CHECK(esp_timer_create(&tick_args, &tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(tick_timer, LVGL_TICK_PERIOD_MS * 1000));

    _lock_acquire(&lvgl_api_lock);
    charge_test_screen_create();
    _lock_release(&lvgl_api_lock);

    xTaskCreate(lvgl_task, "lvgl", 4096, NULL, 2, NULL);
    return ESP_OK;
}

void display_update_power(float I, float V, float P)
{
    charge_test_screen_set_power(I, V, P);
}

void display_update_energy(float energy_Wh, float target_Wh)
{
    charge_test_screen_set_energy(energy_Wh, target_Wh);
}

void display_show_payment(void)
{
    _lock_acquire(&lvgl_api_lock);
    charge_test_screen_set_mode(CHARGE_SCREEN_PAYMENT);
    _lock_release(&lvgl_api_lock);
}

void display_show_charging(int money, float target_Wh)
{
    _lock_acquire(&lvgl_api_lock);
    charge_test_screen_set_charging(money, target_Wh);
    _lock_release(&lvgl_api_lock);
}

void display_show_done(void)
{
    _lock_acquire(&lvgl_api_lock);
    charge_test_screen_set_done();
    _lock_release(&lvgl_api_lock);
}
