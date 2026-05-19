# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

MimiClaw (magic-bean-firmware) — ESP32-S3 AI Agent firmware written in C with ESP-IDF framework. Implements an AI agent powered by Claude API with multi-channel support (Telegram, Feishu, WebSocket, Serial CLI).

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

Copy `main/mimi_secrets.h.example` to `main/mimi_secrets.h` and fill in credentials. This file is gitignored.

Key defines in `mimi_secrets.h`:
- `MIMI_SECRET_WIFI_SSID` / `MIMI_SECRET_WIFI_PASS` — WiFi credentials
- `MIMI_SECRET_TG_TOKEN` — Telegram Bot API token
- `MIMI_SECRET_API_KEY` — Anthropic API key
- `MIMI_SECRET_MODEL` — Claude model ID (default: `claude-opus-4-5`)
- `MIMI_SECRET_MODEL_PROVIDER` — `"anthropic"` (default) or `"openai"`
- `MIMI_SECRET_FEISHU_APP_ID` / `MIMI_SECRET_FEISHU_APP_SECRET` — Feishu bot credentials
- `MIMI_SECRET_MQTT_*` — MQTT broker connection settings
- `MIMI_SECRET_SEARCH_KEY` — Brave/Tavily Search API key

All constants and defaults are in `main/mimi_config.h`.

## Architecture

### Startup Sequence

`main/mimi.c` → `app_main()` orchestrates initialization:
1. NVS init → SPIFFS mount → message bus (2 FreeRTOS queues) → WiFi connect
2. On WiFi success: Telegram poller + agent loop + WebSocket server + outbound dispatch
3. Serial CLI always available (works without WiFi)

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
│   ├── telegram/           Telegram long polling (30s timeout), JSON parsing
│   └── feishu/             Feishu webhook server on port 18790
│
├── llm/llm_proxy.c         Anthropic Messages API (non-streaming), tool_use parsing
│
├── tools/
│   ├── tool_registry.c     Tool registration + JSON schema builder + dispatch by name
│   ├── tool_web_search.c   Brave Search API
│   ├── tool_gpio.c         GPIO control (with gpio_policy.c safety)
│   ├── tool_camera.c       ESP32-CAM photo capture
│   ├── tool_dht11.c        Temperature/humidity sensor
│   ├── tool_rgb.c          WS2812 LED strip control
│   ├── tool_mqtt.c         MQTT publish/subscribe
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
├── onboard/                WiFi captive portal for initial setup
├── ota/ota_manager.c       Over-the-air firmware updates
├── proxy/http_proxy.c      HTTP CONNECT tunnel + TLS
└── cli/serial_cli.c        esp_console REPL for debug/maintenance
```

### FreeRTOS Task Allocation

| Task           | Core | Priority | Purpose                              |
|----------------|------|----------|--------------------------------------|
| `tg_poll`      | 0    | 5        | Telegram long polling                |
| `feishu_webhook` | 0  | 5        | Feishu webhook listener              |
| `agent_loop`   | 1    | 6        | Message processing + Claude API call |
| `outbound`     | 0    | 5        | Route responses to channels          |
| `serial_cli`   | 0    | 3        | USB serial console                   |

Core 0 = I/O tasks. Core 1 = agent loop (CPU-bound JSON + HTTPS).

### Data Flow

```
User message → Channel (Telegram/Feishu/WS/CLI)
  → mimi_msg_t → Inbound Queue (FreeRTOS xQueue)
  → Agent Loop (Core 1):
      Load session history (JSONL from SPIFFS)
      Build system prompt + messages array
      ReAct loop: Claude API → tool_use? → execute tools → repeat
      Save to session file
  → Outbound Queue → Dispatch → Channel response
```

### SPIFFS Storage

Flat filesystem at `/spiffs/` (12 MB partition):
- `/spiffs/config/SOUL.md` — AI personality
- `/spiffs/config/USER.md` — User profile
- `/spiffs/memory/MEMORY.md` — Long-term memory
- `/spiffs/memory/YYYY-MM-DD.md` — Daily notes
- `/spiffs/sessions/tg_<chat_id>.jsonl` — Session history (JSONL)
- `/spiffs/skills/*.md` — Skill definitions
- `/spiffs/cron.json` — Cron job definitions

### Message Bus Protocol

```c
typedef struct {
    char channel[16];   // "telegram", "websocket", "cli", "feishu"
    char chat_id[32];   // Chat identifier
    char *content;      // Heap-allocated text (ownership transferred on push)
} mimi_msg_t;
```

Content string ownership transfers on queue push; receiver must `free()`.

### Claude API Integration

Endpoint: `POST https://api.anthropic.com/v1/messages` (non-streaming)

Key: `system` is a top-level field (not in `messages` array). Response `stop_reason` drives the ReAct loop — `"tool_use"` triggers tool execution, `"end_turn"` completes the turn.

### WebSocket Protocol

Port 18789. Client sends `{"type": "message", "content": "...", "chat_id": "ws_..."}`. Server responds with `{"type": "response", "content": "..."}`.

## Adding a New Tool

1. Create `main/tools/tool_<name>.c` + `.h`
2. Implement: init function, JSON schema builder, execution function
3. Register in `tool_registry.c` via `tool_register()`
4. Tool functions receive `cJSON *input` and return `char *` (heap-allocated result)

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
PSRAM: TLS connections (~120 KB) + JSON buffers (~32 KB each) + session cache (~32 KB)

## Serial CLI Commands

`wifi_status`, `memory_read`, `memory_write <CONTENT>`, `session_list`, `session_clear <CHAT_ID>`, `heap_info`, `restart`, `help`
