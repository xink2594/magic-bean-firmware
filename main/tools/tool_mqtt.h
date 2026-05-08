#pragma once

#include "esp_err.h"

esp_err_t tool_mqtt_init(void);
esp_err_t tool_mqtt_start(void);
esp_err_t tool_mqtt_publish_sensor_data(void);
