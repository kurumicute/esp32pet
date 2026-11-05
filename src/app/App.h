#pragma once
#include <stddef.h>
#include <time.h>

#include "../audio/SoundManager.h"
#include "../clock/ClockManager.h"
#include "../costume/CostumeManager.h"
#include "../diary/Diary.h"
#include "../display/DisplayManager.h"
#include "../input/ButtonManager.h"
#include "../notify/AncsClient.h"
#include "../pet/Characters.h"
#include "../pet/Pet.h"
#include "../power/BatteryMonitor.h"
#include "../scene/Scene.h"
#include "../sensor/MotionSensor.h"
#include "../settings/SettingsManager.h"
#include "../timer/FocusTimer.h"
#include "../walk/WalkMode.h"
#include "../weather/WeatherService.h"
#include "../web/WebPanel.h"
#include "../wifi/WifiManager.h"
#include "Layout.h"
#include "SerialConsole.h"

class App {
 public:
  void begin();
  void loop();
  uint32_t loopDelayMs() const { return powerSaving_ ? 12 : 5; }

  // ---- 給 WebPanel 使用 ----
  Settings webSettings() { return settings_.get(); }
  void webApply(const Settings &s, bool wifiChanged);
  void webPokePet();
  void webSetWifi(const char *ssid, const char *pass);
  void webUnpairPhone() { ancs_.clearBonds(); }
  void webStartWalk();
  const Diary &diary() const { return diary_; }
  AncsState phoneState() const { return ancs_.state(); }
  const CostumeManager &costumes() const { return costumes_; }
  const BatteryMonitor &battery() const { return battery_; }
  WeatherInfo weather() const { return weather_.info(); }
  bool timeString(char *buf, size_t n) const;
  bool webBackup();   // 設定備份到 SD 卡
  bool webRestore();  // 從 SD 卡還原設定
  void webSdChanged(const char *dir);  // 網頁上傳 / 刪除了 SD 卡檔案：重新載入角色或歌單

 private:
  enum class Screen : uint8_t {
    Main, Costume, Settings, Timer, Info, PowerOff, WifiSetup, Walk, Diary, Menu, Rhythm, Music, Character, Photo
  };
  enum class SleepKind : uint8_t { Awake, Nap, Night };

  void enter(Screen s, uint32_t now);
  void handleButton(const ButtonEvent &e, uint32_t now);
  bool wakeIfSleeping(uint32_t now);  // 有互動時呼叫；正在睡覺就叫醒並回傳 true
  void onShake(uint32_t now);
  void onPatTap(uint32_t now);
  void sfx(Sfx s, int8_t pitch = 0);  // 自動音效 (音效開啟且醒著時)
  // ---- AppFun.cpp：活動選單、鬧鐘、節奏遊戲、音樂盒、寵物叫聲 ----
  void drawMenu();
  void menuButton(const ButtonEvent &e, uint32_t now);
  void updateAlarm(uint32_t now);
  void ringAlarm(uint32_t now);
  void stopAlarm(bool snooze, uint32_t now);
  void drawAlarmPopup();
  void startRhythm(uint32_t now);
  void updateRhythm(uint32_t now);
  void rhythmHit(uint32_t t);
  void drawRhythmTrack(uint32_t now);
  void rhythmButton(const ButtonEvent &e, uint32_t now);
  void drawMusic();
  void updateMusic(uint32_t now);
  void musicButton(const ButtonEvent &e, uint32_t now);
  void startPhoto(uint32_t now);  // 相框 (SD 卡照片)
  void drawPhoto(uint32_t now);
  void updatePhoto(uint32_t now);
  void photoButton(const ButtonEvent &e, uint32_t now);
  void logTick(const tm &t);  // 每 10 分鐘寫一行紀錄到 SD 卡
  void updateCry(uint32_t now);  // 醒著時偶爾叫一聲
  void updateMotion(uint32_t now);
  void updateSleep(uint32_t now);
  void updateCostume(uint32_t now);
  void updateTimer(uint32_t now);
  void updateBattery(uint32_t now);
  void hourlyEvents(const tm &t, uint32_t now);
  void applyBrightness();
  void showMessage(const char *msg, uint32_t now, uint32_t ms = 8000);
  bool petVisible() const;
  int16_t petY() const { return screen_ == Screen::Main ? MAIN_PET_Y : PET_Y; }  // 寵物區在螢幕上的 y

  void updateMain(uint32_t now);
  void drawTime(const tm *t);
  void drawColon(bool on);
  void drawLine(const char *s, uint16_t color, int16_t y);  // 下方文字列 (日期 / 訊息 / 計時)
  void drawTopBar(bool force);
  void drawWeather();
  void drawDate(const char *s);  // 最上排中間的小字日期
  void updateScene(uint32_t now, bool force);  // 背景 (時段 + 天氣) 有變就重畫
  void applyPowerMode();
  void applyWeatherToPet();
  void changeSetting(uint8_t item, bool backward);

  void drawCostumeMenu();
  void drawCharacterMenu();
  void characterButton(const ButtonEvent &e, uint32_t now);
  void applyCharacter(uint8_t c, uint32_t now);  // 換角色並存檔
  void drawSettings();
  void drawTimerScreen(uint32_t now, bool force);
  void drawInfo();
  void drawWifiSetup();
  // 散步模式 / 日記
  void startWalk(uint32_t now);
  void updateWalk(uint32_t now);
  void finishWalk(uint32_t now);
  void pushWalkStrips();
  void drawDiary();
  void patPet(uint32_t now);
  // 共用介面元件
  void drawHeader(const char *title, const char *right);
  void drawHintBar(const char *hint);
  // 底部按鍵提示：left / mid / right 對應實體按鍵 BOOT / PWR / PLUS 的位置 (nullptr = 該格空白)
  void drawKeyHints(const char *left, const char *mid, const char *right, int16_t y);
  void drawToggle(int16_t xRight, int16_t yMid, bool on);
  uint16_t drawCard(int16_t x, int16_t y, int16_t w, int16_t h, bool highlight);
  static uint8_t settingGroup(uint8_t item);
  bool consoleCommand(const char *cmd);
  // 上方彈出卡片 (蓋住時間區)：iPhone 通知 / 藍牙配對碼
  void updateNotify(uint32_t now);
  void showNotification(const PhoneNotification &n, uint32_t now);
  void closePopup();
  bool popupActive() const { return popup_ != Popup::None; }

  DisplayManager display_;
  PixelCanvas canvas_;
  ButtonManager buttons_;
  ClockManager clock_;
  WifiManager wifi_;
  SettingsManager settings_;
  CostumeManager costumes_;
  Pet pet_;
  SerialConsole console_;
  MotionSensor motion_;
  BatteryMonitor battery_;
  FocusTimer timer_;
  WebPanel web_;
  WeatherService weather_;
  AncsClient ancs_;
  Diary diary_;
  SoundManager sound_;
  uint8_t menuItem_ = 0;
  uint8_t menuTop_ = 0;  // 活動選單捲動位置
  // 相框
  uint16_t *photoBuf_ = nullptr;
  uint8_t photoCount_ = 0;
  uint8_t photoIdx_ = 0;
  uint32_t photoShownAt_ = 0;
  int32_t lastLogKey_ = -1;  // 上一次寫紀錄的時間 (避免同一分鐘寫兩次)
  // 鬧鐘
  bool alarmRinging_ = false;
  uint32_t alarmStart_ = 0;
  uint32_t alarmNextRing_ = 0;
  uint32_t snoozeUntil_ = 0;
  uint32_t lastAlarmCheck_ = 0;
  int32_t lastAlarmKey_ = -1;
  // 節奏遊戲
  uint32_t rhythmT0_ = 0;     // 第 0 拍的時間
  uint8_t rhythmNextTick_ = 0;
  uint64_t rhythmJudged_ = 0;  // 每一拍是否已判定 (bit)
  uint8_t rhythmHits_ = 0;
  uint8_t rhythmShownBeat_ = 0xFF;
  bool rhythmDone_ = false;
  uint32_t rhythmLastDraw_ = 0;
  const char *rhythmFeedback_ = nullptr;
  // 音樂盒
  uint8_t musicSong_ = 1;
  bool musicPlaying_ = false;
  uint32_t musicStartedAt_ = 0;
  uint32_t musicLastDance_ = 0;
  // 叫聲 / 腳步聲
  uint32_t nextCry_ = 0;
  uint32_t lastStepSound_ = 0;
  WalkMode walk_;
  uint8_t diaryPage_ = 0;  // 0 = 今天
  uint32_t lastDiaryTick_ = 0;
  const char *walkMsg_ = nullptr;
  uint32_t walkMsgUntil_ = 0;
  enum class Popup : uint8_t { None, Passkey, Notify, Alarm };
  Popup popup_ = Popup::None;
  uint32_t popupUntil_ = 0;
  uint32_t shownPasskey_ = 0;
  AncsState lastAncs_ = AncsState::Off;
  PhoneNotification pendingNote_;
  bool hasPendingNote_ = false;
  uint32_t pendingNoteAt_ = 0;
  Scene scene_;
  bool powerSaving_ = false;
  uint32_t lastSceneCheck_ = 0;
  uint8_t settingTop_ = 0;  // 設定清單捲動位置

  Screen screen_ = Screen::Main;
  SleepKind sleep_ = SleepKind::Awake;
  uint32_t lastInput_ = 0;
  uint32_t screenSince_ = 0;
  uint32_t lastClockCheck_ = 0;
  uint32_t manualWakeUntil_ = 0;
  uint32_t lastMotionCheck_ = 0;

  // 傾斜
  int8_t look_ = 0;

  // 訊息 / 閃爍
  const char *message_ = nullptr;
  uint32_t messageUntil_ = 0;
  uint32_t flashUntil_ = 0;
  bool inverted_ = false;
  int16_t lastEventHour_ = -1;
  int16_t lastBirthdayDay_ = -1;
  bool wasLow_ = false;
  bool wasCharging_ = false;

  uint8_t appliedCostume_ = 0xFF;  // 目前寵物身上的裝扮 (自動換裝用)
  uint32_t pendingWifiAt_ = 0;     // 網頁改 Wi-Fi 後延遲套用 (先讓網頁回應送出)

  // 畫面快取：只在內容變化時重畫該區塊 (局部更新)
  int16_t shownMinute_ = -1;
  int8_t shownColon_ = -1;
  int8_t shownWifi_ = -1;
  int32_t shownTimerSec_ = -1;
  char shownLine_[48] = {0};
  char shownDate_[24] = {0};
  uint16_t shownLineColor_ = 0;

  uint8_t preview_ = 0;      // 裝扮選單目前預覽的裝扮
  uint8_t previewChar_ = 0;  // 角色選單目前預覽的角色
  uint8_t settingItem_ = 0;  // 設定選單目前選取的項目
};
