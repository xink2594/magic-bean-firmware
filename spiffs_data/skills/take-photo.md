# Take Photo

Capture a real-time image using the device's onboard camera, upload it to the cloud storage (R2), and retrieve the public image URL.

## When to use

When the user asks to see the plant, check its current visual status, diagnose plant health, or explicitly commands the device to take a picture/photo (e.g., "帮我看看植物", "拍张照", "它今天长得怎么样？").

## How to use

1. Use `camera_capture` to trigger the hardware camera to take a photo.
2. The tool does not require any parameters. Pass an empty JSON `{}`.
3. The system will process the hardware capture and network upload, which may take a few seconds. Do not hallucinate the image content before the tool returns the result.
4. The system will return a success message containing the `Image URL` (e.g., https://pub-xxxx.r2.dev/...).
5. If the user just wants a photo, present the URL to the user in a friendly manner. If the user wants a health diagnosis, use this URL to call the vision analysis tool (e.g., qwen-vl-plus) in your next step before answering.
6. Present the response naturally and warmly, acting as a caring gardening assistant.

## Example

User: "帮我看看这盆植物现在状态怎么样？"
→ camera_capture {}
→ System returns: "Photo successfully taken and uploaded. Image URL: https://pub-1234abcd.r2.dev/mimiclaw_capture.jpg"
→ Assistant: "已经为你拍好啦！📸 你可以点击这个链接查看最新的照片：https://pub-1234abcd.r2.dev/mimiclaw_capture.jpg。需要我帮你详细分析一下它的叶片健康状况吗？"
