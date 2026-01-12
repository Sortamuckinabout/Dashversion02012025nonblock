#include "Waveshare_ST7262_LVGL.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "driver/gpio.h"
#include "driver/i2c.h"

static const char *TAG = "waveshare_st7262";

// Default pin configuration; adjust to your board in sdkconfig or here
#ifndef EXAMPLE_PIN_NUM_BK_LIGHT
#define EXAMPLE_PIN_NUM_BK_LIGHT -1
#endif
#ifndef EXAMPLE_PIN_NUM_PCLK
#define EXAMPLE_PIN_NUM_PCLK 42
#endif
#ifndef EXAMPLE_PIN_NUM_DATA0
#define EXAMPLE_PIN_NUM_DATA0 8
#endif
#ifndef EXAMPLE_PIN_NUM_DATA1
#define EXAMPLE_PIN_NUM_DATA1 3
#endif
#ifndef EXAMPLE_PIN_NUM_DATA2
#define EXAMPLE_PIN_NUM_DATA2 46
#endif
#ifndef EXAMPLE_PIN_NUM_DATA3
#define EXAMPLE_PIN_NUM_DATA3 9
#endif
#ifndef EXAMPLE_PIN_NUM_DATA4
#define EXAMPLE_PIN_NUM_DATA4 1
#endif
#ifndef EXAMPLE_PIN_NUM_DATA5
#define EXAMPLE_PIN_NUM_DATA5 5
#endif
#ifndef EXAMPLE_PIN_NUM_DATA6
#define EXAMPLE_PIN_NUM_DATA6 6
#endif
#ifndef EXAMPLE_PIN_NUM_DATA7
#define EXAMPLE_PIN_NUM_DATA7 7
#endif
#ifndef EXAMPLE_PIN_NUM_DATA8
#define EXAMPLE_PIN_NUM_DATA8 15
#endif
#ifndef EXAMPLE_PIN_NUM_DATA9
#define EXAMPLE_PIN_NUM_DATA9 16
#endif
#ifndef EXAMPLE_PIN_NUM_DATA10
#define EXAMPLE_PIN_NUM_DATA10 4
#endif
#ifndef EXAMPLE_PIN_NUM_DATA11
#define EXAMPLE_PIN_NUM_DATA11 45
#endif
#ifndef EXAMPLE_PIN_NUM_DATA12
#define EXAMPLE_PIN_NUM_DATA12 48
#endif
#ifndef EXAMPLE_PIN_NUM_DATA13
#define EXAMPLE_PIN_NUM_DATA13 47
#endif
#ifndef EXAMPLE_PIN_NUM_DATA14
#define EXAMPLE_PIN_NUM_DATA14 21
#endif
#ifndef EXAMPLE_PIN_NUM_DATA15
#define EXAMPLE_PIN_NUM_DATA15 14
#endif
#ifndef EXAMPLE_PIN_NUM_VSYNC
#define EXAMPLE_PIN_NUM_VSYNC 41
#endif
#ifndef EXAMPLE_PIN_NUM_HSYNC
#define EXAMPLE_PIN_NUM_HSYNC 39
#endif
#ifndef EXAMPLE_PIN_NUM_DE
#define EXAMPLE_PIN_NUM_DE 40
#endif
#ifndef EXAMPLE_PIN_NUM_DISP_EN
#define EXAMPLE_PIN_NUM_DISP_EN -1
#endif
#ifndef EXAMPLE_PIN_NUM_TOUCH_SCL
#define EXAMPLE_PIN_NUM_TOUCH_SCL GPIO_NUM_20
#endif
#ifndef EXAMPLE_PIN_NUM_TOUCH_SDA
#define EXAMPLE_PIN_NUM_TOUCH_SDA GPIO_NUM_19
#endif
#ifndef EXAMPLE_PIN_NUM_TOUCH_RST
#define EXAMPLE_PIN_NUM_TOUCH_RST GPIO_NUM_38
#endif
#ifndef EXAMPLE_PIN_NUM_TOUCH_INT
#define EXAMPLE_PIN_NUM_TOUCH_INT -1
#endif

#ifndef EXAMPLE_LCD_PIXEL_CLOCK_HZ
#define EXAMPLE_LCD_PIXEL_CLOCK_HZ (24 * 1000 * 1000)
#endif
#ifndef EXAMPLE_LCD_H_RES
#define EXAMPLE_LCD_H_RES 800
#endif
#ifndef EXAMPLE_LCD_V_RES
#define EXAMPLE_LCD_V_RES 480
#endif

#define EXAMPLE_LVGL_TICK_PERIOD_MS 2
#define EXAMPLE_LVGL_TASK_MAX_DELAY_MS 500
#define EXAMPLE_LVGL_TASK_MIN_DELAY_MS 1
#define EXAMPLE_LVGL_TASK_STACK_SIZE (4 * 1024)
#define EXAMPLE_LVGL_TASK_PRIORITY 2

static SemaphoreHandle_t lvgl_mux = NULL;
static SemaphoreHandle_t sem_vsync_end = NULL;
static SemaphoreHandle_t sem_gui_ready = NULL;

static bool example_on_vsync_event(esp_lcd_panel_handle_t panel, const esp_lcd_rgb_panel_event_data_t *event_data, void *user_data)
{
    BaseType_t high_task_awoken = pdFALSE;
    if (xSemaphoreTakeFromISR(sem_gui_ready, &high_task_awoken) == pdTRUE) {
        xSemaphoreGiveFromISR(sem_vsync_end, &high_task_awoken);
    }
    return high_task_awoken == pdTRUE;
}

static void example_lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t)drv->user_data;
    int offsetx1 = area->x1;
    int offsetx2 = area->x2;
    int offsety1 = area->y1;
    int offsety2 = area->y2;

    xSemaphoreGive(sem_gui_ready);
    xSemaphoreTake(sem_vsync_end, portMAX_DELAY);

    esp_lcd_panel_draw_bitmap(panel_handle, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, color_map);
    lv_disp_flush_ready(drv);
}

static void example_increase_lvgl_tick(void *arg)
{
    (void)arg;
    lv_tick_inc(EXAMPLE_LVGL_TICK_PERIOD_MS);
}

bool lvgl_port_lock(int timeout_ms)
{
    const TickType_t timeout_ticks = (timeout_ms == -1) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTakeRecursive(lvgl_mux, timeout_ticks) == pdTRUE;
}

void lvgl_port_unlock(void)
{
    xSemaphoreGiveRecursive(lvgl_mux);
}

static void example_lvgl_port_task(void *arg)
{
    ESP_LOGI(TAG, "Starting LVGL task");
    uint32_t task_delay_ms = EXAMPLE_LVGL_TASK_MAX_DELAY_MS;
    while (1) {
        if (lvgl_port_lock(-1)) {
            task_delay_ms = lv_timer_handler();
            lvgl_port_unlock();
        }
        if (task_delay_ms > EXAMPLE_LVGL_TASK_MAX_DELAY_MS) task_delay_ms = EXAMPLE_LVGL_TASK_MAX_DELAY_MS;
        else if (task_delay_ms < EXAMPLE_LVGL_TASK_MIN_DELAY_MS) task_delay_ms = EXAMPLE_LVGL_TASK_MIN_DELAY_MS;
        vTaskDelay(pdMS_TO_TICKS(task_delay_ms));
    }
}

void lcd_init(void)
{
    ESP_LOGI(TAG, "Initializing Waveshare ST7262 display (component)");

    // Turn off backlight if present
#if EXAMPLE_PIN_NUM_BK_LIGHT >= 0
    gpio_config_t bk_gpio_config = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << EXAMPLE_PIN_NUM_BK_LIGHT,
    };
    gpio_config(&bk_gpio_config);
    gpio_set_level(EXAMPLE_PIN_NUM_BK_LIGHT, 0);
#endif

    // Install RGB LCD panel driver
    esp_lcd_panel_handle_t panel_handle = NULL;
    esp_lcd_rgb_panel_config_t panel_config = {
        .data_width = 16,
        .psram_trans_align = 64,
        .num_fbs = 1,
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .disp_gpio_num = EXAMPLE_PIN_NUM_DISP_EN,
        .pclk_gpio_num = EXAMPLE_PIN_NUM_PCLK,
        .vsync_gpio_num = EXAMPLE_PIN_NUM_VSYNC,
        .hsync_gpio_num = EXAMPLE_PIN_NUM_HSYNC,
        .de_gpio_num = EXAMPLE_PIN_NUM_DE,
        .data_gpio_nums = {
            EXAMPLE_PIN_NUM_DATA0, EXAMPLE_PIN_NUM_DATA1, EXAMPLE_PIN_NUM_DATA2, EXAMPLE_PIN_NUM_DATA3,
            EXAMPLE_PIN_NUM_DATA4, EXAMPLE_PIN_NUM_DATA5, EXAMPLE_PIN_NUM_DATA6, EXAMPLE_PIN_NUM_DATA7,
            EXAMPLE_PIN_NUM_DATA8, EXAMPLE_PIN_NUM_DATA9, EXAMPLE_PIN_NUM_DATA10, EXAMPLE_PIN_NUM_DATA11,
            EXAMPLE_PIN_NUM_DATA12, EXAMPLE_PIN_NUM_DATA13, EXAMPLE_PIN_NUM_DATA14, EXAMPLE_PIN_NUM_DATA15
        },
        .timings = {
            .pclk_hz = EXAMPLE_LCD_PIXEL_CLOCK_HZ,
            .h_res = EXAMPLE_LCD_H_RES,
            .v_res = EXAMPLE_LCD_V_RES,
            .hsync_back_porch = 40,
            .hsync_front_porch = 20,
            .hsync_pulse_width = 1,
            .vsync_back_porch = 8,
            .vsync_front_porch = 4,
            .vsync_pulse_width = 1,
            .flags.pclk_active_neg = true,
        },
        .flags.fb_in_psram = true,
    };

    ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&panel_config, &panel_handle));
    esp_lcd_rgb_panel_event_callbacks_t cbs = {
        .on_vsync = example_on_vsync_event,
    };
    ESP_ERROR_CHECK(esp_lcd_rgb_panel_register_event_callbacks(panel_handle, &cbs, NULL));

    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));

    // Initialize LVGL
    lv_init();

    // Setup draw buffers
    void *buf1 = NULL;
    buf1 = heap_caps_malloc(EXAMPLE_LCD_H_RES * 100 * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    assert(buf1);
    static lv_disp_draw_buf_t disp_buf;
    lv_disp_draw_buf_init(&disp_buf, buf1, NULL, EXAMPLE_LCD_H_RES * 100);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = EXAMPLE_LCD_H_RES;
    disp_drv.ver_res = EXAMPLE_LCD_V_RES;
    disp_drv.flush_cb = example_lvgl_flush_cb;
    disp_drv.draw_buf = &disp_buf;
    disp_drv.user_data = panel_handle;
    lv_disp_t *disp = lv_disp_drv_register(&disp_drv);
    (void)disp;

    // Install LVGL tick timer
    const esp_timer_create_args_t lvgl_tick_timer_args = {
        .callback = &example_increase_lvgl_tick,
        .name = "lvgl_tick"
    };
    esp_timer_handle_t lvgl_tick_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_tick_timer, EXAMPLE_LVGL_TICK_PERIOD_MS * 1000));

    // Create semaphores and LVGL task
    lvgl_mux = xSemaphoreCreateRecursiveMutex();
    assert(lvgl_mux);
    sem_vsync_end = xSemaphoreCreateBinary();
    sem_gui_ready = xSemaphoreCreateBinary();

    xTaskCreate(example_lvgl_port_task, "LVGL", EXAMPLE_LVGL_TASK_STACK_SIZE, NULL, EXAMPLE_LVGL_TASK_PRIORITY, NULL);

#if EXAMPLE_PIN_NUM_BK_LIGHT >= 0
    gpio_set_level(EXAMPLE_PIN_NUM_BK_LIGHT, 1);
#endif

    ESP_LOGI(TAG, "Waveshare ST7262 initialized");
}
