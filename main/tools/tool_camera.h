#pragma once

#include "esp_err.h"
#include <stddef.h>

/**
 * @brief 初始化摄像头工具上下文
 * 创建互斥锁等运行时资源，摄像头硬件在真正拍照时才上电初始化。
 *
 * @return ESP_OK 成功，其他表示失败
 */
esp_err_t tool_camera_init(void);

/**
 * @brief 执行拍照并上传
 * * @param input_json   AI传入的JSON参数 (这里可以为空，因为拍照不需要额外参数)
 * @param output       输出给大模型的返回结果 (将包含图片的URL)
 * @param output_size  输出缓冲区的最大长度
 * @return ESP_OK 成功
 */
esp_err_t tool_camera_execute(const char *input_json, char *output, size_t output_size);
