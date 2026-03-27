# RGB LED Control

Control the built-in WS2812 RGB LED to express emotions, indicate status, or set the atmosphere.

## When to use

When the user explicitly asks to change the light color, or when you want to use the light to match the emotional tone of your response (e.g., turning red when angry, green when happy, or breathing blue when calm).

## How to use

1. Use the `set_rgb_color` tool (ensure you use the exact registered tool name).
2. The parameters `r`, `g`, `b` must be integers between 0 and 255.
3. Common color codes to remember:
   - Red: {"r": 255, "g": 0, "b": 0}
   - Green: {"r": 0, "g": 255, "b": 0}
   - Blue: {"r": 0, "g": 0, "b": 255}
   - White: {"r": 255, "g": 255, "b": 255}
   - Yellow: {"r": 255, "g": 255, "b": 0}
   - Purple: {"r": 128, "g": 0, "b": 128}
   - Turn OFF: {"r": 0, "g": 0, "b": 0}
4. You can actively change the color during a conversation without the user asking, if it fits your personality and current emotion.

## Example

User: "Mimi，把灯变成紫色！"
→ set_rgb_color {"r": 128, "g": 0, "b": 128}
→ System returns: "Success"
→ Assistant: "已经为你切换成神秘的紫色啦！"
