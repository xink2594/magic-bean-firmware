#include "tools/tool_mqtt.h"

#include "mimi_config.h"
#include "tools/tool_camera.h"
#include "tools/tool_md0504.h"
#include "tools/tool_rgb.h"

#include "cJSON.h"
#include "dht.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mqtt_client.h"
#include "nvs.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const char *TAG = "tool_mqtt";

#define MQTT_TOPIC_LEN 96
#define MQTT_CLIENT_ID_LEN 40
#define MQTT_COMMAND_STACK (8 * 1024)
#define MQTT_DEBUG_STACK (5 * 1024)
#define MQTT_DATA_STACK (5 * 1024)
#define MQTT_DATA_PRIO 4
#define MQTT_DATA_CHECK_INTERVAL_MS (5 * 1000)
#define MQTT_DHT11_GPIO 2
#define MQTT_WATER_GPIO 37
#define MQTT_WATER_ACTIVE_LEVEL 1

static esp_mqtt_client_handle_t s_client;
static bool s_started;
static bool s_connected;
static char s_mac[13];
static char s_client_id[MQTT_CLIENT_ID_LEN];
static char s_status_topic[MQTT_TOPIC_LEN];
static char s_data_topic[MQTT_TOPIC_LEN];
static char s_cmd_topic[MQTT_TOPIC_LEN];
static char s_response_topic[MQTT_TOPIC_LEN];
static char s_debug_topic[MQTT_TOPIC_LEN];
static char s_log_topic[MQTT_TOPIC_LEN];
static char s_light_topic[MQTT_TOPIC_LEN];
static char s_broker[192];
static char s_port[8];
static char s_broker_uri[256];
static char s_username[128];
static char s_password[128];

typedef struct {
    char *payload;
    int len;
} mqtt_cmd_t;

static esp_err_t publish_light_state(bool on, uint8_t r, uint8_t g, uint8_t b);

static bool mqtt_enabled(void)
{
    return s_broker_uri[0] != '\0';
}

static void load_config_string(const char *key, const char *build_value, char *out, size_t out_size)
{
    out[0] = '\0';

    nvs_handle_t nvs;
    if (nvs_open(MIMI_NVS_MQTT, NVS_READONLY, &nvs) == ESP_OK) {
        size_t len = out_size;
        if (nvs_get_str(nvs, key, out, &len) == ESP_OK && out[0] != '\0') {
            nvs_close(nvs);
            return;
        }
        nvs_close(nvs);
    }

    if (build_value) {
        strlcpy(out, build_value, out_size);
    }
}

static void split_legacy_uri(const char *uri, char *broker, size_t broker_size, char *port, size_t port_size)
{
    const char *start = uri;
    if (strncmp(start, "mqtt://", 7) == 0) {
        start += 7;
    }

    const char *colon = strrchr(start, ':');
    if (colon && colon > start && *(colon + 1) != '\0') {
        size_t host_len = (size_t)(colon - start);
        if (host_len >= broker_size) {
            host_len = broker_size - 1;
        }
        memcpy(broker, start, host_len);
        broker[host_len] = '\0';
        strlcpy(port, colon + 1, port_size);
    } else {
        strlcpy(broker, start, broker_size);
    }
}

static void load_mqtt_config(void)
{
    load_config_string(MIMI_NVS_KEY_MQTT_BROKER, MIMI_SECRET_MQTT_BROKER,
                       s_broker, sizeof(s_broker));
    load_config_string(MIMI_NVS_KEY_MQTT_PORT, MIMI_SECRET_MQTT_PORT,
                       s_port, sizeof(s_port));

    if (s_broker[0] == '\0') {
        char legacy_uri[256] = {0};
        load_config_string(MIMI_NVS_KEY_MQTT_BROKER_URI, MIMI_SECRET_MQTT_BROKER_URI,
                           legacy_uri, sizeof(legacy_uri));
        if (legacy_uri[0] != '\0') {
            split_legacy_uri(legacy_uri, s_broker, sizeof(s_broker), s_port, sizeof(s_port));
        }
    }
    if (strncmp(s_broker, "mqtt://", 7) == 0 || strchr(s_broker, ':') != NULL) {
        char normalized_broker[192] = {0};
        char normalized_port[8] = {0};
        split_legacy_uri(s_broker, normalized_broker, sizeof(normalized_broker),
                         normalized_port, sizeof(normalized_port));
        if (normalized_broker[0] != '\0') {
            strlcpy(s_broker, normalized_broker, sizeof(s_broker));
        }
        if (normalized_port[0] != '\0') {
            strlcpy(s_port, normalized_port, sizeof(s_port));
        }
    }

    if (s_broker[0] != '\0') {
        const char *port = s_port[0] ? s_port : "1883";
        snprintf(s_broker_uri, sizeof(s_broker_uri), "mqtt://%s:%s", s_broker, port);
    } else {
        s_broker_uri[0] = '\0';
    }

    load_config_string(MIMI_NVS_KEY_MQTT_USERNAME, MIMI_SECRET_MQTT_USERNAME,
                       s_username, sizeof(s_username));
    load_config_string(MIMI_NVS_KEY_MQTT_PASSWORD, MIMI_SECRET_MQTT_PASSWORD,
                       s_password, sizeof(s_password));
}

static void init_topics(void)
{
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(s_mac, sizeof(s_mac), "%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    snprintf(s_status_topic, sizeof(s_status_topic), "plant/%s/status", s_mac);
    snprintf(s_data_topic, sizeof(s_data_topic), "plant/%s/data", s_mac);
    snprintf(s_cmd_topic, sizeof(s_cmd_topic), "plant/%s/cmd", s_mac);
    snprintf(s_response_topic, sizeof(s_response_topic), "plant/%s/response", s_mac);
    snprintf(s_debug_topic, sizeof(s_debug_topic), "plant/%s/debug", s_mac);
    snprintf(s_log_topic, sizeof(s_log_topic), "plant/%s/log", s_mac);
    snprintf(s_light_topic, sizeof(s_light_topic), "plant/%s/light", s_mac);

    char configured_client_id[MQTT_CLIENT_ID_LEN] = {0};
    load_config_string(MIMI_NVS_KEY_MQTT_CLIENT_ID, MIMI_SECRET_MQTT_CLIENT_ID,
                       configured_client_id, sizeof(configured_client_id));

    if (configured_client_id[0] != '\0') {
        snprintf(s_client_id, sizeof(s_client_id), "%s", configured_client_id);
    } else {
        snprintf(s_client_id, sizeof(s_client_id), "plant_%s", s_mac);
    }
}

static bool topic_matches(const char *topic, int topic_len, const char *expected)
{
    return topic_len == (int)strlen(expected) && strncmp(topic, expected, topic_len) == 0;
}

typedef struct {
    float temperature;
    float air_humidity;
    float dirt_humidity;
    int soil_raw;
    esp_err_t dht_err;
    esp_err_t soil_err;
} mqtt_sensor_data_t;

#define SENSOR_INVALID_VALUE -999.0f
#define SENSOR_RETRY_COUNT 3
#define SENSOR_RETRY_DELAY_MS 500

static void read_sensor_data(mqtt_sensor_data_t *data)
{
    memset(data, 0, sizeof(*data));

    // 初始化为无效值
    data->temperature = SENSOR_INVALID_VALUE;
    data->air_humidity = SENSOR_INVALID_VALUE;
    data->dirt_humidity = SENSOR_INVALID_VALUE;

    // DHT11 重试读取
    for (int i = 0; i < SENSOR_RETRY_COUNT; i++) {
        data->dht_err = dht_read_float_data(DHT_TYPE_DHT11,
                                            (gpio_num_t)MQTT_DHT11_GPIO,
                                            &data->air_humidity,
                                            &data->temperature);
        if (data->dht_err == ESP_OK) {
            ESP_LOGI(TAG, "DHT11 read success on attempt %d: temp=%.1fC humidity=%.1f%%",
                     i + 1, data->temperature, data->air_humidity);
            break;
        }
        ESP_LOGW(TAG, "DHT11 read attempt %d failed: %s", i + 1, esp_err_to_name(data->dht_err));
        if (i < SENSOR_RETRY_COUNT - 1) {
            vTaskDelay(pdMS_TO_TICKS(SENSOR_RETRY_DELAY_MS));
        }
    }
    if (data->dht_err != ESP_OK) {
        ESP_LOGE(TAG, "DHT11 read failed after %d attempts", SENSOR_RETRY_COUNT);
        data->temperature = SENSOR_INVALID_VALUE;
        data->air_humidity = SENSOR_INVALID_VALUE;
    }

    // MD0504 重试读取
    for (int i = 0; i < SENSOR_RETRY_COUNT; i++) {
        data->soil_err = tool_md0504_read(&data->dirt_humidity, &data->soil_raw);
        if (data->soil_err == ESP_OK) {
            ESP_LOGI(TAG, "MD0504 read success on attempt %d: soil=%.1f%%",
                     i + 1, data->dirt_humidity);
            break;
        }
        ESP_LOGW(TAG, "MD0504 read attempt %d failed: %s", i + 1, esp_err_to_name(data->soil_err));
        if (i < SENSOR_RETRY_COUNT - 1) {
            vTaskDelay(pdMS_TO_TICKS(SENSOR_RETRY_DELAY_MS));
        }
    }
    if (data->soil_err != ESP_OK) {
        ESP_LOGE(TAG, "MD0504 read failed after %d attempts", SENSOR_RETRY_COUNT);
        data->dirt_humidity = SENSOR_INVALID_VALUE;
    }
}

static esp_err_t publish_payload(const char *topic, const char *payload)
{
    if (!s_client || !s_connected) {
        return ESP_ERR_INVALID_STATE;
    }

    int msg_id = esp_mqtt_client_publish(s_client, topic, payload, 0, 1, 0);
    return (msg_id < 0) ? ESP_FAIL : ESP_OK;
}

esp_err_t tool_mqtt_publish_log(const char *module, const char *message)
{
    if (!module) {
        module = "system";
    }
    if (!message) {
        message = "";
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddStringToObject(root, "type", "log");
    cJSON_AddStringToObject(root, "module", module);
    cJSON_AddStringToObject(root, "message", message);

    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!payload) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = publish_payload(s_log_topic, payload);
    free(payload);
    return err;
}

esp_err_t tool_mqtt_publish_sensor_data(void)
{
    mqtt_sensor_data_t data;
    read_sensor_data(&data);

    char payload[128];
    snprintf(payload, sizeof(payload),
             "{\"temperature\":%.1f,\"air_humidity\":%.1f,\"dirt_humidity\":%.1f}",
             data.temperature, data.air_humidity, data.dirt_humidity);

    esp_err_t err = publish_payload(s_data_topic, payload);
    if (err != ESP_OK) {
        return err;
    }

    ESP_LOGI(TAG, "MQTT sensor data published: %s", payload);
    return ESP_OK;
}

esp_err_t tool_mqtt_publish_capture_response(const char *msg_id, const char *image_url)
{
    if (!msg_id || !image_url) {
        return ESP_ERR_INVALID_ARG;
    }

    // 读取当前传感器数据
    mqtt_sensor_data_t data;
    read_sensor_data(&data);

    // 构建响应 JSON
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddStringToObject(root, "msg_id", msg_id);
    cJSON_AddStringToObject(root, "action_reply", "capture");

    cJSON *data_obj = cJSON_CreateObject();
    if (!data_obj) {
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddStringToObject(data_obj, "url", image_url);
    cJSON_AddNumberToObject(data_obj, "temp", data.temperature);
    cJSON_AddNumberToObject(data_obj, "humi", data.air_humidity);
    cJSON_AddNumberToObject(data_obj, "soil", data.dirt_humidity);

    cJSON_AddItemToObject(root, "data", data_obj);

    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!payload) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = publish_payload(s_response_topic, payload);
    ESP_LOGI(TAG, "MQTT capture response published to %s: %s", s_response_topic, payload);
    free(payload);

    return err;
}

static esp_err_t publish_debug_sensor_data(void)
{
    mqtt_sensor_data_t data;
    read_sensor_data(&data);

    char payload[256];
    snprintf(payload, sizeof(payload),
             "{\"type\":\"debug\",\"cmd\":\"data_reply\",\"temperature\":%.1f,\"air_humidity\":%.1f,"
             "\"dirt_humidity\":%.1f,\"soil_raw\":%d,\"dht_error\":\"%s\",\"soil_error\":\"%s\"}",
             data.temperature, data.air_humidity, data.dirt_humidity, data.soil_raw,
             esp_err_to_name(data.dht_err), esp_err_to_name(data.soil_err));

    esp_err_t err = publish_payload(s_log_topic, payload);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "MQTT debug data logged: %s", payload);
    }
    return err;
}

static void publish_status(const char *status)
{
    if (!s_client) {
        return;
    }
    char payload[32];
    snprintf(payload, sizeof(payload), "{\"status\":\"%s\"}", status);
    esp_mqtt_client_publish(s_client, s_status_topic, payload, 0, 1, 1);
}

static void data_publish_task(void *arg)
{
    (void)arg;
    int last_slot_key = -1;

    while (1) {
        if (s_connected) {
            time_t now = time(NULL);
            struct tm tm_now = {0};
            localtime_r(&now, &tm_now);

            bool time_ready = (tm_now.tm_year >= (2024 - 1900));
            if (time_ready && (tm_now.tm_min == 0 || tm_now.tm_min == 30) && tm_now.tm_sec < 10) {
                int slot_key = (tm_now.tm_yday * 24 * 60) + (tm_now.tm_hour * 60) + tm_now.tm_min;
                if (slot_key != last_slot_key) {
                    esp_err_t err = tool_mqtt_publish_sensor_data();
                    ESP_LOGI(TAG, "MQTT scheduled data publish at %02d:%02d:%02d: %s",
                             tm_now.tm_hour, tm_now.tm_min, tm_now.tm_sec, esp_err_to_name(err));
                    last_slot_key = slot_key;
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(MQTT_DATA_CHECK_INTERVAL_MS));
    }
}

static esp_err_t run_water_command(int set_time_s)
{
    if (MQTT_WATER_GPIO < 0) {
        ESP_LOGW(TAG, "MQTT water command ignored; water GPIO is disabled");
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (set_time_s <= 0) {
        set_time_s = 5;
    }
    if (set_time_s > 60) {
        set_time_s = 60;
    }

    int active = MQTT_WATER_ACTIVE_LEVEL ? 1 : 0;
    int inactive = active ? 0 : 1;
    gpio_num_t pin = (gpio_num_t)MQTT_WATER_GPIO;

    ESP_RETURN_ON_ERROR(gpio_set_direction(pin, GPIO_MODE_OUTPUT), TAG, "Failed to configure water GPIO");
    ESP_LOGI(TAG, "Water pump on GPIO%d for %d seconds", (int)pin, set_time_s);
    ESP_RETURN_ON_ERROR(gpio_set_level(pin, active), TAG, "Failed to enable water GPIO");
    vTaskDelay(pdMS_TO_TICKS(set_time_s * 1000));
    ESP_RETURN_ON_ERROR(gpio_set_level(pin, inactive), TAG, "Failed to disable water GPIO");
    ESP_LOGI(TAG, "Water pump stopped");
    return ESP_OK;
}

static void command_task(void *arg)
{
    mqtt_cmd_t *cmd = (mqtt_cmd_t *)arg;
    char *payload = cmd->payload;

    cJSON *root = cJSON_ParseWithLength(payload, cmd->len);
    if (!root) {
        ESP_LOGW(TAG, "Invalid MQTT command JSON");
        goto cleanup;
    }

    cJSON *msg_id = cJSON_GetObjectItem(root, "msg_id");
    cJSON *action = cJSON_GetObjectItem(root, "action");
    cJSON *param = cJSON_GetObjectItem(root, "param");

    if (!cJSON_IsString(msg_id) || !cJSON_IsString(action)) {
        ESP_LOGW(TAG, "MQTT command requires string msg_id and action");
        goto cleanup_json;
    }

    char expected_prefix[16];
    snprintf(expected_prefix, sizeof(expected_prefix), "%s_", s_mac);
    if (strncmp(msg_id->valuestring, expected_prefix, strlen(expected_prefix)) != 0) {
        ESP_LOGW(TAG, "MQTT msg_id does not match expected format %s{timestamp}: %s",
                 expected_prefix, msg_id->valuestring);
    }

    if (strcmp(action->valuestring, "water") == 0) {
        int set_time = 5;
        if (cJSON_IsObject(param)) {
            cJSON *set_time_obj = cJSON_GetObjectItem(param, "set_time");
            if (cJSON_IsNumber(set_time_obj)) {
                set_time = (int)set_time_obj->valuedouble;
            }
        }
        esp_err_t water_err = run_water_command(set_time);
        // 发送浇水响应
        cJSON *water_resp = cJSON_CreateObject();
        if (water_resp) {
            cJSON_AddStringToObject(water_resp, "msg_id", msg_id->valuestring);
            cJSON_AddStringToObject(water_resp, "action_reply", "water");
            cJSON *water_data = cJSON_CreateObject();
            if (water_data) {
                cJSON_AddNumberToObject(water_data, "duration", set_time);
                cJSON_AddBoolToObject(water_data, "success", water_err == ESP_OK);
                cJSON_AddItemToObject(water_resp, "data", water_data);
            }
            char *water_payload = cJSON_PrintUnformatted(water_resp);
            cJSON_Delete(water_resp);
            if (water_payload) {
                publish_payload(s_response_topic, water_payload);
                ESP_LOGI(TAG, "Water response published: %s", water_payload);
                free(water_payload);
            }
        }
    } else if (strcmp(action->valuestring, "capture") == 0) {
        char output[512] = {0};
        esp_err_t err = tool_camera_execute("{}", output, sizeof(output));
        ESP_LOGI(TAG, "MQTT capture result (%s): %s", esp_err_to_name(err), output);

        // 解析输出获取图片 URL
        if (err == ESP_OK) {
            // output 格式: "Photo successfully taken and uploaded. Image URL: https://..."
            const char *url_prefix = "Image URL: ";
            const char *url_start = strstr(output, url_prefix);
            if (url_start) {
                url_start += strlen(url_prefix);
                // 提取 URL（到字符串末尾或换行符）
                char image_url[256] = {0};
                const char *url_end = strchr(url_start, '\n');
                size_t url_len = url_end ? (size_t)(url_end - url_start) : strlen(url_start);
                if (url_len < sizeof(image_url)) {
                    strncpy(image_url, url_start, url_len);
                    image_url[url_len] = '\0';
                    // 发送响应到 MQTT
                    tool_mqtt_publish_capture_response(msg_id->valuestring, image_url);
                }
            }
        }
    } else if (strcmp(action->valuestring, "light") == 0) {
        uint8_t lr = 255, lg = 255, lb = 255;
        if (cJSON_IsObject(param)) {
            cJSON *rv = cJSON_GetObjectItem(param, "r");
            cJSON *gv = cJSON_GetObjectItem(param, "g");
            cJSON *bv = cJSON_GetObjectItem(param, "b");
            if (cJSON_IsNumber(rv)) lr = (uint8_t)((int)rv->valuedouble & 0xFF);
            if (cJSON_IsNumber(gv)) lg = (uint8_t)((int)gv->valuedouble & 0xFF);
            if (cJSON_IsNumber(bv)) lb = (uint8_t)((int)bv->valuedouble & 0xFF);
        }
        tool_rgb_set_all(lr, lg, lb);
        publish_light_state(true, lr, lg, lb);
        ESP_LOGI(TAG, "Grow light ON (%u,%u,%u)", lr, lg, lb);
    } else if (strcmp(action->valuestring, "led_water") == 0) {
        int duration = 5;
        uint8_t wr = 0, wg = 100, wb = 255;
        if (cJSON_IsObject(param)) {
            cJSON *st = cJSON_GetObjectItem(param, "set_time");
            if (cJSON_IsNumber(st)) duration = (int)st->valuedouble;
            cJSON *rv = cJSON_GetObjectItem(param, "r");
            cJSON *gv = cJSON_GetObjectItem(param, "g");
            cJSON *bv = cJSON_GetObjectItem(param, "b");
            if (cJSON_IsNumber(rv)) wr = (uint8_t)((int)rv->valuedouble & 0xFF);
            if (cJSON_IsNumber(gv)) wg = (uint8_t)((int)gv->valuedouble & 0xFF);
            if (cJSON_IsNumber(bv)) wb = (uint8_t)((int)bv->valuedouble & 0xFF);
        }
        tool_rgb_water_flow_start(wr, wg, wb, duration);
        ESP_LOGI(TAG, "LED water flow started for %ds", duration);
    } else if (strcmp(action->valuestring, "water_stop") == 0) {
        tool_rgb_stop();
        publish_light_state(false, 0, 0, 0);
        ESP_LOGI(TAG, "Water flow stopped by command");
    } else {
        ESP_LOGW(TAG, "Unknown MQTT action: %s", action->valuestring);
    }

cleanup_json:
    cJSON_Delete(root);
cleanup:
    free(payload);
    free(cmd);
    vTaskDelete(NULL);
}

static void handle_command(const char *payload, int len)
{
    mqtt_cmd_t *cmd = calloc(1, sizeof(*cmd));
    if (!cmd) {
        return;
    }

    cmd->payload = malloc((size_t)len + 1);
    if (!cmd->payload) {
        free(cmd);
        return;
    }
    memcpy(cmd->payload, payload, (size_t)len);
    cmd->payload[len] = '\0';
    cmd->len = len;

    if (xTaskCreate(command_task, "mqtt_cmd", MQTT_COMMAND_STACK, cmd, MQTT_DATA_PRIO + 1, NULL) != pdPASS) {
        ESP_LOGW(TAG, "Failed to create MQTT command task");
        free(cmd->payload);
        free(cmd);
    }
}

static void debug_task(void *arg)
{
    mqtt_cmd_t *cmd = (mqtt_cmd_t *)arg;
    char *payload = cmd->payload;

    cJSON *root = cJSON_ParseWithLength(payload, cmd->len);
    if (!root) {
        ESP_LOGW(TAG, "Invalid MQTT debug JSON");
        goto cleanup;
    }

    cJSON *cmd_obj = cJSON_GetObjectItem(root, "cmd");
    if (!cJSON_IsString(cmd_obj)) {
        ESP_LOGW(TAG, "MQTT debug payload requires string cmd");
        goto cleanup_json;
    }

    if (strcmp(cmd_obj->valuestring, "data") == 0) {
        esp_err_t err = publish_debug_sensor_data();
        ESP_LOGI(TAG, "MQTT debug data reply result: %s", esp_err_to_name(err));
    } else {
        ESP_LOGW(TAG, "Unknown MQTT debug cmd: %s", cmd_obj->valuestring);
    }

cleanup_json:
    cJSON_Delete(root);
cleanup:
    free(payload);
    free(cmd);
    vTaskDelete(NULL);
}

static void handle_debug(const char *payload, int len)
{
    mqtt_cmd_t *cmd = calloc(1, sizeof(*cmd));
    if (!cmd) {
        return;
    }

    cmd->payload = malloc((size_t)len + 1);
    if (!cmd->payload) {
        free(cmd);
        return;
    }
    memcpy(cmd->payload, payload, (size_t)len);
    cmd->payload[len] = '\0';
    cmd->len = len;

    if (xTaskCreate(debug_task, "mqtt_debug", MQTT_DEBUG_STACK, cmd, MQTT_DATA_PRIO + 1, NULL) != pdPASS) {
        ESP_LOGW(TAG, "Failed to create MQTT debug task");
        free(cmd->payload);
        free(cmd);
    }
}

static esp_err_t publish_light_state(bool on, uint8_t r, uint8_t g, uint8_t b)
{
    char payload[96];
    if (on) {
        snprintf(payload, sizeof(payload), "{\"state\":\"on\",\"r\":%u,\"g\":%u,\"b\":%u}", r, g, b);
    } else {
        snprintf(payload, sizeof(payload), "{\"state\":\"off\"}");
    }
    return publish_payload(s_light_topic, payload);
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    (void)handler_args;
    (void)base;
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        s_connected = true;
        ESP_LOGI(TAG, "MQTT connected");
        esp_mqtt_client_subscribe(s_client, s_cmd_topic, 1);
        esp_mqtt_client_subscribe(s_client, s_debug_topic, 1);
        publish_status("online");
        break;
    case MQTT_EVENT_DISCONNECTED:
        s_connected = false;
        ESP_LOGW(TAG, "MQTT disconnected");
        break;
    case MQTT_EVENT_DATA:
        if (topic_matches(event->topic, event->topic_len, s_cmd_topic)) {
            handle_command(event->data, event->data_len);
        } else if (topic_matches(event->topic, event->topic_len, s_debug_topic)) {
            handle_debug(event->data, event->data_len);
        }
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGW(TAG, "MQTT error");
        break;
    default:
        break;
    }
}

esp_err_t tool_mqtt_init(void)
{
    load_mqtt_config();
    init_topics();
    if (!mqtt_enabled()) {
        ESP_LOGI(TAG, "MQTT disabled; broker URI is empty");
        return ESP_OK;
    }

    tool_md0504_init();
    tool_rgb_init();
    ESP_LOGI(TAG, "MQTT topics: status=%s data=%s cmd=%s light=%s debug=%s log=%s",
             s_status_topic, s_data_topic, s_cmd_topic, s_light_topic, s_debug_topic, s_log_topic);
    return ESP_OK;
}

esp_err_t tool_mqtt_start(void)
{
    if (!mqtt_enabled()) {
        return ESP_OK;
    }
    if (s_started) {
        return ESP_OK;
    }

    esp_mqtt_client_config_t config = {
        .broker.address.uri = s_broker_uri,
        .credentials.client_id = s_client_id,
        .credentials.username = s_username[0] ? s_username : NULL,
        .credentials.authentication.password = s_password[0] ? s_password : NULL,
        .session.last_will.topic = s_status_topic,
        .session.last_will.msg = "{\"status\":\"offline\"}",
        .session.last_will.qos = 1,
        .session.last_will.retain = true,
    };

    s_client = esp_mqtt_client_init(&config);
    if (!s_client) {
        return ESP_ERR_NO_MEM;
    }

    ESP_RETURN_ON_ERROR(esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL),
                        TAG, "MQTT event register failed");
    ESP_RETURN_ON_ERROR(esp_mqtt_client_start(s_client), TAG, "MQTT start failed");

    if (xTaskCreate(data_publish_task, "mqtt_data", MQTT_DATA_STACK, NULL, MQTT_DATA_PRIO, NULL) != pdPASS) {
        ESP_LOGW(TAG, "Failed to create MQTT data publish task");
    }

    s_started = true;
    ESP_LOGI(TAG, "MQTT started with client_id=%s", s_client_id);
    return ESP_OK;
}
