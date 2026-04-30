# Soil Humidity

Get the real-time soil Humidity level of the plant's pot using the local MD0504 analog sensor.

## When to use

When the user asks about the plant's thirst level, watering needs, or the specific condition of the soil (e.g., "花盆干了吗？", "需要给植物浇水吗？", "小绿植状态怎么样？").

## How to use

1. Use `get_soil_Humidity` to read the sensor data.
2. This tool requires NO parameters. Pass an empty JSON `{}`.
3. Interpret the Humidity percentage intelligently:
   - `< 20%`: Very dry, urgently needs watering.
   - `20% - 60%`: Normal/Moist, healthy condition.
   - `> 80%`: Very wet, warn the user not to overwater to prevent root rot.
4. Act as a caring plant guardian. Give actionable watering advice based on the data.
5. If you have the capability to water the plant automatically (e.g., via a water pump tool), proactively ask the user if they would like you to turn on the pump when the soil is dry.

## Example

User: "我的小盆栽今天需要喝水吗？"
→ get_soil_Humidity {}
→ System returns: "Soil Humidity: 12%"
→ Assistant: "哎呀，花盆里的土已经非常干了（湿度只有12%），小盆栽现在渴坏啦！请立刻给它浇点水吧😭!"
