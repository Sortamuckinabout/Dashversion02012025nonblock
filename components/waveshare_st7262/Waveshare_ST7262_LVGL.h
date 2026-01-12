#pragma once

#include "lvgl.h"
#include "esp_lcd_panel_ops.h"

#ifdef __cplusplus
extern "C" {
#endif

void lcd_init(void);
// Lock the LVGL port (timeout_ms: -1 for infinite)
bool lvgl_port_lock(int timeout_ms);
void lvgl_port_unlock(void);

#ifdef __cplusplus
}
#endif
