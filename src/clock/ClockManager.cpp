#include "ClockManager.h"

#include <Arduino.h>

namespace {
constexpr const char *TZ_TAIPEI = "CST-8";  // Asia/Taipei, UTC+8，無日光節約
constexpr time_t VALID_AFTER = 1704067200;  // 2024-01-01，早於此視為尚未同步
}  // namespace

void ClockManager::startSync() {
  if (started_) return;  // SNTP 啟動後會自動定期重新同步
  configTzTime(TZ_TAIPEI, "time.stdtime.gov.tw", "pool.ntp.org", "time.google.com");
  started_ = true;
}

bool ClockManager::isSynced() const { return time(nullptr) > VALID_AFTER; }

bool ClockManager::now(tm &out) const {
  const time_t t = time(nullptr);
  if (t <= VALID_AFTER) return false;
  localtime_r(&t, &out);
  return true;
}
