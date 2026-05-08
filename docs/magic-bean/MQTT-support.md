# Magic Bean MQTT 支持说明

本文档说明 Magic Bean 固件当前已经支持的 MQTT 能力。

## 配置

MQTT 通过 `main/mimi_secrets.h` 进行编译期配置。

```c
#define MIMI_SECRET_MQTT_BROKER_URI "mqtt://broker.example.com:1883"
#define MIMI_SECRET_MQTT_USERNAME ""
#define MIMI_SECRET_MQTT_PASSWORD ""
#define MIMI_SECRET_MQTT_CLIENT_ID ""  /* 为空时使用 plant_<MAC> */
```

如果 `MIMI_SECRET_MQTT_BROKER_URI` 为空，则 MQTT 功能不会启动。

设备 MAC 地址格式为 12 位大写十六进制字符，不包含冒号或短横线，例如 `AABBCCDDEEFF`。

## 主题规则

所有主题都使用以下前缀：

```text
plant/{MAC}
```

示例：

```text
plant/AABBCCDDEEFF/status
```

## 在线状态

主题：

```text
plant/{MAC}/status
```

离线遗嘱消息 LWT：

```json
{"status":"offline"}
```

上线消息：

```json
{"status":"online"}
```

上线和离线消息均使用 QoS 1，并开启 retain。

## 传感器数据

主题：

```text
plant/{MAC}/data
```

Payload：

```json
{
  "temperature": 24.5,
  "air_humidity": 55.0,
  "dirt_humidity": 30.2
}
```

主动上报规则：

- MQTT 连接成功后，不会立即发布传感器数据。
- 固件会在每个整点和半点自动发布数据，例如 `09:00`、`09:30`、`10:00`。
- 系统时间必须已经同步。若本地年份早于 2024，固件会跳过定时发布。
- 调度任务每 5 秒检查一次时间，同一个整点/半点时间槽只发布一次。

数据来源：

- `temperature`：DHT11，GPIO2。
- `air_humidity`：DHT11，GPIO2。
- `dirt_humidity`：MD0504 模拟土壤湿度传感器，AO 接开发板 GPIO19。

## 控制命令

主题：

```text
plant/{MAC}/cmd
```

### 浇水

Payload：

```json
{
  "msg_id": "AABBCCDDEEFF_1715169600",
  "action": "water",
  "param": {
    "set_time": 5
  },
  "timestamp": 1715169600
}
```

当前行为：

- 固件会解析命令，并检查 `msg_id` 是否以 `{MAC}_` 开头。
- 当前代码中水泵 GPIO 处于禁用状态：`MQTT_WATER_GPIO -1`。
- 在代码中启用水泵 GPIO 后，`set_time` 会被限制在 1 到 60 秒之间，默认值为 5 秒。

### 拍照

Payload：

```json
{
  "msg_id": "AABBCCDDEEFF_1715169600",
  "action": "capture",
  "param": {},
  "timestamp": 1715169600
}
```

当前行为：

- 固件会调用现有摄像头工具。
- 摄像头会拍照，并通过 `MIMI_SECRET_UPLOAD_API_URL` 上传图片。
- 当前拍照结果会写入设备日志。

## 调试命令

调试命令主题：

```text
plant/{MAC}/debug
```

发送：

```json
{"cmd":"data"}
```

固件收到后会立即读取一次 DHT11 和 MD0504，并将结果发布到 log 主题，而不是回复到 debug 主题。

## 远程日志

日志主题：

```text
plant/{MAC}/log
```

log 主题用于无串口线场景下的远程调试。当前 debug 响应会发布到这里，后续其他模块也可以复用这个主题输出日志。

调试数据响应示例：

```json
{
  "type": "debug",
  "cmd": "data_reply",
  "temperature": 24.5,
  "air_humidity": 55.0,
  "dirt_humidity": 30.2,
  "soil_raw": 1800,
  "dht_error": "ESP_OK",
  "soil_error": "ESP_OK"
}
```

通用日志发布接口：

```c
esp_err_t tool_mqtt_publish_log(const char *module, const char *message);
```

通用日志 payload：

```json
{
  "type": "log",
  "module": "module_name",
  "message": "log message"
}
```

## MD0504 土壤湿度计算方式

MD0504 工具实现在 `main/tools/tool_md0504.c`。

当前常量：

```c
#define MD0504_AO_GPIO 19
#define MD0504_ADC_UNIT ADC_UNIT_2
#define MD0504_ADC_CHANNEL ADC_CHANNEL_8
#define MD0504_DRY_RAW 3000
#define MD0504_WET_RAW 1200
```

换算公式：

```text
dirt_humidity = (MD0504_DRY_RAW - raw) * 100 / (MD0504_DRY_RAW - MD0504_WET_RAW)
```

计算结果会被限制在 `0.0` 到 `100.0` 之间。

含义：

- `raw` 越大，表示土壤越干。
- `raw` 越小，表示土壤越湿。
- 当 `raw` 大于 `MD0504_DRY_RAW` 时，湿度会被限制为 `0.0%`。
- 当 `raw` 小于 `MD0504_WET_RAW` 时，湿度会被限制为 `100.0%`。

`MD0504_DRY_RAW` 和 `MD0504_WET_RAW` 是校准值。如果传感器或开发板读数范围不同，应在代码中调整这两个常量。
