# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

MimiClaw (magic-bean-firmware) — ESP32-S3 AI Agent firmware written in C with ESP-IDF framework. Implements an AI agent powered by Claude API (or OpenAI-compatible APIs) with multi-channel support (Feishu, WebSocket, Serial CLI). Features plant-care sensors (DHT11, MD0504 soil moisture, camera) and MQTT IoT communication.

## Build & Flash

```bash
# Set up ESP-IDF environment (Windows)
# Run ESP-IDF PowerShell or CMD terminal first

# Configure build
idf.py menuconfig

# Build firmware
idf.py build

# Flash to device
idf.py -p COMx flash

# Monitor serial output
idf.py -p COMx monitor

# Build + flash + monitor in one command
idf.py -p COMx flash monitor

# Full clean build (required after changing mimi_secrets.h)
idf.py fullclean && idf.py build
```

**Important**: All configuration is compile-time only via `mimi_secrets.h`. Changing any setting requires a full clean rebuild.

## Configuration

Copy `main/mimi_secrets.h.example` to `main/mimi_secrets.h` and fill in credentials. This file is gitignored. Runtime overrides can be set via Serial CLI (stored in NVS).

Key defines in `mimi_secrets.h`:
- `MIMI_SECRET_WIFI_SSID` / `MIMI_SECRET_WIFI_PASS` — WiFi credentials
- `MIMI_SECRET_TG_TOKEN` — Telegram Bot API token (currently disabled in code)
- `MIMI_SECRET_API_KEY` — LLM API key (Anthropic or OpenAI-compatible)
- `MIMI_SECRET_MODEL` — Model ID (default: `claude-opus-4-5`)
- `MIMI_SECRET_MODEL_PROVIDER` — `"anthropic"` (default) or `"openai"`
- `MIMI_SECRET_FEISHU_APP_ID` / `MIMI_SECRET_FEISHU_APP_SECRET` — Feishu bot credentials
- `MIMI_SECRET_FEISHU_ADMIN_ID` — Admin open_id for soil moisture alerts
- `MIMI_SECRET_MQTT_*` — MQTT broker connection settings (broker, port, username, password, client_id)
- `MIMI_SECRET_SEARCH_KEY` — Brave Search API key
- `MIMI_SECRET_TAVILY_KEY` — Tavily Search API key (preferred over Brave)
- `MIMI_SECRET_SENIVERSE_KEY` — 心知天气 API key for weather queries
- `MIMI_SECRET_UPLOAD_API_URL` — Image upload server URL
- `MIMI_SECRET_PROXY_HOST` / `MIMI_SECRET_PROXY_PORT` / `MIMI_SECRET_PROXY_TYPE` — HTTP/SOCKS5 proxy

All constants and defaults are in `main/mimi_config.h`.

## Architecture

### Startup Sequence

`main/mimi.c` → `app_main()` orchestrates initialization:
1. NVS init → SPIFFS mount → message bus (2 FreeRTOS queues) → subsystem init (memory, skills, sessions, WiFi, proxy, Feishu, LLM, tools, cron, heartbeat, agent, CLI)
2. Start WiFi → SNTP time sync → on WiFi success: agent loop + Feishu webhook + outbound dispatch + WebSocket server + MQTT client
3. If WiFi fails: enter captive portal mode (AP + HTTP config page), block until configured
4. Serial CLI always available (works without WiFi)
5. Telegram bot init/start is commented out in current code — Feishu is the primary IM channel

### Core Modules

```
main/
├── mimi.c                  Entry point — app_main()
├── mimi_config.h           Compile-time constants + defaults
├── mimi_secrets.h          Build credentials (gitignored)
│
├── agent/
│   ├── agent_loop.c        ReAct loop: LLM call → tool exec → repeat (max 10 iterations)
│   └── context_builder.c   Builds system prompt from SOUL.md + USER.md + MEMORY.md + tool guidance
│
├── bus/message_bus.c       Two FreeRTOS queues: inbound (channels→agent) + outbound (agent→dispatch)
│
├── channels/
│   ├── telegram/           Telegram long polling (30s timeout) — currently disabled
│   └── feishu/             Feishu webhook server on port 18790
│
├── llm/llm_proxy.c         Anthropic / OpenAI-compatible Messages API (non-streaming), tool_use parsing
│
├── tools/
│   ├── tool_registry.c     Tool registration + JSON schema builder + dispatch by name
│   ├── tool_web_search.c   Tavily (preferred) / Brave Search API
│   ├── tool_gpio.c         GPIO control (with gpio_policy.c safety)
│   ├── tool_camera.c       ESP32-CAM photo capture + HTTP upload
│   ├── tool_dht11.c        DHT11 temperature/humidity sensor (default GPIO2)
│   ├── tool_md0504.c       MD0504 soil moisture sensor (ADC on GPIO19)
│   ├── tool_rgb.c          WS2812 LED strip (22 LEDs on GPIO38) — grow light + water flow animation
│   ├── tool_weather.c      心知天气 API — current weather + 3-day forecast
│   ├── tool_mqtt.c         MQTT client — sensor data publish, command handling, soil alarm
│   ├── tool_cron.c         Scheduled task management
│   ├── tool_files.c        SPIFFS file operations
│   └── tool_get_time.c     NTP time retrieval
│
├── memory/
│   ├── memory_store.c      MEMORY.md + daily .md notes
│   └── session_mgr.c       JSONL session files per chat, ring buffer history
│
├── skills/skill_loader.c   Loads .md skill definitions from /spiffs/skills/
│
├── gateway/ws_server.c     WebSocket server on port 18789 (max 4 clients)
│
├── cron/cron_service.c     Cron job scheduler
├── heartbeat/heartbeat.c   Periodic heartbeat/health check
├── onboard/                WiFi captive portal / admin AP for initial setup
├── ota/ota_manager.c       Over-the-air firmware updates
├── proxy/http_proxy.c      HTTP CONNECT tunnel + TLS (HTTP/SOCKS5)
├── cli/serial_cli.c        esp_console REPL for debug/maintenance
└── wifi/wifi_manager.c     WiFi STA connect + scan + credential management
```

### FreeRTOS Task Allocation

| Task              | Core | Priority | Purpose                                    |
|-------------------|------|----------|--------------------------------------------|
| `feishu_webhook`  | 0    | 5        | Feishu webhook listener (port 18790)       |
| `agent_loop`      | 1    | 6        | Message processing + LLM API call          |
| `outbound`        | 0    | 5        | Route responses to channels                |
| `serial_cli`      | 0    | 3        | USB serial console                         |
| `mqtt_data`       | 0    | 4        | MQTT periodic sensor data publish          |
| `mqtt_cmd`        | 0    | 5        | MQTT command execution (spawned per cmd)   |
| `water_anim`      | 0    | 4        | WS2812 water flow animation (spawned on demand) |

Note: Telegram polling task (`tg_poll`) is commented out in current code.

Core 0 = I/O tasks. Core 1 = agent loop (CPU-bound JSON + HTTPS).

### Data Flow

```
User message → Channel (Feishu/WS/CLI)
  → mimi_msg_t → Inbound Queue (FreeRTOS xQueue)
  → Agent Loop (Core 1):
      Load session history (JSONL from SPIFFS)
      Build system prompt + messages array
      ReAct loop: LLM API → tool_use? → execute tools → repeat
      Save to session file
  → Outbound Queue → Dispatch → Channel response
```

### SPIFFS Storage

Flat filesystem at `/spiffs/` (~12 MB partition):
- `/spiffs/config/SOUL.md` — AI personality
- `/spiffs/config/USER.md` — User profile
- `/spiffs/memory/MEMORY.md` — Long-term memory
- `/spiffs/memory/daily/<YYYY-MM-DD>.md` — Daily notes
- `/spiffs/sessions/<chat_id>.jsonl` — Session history (JSONL)
- `/spiffs/skills/*.md` — Skill definitions
- `/spiffs/cron.json` — Cron job definitions

### Message Bus Protocol

```c
typedef struct {
    char channel[16];   // "telegram", "websocket", "cli", "feishu", "system"
    char chat_id[96];   // Chat identifier (open_id, chat_id, or WS client id)
    char *content;      // Heap-allocated text (ownership transferred on push)
} mimi_msg_t;
```

Content string ownership transfers on queue push; receiver must `free()`.

### LLM API Integration

Supports two providers via `MIMI_SECRET_MODEL_PROVIDER`:

**Anthropic** (default): `POST https://api.anthropic.com/v1/messages` (non-streaming)
- `system` is a top-level field (not in `messages` array)
- `stop_reason`: `"tool_use"` → execute tools, `"end_turn"` → complete

**OpenAI-compatible**: `POST https://open.bigmodel.cn/api/paas/v4/chat/completions` (configurable URL)
- Supports GLM, DashScope, DeepSeek and other OpenAI-compatible providers
- `system` goes as first message with `role: "system"`
- `finish_reason`: `"tool_calls"` → execute tools, `"stop"` → complete
- Tool schema auto-converted to OpenAI function calling format

### WebSocket Protocol

Port 18789. Client sends `{"type": "message", "content": "...", "chat_id": "ws_..."}`. Server responds with `{"type": "response", "content": "...", "sender": "bot"}`.

### MQTT Topics (auto-generated from device MAC)

```
plant/{MAC}/status    — Online/offline (LWT)
plant/{MAC}/data      — Sensor data (temperature, air_humidity, dirt_humidity)
plant/{MAC}/cmd       — Remote commands (water, capture, light, led_water, water_stop, fetch)
plant/{MAC}/response  — Command results (capture URL + sensor snapshot, water result)
plant/{MAC}/light     — Grow light state (on/off + RGB)
plant/{MAC}/debug     — Debug commands
plant/{MAC}/log       — System log
```

Sensor data auto-publishes at every hour/30min mark (xx:00, xx:30).

## Adding a New Tool

1. Create `main/tools/tool_<name>.c` + `.h`
2. Implement: init function, JSON schema builder, execution function
3. Register in `tool_registry.c` via `register_tool()`
4. Tool functions receive `cJSON *input` (as string) and return `char *` (written to provided buffer)

## Adding a New Channel

1. Create `main/channels/<name>/` with bot init + polling/webhook logic
2. Push received messages to inbound queue via `message_bus_push_inbound()`
3. Register outbound handler in dispatch task to route responses

## Code Style

- C99 with ESP-IDF conventions
- `MIMI_` prefix for all config constants
- `mimi_` prefix for public functions
- Use `ESP_LOGx(TAG, ...)` for logging
- Large buffers (32 KB+) allocated from PSRAM: `heap_caps_calloc(1, size, MALLOC_CAP_SPIRAM)`
- Use `cJSON` for all JSON operations
- Error handling: return `esp_err_t`, use `ESP_GOTO_ON_ERROR` macro pattern

## Memory Budget

Internal SRAM: task stacks (~40 KB) + WiFi buffers (~30 KB)
PSRAM: TLS connections (~120 KB) + JSON buffers (~32 KB each) + session cache (~32 KB) + camera frame buffers

## Serial CLI Commands

**Runtime configuration**: `set_wifi`, `set_tg_token`, `set_feishu_creds`, `feishu_send`, `set_api_key`, `set_model`, `set_model_provider`, `set_mqtt_broker`, `set_mqtt_port`, `set_search_key`, `set_tavily_key`, `set_proxy`, `clear_proxy`, `config_show`, `config_reset`

**Debug & ops**: `wifi_status`, `wifi_scan`, `memory_read`, `memory_write`, `session_list`, `session_clear`, `heap_info`, `heartbeat_trigger`, `cron_start`, `tool_exec`, `web_search`, `skill_list`, `skill_show`, `skill_search`, `restart`, `help`
