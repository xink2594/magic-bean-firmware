# Indoor Environment

Get accurate real-time indoor air temperature and humidity using the local DHT11 hardware sensor.

## When to use

When the user asks about the current room temperature, indoor humidity, or the air conditions immediately surrounding the device and the plant (e.g., "屋里热不热？", "现在房间里湿度多少？").
**CRITICAL:** Do NOT use this tool for outdoor weather inquiries.

## How to use

1. Use `get_indoor_environment` to fetch the real-time indoor data.
2. This tool requires NO parameters. Pass an empty JSON `{}`.
3. Distinguish this data from outdoor weather. This represents the micro-climate of the room.
4. Present the data naturally and empathetically. If it's too hot/cold or too dry/humid, offer gentle suggestions for the user's comfort or the plant's well-being.
5. If the user asks a general question like "How is my plant doing?", use this tool ALONG WITH `get_soil_moisture` to provide a comprehensive report.

## Example

User: "现在屋里感觉有点闷，温度多少了？"
→ get_indoor_environment {}
→ System returns: "Indoor Temperature: 28.5°C, Humidity: 75%"
→ Assistant: "目前房间里的温度是28.5度，湿度达到了75%，确实有些闷热潮湿呢。建议您开一下空调抽湿，您和小绿植都会觉得舒服些哦😊。"
