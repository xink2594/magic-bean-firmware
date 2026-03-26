#include "display_face.h"
#include "esp_log.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"

static const char *TAG = "display_face";

LV_IMAGE_DECLARE(staticstate);
LV_IMAGE_DECLARE(sad);
LV_IMAGE_DECLARE(happy);
// LV_IMAGE_DECLARE(scare);
LV_IMAGE_DECLARE(buxue);
// LV_IMAGE_DECLARE(anger);

// ========== 硬件引脚定义 ==========
#define PIN_NUM_SCLK 38
#define PIN_NUM_MOSI 39
#define PIN_NUM_RST 40
#define PIN_NUM_DC 41
#define PIN_NUM_CS 42
#define PIN_NUM_BLK 2

#define LCD_H_RES 240
#define LCD_V_RES 240

// ========== UI 全局变量 ==========
static lv_obj_t *face_img; // 在 LVGL v9 中，GIF 是普通的图像容器

esp_err_t display_face_init(void)
{
    ESP_LOGI(TAG, "Initialize SPI bus");
    spi_bus_config_t buscfg = {
        .sclk_io_num = PIN_NUM_SCLK,
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_H_RES * LCD_V_RES * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));

    ESP_LOGI(TAG, "Install panel IO");
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = PIN_NUM_DC,
        .cs_gpio_num = PIN_NUM_CS,
        .pclk_hz = 40 * 1000 * 1000,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_config, &io_handle));

    ESP_LOGI(TAG, "Install ST7789 panel driver");
    esp_lcd_panel_handle_t panel_handle = NULL;
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = PIN_NUM_RST,
        .rgb_endian = LCD_RGB_ENDIAN_RGB,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io_handle, &panel_config, &panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle, true));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

    // 点亮背光
    gpio_set_direction(PIN_NUM_BLK, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_NUM_BLK, 1);

    ESP_LOGI(TAG, "Initialize LVGL");
    lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    lvgl_cfg.task_stack = 32768; //  4KB 栈 --> 32KB
    lvgl_cfg.task_priority = 4;  // 提高渲染优先级
    ESP_ERROR_CHECK(lvgl_port_init(&lvgl_cfg));

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = io_handle,
        .panel_handle = panel_handle,
        .buffer_size = LCD_H_RES * LCD_V_RES / 5, // 缓冲区增加至1/5屏幕大小
        .double_buffer = false,
        .hres = LCD_H_RES,
        .vres = LCD_V_RES,
        .monochrome = false,
        .flags = {
            .buff_dma = true,
            .swap_bytes = true}};
    lv_disp_t *disp = lvgl_port_add_disp(&disp_cfg);

    // ========== 绘制 UI 界面 ==========
    lvgl_port_lock(0);
    lv_obj_t *scr = lv_disp_get_scr_act(disp);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);

    // 使用 lv_image_create 创建图像容器
    face_img = lv_gif_create(scr);

    // 默认加载“发呆(staticstate)”的 GIF 数据
    lv_gif_set_src(face_img, &staticstate);

    lv_obj_center(face_img);
    lvgl_port_unlock();

    ESP_LOGI(TAG, "Display initialized with Otto GIF animations");
    return ESP_OK;
}

// 情绪状态机联动大模型
void display_set_mood(int mood)
{
    // 默认情绪
    const void *emotion_src = &staticstate;

    // 0: 发呆, 1: 思考(困惑), 2: 开心, 3: 睡觉/悲伤
    if (mood == 1)
    {
        emotion_src = &buxue; // 大模型思考时，显示困惑/不屑
    }
    else if (mood == 2)
    {
        emotion_src = &happy; // 开心
    }
    else if (mood == 3)
    {
        emotion_src = &sad; // 悲伤或待机
    }

    lvgl_port_lock(0);
    lv_gif_set_src(face_img, emotion_src);
    lvgl_port_unlock();
}