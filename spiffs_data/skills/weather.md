# Weather

Get accurate real-time weather and 3-day forecasts using dedicated weather APIs.

## When to use

When the user asks about the current weather, temperature, outdoor environment, or future weather forecasts (like "Will it rain tomorrow?").

## How to use

1. Use `get_weather_now` for real-time temperature and conditions.
2. Use `get_weather_forecast` to answer questions about tomorrow, the day after, or general temperature ranges.
3. If the user DOES NOT specify a city, pass an empty JSON `{}` or omit the `location` parameter. The system will automatically locate the device via IP.
4. Present the data in a warm, concise, and natural format. Do not act like a weather machine.
5. If combining with time, use `get_current_time` first to establish the current date.

## Example

User: "明天出门需要带伞吗？"
→ get_weather_forecast {}
→ System returns: "3-Day Forecast for 杭州: - 2026-03-28: Day 小雨, Night 阴..."
→ Assistant: "明天杭州白天有小雨哦，记得带好雨伞，注意保暖。"
