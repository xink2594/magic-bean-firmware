#pragma once

#include "esp_err.h"

// 定义系统的全局 UI 状态
typedef enum
{
    UI_STATE_BOOTING = 0,     // 系统启动中
    UI_STATE_WIFI_CONNECTING, // 网络连接中
    UI_STATE_IDLE,            // 待机状态 (大眼睛)
    UI_STATE_THINKING,        // LLM 思考中 (>_<)
    UI_STATE_SPEAKING,        // 输出中/动作执行中
    UI_STATE_ERROR            // 发生错误 (如死锁或断网)
} mimi_ui_state_t;

/**
 * @brief 初始化 I2C 总线并点亮 SSD1306 OLED 屏幕
 * @note  I2C SDA: 41, SCL: 42. 预留给后续 APDS9960 复用。
 * @return esp_err_t ESP_OK 成功
 */
esp_err_t oled_init(void);

/**
 * @brief 更新屏幕显示的 UI 状态（非阻塞）
 * @param state 目标 UI 状态枚举
 */
void oled_set_state(mimi_ui_state_t state);