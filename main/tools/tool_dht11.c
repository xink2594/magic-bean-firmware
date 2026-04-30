#include "tools/tool_dht11.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "cJSON.h"
#include "dht.h"

#include <stdio.h>

static const char *TAG = "tool_dht11";

#define DHT11_DEFAULT_GPIO GPIO_NUM_2

esp_err_t tool_dht11_init(void)
{
    ESP_LOGI(TAG, "DHT11 tool initialized (default pin=%d)", DHT11_DEFAULT_GPIO);
    return ESP_OK;
}

esp_err_t tool_dht11_read_execute(const char *input_json, char *output, size_t output_size)
{
    gpio_num_t pin = DHT11_DEFAULT_GPIO;

    if (input_json != NULL && input_json[0] != '\0')
    {
        cJSON *root = cJSON_Parse(input_json);
        if (!root)
        {
            snprintf(output, output_size, "Error: invalid JSON input");
            return ESP_ERR_INVALID_ARG;
        }

        cJSON *pin_obj = cJSON_GetObjectItem(root, "pin");
        if (pin_obj != NULL)
        {
            if (!cJSON_IsNumber(pin_obj))
            {
                snprintf(output, output_size, "Error: 'pin' must be an integer");
                cJSON_Delete(root);
                return ESP_ERR_INVALID_ARG;
            }

            int pin_num = (int)pin_obj->valuedouble;
            if (pin_num < 0 || pin_num >= GPIO_NUM_MAX)
            {
                snprintf(output, output_size, "Error: invalid GPIO pin %d", pin_num);
                cJSON_Delete(root);
                return ESP_ERR_INVALID_ARG;
            }
            pin = (gpio_num_t)pin_num;
        }

        cJSON_Delete(root);
    }

    float humidity = 0.0f;
    float temperature = 0.0f;
    esp_err_t err = dht_read_float_data(DHT_TYPE_DHT11, pin, &humidity, &temperature);
    if (err != ESP_OK)
    {
        snprintf(output, output_size,
                 "Error: failed to read DHT11 on GPIO%d (%s)",
                 (int)pin, esp_err_to_name(err));
        ESP_LOGE(TAG, "DHT11 read failed on GPIO%d: %s", (int)pin, esp_err_to_name(err));
        return err;
    }

    snprintf(output, output_size,
             "Indoor temperature is %.1f C, humidity is %.1f%% (DHT11 on GPIO%d).",
             temperature, humidity, (int)pin);
    ESP_LOGI(TAG, "DHT11 read success on GPIO%d: temp=%.1fC humidity=%.1f%%",
             (int)pin, temperature, humidity);
    return ESP_OK;
}
