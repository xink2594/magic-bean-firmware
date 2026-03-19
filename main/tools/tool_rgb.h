#pragma once

#include "esp_err.h"
#include <stddef.h>

/**
 * @brief 初始化 WS2812 RGB 灯条
 * @return esp_err_t ESP_OK 成功
 */
esp_err_t tool_rgb_init(void);

/**
 * @brief 解析大模型传入的 JSON 并控制 RGB 灯
 * @param input_json 大模型生成的 JSON 字符串 (如 {"r": 255, "g": 0, "b": 128})
 * @param output 存放执行结果的缓冲区，将返回给大模型
 * @param output_size 缓冲区大小
 * @return esp_err_t ESP_OK 成功
 */
esp_err_t tool_rgb_execute(const char *input_json, char *output, size_t output_size);