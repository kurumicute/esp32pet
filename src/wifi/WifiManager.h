#pragma once
#include <stdint.h>

enum class WifiState : uint8_t { NoCredentials, Connecting, Connected, Disconnected };

// 非阻塞 Wi-Fi 連線管理：連線失敗只會定期重試，不會卡住主畫面。
// 連不上 (或還沒設定) 時自動開「設定熱點」，手機連上後由 WebPanel 提供設定頁。
class WifiManager {
 public:
  void begin(const char *ssid, const char *pass, uint32_t now);
  void setCredentials(const char *ssid, const char *pass, uint32_t now);
  void update(uint32_t now);
  WifiState state() const { return state_; }
  bool justConnected();  // 剛連上 (讀取後清除)

  // ---- 設定熱點 ----
  void startAp(uint32_t now);
  void stopAp();
  bool apActive() const { return apActive_; }
  bool apJustStarted();  // 剛開熱點 (讀取後清除)
  const char *apSsid() const { return apSsid_; }
  const char *apPass() const { return apPass_; }

 private:
  void connect(uint32_t now);

  char ssid_[33] = {0};
  char pass_[65] = {0};
  WifiState state_ = WifiState::NoCredentials;
  uint32_t lastAttempt_ = 0;
  uint32_t retryMs_ = 15000;
  uint32_t lastPoll_ = 0;
  uint32_t offlineSince_ = 0;  // 從什麼時候開始沒連上 (0 = 已連上)
  bool justConnected_ = false;

  bool apActive_ = false;
  bool apJustStarted_ = false;
  uint32_t apStopAt_ = 0;  // 連上後延遲關熱點 (讓手機頁面看得到成功)
  char apSsid_[20] = {0};
  char apPass_[9] = {0};
};
