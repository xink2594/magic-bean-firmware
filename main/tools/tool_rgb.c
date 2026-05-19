#include "tools/tool_registry.h"
#include "tools/tool_rgb.h"
#include "led_strip.h"
#include "esp_log.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "tool_rgb";

#define RGB_LED_PIN   38
#define WS2812_COUNT  22
#define WATER_CHUNK    6
#define ANIM_INTERVAL_MS  120
#define WATER_DEFAULT_DURATION_S  5
#define WATER_DEFAULT_R  0
#define WATER_DEFAULT_G  100
#define WATER_DEFAULT_B  255

typedef enum { LED_OFF, LED_GROW_LIGHT, LED_WATER_FLOW } led_mode_t;

static led_strip_handle_t led_strip;
static bool is_initialized = false;
static SemaphoreHandle_t s_led_mutex = NULL;
static StaticSemaphore_t s_mutex_buf;

static led_mode_t s_mode = LED_OFF;
static uint8_t s_light_r, s_light_g, s_light_b;
static volatile bool s_water_running = false;
static TaskHandle_t s_water_task = NULL;
static uint8_t s_water_r, s_water_g, s_water_b;
static int s_water_total_ms;
static int s_water_elapsed;

static void set_all_pixels(uint8_t r, uint8_t g, uint8_t b)
{
    xSemaphoreTake(s_led_mutex, portMAX_DELAY);
    for (int i = 0; i < WS2812_COUNT; i++) {
        led_strip_set_pixel(led_strip, i, r, g, b);
    }
    led_strip_refresh(led_strip);
    xSemaphoreGive(s_led_mutex);
}

esp_err_t tool_rgb_init(void)
{
    if (is_initialized) return ESP_OK;

    led_strip_config_t strip_config = {
        .strip_gpio_num = RGB_LED_PIN,
        .max_leds = WS2812_COUNT,
    };
    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000,
    };

    esp_err_t err = led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize WS2812 on GPIO %d", RGB_LED_PIN);
        return err;
    }

    s_led_mutex = xSemaphoreCreateMutexStatic(&s_mutex_buf);
    led_strip_clear(led_strip);
    s_mode = LED_OFF;
    s_water_running = false;
    is_initialized = true;
    ESP_LOGI(TAG, "WS2812 %d-LED strip initialized on GPIO %d", WS2812_COUNT, RGB_LED_PIN);
    return ESP_OK;
}

/* 核心执行逻辑：接收 {"r": 255, "g": 0, "b": 128} 格式的 JSON */
esp_err_t tool_rgb_execute(const char *input_json, char *output, size_t output_size)
{
    if (!is_initialized)
    {
        snprintf(output, output_size, "Error: RGB LED not initialized");
        return ESP_FAIL;
    }

    cJSON *root = cJSON_Parse(input_json);
    if (!root)
    {
        snprintf(output, output_size, "Error: invalid JSON input");
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *r_obj = cJSON_GetObjectItem(root, "r");
    cJSON *g_obj = cJSON_GetObjectItem(root, "g");
    cJSON *b_obj = cJSON_GetObjectItem(root, "b");

    // 严谨校验输入参数
    if (!cJSON_IsNumber(r_obj) || !cJSON_IsNumber(g_obj) || !cJSON_IsNumber(b_obj))
    {
        snprintf(output, output_size, "Error: 'r', 'g', 'b' required (0-255)");
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    int r = (int)r_obj->valuedouble;
    int g = (int)g_obj->valuedouble;
    int b = (int)b_obj->valuedouble;

    // 防止大模型乱给数值
    r = (r < 0) ? 0 : (r > 255) ? 255
                                : r;
    g = (g < 0) ? 0 : (g > 255) ? 255
                                : g;
    b = (b < 0) ? 0 : (b > 255) ? 255
                                : b;

    // 驱动底层发光
    set_all_pixels(r, g, b);
    s_mode = LED_GROW_LIGHT;
    s_light_r = r; s_light_g = g; s_light_b = b;

    // 组装返回给大模型的话术
    snprintf(output, output_size, "{\"status\":\"success\", \"current_color\": {\"r\":%d, \"g\":%d, \"b\":%d}}", r, g, b);
    ESP_LOGI(TAG, "RGB LED set to (%d, %d, %d)", r, g, b);

    cJSON_Delete(root);
    return ESP_OK;
}

/* ========== 补光灯：全亮指定颜色 ========== */

esp_err_t tool_rgb_set_all(uint8_t r, uint8_t g, uint8_t b)
{
    if (!is_initialized) return ESP_ERR_INVALID_STATE;

    if (s_water_running) {
        s_water_running = false;
        if (s_water_task) {
            vTaskDelay(pdMS_TO_TICKS(ANIM_INTERVAL_MS * 2));
        }
    }

    s_mode = LED_GROW_LIGHT;
    s_light_r = r; s_light_g = g; s_light_b = b;
    set_all_pixels(r, g, b);
    ESP_LOGI(TAG, "Grow light ON (%d,%d,%d)", r, g, b);
    return ESP_OK;
}

/* ========== 流水动画任务 ========== */

static void water_flow_task(void *arg)
{
    (void)arg;
    int pos = 0;

    while (s_water_running) {
        xSemaphoreTake(s_led_mutex, portMAX_DELAY);
        for (int i = 0; i < WS2812_COUNT; i++) {
            led_strip_set_pixel(led_strip, i, 0, 0, 0);
        }
        for (int j = 0; j < WATER_CHUNK; j++) {
            int idx = (pos + j) % WS2812_COUNT;
            led_strip_set_pixel(led_strip, idx, s_water_r, s_water_g, s_water_b);
        }
        led_strip_refresh(led_strip);
        xSemaphoreGive(s_led_mutex);

        vTaskDelay(pdMS_TO_TICKS(ANIM_INTERVAL_MS));
        pos = (pos + 1) % WS2812_COUNT;
        s_water_elapsed += ANIM_INTERVAL_MS;

        if (s_water_elapsed >= s_water_total_ms) {
            s_water_running = false;
        }
    }

    /* 恢复之前的状态 */
    if (s_mode == LED_GROW_LIGHT) {
        set_all_pixels(s_light_r, s_light_g, s_light_b);
        ESP_LOGI(TAG, "Water flow done, grow light restored");
    } else {
        set_all_pixels(0, 0, 0);
        ESP_LOGI(TAG, "Water flow done, LEDs off");
    }

    s_water_task = NULL;
    vTaskDelete(NULL);
}

/* ========== 启动流水动画 ========== */

esp_err_t tool_rgb_water_flow_start(uint8_t r, uint8_t g, uint8_t b, int duration_s)
{
    if (!is_initialized) return ESP_ERR_INVALID_STATE;

    if (s_water_running) {
        s_water_running = false;
        vTaskDelay(pdMS_TO_TICKS(ANIM_INTERVAL_MS * 2));
    }

    if (duration_s <= 0) duration_s = WATER_DEFAULT_DURATION_S;

    s_water_r = r; s_water_g = g; s_water_b = b;
    s_water_total_ms = duration_s * 1000;
    s_water_elapsed = 0;
    s_water_running = true;

    if (xTaskCreate(water_flow_task, "water_anim",
                    3 * 1024, NULL, 4, &s_water_task) != pdPASS) {
        s_water_running = false;
        ESP_LOGE(TAG, "Failed to create water flow task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Water flow started (%d,%d,%d) for %ds", r, g, b, duration_s);
    return ESP_OK;
}

/* ========== 停止一切 ========== */

esp_err_t tool_rgb_stop(void)
{
    if (!is_initialized) return ESP_ERR_INVALID_STATE;

    if (s_water_running) {
        s_water_running = false;
        vTaskDelay(pdMS_TO_TICKS(ANIM_INTERVAL_MS * 2));
    }

    s_mode = LED_OFF;
    set_all_pixels(0, 0, 0);
    ESP_LOGI(TAG, "All LEDs off");
    return ESP_OK;
}