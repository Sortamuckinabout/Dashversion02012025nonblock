#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "driver/twai.h"
#include "lvgl.h"
#include "Waveshare_ST7262_LVGL.h"
#include "ui.h"

static const char *TAG = "dash";

// timing
int interval_can = 50;
int interval_screenchange = 250;

// state variables (moved from sketch globals)
int Idle;
int32_t rpm = 0;
int32_t rearwheelspeed = 0;
int8_t TPS = 0;
int8_t APS = 0;
int8_t gear = 0;
int16_t EngineTemp = 0;
int16_t BatteryV = 0;
int16_t AmbientTemp = 0;
int8_t fuel = 0;
int16_t MAPV = 0;
int16_t MAPH = 0;
int OxyV = 0;
int OxyH = 0;
int32_t speed = 0;
int Indic = 0;
int LHindicat = 0;
int RHindicat = 0;
int Brake = 0;
uint32_t ODO = 0;
uint32_t ODOprevious = 0;
uint32_t Kilometer = 0;
int ClockMins = 0;
int ClockHrs = 0;
uint32_t TotalTrip = 0;
uint32_t Trip = 0;
int KEYSense = 0;
uint16_t KEYnumber = 0;
char buf[32];

// NVS helper wrappers (basic replacements for Preferences.getLong/putLong)
static esp_err_t nvs_get_long_default(const char* key, int64_t *out, int64_t def_value)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open("SETTINGS", NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_get_i64(h, key, out);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        *out = def_value;
        err = ESP_OK;
    }
    nvs_close(h);
    return err;
}

static esp_err_t nvs_put_long(const char* key, int64_t value)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open("SETTINGS", NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_i64(h, key, value);
    if (err == ESP_OK) nvs_commit(h);
    nvs_close(h);
    return err;
}

// CAN receiver task
static void twai_task(void* arg)
{
    twai_message_t message;

    while (true) {
        if (twai_receive(&message, pdMS_TO_TICKS(1000)) == ESP_OK) {
            if (message.identifier == 0x18) {
                gear = ((message.data[4] >> 4) / 2);
                rearwheelspeed = ((((message.data[4] & 0xf) * 256) + message.data[5]) * 0.15) / 0.6213; // km/h
                speed = rearwheelspeed;
                fuel = ((message.data[6]) / 10);
            }
            else if (message.identifier == 0x20) {
                Indic = ((message.data[1]) >> 4);
                // Fixed conditional checks (was using assignment in original sketch)
                if (Indic == 0) { LHindicat = 0; Brake = 0; RHindicat = 0; }
                if (Indic == 1) { LHindicat = 0; Brake = 1; RHindicat = 0; }
                if (Indic == 4) { LHindicat = 1; Brake = 0; RHindicat = 0; }
                if (Indic == 5) { LHindicat = 1; Brake = 1; RHindicat = 0; }
                if (Indic == 8) { LHindicat = 0; Brake = 0; RHindicat = 1; }
                if (Indic == 9) { LHindicat = 0; Brake = 1; RHindicat = 1; }

                KEYnumber = (message.data[3] + message.data[4] + message.data[5]);
                if (KEYnumber != 522) KEYSense = 0; else KEYSense = 522;
            }
            else if (message.identifier == 0x80) {
                TPS = ((message.data[1]) / 2);
                APS = ((message.data[0]) / 2);
                rpm = (message.data[5]) * 256 + (message.data[6]);
            }
            else if (message.identifier == 0x100) {
                EngineTemp = message.data[3] - 40;
                BatteryV = message.data[4];
                AmbientTemp = message.data[5] - 40;
            }
            else if (message.identifier == 0x150) {
                MAPV = (((message.data[6] & 0xf)) * 256) + message.data[7];
                OxyV = message.data[1];
                OxyH = message.data[2];
            }
            else if (message.identifier == 0x160) {
                MAPH = (((message.data[6] & 0xf)) * 256) + message.data[7];
            }
            else if (message.identifier == 0x300) {
                // Read and update odometer values using NVS
                int64_t km_val = 0;
                nvs_get_long_default("Kilometer", &km_val, Kilometer);
                Kilometer = (uint32_t)km_val;
                int64_t prev_val = 0;
                nvs_get_long_default("ODOprevious", &prev_val, ODOprevious);
                ODOprevious = (uint32_t)prev_val;

                ODO = ((message.data[5]));
                if (ODO < ODOprevious) {
                    ODOprevious = 0;
                } else {
                    Trip = ODO - ODOprevious;
                }
                ODOprevious = ODO;
                Kilometer += Trip;
                nvs_put_long("Kilometer", (int64_t)Kilometer);
                nvs_put_long("ODOprevious", (int64_t)ODOprevious);

                ClockMins = (message.data[3] / 2);
                ClockHrs = (message.data[6] >> 4);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// Screen update task (runs in LVGL context)
static void screen_task(void* arg)
{
    while (true) {
        // Update UI safely
        lvgl_port_lock(-1);
        lv_bar_set_value(ui_fuellevelbar, fuel, LV_ANIM_ON);
        sprintf(buf, "%2d", fuel);
        lv_label_set_text(ui_fuelevelnumber, buf);

        lv_bar_set_value(ui_RPMbar, rpm, LV_ANIM_ON);
        sprintf(buf, "%6d", rpm);
        lv_label_set_text(ui_rpmNumber, buf);

        if (gear != 0) {
            sprintf(buf, "%1d", gear);
            lv_label_set_text(ui_GearNumber, buf);
        } else {
            lv_label_set_text(ui_GearNumber, "N");
        }

        sprintf(buf, "%3d", speed);
        lv_label_set_text(ui_Speednumber, buf);

        sprintf(buf, "%3d", EngineTemp);
        lv_label_set_text(ui_engtempnumber, buf);

        sprintf(buf, "%2d", AmbientTemp);
        lv_label_set_text(ui_Airtempnumber, buf);

        sprintf(buf, "%2d", ClockMins);
        lv_label_set_text(ui_Timenumbermins, buf);

        sprintf(buf, "%2d", ClockHrs);
        lv_label_set_text(ui_Timenumberhr, buf);

        sprintf(buf, "%4d", MAPH);
        lv_label_set_text(ui_mapHnum, buf);
        lv_bar_set_value(ui_MapbarH, MAPH, LV_ANIM_ON);

        sprintf(buf, "%4d", MAPV);
        lv_label_set_text(ui_mapVnum, buf);
        lv_bar_set_value(ui_MapVbar, MAPV, LV_ANIM_ON);

        lv_bar_set_value(ui_APSbar, APS, LV_ANIM_ON);
        lv_bar_set_value(ui_TPSbar, TPS, LV_ANIM_ON);

        sprintf(buf, "%3d", OxyV);
        lv_label_set_text(ui_oxyVnum, buf);
        sprintf(buf, "%3d", OxyH);
        lv_label_set_text(ui_xyHnum, buf);

        sprintf(buf, "%4d", BatteryV);
        lv_label_set_text(ui_Batnum, buf);

        sprintf(buf, "%d", KEYSense);
        lv_label_set_text(ui_keysense, buf);

        sprintf(buf, "%7d", Kilometer);
        lv_label_set_text(ui_Odometer, buf);

        lvgl_port_unlock();

        // Let LVGL run its timers/handlers if needed
        lv_timer_handler();

        vTaskDelay(pdMS_TO_TICKS(interval_screenchange));
    }
}

extern "C" void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_LOGI(TAG, "Starting dash (ESP-IDF)");

    // Configure TWAI (CAN) driver
    const gpio_num_t txPin = (gpio_num_t)20; // note: original sketch had TX_PIN=20
    const gpio_num_t rxPin = (gpio_num_t)19; // RX_PIN=19

    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(txPin, rxPin, TWAI_MODE_LISTEN_ONLY);
    twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    if (twai_driver_install(&g_config, &t_config, &f_config) == ESP_OK) {
        ESP_LOGI(TAG, "TWAI driver installed");
    } else {
        ESP_LOGE(TAG, "Failed to install TWAI driver");
    }

    if (twai_start() == ESP_OK) {
        ESP_LOGI(TAG, "TWAI driver started");
    } else {
        ESP_LOGE(TAG, "Failed to start TWAI driver");
    }

    // Initialize display / LVGL UI
    lcd_init();
    lvgl_port_lock(-1);
    ui_init();
    lvgl_port_unlock();

    // Restore stored values
    int64_t v = 0;
    nvs_get_long_default("Kilometer", &v, 0);
    Kilometer = (uint32_t)v;
    nvs_get_long_default("ODOprevious", &v, 0);
    ODOprevious = (uint32_t)v;

    // Create tasks
    xTaskCreatePinnedToCore(twai_task, "twai_task", 4096, NULL, 5, NULL, 1);
    xTaskCreatePinnedToCore(screen_task, "screen_task", 4096, NULL, 5, NULL, 1);

    // Keep app_main alive
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
