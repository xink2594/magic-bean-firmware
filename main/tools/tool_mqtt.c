#include "tools/tool_mqtt.h"

#include "mimi_config.h"
#include "tools/tool_camera.h"
#include "tools/tool_md0504.h"

#include "cJSON.h"
#include "dht.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mqtt_client.h"

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
#define MQTT_WATER_GPIO -1
#define MQTT_WATER_ACTIVE_LEVEL 1

static esp_mqtt_client_handle_t s_client;
static bool s_started;
static bool s_connected;
static char s_mac[13];
static char s_client_id[MQTT_CLIENT_ID_LEN];
static char s_status_topic[MQTT_TOPIC_LEN];
static char s_data_topic[MQTT_TOPIC_LEN];
static char s_cmd_topic[MQTT_TOPIC_LEN];
static char s_debug_topic[MQTT_TOPIC_LEN];

typedef struct {
    char *payload;
    int len;
} mqtt_cmd_t;

static bool mqtt_enabled(void)
{
    return MIMI_SECRET_MQTT_BROKER_URI[0] != '\0';
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
    snprintf(s_debug_topic, sizeof(s_debug_topic), "plant/%s/debug", s_mac);

    if (MIMI_SECRET_MQTT_CLIENT_ID[0] != '\0') {
        snprintf(s_client_id, sizeof(s_client_id), "%s", MIMI_SECRET_MQTT_CLIENT_ID);
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

static void read_sensor_data(mqtt_sensor_data_t *data)
{
    memset(data, 0, sizeof(*data));

    data->dht_err = dht_read_float_data(DHT_TYPE_DHT11,
                                        (gpio_num_t)MQTT_DHT11_GPIO,
                                        &data->air_humidity,
                                        &data->temperature);
    if (data->dht_err != ESP_OK) {
        ESP_LOGW(TAG, "DHT11 read failed for MQTT data: %s", esp_err_to_name(data->dht_err));
    }

    data->soil_err = tool_md0504_read(&data->dirt_humidity, &data->soil_raw);
    if (data->soil_err != ESP_OK) {
        ESP_LOGW(TAG, "MD0504 read failed for MQTT data: %s", esp_err_to_name(data->soil_err));
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

static esp_err_t publish_debug_sensor_data(void)
{
    mqtt_sensor_data_t data;
    read_sensor_data(&data);

    char payload[256];
    snprintf(payload, sizeof(payload),
             "{\"cmd\":\"data_reply\",\"temperature\":%.1f,\"air_humidity\":%.1f,"
             "\"dirt_humidity\":%.1f,\"soil_raw\":%d,\"dht_error\":\"%s\",\"soil_error\":\"%s\"}",
             data.temperature, data.air_humidity, data.dirt_humidity, data.soil_raw,
             esp_err_to_name(data.dht_err), esp_err_to_name(data.soil_err));

    esp_err_t err = publish_payload(s_debug_topic, payload);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "MQTT debug data replied: %s", payload);
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
        run_water_command(set_time);
    } else if (strcmp(action->valuestring, "capture") == 0) {
        char output[512] = {0};
        esp_err_t err = tool_camera_execute("{}", output, sizeof(output));
        ESP_LOGI(TAG, "MQTT capture result (%s): %s", esp_err_to_name(err), output);
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
    } else if (strcmp(cmd_obj->valuestring, "data_reply") == 0) {
        ESP_LOGD(TAG, "Ignoring MQTT debug reply echo");
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
    init_topics();
    if (!mqtt_enabled()) {
        ESP_LOGI(TAG, "MQTT disabled; broker URI is empty");
        return ESP_OK;
    }

    tool_md0504_init();
    ESP_LOGI(TAG, "MQTT topics: status=%s data=%s cmd=%s debug=%s",
             s_status_topic, s_data_topic, s_cmd_topic, s_debug_topic);
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
        .broker.address.uri = MIMI_SECRET_MQTT_BROKER_URI,
        .credentials.client_id = s_client_id,
        .credentials.username = MIMI_SECRET_MQTT_USERNAME[0] ? MIMI_SECRET_MQTT_USERNAME : NULL,
        .credentials.authentication.password = MIMI_SECRET_MQTT_PASSWORD[0] ? MIMI_SECRET_MQTT_PASSWORD : NULL,
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
