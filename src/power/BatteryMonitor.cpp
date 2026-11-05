#include "BatteryMonitor.h"

#include <Arduino.h>

#include "../config/BoardConfig.h"

namespace {
constexpr uint32_t SAMPLE_MS = 250;
constexpr uint8_t SAMPLES = 8;  // 8 次取平均 → 每 2 秒更新一次
constexpr float DIVIDER = 3.0f;

// 鋰電池電壓 → 電量 (與官方門檻一致，中間線性內插)
uint8_t toPercent(float v) {
  const float pts[][2] = {{3.40f, 0}, {3.52f, 5}, {3.64f, 20}, {3.76f, 40},
                          {3.88f, 60}, {4.00f, 80}, {4.15f, 100}};
  if (v <= pts[0][0]) return 0;
  for (uint8_t i = 1; i < sizeof(pts) / sizeof(pts[0]); i++) {
    if (v < pts[i][0]) {
      const float t = (v - pts[i - 1][0]) / (pts[i][0] - pts[i - 1][0]);
      return (uint8_t)(pts[i - 1][1] + t * (pts[i][1] - pts[i - 1][1]));
    }
  }
  return 100;
}
}  // namespace

void BatteryMonitor::begin() {
  pinMode(Pins::BAT_CHARGING, INPUT_PULLUP);
  analogSetPinAttenuation(Pins::BAT_ADC, ADC_11db);
}

bool BatteryMonitor::changed() {
  const bool c = changed_;
  changed_ = false;
  return c;
}

void BatteryMonitor::update(uint32_t now) {
  if (now - lastRead_ < SAMPLE_MS) return;
  lastRead_ = now;
  mvSum_ += analogReadMilliVolts(Pins::BAT_ADC);
  if (++samples_ < SAMPLES) return;

  voltage_ = (mvSum_ / (float)samples_) / 1000.0f * DIVIDER;
  mvSum_ = 0;
  samples_ = 0;

  const bool chg = digitalRead(Pins::BAT_CHARGING) == LOW;
  uint8_t p = toPercent(voltage_);
  // 遲滯：電量只在變化 >= 5% 時更新，避免數字跳來跳去
  if (chg != charging_ || p == 100 || p == 0 || (p > percent_ ? p - percent_ : percent_ - p) >= 5) {
    if (chg != charging_ || p != percent_) changed_ = true;
    charging_ = chg;
    percent_ = p;
  }
}
