#pragma once
#include <stdint.h>

// 電池電壓 (GPIO1 ADC，板上分壓 1/3) 與充電狀態 (GPIO3，LOW = 充電中)
// 參考 Waveshare 官方 bsp_power_manager.c
class BatteryMonitor {
 public:
  void begin();
  void update(uint32_t now);
  float voltage() const { return voltage_; }
  uint8_t percent() const { return percent_; }
  bool charging() const { return charging_; }
  bool low() const { return percent_ <= 20 && !charging_; }
  bool changed();  // 顯示內容有變 (讀取後清除)

 private:
  float voltage_ = 0;
  uint8_t percent_ = 100;
  bool charging_ = false;
  bool changed_ = true;
  uint32_t lastRead_ = 0;
  uint32_t mvSum_ = 0;
  uint8_t samples_ = 0;
};
