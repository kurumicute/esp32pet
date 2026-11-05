#pragma once
#include <stdint.h>

#include "../assets/SpriteTypes.h"
#include "../weather/WeatherService.h"

enum class DayPeriod : uint8_t { Dawn, Day, Dusk, Night };

// 草地背景合成：時段底圖 (清晨/白天/黃昏/夜晚) + 天氣圖層 (太陽/月亮/雲/霧)
// 合成結果放在靜態 RGB565 緩衝區 (邏輯解析度 120x68)，主畫面與寵物畫布共用，
// 只在時段或天氣改變時重新合成。
class Scene {
 public:
  static constexpr int16_t W = 120;
  static constexpr int16_t H = 68;

  bool begin();  // 配置緩衝區 (開機呼叫一次)
  static DayPeriod periodForHour(int hour);
  // 依條件合成；內容有變回傳 true
  bool build(DayPeriod period, WeatherKind kind, bool weatherFx);
  const uint16_t *pixels() const { return buf_; }

 private:
  void blit(const Sprite &s, int16_t x, int16_t y);

  uint16_t *buf_ = nullptr;
  bool built_ = false;
  DayPeriod period_ = DayPeriod::Night;
  WeatherKind kind_ = WeatherKind::Unknown;
  bool fx_ = false;
};
