#include "WeatherService.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <stdlib.h>
#include <string.h>

#include "../display/LargeAlloc.h"
#include "../ui/Strings.h"

namespace {
constexpr uint32_t REFRESH_MS = 30UL * 60 * 1000;  // 30 分鐘更新
constexpr uint32_t RETRY_MS = 3UL * 60 * 1000;     // 失敗 3 分鐘後重試
constexpr uint32_t STACK_WORDS = 8192;

static_assert(sizeof(Str::COUNTIES) / sizeof(Str::COUNTIES[0]) == WeatherService::COUNTY_COUNT,
              "縣市數量不一致");
const char *const *COUNTIES = Str::COUNTIES;

// 跨工作共用的資料，以 critical section 保護
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
char cfgKey[65] = {0};
uint8_t cfgCounty = 0;
WeatherInfo shared;
bool changedFlag = false;

TaskHandle_t task = nullptr;
StaticTask_t taskTcb;
StackType_t taskStack[STACK_WORDS];

constexpr size_t RESP_SIZE = 6144;
char *resp = nullptr;  // 只在天氣工作中使用 (PSRAM)

WeatherKind kindFromCode(int c) {
  if (c == 1) return WeatherKind::Sun;
  if (c == 2 || c == 3) return WeatherKind::Partly;
  if (c >= 4 && c <= 7) return WeatherKind::Cloud;
  if (c == 23 || c == 37 || c == 42) return WeatherKind::Snow;
  if ((c >= 15 && c <= 18) || c == 21 || c == 22 || (c >= 33 && c <= 36) || c == 41)
    return WeatherKind::Thunder;
  if (c >= 24 && c <= 28) return WeatherKind::Fog;
  if ((c >= 8 && c <= 14) || (c >= 19 && c <= 20) || (c >= 29 && c <= 32) || (c >= 38 && c <= 39))
    return WeatherKind::Rain;
  return WeatherKind::Unknown;
}

// 在 elementName 之後找第一個 key 的字串值，例如 "parameterValue":"2"
bool findValue(const char *json, const char *element, const char *key, int &out) {
  char pat[48];
  snprintf(pat, sizeof(pat), "\"elementName\":\"%s\"", element);
  const char *p = strstr(json, pat);
  if (!p) return false;
  snprintf(pat, sizeof(pat), "\"%s\":\"", key);
  p = strstr(p, pat);
  if (!p) return false;
  p += strlen(pat);
  char *end = nullptr;
  const long v = strtol(p, &end, 10);
  if (end == p || *end != '"') return false;
  out = (int)v;
  return true;
}

void urlEncode(const char *in, char *out, size_t cap) {
  static const char *HEX_DIGITS = "0123456789ABCDEF";
  size_t o = 0;
  for (; *in && o + 4 < cap; in++) {
    const uint8_t c = (uint8_t)*in;
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      out[o++] = (char)c;
    } else {
      out[o++] = '%';
      out[o++] = HEX_DIGITS[c >> 4];
      out[o++] = HEX_DIGITS[c & 15];
    }
  }
  out[o] = '\0';
}

void taskEntry(void *arg) { static_cast<WeatherService *>(arg)->taskLoop(); }
}  // namespace

const char *WeatherService::countyName(uint8_t i) {
  return COUNTIES[i < COUNTY_COUNT ? i : 0];
}

bool WeatherService::validKey(const char *key) {
  const size_t n = strlen(key);
  if (n < 10 || n > 64) return false;
  for (size_t i = 0; i < n; i++)
    if (!isalnum((uint8_t)key[i]) && key[i] != '-') return false;
  return true;
}

void WeatherService::begin() {
  if (task) return;
  if (resp == nullptr) resp = allocLarge<char>(RESP_SIZE);
  if (resp == nullptr) return;
  task = xTaskCreateStaticPinnedToCore(taskEntry, "weather", STACK_WORDS, this, 1, taskStack,
                                       &taskTcb, 0);
}

void WeatherService::configure(const char *apiKey, uint8_t county) {
  portENTER_CRITICAL(&mux);
  strncpy(cfgKey, apiKey ? apiKey : "", sizeof(cfgKey) - 1);
  cfgKey[sizeof(cfgKey) - 1] = '\0';
  cfgCounty = county < COUNTY_COUNT ? county : 0;
  shared.valid = false;
  portEXIT_CRITICAL(&mux);
  if (task) xTaskNotifyGive(task);  // 立即重新抓取
}

WeatherInfo WeatherService::info() const {
  portENTER_CRITICAL(&mux);
  const WeatherInfo w = shared;
  portEXIT_CRITICAL(&mux);
  return w;
}

bool WeatherService::changed() {
  portENTER_CRITICAL(&mux);
  const bool c = changedFlag;
  changedFlag = false;
  portEXIT_CRITICAL(&mux);
  return c;
}

void WeatherService::taskLoop() {
  for (;;) {
    uint32_t wait = RETRY_MS;
    char key[65];
    portENTER_CRITICAL(&mux);
    memcpy(key, cfgKey, sizeof(key));
    portEXIT_CRITICAL(&mux);
    if (key[0] != '\0' && WiFi.isConnected()) {
      wait = fetch() ? REFRESH_MS : RETRY_MS;
    } else if (key[0] != '\0') {
      wait = 10000;  // 等 Wi-Fi 連上
    } else {
      wait = portMAX_DELAY;  // 沒有金鑰：等 configure() 喚醒
    }
    ulTaskNotifyTake(pdTRUE, wait == portMAX_DELAY ? portMAX_DELAY : pdMS_TO_TICKS(wait));
  }
}

bool WeatherService::fetch() {
  char key[65];
  uint8_t county;
  portENTER_CRITICAL(&mux);
  memcpy(key, cfgKey, sizeof(key));
  county = cfgCounty;
  portEXIT_CRITICAL(&mux);
  if (!validKey(key)) return false;

  char loc[64];
  urlEncode(COUNTIES[county], loc, sizeof(loc));
  char url[256];
  snprintf(url, sizeof(url),
           "https://opendata.cwa.gov.tw/api/v1/rest/datastore/F-C0032-001"
           "?Authorization=%s&locationName=%s&elementName=Wx,MinT,MaxT,PoP",
           key, loc);
  memset(key, 0, sizeof(key));

  esp_http_client_config_t cfg = {};
  cfg.url = url;
  cfg.crt_bundle_attach = esp_crt_bundle_attach;  // 用內建 CA 憑證包驗證伺服器
  cfg.timeout_ms = 10000;
  cfg.user_agent = "DeskPet/1.0";
  esp_http_client_handle_t client = esp_http_client_init(&cfg);
  memset(url, 0, sizeof(url));
  if (!client) return false;

  int status = 0;
  int len = 0;
  bool ok = false;
  if (esp_http_client_open(client, 0) == ESP_OK) {
    esp_http_client_fetch_headers(client);
    status = esp_http_client_get_status_code(client);
    while (len < (int)RESP_SIZE - 1) {
      const int r = esp_http_client_read(client, resp + len, RESP_SIZE - 1 - len);
      if (r <= 0) break;
      len += r;
    }
    resp[len] = '\0';
    ok = status == 200;
  }
  esp_http_client_close(client);
  esp_http_client_cleanup(client);

  WeatherInfo w;
  w.lastHttp = (int16_t)status;
  int wx = 0, mn = 0, mx = 0, pop = 0;
  if (ok && strstr(resp, "\"success\":\"true\"") && findValue(resp, "Wx", "parameterValue", wx) &&
      findValue(resp, "MinT", "parameterName", mn) && findValue(resp, "MaxT", "parameterName", mx)) {
    findValue(resp, "PoP", "parameterName", pop);
    w.valid = true;
    w.wxCode = (uint8_t)wx;
    w.kind = kindFromCode(wx);
    w.minT = (int8_t)mn;
    w.maxT = (int8_t)mx;
    w.pop = (uint8_t)constrain(pop, 0, 100);
    w.updatedAt = millis();
    Serial.printf("[DeskPet] weather: wx=%d %d~%dC pop=%d%%\n", wx, mn, mx, pop);
  } else {
    Serial.printf("[DeskPet] weather fetch failed (http %d)\n", status);
  }

  portENTER_CRITICAL(&mux);
  if (w.valid || !shared.valid) {
    shared = w;  // 失敗時保留上一次的有效資料
  } else {
    shared.lastHttp = w.lastHttp;
  }
  changedFlag = true;
  portEXIT_CRITICAL(&mux);
  return w.valid;
}
