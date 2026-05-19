#pragma once

#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>

esp_err_t tool_rgb_init(void);
esp_err_t tool_rgb_execute(const char *input_json, char *output, size_t output_size);

/** 补光灯：全亮指定颜色 */
esp_err_t tool_rgb_set_all(uint8_t r, uint8_t g, uint8_t b);

/** 流水动画（duration_s 秒后自动停止，停止后恢复之前状态） */
esp_err_t tool_rgb_water_flow_start(uint8_t r, uint8_t g, uint8_t b, int duration_s);

/** 停止一切，熄灭所有灯 */
esp_err_t tool_rgb_stop(void);