#include "tools/tool_md0504.h"

#include "cJSON.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "tool_md0504";

#define MD0504_AO_GPIO 19
#define MD0504_ADC_UNIT ADC_UNIT_2
#define MD0504_ADC_CHANNEL ADC_CHANNEL_8
#define MD0504_DRY_RAW 3000
#define MD0504_WET_RAW 1200

static adc_oneshot_unit_handle_t s_adc_handle;
static SemaphoreHandle_t s_adc_mutex;
static bool s_initialized;

static float raw_to_percent(int raw)
{
    float dry = (float)MD0504_DRY_RAW;
    float wet = (float)MD0504_WET_RAW;
    if (fabsf(dry - wet) < 1.0f) {
        return 0.0f;
    }

    float pct = (dry - (float)raw) * 100.0f / (dry - wet);
    if (pct < 0.0f) pct = 0.0f;
    if (pct > 100.0f) pct = 100.0f;
    return pct;
}

esp_err_t tool_md0504_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    if (!s_adc_mutex) {
        s_adc_mutex = xSemaphoreCreateMutex();
        if (!s_adc_mutex) {
            return ESP_ERR_NO_MEM;
        }
    }

    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = MD0504_ADC_UNIT,
    };
    esp_err_t err = adc_oneshot_new_unit(&unit_cfg, &s_adc_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ADC unit init failed: %s", esp_err_to_name(err));
        return err;
    }

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    err = adc_oneshot_config_channel(s_adc_handle, MD0504_ADC_CHANNEL, &chan_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ADC channel config failed: %s", esp_err_to_name(err));
        adc_oneshot_del_unit(s_adc_handle);
        s_adc_handle = NULL;
        return err;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "MD0504 initialized: AO=GPIO%d ADC%d_CH%d",
             MD0504_AO_GPIO, MD0504_ADC_UNIT + 1, MD0504_ADC_CHANNEL);
    return ESP_OK;
}

esp_err_t tool_md0504_read(float *humidity_percent, int *raw_value)
{
    if (!humidity_percent || !raw_value) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = tool_md0504_init();
    if (err != ESP_OK) {
        return err;
    }

    if (xSemaphoreTake(s_adc_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    int raw = 0;
    err = adc_oneshot_read(s_adc_handle, MD0504_ADC_CHANNEL, &raw);
    xSemaphoreGive(s_adc_mutex);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "MD0504 ADC read failed: %s", esp_err_to_name(err));
        return err;
    }

    *raw_value = raw;
    *humidity_percent = raw_to_percent(raw);
    ESP_LOGI(TAG, "MD0504 read: raw=%d humidity=%.1f%%", raw, *humidity_percent);
    return ESP_OK;
}

esp_err_t tool_md0504_read_execute(const char *input_json, char *output, size_t output_size)
{
    (void)input_json;

    float humidity = 0.0f;
    int raw = 0;
    esp_err_t err = tool_md0504_read(&humidity, &raw);
    if (err != ESP_OK) {
        snprintf(output, output_size, "Error: failed to read MD0504 on GPIO%d (%s)",
                 MD0504_AO_GPIO, esp_err_to_name(err));
        return err;
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        snprintf(output, output_size, "Error: not enough memory");
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddNumberToObject(root, "dirt_humidity", humidity);
    cJSON_AddNumberToObject(root, "raw", raw);
    cJSON_AddNumberToObject(root, "ao_gpio", MD0504_AO_GPIO);

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) {
        snprintf(output, output_size, "Error: not enough memory");
        return ESP_ERR_NO_MEM;
    }

    snprintf(output, output_size, "%s", json);
    free(json);
    return ESP_OK;
}
