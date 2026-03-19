#include "oled_ssd1306.h"
#include "esp_log.h"
#include "driver/i2c.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "oled";

// 引脚与 I2C 配置 (与 APDS9960 共享总线)
#define I2C_HOST_NUM I2C_NUM_0
#define I2C_SDA_PIN 41
#define I2C_SCL_PIN 42
#define I2C_CLK_SPEED_HZ 400000

// 0.91寸 OLED 分辨率
#define OLED_H_RES 128
#define OLED_V_RES 32

static esp_lcd_panel_handle_t s_panel_handle = NULL;
static mimi_ui_state_t s_current_state = UI_STATE_BOOTING;

// 内部函数：用于将文本/颜文字渲染为 Bitmap 并推送到屏幕
// 真实项目中，你需要在这里引入一个轻量级字库 (如 u8g2 剥离版) 或硬编码的图案数组
static void oled_draw_kaomoji(const char *kaomoji)
{
    if (!s_panel_handle)
        return;

    // 提示：esp_lcd_panel_draw_bitmap 接收的是单色像素数组
    // uint8_t *buffer = ... (将 kaomoji 转换为 128x32 的位图数据)
    // esp_lcd_panel_draw_bitmap(s_panel_handle, 0, 0, OLED_H_RES, OLED_V_RES, buffer);

    ESP_LOGI(TAG, "Screen update -> %s", kaomoji);
}

esp_err_t oled_init(void)
{
    ESP_LOGI(TAG, "Initializing I2C bus for OLED...");

    // 1. 初始化 I2C Master (为主板后续挂载传感器打基础)
    i2c_config_t i2c_conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_SDA_PIN,
        .scl_io_num = I2C_SCL_PIN,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_CLK_SPEED_HZ,
    };
    ESP_ERROR_CHECK(i2c_param_config(I2C_HOST_NUM, &i2c_conf));
    // 注意：如果其他地方(如 APDS9960)也初始化了 I2C_NUM_0，这里会报错，需要做好单例管理
    ESP_ERROR_CHECK(i2c_driver_install(I2C_HOST_NUM, i2c_conf.mode, 0, 0, 0));

    // 2. 配置 LCD Panel IO (绑定到 I2C)
    ESP_LOGI(TAG, "Install panel IO");
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_i2c_config_t io_config = {
        .dev_addr = 0x3C, // SSD1306 默认 I2C 地址，极少数为 0x3D
        .control_phase_bytes = 1,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .dc_bit_offset = 6,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c((esp_lcd_i2c_bus_handle_t)I2C_HOST_NUM, &io_config, &io_handle));

    // 3. 初始化 SSD1306 驱动面板
    ESP_LOGI(TAG, "Install SSD1306 panel driver");
    esp_lcd_panel_dev_config_t panel_config = {
        .bits_per_pixel = 1,
        .reset_gpio_num = -1, // 我们使用纯 I2C，没有物理 Reset 引脚
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_ssd1306(io_handle, &panel_config, &s_panel_handle));

    // 4. 重置并打开屏幕
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel_handle, true));

    ESP_LOGI(TAG, "OLED initialization successful.");

    // 初始化完成，显示默认启动界面
    oled_set_state(UI_STATE_BOOTING);

    return ESP_OK;
}

void oled_set_state(mimi_ui_state_t state)
{
    if (s_current_state == state && state != UI_STATE_BOOTING)
    {
        return; // 状态未改变，不刷新
    }
    s_current_state = state;

    switch (state)
    {
    case UI_STATE_BOOTING:
        oled_draw_kaomoji("[ SYSTEM BOOT ]");
        break;
    case UI_STATE_WIFI_CONNECTING:
        oled_draw_kaomoji("(o_o) WiFi...");
        break;
    case UI_STATE_IDLE:
        oled_draw_kaomoji("(^_^) Ready");
        break;
    case UI_STATE_THINKING:
        oled_draw_kaomoji("(>_<) Thinking...");
        break;
    case UI_STATE_SPEAKING:
        oled_draw_kaomoji("(((o(*ﾟ▽ﾟ*)o)))");
        break;
    case UI_STATE_ERROR:
        oled_draw_kaomoji("(x_x) Error");
        break;
    default:
        break;
    }
}