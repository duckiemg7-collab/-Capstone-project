#include "charge_test_screen.h"

#define SCREEN_H_RES 320
#define SCREEN_V_RES 240

#define PAY_BANK_NAME "ADC BANK"
#define PAY_ACCOUNT_NO "0123456789"
#define PAY_ACCOUNT_NAME "ADC CHARGE"
#define PAY_CONTENT "ESP32_001 1 10000"

static lv_obj_t *payment_screen;
static lv_obj_t *charging_screen;
static lv_obj_t *label_I;
static lv_obj_t *label_V;
static lv_obj_t *label_P;
static lv_obj_t *label_energy;
static lv_obj_t *label_status;

static float g_I = 0.0f;
static float g_V = 0.0f;
static float g_P = 0.0f;
static float g_energy_Wh = 0.0f;
static float g_target_Wh = 0.0f;
static int g_money = 0;
static charge_screen_mode_t g_mode = CHARGE_SCREEN_PAYMENT;

static void power_ui_timer_cb(lv_timer_t *timer);
static lv_obj_t *create_text(lv_obj_t *parent, const char *text, lv_color_t color,
                             int y, const lv_font_t *font);

static lv_obj_t *create_text(lv_obj_t *parent, const char *text, lv_color_t color,
                             int y, const lv_font_t *font)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_width(label, SCREEN_H_RES - 24);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, color, 0);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 12, y);
    return label;
}

void charge_test_screen_create(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x101820), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    payment_screen = lv_obj_create(scr);
    lv_obj_remove_style_all(payment_screen);
    lv_obj_set_size(payment_screen, SCREEN_H_RES, SCREEN_V_RES);
    lv_obj_set_style_bg_color(payment_screen, lv_color_hex(0x101820), 0);
    lv_obj_set_style_bg_opa(payment_screen, LV_OPA_COVER, 0);

    create_text(payment_screen, "THANH TOAN SAC", lv_color_hex(0xFFFFFF), 12,
                LV_FONT_DEFAULT);
    create_text(payment_screen, PAY_BANK_NAME, lv_color_hex(0x00D4FF), 48,
                LV_FONT_DEFAULT);
    create_text(payment_screen, "STK: " PAY_ACCOUNT_NO, lv_color_hex(0xFFFFFF), 76,
                LV_FONT_DEFAULT);
    create_text(payment_screen, PAY_ACCOUNT_NAME, lv_color_hex(0xE8F0F2), 104,
                LV_FONT_DEFAULT);

    lv_obj_t *code_box = lv_obj_create(payment_screen);
    lv_obj_set_size(code_box, 296, 72);
    lv_obj_align(code_box, LV_ALIGN_TOP_LEFT, 12, 140);
    lv_obj_set_style_bg_color(code_box, lv_color_hex(0x1E2A32), 0);
    lv_obj_set_style_border_color(code_box, lv_color_hex(0x00D4FF), 0);
    lv_obj_set_style_border_width(code_box, 2, 0);
    lv_obj_set_style_radius(code_box, 6, 0);

    lv_obj_t *code = lv_label_create(code_box);
    lv_label_set_text(code, PAY_CONTENT);
    lv_obj_set_style_text_color(code, lv_color_hex(0xFFE066), 0);
    lv_obj_set_style_text_font(code, LV_FONT_DEFAULT, 0);
    lv_obj_center(code);

    create_text(payment_screen, "Cho MQTT: ESP32_001 1 10000", lv_color_hex(0x8FA3AD),
                218, LV_FONT_DEFAULT);

    charging_screen = lv_obj_create(scr);
    lv_obj_remove_style_all(charging_screen);
    lv_obj_set_size(charging_screen, SCREEN_H_RES, SCREEN_V_RES);
    lv_obj_set_style_bg_color(charging_screen, lv_color_hex(0x081014), 0);
    lv_obj_set_style_bg_opa(charging_screen, LV_OPA_COVER, 0);

    label_status = create_text(charging_screen, "DANG SAC", lv_color_hex(0xFFE066), 12,
                               LV_FONT_DEFAULT);
    label_V = create_text(charging_screen, "V: 0.0 V", lv_color_hex(0x00D4FF), 52,
                          LV_FONT_DEFAULT);
    label_I = create_text(charging_screen, "I: 0.00 A", lv_color_hex(0xA7F3D0), 96,
                          LV_FONT_DEFAULT);
    label_P = create_text(charging_screen, "P: 0.0 W", lv_color_hex(0xFF6B6B), 140,
                          LV_FONT_DEFAULT);
    label_energy = create_text(charging_screen, "0.00 / 0.00 Wh",
                               lv_color_hex(0xE8F0F2), 188, LV_FONT_DEFAULT);

    lv_timer_create(power_ui_timer_cb, 1000, NULL);
    charge_test_screen_set_mode(CHARGE_SCREEN_PAYMENT);
}

void charge_test_screen_set_mode(charge_screen_mode_t mode)
{
    if (!payment_screen || !charging_screen) {
        return;
    }

    g_mode = mode;

    if (mode == CHARGE_SCREEN_PAYMENT) {
        lv_obj_remove_flag(payment_screen, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(charging_screen, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(payment_screen, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(charging_screen, LV_OBJ_FLAG_HIDDEN);
    }
}

void charge_test_screen_set_power(float I, float V, float P)
{
    g_I = I;
    g_V = V;
    g_P = P;
}

void charge_test_screen_set_energy(float energy_Wh, float target_Wh)
{
    g_energy_Wh = energy_Wh;
    g_target_Wh = target_Wh;
}

void charge_test_screen_set_charging(int money, float target_Wh)
{
    g_money = money;
    g_target_Wh = target_Wh;
    g_energy_Wh = 0.0f;
    charge_test_screen_set_mode(CHARGE_SCREEN_CHARGING);
    lv_label_set_text_fmt(label_status, "DANG SAC | %d VND", g_money);
}

void charge_test_screen_set_done(void)
{
    charge_test_screen_set_mode(CHARGE_SCREEN_DONE);
    lv_label_set_text(label_status, "SAC XONG");
}

static void power_ui_timer_cb(lv_timer_t *timer)
{
    (void)timer;

    if (g_mode == CHARGE_SCREEN_PAYMENT || !label_I || !label_V || !label_P) {
        return;
    }

    int I_disp = (int)(g_I * 100);
    int V_disp = (int)(g_V * 10);
    int P_disp = (int)(g_P * 10);
    int E_disp = (int)(g_energy_Wh * 100);
    int T_disp = (int)(g_target_Wh * 100);

    lv_label_set_text_fmt(label_V, "V: %d.%d V", V_disp / 10, V_disp % 10);
    lv_label_set_text_fmt(label_I, "I: %d.%02d A", I_disp / 100, I_disp % 100);
    lv_label_set_text_fmt(label_P, "P: %d.%d W", P_disp / 10, P_disp % 10);
    lv_label_set_text_fmt(label_energy, "%d.%02d / %d.%02d Wh",
                          E_disp / 100, E_disp % 100,
                          T_disp / 100, T_disp % 100);
}
