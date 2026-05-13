#pragma once

#include "esp_err.h"

esp_err_t tool_mqtt_init(void);
esp_err_t tool_mqtt_start(void);
esp_err_t tool_mqtt_publish_sensor_data(void);
esp_err_t tool_mqtt_publish_capture_response(const char *msg_id, const char *image_url);
esp_err_t tool_mqtt_publish_log(const char *module, const char *message);
