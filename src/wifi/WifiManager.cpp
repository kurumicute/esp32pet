#include "WifiManager.h"

#include <WiFi.h>
#include <string.h>

namespace {
constexpr uint32_t POLL_MS = 500;
constexpr uint32_t RETRY_MIN_MS = 15000;
constexpr uint32_t RETRY_MAX_MS = 120000;
constexpr uint32_t AP_AFTER_MS = 60000;      // 連不上多久就開設定熱點
// 熱點開著時，多久用舊設定再試一次 (沒人連熱點時才試)。
// 板子只有一顆射頻：STA 掃描找路由器時熱點會離開自己的頻道，手機正在連線/交握就會失敗
// (常顯示「密碼錯誤」)，所以間隔拉長，而且每次只試一輪、不自動重連。
constexpr uint32_t AP_RETRY_MS = 300000;
constexpr uint8_t AP_CHANNEL = 1;
constexpr uint32_t AP_STOP_DELAY_MS = 20000; // 連上後過多久關熱點
constexpr uint32_t AP_IDLE_STOP_MS = 180000; // 已連線時手動開的熱點，多久自動關

void copyStr(char *dst, size_t cap, const char *src) {
  strncpy(dst, src ? src : "", cap - 1);
  dst[cap - 1] = '\0';
}
}  // namespace

void WifiManager::begin(const char *ssid, const char *pass, uint32_t now) {
  WiFi.persistent(false);  // 帳密由 SettingsManager 管理，不讓 WiFi 函式庫另存一份
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  offlineSince_ = now ? now : 1;
  setCredentials(ssid, pass, now);
  if (state_ == WifiState::NoCredentials) startAp(now);  // 還沒設定過：直接開設定熱點
}

void WifiManager::setCredentials(const char *ssid, const char *pass, uint32_t now) {
  copyStr(ssid_, sizeof(ssid_), ssid);
  copyStr(pass_, sizeof(pass_), pass);
  WiFi.disconnect();
  if (ssid_[0] == '\0') {
    state_ = WifiState::NoCredentials;
    return;
  }
  retryMs_ = RETRY_MIN_MS;
  offlineSince_ = now ? now : 1;
  connect(now);
}

void WifiManager::connect(uint32_t now) {
  WiFi.begin(ssid_, pass_);  // 非阻塞
  lastAttempt_ = now;
  state_ = WifiState::Connecting;
}

bool WifiManager::justConnected() {
  const bool j = justConnected_;
  justConnected_ = false;
  return j;
}

bool WifiManager::apJustStarted() {
  const bool j = apJustStarted_;
  apJustStarted_ = false;
  return j;
}

void WifiManager::startAp(uint32_t now) {
  if (apActive_) return;
  uint8_t mac[6];
  WiFi.macAddress(mac);
  snprintf(apSsid_, sizeof(apSsid_), "DeskPet-%02X%02X", mac[4], mac[5]);
  // 每次開熱點都換一組 8 位數密碼，只顯示在板子螢幕上
  snprintf(apPass_, sizeof(apPass_), "%08lu", (unsigned long)(esp_random() % 100000000UL));
  WiFi.mode(WIFI_AP_STA);
  // 停掉 STA 的自動重連：否則連不上的舊設定會一直掃描各頻道，熱點跟著跳頻，手機連不進來
  // (已連線時手動開熱點則保留原連線，熱點跟著路由器的頻道)
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.setAutoReconnect(false);
    WiFi.disconnect();
  }
  if (!WiFi.softAP(apSsid_, apPass_, AP_CHANNEL)) Serial.println("[DeskPet] softAP start failed");
  lastAttempt_ = now;  // 開熱點後先給手機一段時間，不馬上重試舊設定
  apActive_ = true;
  apJustStarted_ = true;
  apStopAt_ = 0;
  Serial.printf("[DeskPet] setup hotspot on: %s (password on screen), http://%s/\n", apSsid_,
                WiFi.softAPIP().toString().c_str());
}

void WifiManager::stopAp() {
  if (!apActive_) return;
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  apActive_ = false;
  memset(apPass_, 0, sizeof(apPass_));
  Serial.println("[DeskPet] setup hotspot off");
}

void WifiManager::update(uint32_t now) {
  if (now - lastPoll_ < POLL_MS) return;
  lastPoll_ = now;

  if (apActive_ && apStopAt_ != 0 && (int32_t)(now - apStopAt_) >= 0) {
    if (WiFi.softAPgetStationNum() > 0) apStopAt_ = now + 60000;  // 還有手機連著：晚點再關
    else stopAp();
  }
  // 已經連上 Wi-Fi 卻開著熱點 (例如手動 ap 指令)：3 分鐘後自動關
  if (apActive_ && apStopAt_ == 0 && state_ == WifiState::Connected) apStopAt_ = now + AP_IDLE_STOP_MS;

  if (state_ == WifiState::NoCredentials) {
    if (!apActive_) startAp(now);
    return;
  }

  if (WiFi.status() == WL_CONNECTED) {
    if (state_ != WifiState::Connected) {
      state_ = WifiState::Connected;
      justConnected_ = true;
      retryMs_ = RETRY_MIN_MS;
      offlineSince_ = 0;
      if (apActive_) apStopAt_ = now + AP_STOP_DELAY_MS;
    }
    return;
  }

  if (state_ == WifiState::Connected) {  // 斷線：先交給自動重連
    state_ = WifiState::Disconnected;
    lastAttempt_ = now;
    offlineSince_ = now ? now : 1;
  }
  if (offlineSince_ == 0) offlineSince_ = now ? now : 1;

  // 連不上太久：開設定熱點
  if (!apActive_ && now - offlineSince_ >= AP_AFTER_MS) startAp(now);

  if (apActive_) {
    // 熱點開著時，重試連線會讓 Wi-Fi 換頻道，手機可能斷線；所以只在沒人連熱點時才用舊設定再試
    if (WiFi.softAPgetStationNum() == 0 && now - lastAttempt_ >= AP_RETRY_MS) connect(now);
    return;
  }
  // 太久沒連上就重新發起連線，間隔逐步拉長
  if (now - lastAttempt_ >= retryMs_) {
    WiFi.disconnect();
    connect(now);
    retryMs_ = retryMs_ * 2 > RETRY_MAX_MS ? RETRY_MAX_MS : retryMs_ * 2;
  }
}
