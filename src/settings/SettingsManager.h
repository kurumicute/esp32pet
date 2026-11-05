#pragma once
#include <stdint.h>

struct Settings {
  uint8_t costume = 0;
  uint8_t character = 0;    // 角色 (0 水伊布，1 水晶燈火靈，2 達克萊伊，3 以後 = SD 卡角色；見 Characters)
  bool sleepEnabled = true;
  uint8_t sleepStart = 23;  // 時 (0~23)
  uint8_t sleepEnd = 7;     // 時 (0~23)
  uint8_t brightness = 80;  // %
  uint8_t napMinutes = 3;   // 閒置幾分鐘後小睡，0 = 關閉
  bool autoCostume = true;  // 季節 / 生日 / 夜間自動換裝
  uint8_t birthMonth = 0;   // 0 = 未設定
  uint8_t birthDay = 0;
  uint8_t focusMin = 25;    // 番茄鐘專注分鐘
  uint8_t breakMin = 5;     // 番茄鐘休息分鐘
  uint8_t county = 0;       // 天氣縣市 (WeatherService::countyName)
  uint8_t bgMode = 0;       // 背景：0 自動 (依時間)，1 清晨，2 白天，3 黃昏，4 夜晚
  bool weatherFx = true;    // 背景天氣特效 (雲 / 雨 / 雪 / 霧)
  bool tempMood = true;     // 溫度表情 (熱 → 扇子流汗，冷 → 圍巾發抖)
  bool hourlyFx = true;     // 時段小動作 / 整點提示
  bool tiltOn = true;       // 傾斜互動
  uint8_t shakeLevel = 1;   // 搖晃靈敏度：0 低，1 中，2 高
  uint8_t patLevel = 2;     // 拍頭偵測：0 關，1 低，2 中，3 高
  bool soundOn = false;     // 音效 (預設關閉)
  uint8_t soundVol = 1;     // 音量：0 低，1 中，2 高
  bool alarmOn = false;     // 鬧鐘
  uint8_t alarmHour = 7;
  uint8_t alarmMin = 0;
  uint8_t alarmDays = 0;    // 0 每天，1 平日 (週一~五)，2 週末
  uint8_t powerSave = 1;    // 省電：0 關，1 自動 (沒在充電時)，2 開
  bool notifyOn = true;     // iPhone 通知同步 (藍牙 ANCS)
  bool notifyDetail = true; // 通知顯示內容；false = 只顯示 App 名稱
  char ssid[33] = {0};
  char pass[65] = {0};
  char cwaKey[65] = {0};    // 中央氣象署 API 金鑰 (只存在 NVS)
};

// 存在 NVS (Preferences)。只寫入有變動的欄位，降低 Flash 磨損。
class SettingsManager {
 public:
  void load();
  Settings &get() { return cur_; }
  void save();
  void setWifi(const char *ssid, const char *pass);
  void setCwaKey(const char *key);
  static void sanitize(Settings &s);  // 把超出範圍的值修正回合法值

 private:
  Settings cur_;
  Settings saved_;
};
