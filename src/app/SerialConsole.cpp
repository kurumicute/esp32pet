#include "SerialConsole.h"

#include <Arduino.h>
#include <SD_MMC.h>

#include "../storage/SdStore.h"
#include <WiFi.h>
#include <Wire.h>
#include <string.h>

#include "../clock/ClockManager.h"
#include "../config/BoardConfig.h"
#include "../costume/CostumeManager.h"
#include "../power/BatteryMonitor.h"
#include "../sensor/MotionSensor.h"
#include "../settings/SettingsManager.h"
#include "../weather/WeatherService.h"
#include "../wifi/WifiManager.h"

namespace {
const char *wifiStateName(WifiState s) {
  switch (s) {
    case WifiState::NoCredentials: return "not configured";
    case WifiState::Connecting: return "connecting";
    case WifiState::Connected: return "connected";
    default: return "disconnected";
  }
}
}  // namespace

void SerialConsole::begin(SettingsManager &s, WifiManager &w, ClockManager &c,
                          CostumeManager &cos, const MotionSensor &m,
                          const BatteryMonitor &b, WeatherService &ws) {
  weather_ = &ws;
  settings_ = &s;
  wifi_ = &w;
  clock_ = &c;
  costumes_ = &cos;
  motion_ = &m;
  battery_ = &b;
  Serial.println("\n[DeskPet] type 'help' for commands");
}

void SerialConsole::update(uint32_t now) {
  // 每次最多處理 64 字元，避免大量輸入佔住主迴圈
  for (uint8_t n = 0; n < 64 && Serial.available() > 0; n++) {
    const int ch = Serial.read();
    const bool crlf = (ch == '\n' && lastWasCR_);  // \r\n 只算一次換行
    lastWasCR_ = (ch == '\r');
    if (crlf) continue;
    if (ch == '\r' || ch == '\n') {
      if (len_ == 0 && mode_ == Mode::Command) continue;
      line_[len_] = '\0';
      len_ = 0;
      handleLine(line_, now);
    } else if (len_ < sizeof(line_) - 1) {
      line_[len_++] = (char)ch;
    }
  }
}

void SerialConsole::handleLine(char *line, uint32_t now) {
  if (mode_ == Mode::AskSsid) {
    if (strlen(line) == 0 || strlen(line) > 32) {
      Serial.println("SSID must be 1-32 chars. Cancelled.");
      mode_ = Mode::Command;
      return;
    }
    strncpy(pendingSsid_, line, sizeof(pendingSsid_) - 1);
    pendingSsid_[sizeof(pendingSsid_) - 1] = '\0';
    Serial.println("Password (empty line = open network):");
    mode_ = Mode::AskPass;
    return;
  }
  if (mode_ == Mode::AskKey) {
    mode_ = Mode::Command;
    if (strlen(line) == 0) {
      settings_->setCwaKey("");
      weather_->configure("", settings_->get().county);
      Serial.println("CWA key cleared.");
    } else if (!WeatherService::validKey(line)) {
      Serial.println("Invalid key format (letters, digits, '-', 10-64 chars). Cancelled.");
    } else {
      settings_->setCwaKey(line);
      weather_->configure(line, settings_->get().county);
      Serial.println("CWA key saved. Fetching weather...");
    }
    memset(line, 0, sizeof(line_));
    return;
  }
  if (mode_ == Mode::AskPass) {
    mode_ = Mode::Command;
    const size_t n = strlen(line);
    if (n > 64 || (n > 0 && n < 8)) {
      Serial.println("Password must be 8-64 chars. Cancelled.");
      memset(line, 0, sizeof(line_));
      return;
    }
    settings_->setWifi(pendingSsid_, line);
    wifi_->setCredentials(pendingSsid_, line, now);
    memset(line, 0, sizeof(line_));  // 不在記憶體中殘留明文
    Serial.printf("Saved. Connecting to \"%s\"...\n", pendingSsid_);
    return;
  }

  // 去除前後空白
  while (*line == ' ') line++;
  for (int i = (int)strlen(line) - 1; i >= 0 && line[i] == ' '; i--) line[i] = '\0';

  if (strcmp(line, "help") == 0) {
    Serial.println("Commands:\n  status      show clock / wifi state\n"
                   "  wifi        set Wi-Fi SSID and password\n  wifi clear  forget Wi-Fi\n"
                   "  i2c         scan onboard I2C bus\n  imu         print gravity vector\n"
                   "  sd          show TF (microSD) card status\n"
                   "  backup      save settings to SD card\n"
                   "  restore     load settings from SD card\n"
                   "  ap          open the Wi-Fi setup hotspot now\n"
                   "  cwakey      set CWA weather API key (empty = clear)\n"
                   "  county [n]  list counties / set weather county\n"
                   "  weather     show weather state\n"
                   "  walk        start / stop the pet walk\n"
                   "  char [n]    list characters / switch character\n"
                   "  diary       print the daily diary\n"
                   "  tap         toggle tap (pat) debug output\n"
                   "  sound       play a test sound (if sound is on)\n"
                   "  sound on|off turn sound effects on / off\n"
                   "  cry         pet cry sound\n"
                   "  song 0~3    play music box song\n"
                   "  rhythm      start the rhythm game\n"
                   "  alarm test  ring the alarm now");
  } else if (strcmp(line, "ap") == 0) {
    if (wifi_->apActive()) {
      Serial.printf("hotspot already on: %s\n", wifi_->apSsid());
    } else {
      wifi_->startAp(now);
    }
  } else if (strcmp(line, "cwakey") == 0) {
    Serial.println("CWA API key (empty line = clear):");
    mode_ = Mode::AskKey;
  } else if (strncmp(line, "county", 6) == 0) {
    // county        列出縣市編號
    // county <編號> 設定天氣縣市
    const char *arg = line + 6;
    while (*arg == ' ') arg++;
    if (*arg == '\0') {
      for (uint8_t i = 0; i < WeatherService::COUNTY_COUNT; i++)
        Serial.printf("%2u %s%s\n", i, WeatherService::countyName(i),
                      i == settings_->get().county ? "  <- current" : "");
    } else {
      char *end = nullptr;
      const long v = strtol(arg, &end, 10);
      if (end == arg || *end != '\0' || v < 0 || v >= WeatherService::COUNTY_COUNT) {
        Serial.println("Usage: county <0-21>");
      } else {
        settings_->get().county = (uint8_t)v;
        settings_->save();
        weather_->configure(settings_->get().cwaKey, (uint8_t)v);
        Serial.printf("County set to %s. Fetching weather...\n", WeatherService::countyName((uint8_t)v));
      }
    }
  } else if (strcmp(line, "weather") == 0) {
    const WeatherInfo w = weather_->info();
    Serial.printf("weather: key %s, county #%u", settings_->get().cwaKey[0] ? "set" : "not set",
                  settings_->get().county);
    if (w.valid) {
      Serial.printf(", wx=%u %d~%dC pop=%u%%, %lus ago\n", w.wxCode, w.minT, w.maxT, w.pop,
                    (unsigned long)((millis() - w.updatedAt) / 1000));
    } else {
      Serial.printf(", no data (last http %d)\n", w.lastHttp);
    }
  } else if (strcmp(line, "i2c") == 0) {
    // 掃描板載 I2C (SDA=42, SCL=41) 上的裝置
    Serial.print("i2c devices:");
    uint8_t found = 0;
    for (uint8_t a = 0x08; a < 0x78; a++) {
      Wire.beginTransmission(a);
      if (Wire.endTransmission() == 0) {
        Serial.printf(" 0x%02X", a);
        found++;
      }
    }
    Serial.printf("  (%d found)\n", found);
  } else if (strcmp(line, "sd") == 0) {
    checkSdCard();
  } else if (strcmp(line, "imu") == 0) {
    Serial.printf("gravity g: x=%.2f y=%.2f z=%.2f\n", motion_->gx(), motion_->gy(), motion_->gz());
  } else if (strcmp(line, "status") == 0) {
    printStatus();
  } else if (strcmp(line, "wifi") == 0) {
    Serial.println("SSID:");
    mode_ = Mode::AskSsid;
  } else if (strcmp(line, "wifi clear") == 0) {
    settings_->setWifi("", "");
    wifi_->setCredentials("", "", now);
    Serial.println("Wi-Fi credentials cleared.");
  } else {
    if (!(extra_ && extra_(line))) Serial.println("Unknown command. Type 'help'.");
  }
}

// TF 卡狀態 (開機時已掛載)：卡片類型 / 容量與 /deskpet 底下的資料夾
void SerialConsole::checkSdCard() {
  if (!SdStore::ok()) {
    Serial.println("sd: no card (insert a FAT32/exFAT card and reboot)");
    return;
  }
  const uint8_t type = SdStore::cardType();
  const char *typeName = type == CARD_MMC ? "MMC" : type == CARD_SD ? "SDSC"
                         : type == CARD_SDHC ? "SDHC/SDXC" : "unknown";
  Serial.printf("sd: card %s, used %lu MB / %lu MB\n", typeName, (unsigned long)SdStore::usedMB(),
                (unsigned long)SdStore::totalMB());
  File root = SdStore::fs().open(SdStore::ROOT);
  if (root && root.isDirectory()) {
    for (File f = root.openNextFile(); f; f = root.openNextFile()) {
      uint16_t n = 0;
      if (f.isDirectory()) {
        for (File g = f.openNextFile(); g; g = f.openNextFile()) {
          n++;
          g.close();
        }
      }
      Serial.printf("  %s/%s  %u\n", SdStore::ROOT, f.name(), (unsigned)(f.isDirectory() ? n : f.size()));
      f.close();
    }
    root.close();
  }
}

void SerialConsole::printStatus() {
  tm t;
  if (clock_->now(t)) {
    Serial.printf("time: %04d/%02d/%02d %02d:%02d:%02d (UTC+8)\n", t.tm_year + 1900, t.tm_mon + 1,
                  t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
  } else {
    Serial.println("time: not synced");
  }
  const Settings &s = settings_->get();
  Serial.printf("wifi: %s, ssid=\"%s\"", wifiStateName(wifi_->state()), s.ssid);
  if (wifi_->state() == WifiState::Connected) Serial.printf(", ip=%s", WiFi.localIP().toString().c_str());
  Serial.println();
  Serial.printf("costume: %s | sleep: %s %02d:00-%02d:00 | nap: %d min | brightness: %d%%\n",
                costumes_->get(costumes_->current()).name, s.sleepEnabled ? "on" : "off",
                s.sleepStart, s.sleepEnd, s.napMinutes, s.brightness);
  Serial.printf("battery: %.2fV %u%%%s\n", battery_->voltage(), battery_->percent(),
                battery_->charging() ? " (charging)" : "");
  Serial.printf("cpu: %lu MHz (80 = power save)\n", (unsigned long)getCpuFrequencyMhz());
  Serial.printf("motion sensor: %s\n", motion_->available() ? "QMI8658 ok" : "not found");
  Serial.printf("free heap: %u bytes (min %u, largest block %u), psram free %u\n",
                (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap(),
                (unsigned)ESP.getMaxAllocHeap(), (unsigned)ESP.getFreePsram());
}
