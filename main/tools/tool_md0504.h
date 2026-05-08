#pragma once

#include "esp_err.h"
#include <stddef.h>

esp_err_t tool_md0504_init(void);
esp_err_t tool_md0504_read(float *humidity_percent, int *raw_value);
esp_err_t tool_md0504_read_execute(const char *input_json, char *output, size_t output_size);
