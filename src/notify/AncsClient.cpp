#include "AncsClient.h"

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLESecurity.h>
#include <BLEServer.h>
#include <string.h>

#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_store.h"
#include "host/ble_uuid.h"

// Apple ANCS 規格：https://developer.apple.com/library/archive/documentation/CoreBluetooth/Reference/AppleNotificationCenterServiceSpecification/
namespace {
// 128-bit UUID (little-endian)
const ble_uuid128_t UUID_ANCS = BLE_UUID128_INIT(0xD0, 0x00, 0x2D, 0x12, 0x1E, 0x4B, 0x0F, 0xA4, 0x99,
                                                 0x4E, 0xCE, 0xB5, 0x31, 0xF4, 0x05, 0x79);
const ble_uuid128_t UUID_NS = BLE_UUID128_INIT(0xBD, 0x1D, 0xA2, 0x99, 0xE6, 0x25, 0x58, 0x8C, 0xD9,
                                               0x42, 0x01, 0x63, 0x0D, 0x12, 0xBF, 0x9F);
const ble_uuid128_t UUID_CP = BLE_UUID128_INIT(0xD9, 0xD9, 0xAA, 0xFD, 0xBD, 0x9B, 0x21, 0x98, 0xA8,
                                               0x49, 0xE1, 0x45, 0xF3, 0xD8, 0xD1, 0x69);
const ble_uuid128_t UUID_DS = BLE_UUID128_INIT(0xFB, 0x7B, 0x7C, 0xCE, 0x6A, 0xB3, 0x44, 0xBE, 0xB5,
                                               0x4B, 0xD6, 0x24, 0xE9, 0xC6, 0xEA, 0x22);
const ble_uuid16_t UUID_CCCD = BLE_UUID16_INIT(0x2902);

constexpr uint8_t ATTR_APP_ID = 0, ATTR_TITLE = 1, ATTR_MESSAGE = 3;
constexpr uint16_t MAX_TITLE = sizeof(PhoneNotification::title) - 1;
constexpr uint16_t MAX_MESSAGE = sizeof(PhoneNotification::message) - 1;
constexpr uint32_t REQUEST_TIMEOUT_MS = 4000;

// ---- 共用狀態 (NimBLE 工作 ↔ 主迴圈) ----
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
volatile AncsState state = AncsState::Off;
volatile uint16_t conn = BLE_HS_CONN_HANDLE_NONE;
volatile uint32_t shownPasskey = 0;
volatile bool passkeyActive = false;

// 探索到的 handle
uint16_t svcStart = 0, svcEnd = 0;
uint16_t nsVal = 0, cpVal = 0, dsVal = 0;
uint16_t nsCccd = 0, dsCccd = 0;
uint16_t chrDefs[12];
uint8_t chrCount = 0;

// 屬性請求：一次只發一個，其他排隊
uint32_t pendingUids[8];
uint8_t pendingHead = 0, pendingTail = 0;
volatile bool requestBusy = false;
volatile uint32_t requestAt = 0;
uint8_t dsBuf[512];
uint16_t dsLen = 0;

// 解析完成的通知
PhoneNotification queue[4];
uint8_t qHead = 0, qTail = 0;

ble_gap_event_listener listener;

// App Bundle ID → 顯示名稱
struct AppName {
  const char *id;
  const char *name;
};
const AppName APP_NAMES[] = {
    {"com.burbn.instagram", "IG"},          {"com.facebook.Facebook", "FB"},
    {"com.facebook.Messenger", "Messenger"}, {"jp.naver.line", "LINE"},
    {"com.hammerandchisel.discord", "Discord"}, {"net.whatsapp.WhatsApp", "WhatsApp"},
    {"ph.telegra.Telegraph", "Telegram"},    {"com.atebits.Tweetie2", "X"},
    {"com.zhiliaoapp.musically", "TikTok"},  {"com.google.ios.youtube", "YouTube"},
    {"com.google.Gmail", "Gmail"},           {"com.apple.MobileSMS", "訊息"},
    {"com.apple.mobilephone", "電話"},       {"com.apple.mobilemail", "郵件"},
    {"com.apple.mobilecal", "行事曆"},       {"com.apple.reminders", "提醒事項"},
    {"com.apple.facetime", "FaceTime"},      {"com.toyopagroup.picaboo", "Snapchat"},
    {"com.threads.app", "Threads"},          {"com.apple.Passbook", "錢包"},
};

void appDisplayName(const char *bundle, char *out, size_t cap) {
  for (const AppName &a : APP_NAMES) {
    if (strcmp(bundle, a.id) == 0) {
      strncpy(out, a.name, cap - 1);
      out[cap - 1] = '\0';
      return;
    }
  }
  const char *dot = strrchr(bundle, '.');  // 不認識的 App：取最後一段
  strncpy(out, dot ? dot + 1 : bundle, cap - 1);
  out[cap - 1] = '\0';
}

void copyUtf8(char *dst, size_t cap, const uint8_t *src, size_t len) {
  if (len > cap - 1) {
    len = cap - 1;
    while (len > 0 && (src[len] & 0xC0) == 0x80) len--;  // 不要切在多位元組字元中間
  }
  memcpy(dst, src, len);
  dst[len] = '\0';
  for (size_t i = 0; i < len; i++)
    if (dst[i] == '\r') dst[i] = ' ';
}

void resetSession() {
  svcStart = svcEnd = nsVal = cpVal = dsVal = nsCccd = dsCccd = 0;
  chrCount = 0;
  pendingHead = pendingTail = 0;
  requestBusy = false;
  dsLen = 0;
}

// ---------------- 屬性請求 ----------------
int cpWriteCb(uint16_t, const ble_gatt_error *err, ble_gatt_attr *, void *) {
  if (err->status != 0) {
    Serial.printf("[ANCS] control point write failed: %d\n", err->status);
    requestBusy = false;
  }
  return 0;
}

void sendNextRequest() {
  if (requestBusy || cpVal == 0) return;
  uint32_t uid;
  portENTER_CRITICAL(&mux);
  const bool has = pendingHead != pendingTail;
  if (has) {
    uid = pendingUids[pendingTail];
    pendingTail = (pendingTail + 1) % 8;
  }
  portEXIT_CRITICAL(&mux);
  if (!has) return;

  // Get Notification Attributes：App ID、標題 (限長)、內容 (限長)
  uint8_t cmd[] = {0x00,
                   (uint8_t)uid, (uint8_t)(uid >> 8), (uint8_t)(uid >> 16), (uint8_t)(uid >> 24),
                   ATTR_APP_ID,
                   ATTR_TITLE, (uint8_t)MAX_TITLE, (uint8_t)(MAX_TITLE >> 8),
                   ATTR_MESSAGE, (uint8_t)MAX_MESSAGE, (uint8_t)(MAX_MESSAGE >> 8)};
  dsLen = 0;
  requestBusy = true;
  requestAt = millis();
  if (ble_gattc_write_flat(conn, cpVal, cmd, sizeof(cmd), cpWriteCb, nullptr) != 0) requestBusy = false;
}

// Data Source 回應可能被拆成好幾個封包：收齊 3 個屬性才算完成
void handleDataSource(const uint8_t *d, uint16_t len) {
  if (!requestBusy) return;
  if (dsLen + len > sizeof(dsBuf)) {
    requestBusy = false;
    dsLen = 0;
    return;
  }
  memcpy(dsBuf + dsLen, d, len);
  dsLen += len;
  if (dsLen < 5) return;

  PhoneNotification n = {};
  char bundle[64] = "";
  uint16_t p = 5;  // 跳過 CommandID + NotificationUID
  uint8_t got = 0;
  while (got < 3) {
    if (p + 3 > dsLen) return;  // 還沒收完
    const uint8_t id = dsBuf[p];
    const uint16_t l = dsBuf[p + 1] | (dsBuf[p + 2] << 8);
    if (p + 3 + l > dsLen) return;
    const uint8_t *v = dsBuf + p + 3;
    if (id == ATTR_APP_ID) copyUtf8(bundle, sizeof(bundle), v, l);
    else if (id == ATTR_TITLE) copyUtf8(n.title, sizeof(n.title), v, l);
    else if (id == ATTR_MESSAGE) copyUtf8(n.message, sizeof(n.message), v, l);
    p += 3 + l;
    got++;
  }
  appDisplayName(bundle, n.app, sizeof(n.app));
  portENTER_CRITICAL(&mux);
  const uint8_t next = (qHead + 1) % 4;
  if (next != qTail) {  // 佇列滿就丟掉最新的，避免卡住
    queue[qHead] = n;
    qHead = next;
  }
  portEXIT_CRITICAL(&mux);
  requestBusy = false;
  dsLen = 0;
  sendNextRequest();
}

// Notification Source：8 bytes = EventID, Flags, Category, Count, UID(4)
void handleNotificationSource(const uint8_t *d, uint16_t len) {
  if (len < 8) return;
  const uint8_t eventId = d[0], flags = d[1];
  const uint32_t uid = d[4] | (d[5] << 8) | (d[6] << 16) | ((uint32_t)d[7] << 24);
  if (eventId != 0) return;    // 只處理「新增」
  if (flags & 0x04) return;    // PreExisting：連線前就存在的舊通知，不顯示
  portENTER_CRITICAL(&mux);
  const uint8_t next = (pendingHead + 1) % 8;
  if (next != pendingTail) {
    pendingUids[pendingHead] = uid;
    pendingHead = next;
  }
  portEXIT_CRITICAL(&mux);
  sendNextRequest();
}

// ---------------- 服務探索 → 訂閱 ----------------
int subscribeCb(uint16_t, const ble_gatt_error *err, ble_gatt_attr *, void *arg) {
  const int step = (int)(intptr_t)arg;
  if (err->status != 0) {
    Serial.printf("[ANCS] subscribe step %d failed: %d\n", step, err->status);
    return 0;
  }
  if (step == 1) {  // Data Source 訂閱完成 → 再訂閱 Notification Source
    const uint8_t on[2] = {0x01, 0x00};
    ble_gattc_write_flat(conn, nsCccd, on, 2, subscribeCb, (void *)2);
  } else {
    state = AncsState::Ready;
    Serial.println("[ANCS] ready: iPhone notifications will show on screen");
  }
  return 0;
}

void subscribe() {
  if (nsCccd == 0 || dsCccd == 0 || cpVal == 0) {
    Serial.println("[ANCS] characteristics not found");
    return;
  }
  const uint8_t on[2] = {0x01, 0x00};
  ble_gattc_write_flat(conn, dsCccd, on, 2, subscribeCb, (void *)1);
}

uint16_t chrEnd(uint16_t valHandle) {  // 特徵值之後到下一個特徵宣告前，是它的描述符範圍
  uint16_t end = svcEnd;
  for (uint8_t i = 0; i < chrCount; i++)
    if (chrDefs[i] > valHandle && chrDefs[i] - 1 < end) end = chrDefs[i] - 1;
  return end;
}

int dscCb(uint16_t, const ble_gatt_error *err, uint16_t chrVal, const ble_gatt_dsc *dsc, void *arg) {
  const int which = (int)(intptr_t)arg;  // 1 = NS, 2 = DS
  if (err->status == 0 && dsc != nullptr) {
    if (ble_uuid_cmp(&dsc->uuid.u, &UUID_CCCD.u) == 0) {
      if (which == 1) nsCccd = dsc->handle;
      else dsCccd = dsc->handle;
    }
    return 0;
  }
  if (err->status == BLE_HS_EDONE) {
    if (which == 1) ble_gattc_disc_all_dscs(conn, dsVal, chrEnd(dsVal), dscCb, (void *)2);
    else subscribe();
  }
  return 0;
}

int chrCb(uint16_t, const ble_gatt_error *err, const ble_gatt_chr *chr, void *) {
  if (err->status == 0 && chr != nullptr) {
    if (chrCount < sizeof(chrDefs) / sizeof(chrDefs[0])) chrDefs[chrCount++] = chr->def_handle;
    if (ble_uuid_cmp(&chr->uuid.u, &UUID_NS.u) == 0) nsVal = chr->val_handle;
    else if (ble_uuid_cmp(&chr->uuid.u, &UUID_CP.u) == 0) cpVal = chr->val_handle;
    else if (ble_uuid_cmp(&chr->uuid.u, &UUID_DS.u) == 0) dsVal = chr->val_handle;
    return 0;
  }
  if (err->status == BLE_HS_EDONE) {
    if (nsVal == 0 || dsVal == 0 || cpVal == 0) {
      Serial.println("[ANCS] ANCS characteristics missing");
      return 0;
    }
    ble_gattc_disc_all_dscs(conn, nsVal, chrEnd(nsVal), dscCb, (void *)1);
  }
  return 0;
}

int svcCb(uint16_t, const ble_gatt_error *err, const ble_gatt_svc *svc, void *) {
  if (err->status == 0 && svc != nullptr) {
    svcStart = svc->start_handle;
    svcEnd = svc->end_handle;
    return 0;
  }
  if (err->status == BLE_HS_EDONE) {
    if (svcStart == 0) {
      Serial.println("[ANCS] iPhone did not expose ANCS (not an iPhone, or notifications not allowed)");
      return 0;
    }
    ble_gattc_disc_all_chrs(conn, svcStart, svcEnd, chrCb, nullptr);
  }
  return 0;
}

void startDiscovery() {
  resetSession();
  ble_gattc_exchange_mtu(conn, nullptr, nullptr);
  ble_gattc_disc_svc_by_uuid(conn, &UUID_ANCS.u, svcCb, nullptr);
}

// 收 iPhone 送來的 notify (包裝函式庫不處理這種事件，所以掛全域監聽器)
int gapListener(ble_gap_event *event, void *) {
  if (event->type != BLE_GAP_EVENT_NOTIFY_RX || event->notify_rx.conn_handle != conn) return 0;
  uint8_t buf[256];
  uint16_t len = 0;
  if (ble_hs_mbuf_to_flat(event->notify_rx.om, buf, sizeof(buf), &len) != 0 && len == 0) return 0;
  if (event->notify_rx.attr_handle == dsVal) handleDataSource(buf, len);
  else if (event->notify_rx.attr_handle == nsVal) handleNotificationSource(buf, len);
  return 0;
}

// ---------------- 連線 / 配對 ----------------
class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *, ble_gap_conn_desc *desc) override {
    conn = desc->conn_handle;
    state = AncsState::Pairing;
    resetSession();
    Serial.println("[ANCS] phone connected, securing link...");
    ble_gap_security_initiate(conn);  // 已配對過會直接加密；沒配對過 iPhone 會跳出配對碼輸入
  }
  void onDisconnect(BLEServer *, ble_gap_conn_desc *) override {
    conn = BLE_HS_CONN_HANDLE_NONE;
    state = AncsState::Waiting;
    passkeyActive = false;
    resetSession();
    Serial.println("[ANCS] phone disconnected");
  }
};

class SecurityCallbacks : public BLESecurityCallbacks {
  void onPassKeyNotify(uint32_t pk) override {
    shownPasskey = pk;
    passkeyActive = true;
  }
  void onAuthenticationComplete(ble_gap_conn_desc *desc, int status) override {
    passkeyActive = false;
    if (status != 0 || !desc->sec_state.encrypted) {
      Serial.printf("[ANCS] pairing failed (%d)\n", status);
      return;
    }
    state = AncsState::Connected;
    Serial.println("[ANCS] paired, discovering ANCS...");
    startDiscovery();
  }
};

ServerCallbacks serverCallbacks;
SecurityCallbacks securityCallbacks;
}  // namespace

void AncsClient::begin(const char *deviceName) {
  if (started_) return;
  started_ = true;
  BLEDevice::init(deviceName);
  // 配對：顯示 6 位數配對碼 (板子只有螢幕、沒有鍵盤)、要綁定、防中間人、Secure Connections
  BLESecurity::setCapability(BLE_HS_IO_DISPLAY_ONLY);
  BLESecurity::setAuthenticationMode(true, true, true);
  BLESecurity::setPassKey(false);  // 隨機配對碼
  BLESecurity::regenPassKeyOnConnect(true);
  BLEDevice::setSecurityCallbacks(&securityCallbacks);

  BLEServer *server = BLEDevice::createServer();
  server->setCallbacks(&serverCallbacks);
  server->advertiseOnDisconnect(true);
  server->start();

  listener = {};
  ble_gap_event_listener_register(&listener, gapListener, nullptr);

  // 廣播「請求 ANCS 服務」，iPhone 才會在「設定 > 藍牙」列出這個裝置
  BLEAdvertisementData adv;
  adv.setFlags(0x06);
  char solicit[18] = {17, 0x15};  // 長度 17、AD type 0x15 = 128-bit Service Solicitation
  memcpy(solicit + 2, UUID_ANCS.value, 16);
  adv.addData(solicit, sizeof(solicit));
  BLEAdvertisementData scan;
  scan.setName(deviceName);
  BLEAdvertising *a = BLEDevice::getAdvertising();
  a->setAdvertisementData(adv);
  a->setScanResponseData(scan);
  a->start();
  ::state = AncsState::Waiting;
  Serial.printf("[ANCS] advertising as \"%s\"\n", deviceName);
}

void AncsClient::update(uint32_t now) {
  if (!started_) return;
  // 屬性請求逾時 (iPhone 沒回或封包遺失)：放棄這則，處理下一則
  if (requestBusy && now - requestAt > REQUEST_TIMEOUT_MS) {
    requestBusy = false;
    dsLen = 0;
    sendNextRequest();
  }
}

AncsState AncsClient::state() const { return ::state; }

bool AncsClient::pop(PhoneNotification &out) {
  bool has = false;
  portENTER_CRITICAL(&mux);
  if (qHead != qTail) {
    out = queue[qTail];
    qTail = (qTail + 1) % 4;
    has = true;
  }
  portEXIT_CRITICAL(&mux);
  return has;
}

bool AncsClient::passkey(uint32_t &out) const {
  if (!passkeyActive) return false;
  out = shownPasskey;
  return true;
}

void AncsClient::setEnabled(bool on) {
  if (!started_) {
    if (on) begin("DeskPet");
    return;
  }
  BLEServer *server = BLEDevice::createServer();  // 已建立過，會回傳同一個
  if (on) {
    server->advertiseOnDisconnect(true);
    if (conn == BLE_HS_CONN_HANDLE_NONE) BLEDevice::getAdvertising()->start();
    if (::state == AncsState::Off) ::state = AncsState::Waiting;
  } else {
    server->advertiseOnDisconnect(false);
    BLEDevice::getAdvertising()->stop();
    if (conn != BLE_HS_CONN_HANDLE_NONE) ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
    ::state = AncsState::Off;
  }
}

void AncsClient::clearBonds() {
  if (conn != BLE_HS_CONN_HANDLE_NONE) ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
  ble_store_clear();
  Serial.println("[ANCS] all pairings removed (also tap \"Forget This Device\" on the iPhone)");
}
