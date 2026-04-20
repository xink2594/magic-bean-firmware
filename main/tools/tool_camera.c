#include "tool_camera.h"
// 引入包含 MIMI_SECRET_UPLOAD_API_URL 的头文件，根据你的项目结构调整
#include "mimi_secrets.h"

#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_camera.h"
#include "esp_http_client.h"
#include "esp_heap_caps.h"
#include "cJSON.h"

static const char *TAG = "tool_camera";
static bool s_cam_initialized = false;
static SemaphoreHandle_t s_camera_mutex = NULL;

// ================= 引脚定义 (来自您的 cam_test.c) =================
#define CAM_PIN_SIOD 4
#define CAM_PIN_SIOC 5
#define CAM_PIN_VSYNC 6
#define CAM_PIN_HREF 7
#define CAM_PIN_XCLK 15
#define CAM_PIN_Y9 16
#define CAM_PIN_Y8 17
#define CAM_PIN_Y7 18
#define CAM_PIN_Y6 12
#define CAM_PIN_Y5 10
#define CAM_PIN_Y4 8
#define CAM_PIN_Y3 9
#define CAM_PIN_Y2 11
#define CAM_PIN_PCLK 13

#define CAM_PIN_PWDN -1
#define CAM_PIN_RESET -1
// =================================================================

// 用于 HTTP 上传的表单边界
#define UPLOAD_BOUNDARY "----MimiCameraUploadBoundary7MA4YWxkTrZu0gW"

static esp_err_t tool_camera_hw_init(void)
{
    if (s_cam_initialized)
    {
        return ESP_OK;
    }

    camera_config_t camera_config = {
        .pin_pwdn = CAM_PIN_PWDN,
        .pin_reset = CAM_PIN_RESET,
        .pin_xclk = CAM_PIN_XCLK,
        .pin_sccb_sda = CAM_PIN_SIOD,
        .pin_sccb_scl = CAM_PIN_SIOC,
        .pin_d7 = CAM_PIN_Y9,
        .pin_d6 = CAM_PIN_Y8,
        .pin_d5 = CAM_PIN_Y7,
        .pin_d4 = CAM_PIN_Y6,
        .pin_d3 = CAM_PIN_Y5,
        .pin_d2 = CAM_PIN_Y4,
        .pin_d1 = CAM_PIN_Y3,
        .pin_d0 = CAM_PIN_Y2,
        .pin_vsync = CAM_PIN_VSYNC,
        .pin_href = CAM_PIN_HREF,
        .pin_pclk = CAM_PIN_PCLK,
        .xclk_freq_hz = 10000000,
        .ledc_timer = LEDC_TIMER_0,
        .ledc_channel = LEDC_CHANNEL_0,
        .pixel_format = PIXFORMAT_JPEG,
        .frame_size = FRAMESIZE_VGA, // 对于大模型分析，VGA(640x480) 足够清晰且体积适中
        .jpeg_quality = 12,
        .fb_count = 1,
        .grab_mode = CAMERA_GRAB_WHEN_EMPTY};

    esp_err_t err = esp_camera_init(&camera_config);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Camera Init Failed: 0x%x", err);
        return err;
    }

    s_cam_initialized = true;
    ESP_LOGI(TAG, "Camera initialized successfully.");
    return ESP_OK;
}

static void tool_camera_hw_deinit(void)
{
    if (!s_cam_initialized)
    {
        return;
    }

    esp_err_t err = esp_camera_deinit();
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "Camera deinit failed: %s", esp_err_to_name(err));
        return;
    }

    s_cam_initialized = false;
    ESP_LOGI(TAG, "Camera deinitialized to reduce idle heat.");
}

esp_err_t tool_camera_init(void)
{
    if (s_camera_mutex == NULL)
    {
        s_camera_mutex = xSemaphoreCreateMutex();
        if (s_camera_mutex == NULL)
        {
            ESP_LOGE(TAG, "Failed to create camera mutex");
            return ESP_ERR_NO_MEM;
        }
    }

    return ESP_OK;
}

esp_err_t tool_camera_execute(const char *input_json, char *output, size_t output_size)
{
    (void)input_json;

    if (s_camera_mutex == NULL)
    {
        snprintf(output, output_size, "Error: Camera tool not initialized.");
        return ESP_FAIL;
    }

    if (xSemaphoreTake(s_camera_mutex, pdMS_TO_TICKS(30000)) != pdTRUE)
    {
        snprintf(output, output_size, "Error: Camera busy.");
        return ESP_ERR_TIMEOUT;
    }

    ESP_LOGI(TAG, "Taking picture...");

    esp_err_t err = tool_camera_hw_init();
    if (err != ESP_OK)
    {
        snprintf(output, output_size, "Error: Camera init failed.");
        goto cleanup;
    }

    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb)
    {
        ESP_LOGE(TAG, "Failed to get camera frame buffer");
        snprintf(output, output_size, "Error: Failed to capture image.");
        err = ESP_FAIL;
        goto cleanup;
    }

    size_t image_len = fb->len;
    uint8_t *image_buf = heap_caps_malloc(image_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!image_buf)
    {
        image_buf = heap_caps_malloc(image_len, MALLOC_CAP_8BIT);
    }
    if (!image_buf)
    {
        ESP_LOGE(TAG, "Failed to allocate %zu bytes for image copy", image_len);
        esp_camera_fb_return(fb);
        snprintf(output, output_size, "Error: Not enough memory for captured image.");
        err = ESP_ERR_NO_MEM;
        goto cleanup;
    }

    memcpy(image_buf, fb->buf, image_len);
    esp_camera_fb_return(fb);
    fb = NULL;
    tool_camera_hw_deinit();

    ESP_LOGI(TAG, "Picture taken. Size: %zu bytes. Uploading to backend...", image_len);

    // 2. 准备 HTTP multipart/form-data 请求头和尾
    const char *header_format =
        "--" UPLOAD_BOUNDARY "\r\n"
        "Content-Disposition: form-data; name=\"file\"; filename=\"mimiclaw_capture.jpg\"\r\n"
        "Content-Type: image/jpeg\r\n\r\n";
    const char *tail = "\r\n--" UPLOAD_BOUNDARY "--\r\n";

    char head[256];
    snprintf(head, sizeof(head), header_format);

    size_t total_len = strlen(head) + image_len + strlen(tail);

    esp_http_client_config_t config = {
        .url = MIMI_SECRET_UPLOAD_API_URL,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 15000, // 图片上传需要较长时间，设置 15 秒超时
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client)
    {
        snprintf(output, output_size, "Error: Failed to init HTTP client.");
        free(image_buf);
        err = ESP_FAIL;
        goto cleanup;
    }

    // 设置 Content-Type
    char content_type[128];
    snprintf(content_type, sizeof(content_type), "multipart/form-data; boundary=%s", UPLOAD_BOUNDARY);
    esp_http_client_set_header(client, "Content-Type", content_type);

    // 3. 打开连接并分段写入数据
    err = esp_http_client_open(client, total_len);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        free(image_buf);
        snprintf(output, output_size, "Error: Cannot connect to backend server.");
        goto cleanup;
    }

    esp_http_client_write(client, head, strlen(head));
    esp_http_client_write(client, (const char *)image_buf, image_len);
    esp_http_client_write(client, tail, strlen(tail));

    // 4. 读取后端响应
    int content_length = esp_http_client_fetch_headers(client);
    char resp_buf[512] = {0};
    int read_len = 0;

    if (content_length >= 0)
    {
        read_len = esp_http_client_read_response(client, resp_buf, sizeof(resp_buf) - 1);
    }

    int status_code = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG, "HTTP Upload Status = %d", status_code);

    // 清理网络与摄像头资源
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    free(image_buf);

    // 5. 解析后端的 JSON 返回值，提取 URL 给大模型
    if (status_code == 200 && read_len > 0)
    {
        cJSON *root = cJSON_Parse(resp_buf);
        if (root)
        {
            cJSON *code = cJSON_GetObjectItem(root, "code");
            if (code && code->valueint == 200)
            {
                cJSON *data = cJSON_GetObjectItem(root, "data");
                if (data)
                {
                    cJSON *url = cJSON_GetObjectItem(data, "url");
                    if (url && cJSON_IsString(url))
                    {
                        // 成功！将获取到的 URL 告知大模型
                        snprintf(output, output_size, "Photo successfully taken and uploaded. Image URL: %s", url->valuestring);
                        ESP_LOGI(TAG, "Upload Success! URL: %s", url->valuestring);
                        cJSON_Delete(root);
                        err = ESP_OK;
                        goto cleanup;
                    }
                }
            }
            cJSON_Delete(root);
        }
        // 如果 JSON 解析失败或状态码不等于 200
        snprintf(output, output_size, "Error: Uploaded, but failed to parse response. Server returned: %s", resp_buf);
        err = ESP_FAIL;
        goto cleanup;
    }

    snprintf(output, output_size, "Error: Upload failed with HTTP status %d", status_code);
    err = ESP_FAIL;

cleanup:
    if (s_cam_initialized)
    {
        tool_camera_hw_deinit();
    }
    xSemaphoreGive(s_camera_mutex);
    return err;
}
