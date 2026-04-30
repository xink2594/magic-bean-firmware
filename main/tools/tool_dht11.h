#pragma once

#include "esp_err.h"
#include <stddef.h>

/**
 * Initialize the DHT11 tool.
 */
esp_err_t tool_dht11_init(void);

/**
 * Read indoor temperature and humidity from a DHT11 sensor.
 * Input JSON: {"pin": <int>} where pin is optional and defaults to GPIO2.
 */
esp_err_t tool_dht11_read_execute(const char *input_json, char *output, size_t output_size);
