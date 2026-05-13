# Magic Bean: AI 驱动的智能植物养护系统

<p align="center">
  <img src="assets/banner.png" alt="Magic Bean" width="500" />
</p>

<p align="center">
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-MIT-yellow.svg" alt="License: MIT"></a>
  <a href="https://github.com/yourusername/magic-bean-firmware"><img src="https://img.shields.io/badge/Platform-ESP32--S3-green.svg" alt="Platform"></a>
</p>

<p align="center">
  <strong><a href="README.md">中文</a> | <a href="README_Mimiclaw.md">English (原版)</a></strong>
</p>

**基于 [MimiClaw](https://github.com/memovai/mimiclaw) 二次开发的 ESP32-S3 智能植物养护固件**

Magic Bean 将 ESP32-S3 开发板变成你的智能植物管家。通过传感器实时监测植物生长环境，AI 分析并自主执行养护任务，支持远程拍照、自动浇水、MQTT 物联网通信 — 全部跑在一颗拇指大小的芯片上。

## 核心特性

- **智能感知** — DHT11 温湿度传感器 + MD0504 土壤湿度传感器，实时监测植物环境
- **视觉监控** — OV3660 摄像头拍照上传，远程查看植物生长状态
- **MQTT 物联网** — 完整的 MQTT 支持，实现设备与云端双向通信
- **AI 驱动** — 基于 LLM Agent 循环，AI 可自主分析传感器数据并执行养护决策
- **远程控制** — 通过 Telegram/飞书发送指令，或通过 MQTT 远程控制浇水和拍照
- **定时任务** — 内置 Cron 调度器，支持定时巡检和周期性任务
- **RGB 指示灯** — WS2812 RGB LED，直观显示设备状态
- **天气查询** — 实时获取天气信息，结合天气制定养护策略

## 系统架构

```
┌─────────────────────────────────────────────────────────────────┐
│                        Magic Bean 固件                          │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐        │
│  │ 摄像头   │  │ DHT11    │  │ MD0504   │  │ RGB LED  │        │
│  │ OV3660   │  │ 温湿度   │  │ 土壤湿度 │  │ WS2812   │        │
│  └────┬─────┘  └────┬─────┘  └────┬─────┘  └────┬─────┘        │
│       │              │              │              │              │
│  ┌────┴──────────────┴──────────────┴──────────────┴────┐       │
│  │                   AI Agent 循环                       │       │
│  │         (ReAct 模式 + 工具调用 + 记忆系统)            │       │
│  └──────────────────────┬───────────────────────────────┘       │
│                         │                                        │
│  ┌──────────┐  ┌────────┴────────┐  ┌──────────┐                │
│  │ Telegram │  │     MQTT        │  │   飞书   │                │
│  │   Bot    │  │ (物联网通信)    │  │   Bot    │                │
│  └──────────┘  └─────────────────┘  └──────────┘                │
└─────────────────────────────────────────────────────────────────┘
         │                    │                    │
         └────────────────────┼────────────────────┘
                              │
                    ┌─────────┴─────────┐
                    │   云端 / 用户端   │
                    └───────────────────┘
```

## 快速开始

### 硬件需求

- **ESP32-S3 开发板** — 16MB Flash + 8MB PSRAM（如小智 AI 开发板，~¥30）
- **OV3660 摄像头模块** — 用于拍照监控
- **DHT11 温湿度传感器** — 默认 GPIO2
- **MD0504 土壤湿度传感器** — 模拟量输出，GPIO19
- **WS2812 RGB LED** — 状态指示灯
- **继电器模块** — 用于控制水泵（可选）
- **USB Type-C 数据线**

### 软件需求

- ESP-IDF v5.5+
- Telegram Bot Token（可选）
- Anthropic API Key 或 OpenAI API Key
- MQTT Broker（如 EMQX、Mosquitto）
- 心知天气 API Key（可选，用于天气查询）

### 安装

```bash
# 安装 ESP-IDF v5.5+
# https://docs.espressif.com/projects/esp-idf/en/v5.5.2/esp32s3/get-started/

git clone https://github.com/yourusername/magic-bean-firmware.git
cd magic-bean-firmware

idf.py set-target esp32s3
```

<details>
<summary>Ubuntu 安装</summary>

```bash
sudo apt-get update
sudo apt-get install -y git wget flex bison gperf python3 python3-pip python3-venv \
  cmake ninja-build ccache libffi-dev libssl-dev dfu-util libusb-1.0-0

./scripts/setup_idf_ubuntu.sh
./scripts/build_ubuntu.sh
```

</details>

<details>
<summary>macOS 安装</summary>

```bash
xcode-select --install
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

./scripts/setup_idf_macos.sh
./scripts/build_macos.sh
```

</details>

### 配置

Magic Bean 使用**两层配置**：`mimi_secrets.h` 提供编译时默认值，串口 CLI 可在运行时覆盖。

```bash
cp main/mimi_secrets.h.example main/mimi_secrets.h
```

编辑 `main/mimi_secrets.h`：

```c
/* 基础配置 */
#define MIMI_SECRET_WIFI_SSID       "你的WiFi名"
#define MIMI_SECRET_WIFI_PASS       "你的WiFi密码"
#define MIMI_SECRET_TG_TOKEN        "你的Telegram Bot Token"
#define MIMI_SECRET_API_KEY         "你的API Key"
#define MIMI_SECRET_MODEL_PROVIDER  "anthropic"     // "anthropic" 或 "openai"

/* MQTT 配置 */
#define MIMI_SECRET_MQTT_BROKER     "broker.emqx.io"   // MQTT Broker 地址
#define MIMI_SECRET_MQTT_PORT       "1883"              // MQTT 端口
#define MIMI_SECRET_MQTT_USERNAME   ""                  // MQTT 用户名（可选）
#define MIMI_SECRET_MQTT_PASSWORD   ""                  // MQTT 密码（可选）
#define MIMI_SECRET_MQTT_CLIENT_ID  ""                  // 客户端 ID，留空自动生成 plant_<MAC>

/* 图片上传 */
#define MIMI_SECRET_UPLOAD_API_URL  "https://your-server.com/upload"  // 图片上传服务器

/* 可选配置 */
#define MIMI_SECRET_SEARCH_KEY      ""              // Brave Search API Key
#define MIMI_SECRET_TAVILY_KEY      ""              // Tavily API Key（优先）
#define MIMI_SECRET_PROXY_HOST      ""              // 代理地址
#define MIMI_SECRET_PROXY_PORT      ""              // 代理端口
```

编译烧录：

```bash
# 完整编译（修改 mimi_secrets.h 后必须 fullclean）
idf.py fullclean && idf.py build

# 烧录并监控
idf.py -p PORT flash monitor
```

## MQTT 物联网通信

Magic Bean 实现了完整的 MQTT 通信协议，支持远程监控和控制。

### 主题结构

所有主题基于设备 MAC 地址自动生成：

```
plant/{MAC}/status    # 设备在线状态（LWT 机制）
plant/{MAC}/data      # 传感器数据发布（温湿度、土壤湿度）
plant/{MAC}/cmd       # 远程控制命令（浇水、拍照）
plant/{MAC}/response  # 命令执行响应（拍照结果、传感器快照）
plant/{MAC}/debug     # 调试命令
plant/{MAC}/log       # 远程日志输出
```

### 数据格式

**传感器数据 (plant/{MAC}/data)**：
```json
{
  "temperature": 25.6,
  "humidity": 65.2,
  "soil_moisture": 45.8,
  "timestamp": 1706123456
}
```

**控制命令 (plant/{MAC}/cmd)**：
```json
// 浇水命令
{
  "action": "water",
  "duration": 5000  // 浇水时长（毫秒）
}

// 拍照命令
{
  "action": "photo"
}
```

**命令响应 (plant/{MAC}/response)**：
```json
// 拍照响应 - 包含图片 URL 和当前传感器快照
{
  "msg_id": "cmd_1706123456789",
  "action_reply": "capture",
  "data": {
    "url": "https://your-server.com/uploads/plant_abc123_1706123456.jpg",
    "temp": 25.6,
    "humi": 65.2,
    "soil": 45.8
  }
}

// 浇水响应 - 包含执行结果
{
  "msg_id": "cmd_1706123456789",
  "action_reply": "water",
  "data": {
    "duration": 5,
    "success": true
  }
}
```

字段说明：
- `msg_id` — 命令消息 ID，用于关联请求和响应
- `action_reply` — 响应类型（`capture` 或 `water`）
- **capture 响应**：
  - `data.url` — 上传后的图片 URL
  - `data.temp` — 拍照时的温度（℃）
  - `data.humi` — 拍照时的湿度（%）
  - `data.soil` — 拍照时的土壤湿度（%）
- **water 响应**：
  - `data.duration` — 实际浇水时长（秒）
  - `data.success` — 是否执行成功

### 数据发布策略

- 每个整点（xx:00）和半点（xx:30）自动发布传感器数据
- 收到控制命令后立即执行并反馈结果
- 设备上线/离线通过 LWT 机制自动通知

## AI 工具列表

Magic Bean 继承了 MimiClaw 的所有工具，并新增了植物养护相关工具：

### 植物养护工具

| 工具 | 说明 |
|------|------|
| `get_indoor_temperature` | 读取 DHT11 温湿度传感器数据 |
| `get_soil_Humidity` | 读取 MD0504 土壤湿度传感器数据 |
| `get_camera_image` | 拍照并上传到指定服务器 |
| `set_rgb_color` | 设置 WS2812 RGB LED 颜色 |
| `get_weather_now` | 查询当前天气（心知天气 API） |
| `get_weather_forecast` | 查询 3 天天气预报 |

### 原有工具

| 工具 | 说明 |
|------|------|
| `web_search` | 通过 Tavily 或 Brave 搜索网页 |
| `get_current_time` | 获取当前时间并同步系统时钟 |
| `cron_add` / `cron_list` / `cron_remove` | 定时任务管理 |
| `read_file` / `write_file` / `edit_file` | 文件操作 |
| `gpio_write` / `gpio_read` | GPIO 控制 |

## CLI 命令

通过串口连接即可配置和调试。

**运行时配置**：

```
mimi> wifi_set MySSID MyPassword     # 换 WiFi
mimi> set_tg_token 123456:ABC...     # 换 Telegram Token
mimi> set_api_key sk-ant-api03-...   # 换 API Key
mimi> set_model_provider openai      # 切换 LLM 提供商
mimi> set_mqtt_broker broker.emqx.io # 设置 MQTT Broker
mimi> set_mqtt_port 1883             # 设置 MQTT 端口
mimi> config_show                    # 查看所有配置
mimi> config_reset                   # 恢复默认配置
```

**调试与运维**：

```
mimi> wifi_status              # 查看连接状态
mimi> memory_read              # 查看记忆内容
mimi> heap_info                # 查看内存使用
mimi> session_list             # 列出所有会话
mimi> heartbeat_trigger        # 手动触发心跳检查
mimi> restart                  # 重启设备
```

## 记忆系统

所有数据存储为纯文本文件，可直接读取和编辑：

| 文件 | 说明 |
|------|------|
| `SOUL.md` | AI 人格设定 — 编辑它来改变行为方式 |
| `USER.md` | 用户信息 — 姓名、偏好、语言 |
| `MEMORY.md` | 长期记忆 — AI 应该一直记住的事 |
| `HEARTBEAT.md` | 待办清单 — AI 定期检查并自主执行 |
| `cron.json` | 定时任务 — 周期性或一次性任务 |
| `*.md` | 每日笔记 — 记录当天事件 |

## 典型应用场景

### 1. 自动浇水系统

AI 根据土壤湿度传感器数据自主决策浇水：

```
传感器数据 → AI 分析 → 判断是否需要浇水 → 控制继电器 → 记录浇水日志
```

### 2. 定时巡检

通过 Cron 设置定时任务，每小时检查植物状态：

```
mimi> cron_add "每小时检查一次植物状态，如果土壤湿度低于40%则提醒浇水"
```

### 3. 远程监控

通过 MQTT 或 Telegram 随时查看植物状态：

```
# MQTT 订阅
mosquitto_sub -h broker.emqx.io -t "plant/+/data"

# Telegram 发送
"帮我拍一张植物的照片"
```

### 4. 天气联动

AI 结合天气预报调整养护策略：

```
"明天要下雨了，今天不需要浇水"
"连续高温，增加浇水频率"
```

## 开发者文档

技术细节在 `docs/` 文件夹：

- **[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)** — 系统设计、模块划分、内存分配
- **[docs/magic-bean/MQTT-support.md](docs/magic-bean/MQTT-support.md)** — MQTT 功能详细说明
- **[docs/WIFI_ONBOARDING_AP.md](docs/WIFI_ONBOARDING_AP.md)** — WiFi 配网流程
- **[docs/tool-setup/](docs/tool-setup/README.md)** — 外部服务集成配置

## 与原版 MimiClaw 的区别

| 功能 | MimiClaw | Magic Bean |
|------|----------|------------|
| AI Agent 循环 | ✅ | ✅ |
| Telegram/飞书 | ✅ | ✅ |
| WebSocket 网关 | ✅ | ✅ |
| MQTT 物联网 | ❌ | ✅ |
| 摄像头拍照 | ❌ | ✅ |
| 温湿度传感器 | ❌ | ✅ |
| 土壤湿度传感器 | ❌ | ✅ |
| RGB LED 控制 | ❌ | ✅ |
| 天气查询 | ❌ | ✅ |
| 植物养护优化 | ❌ | ✅ |

## 致谢

- 基于 [MimiClaw](https://github.com/memovai/mimiclaw) 二次开发
- 灵感来自 [OpenClaw](https://github.com/openclaw/openclaw) 和 [Nanobot](https://github.com/HKUDS/nanobot)

## 许可证

MIT

---

*让每一株植物都能享受 AI 的呵护* 🌱
