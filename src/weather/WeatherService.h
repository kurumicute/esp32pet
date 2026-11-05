#pragma once
#include <stdint.h>

// 中央氣象署 開放資料 F-C0032-001 (縣市 36 小時預報)
// 在獨立的 FreeRTOS 工作中以 HTTPS (驗證憑證) 抓資料，不會阻塞畫面。
// API 金鑰存在 NVS，不寫在原始碼；記錄訊息不會印出金鑰。
enum class WeatherKind : uint8_t { Unknown, Sun, Partly, Cloud, Rain, Thunder, Snow, Fog };

struct WeatherInfo {
  bool valid = false;
  WeatherKind kind = WeatherKind::Unknown;
  int8_t minT = 0;
  int8_t maxT = 0;
  uint8_t pop = 0;          // 降雨機率 %
  uint8_t wxCode = 0;       // 氣象署天氣代碼
  uint32_t updatedAt = 0;   // millis()
  int16_t lastHttp = 0;     // 最後一次 HTTP 狀態碼 (0 = 連線失敗)
};

class WeatherService {
 public:
  static constexpr uint8_t COUNTY_COUNT = 22;
  static const char *countyName(uint8_t i);  // UTF-8，例如 "臺北市"
  static bool validKey(const char *key);

  void begin();
  void configure(const char *apiKey, uint8_t county);  // 變更後會立即重新抓取
  WeatherInfo info() const;
  bool changed();  // 有新資料 (讀取後清除)

  // 以下僅供內部工作使用
  void taskLoop();

 private:
  bool fetch();
};
