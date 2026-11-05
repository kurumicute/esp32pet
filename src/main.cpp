// 水伊布風格桌面時鐘寵物 — 進入點
// 板子：Waveshare ESP32-S3-LCD-1.54 (ST7789 240x240)
#include <Arduino.h>

#include "app/App.h"

namespace {
App app;  // 全部模組皆為靜態配置
}

void setup() { app.begin(); }

void loop() {
  app.loop();
  delay(app.loopDelayMs());  // 讓出 CPU (省電模式時休息久一點)；動畫與時鐘皆以 millis() 計時
}
