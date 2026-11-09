#pragma once
#include <stdint.h>

class SettingsManager;
class WifiManager;
class ClockManager;
class CostumeManager;
class MotionSensor;
class BatteryMonitor;
class WeatherService;

// USB 序列埠指令 (115200)：設定 Wi-Fi 帳密，帳密存在 NVS，不寫進原始碼。
//   help / status / wifi / wifi clear
class SerialConsole {
 public:
  void begin(SettingsManager &s, WifiManager &w, ClockManager &c, CostumeManager &cos,
             const MotionSensor &m, const BatteryMonitor &b, WeatherService &ws);
  void update(uint32_t now);
  // 其他指令交給 App 處理 (walk / diary)；處理了回傳 true
  void setExtra(bool (*fn)(const char *)) { extra_ = fn; }

 private:
  enum class Mode : uint8_t { Command, AskSsid, AskPass, AskKey };
  void handleLine(char *line, uint32_t now);
  void printStatus();
  void checkSdCard();

  SettingsManager *settings_ = nullptr;
  WifiManager *wifi_ = nullptr;
  ClockManager *clock_ = nullptr;
  CostumeManager *costumes_ = nullptr;
  const MotionSensor *motion_ = nullptr;
  const BatteryMonitor *battery_ = nullptr;
  WeatherService *weather_ = nullptr;
  Mode mode_ = Mode::Command;
  char line_[96];
  uint8_t len_ = 0;
  bool lastWasCR_ = false;
  bool (*extra_)(const char *) = nullptr;
  char pendingSsid_[33];
};
