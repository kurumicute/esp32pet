#pragma once
#include <time.h>

// 時間來源：Wi-Fi + NTP；同步後由 ESP32 系統時鐘持續計時，Wi-Fi 斷線不影響。
// (斷電後時間會遺失，需重新連網同步)
class ClockManager {
 public:
  void startSync();       // 連上網路後呼叫，可重複呼叫
  bool isSynced() const;  // 是否已取得有效時間
  bool now(tm &out) const;

 private:
  bool started_ = false;
};
