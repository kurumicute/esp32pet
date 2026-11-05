#include "App.h"

#include <Arduino.h>
#include <WiFi.h>

#include "../assets/Sprites.h"
#include "../config/BoardConfig.h"
#include "../display/Theme.h"
#include "../ui/Strings.h"
#include "Layout.h"
#include "../storage/SdArchive.h"
#include "../storage/SdStore.h"
#include "../storage/SdUpdate.h"

namespace {
constexpr int16_t WIFI_X = 222, WIFI_Y = 4;
constexpr int16_t BAT_X = 194, BAT_Y = 4;

constexpr uint32_t CLOCK_CHECK_MS = 100;
constexpr uint32_t MOTION_CHECK_MS = 100;
constexpr uint32_t MENU_TIMEOUT_MS = 30000;
constexpr uint32_t MANUAL_WAKE_MS = 60000;
constexpr uint32_t FLASH_MS = 3000;
constexpr uint32_t FLASH_STEP_MS = 250;
constexpr uint8_t NAP_BRIGHTNESS_MIN = 8;  // 閒置小睡亮度下限 (設定亮度的 1/3)
constexpr float TILT_ON_G = 0.35f;         // 約 20 度
constexpr float TILT_OFF_G = 0.20f;

constexpr uint8_t SETTING_ITEMS = 27;
constexpr uint32_t TAP_BLOCK_MS = 400;  // 按鍵前後這段時間不算拍頭
constexpr uint32_t WALK_MAX_SEC = 30 * 60;  // 散步最久 30 分鐘就自己回家
constexpr uint32_t WALK_MSG_MS = 3000;
constexpr uint32_t NOTIFY_SHOW_MS = 8000;
constexpr uint8_t SETTING_ROWS = 6;  // 一頁顯示幾項 (超過就捲動)
const uint8_t NAP_OPTIONS[] = {0, 1, 3, 5, 10, 30};  // 分鐘，0 = 關閉
const uint8_t FOCUS_OPTIONS[] = {25, 30, 45, 50, 60, 5, 10, 15, 20};  // 番茄鐘專注分鐘
const uint8_t BREAK_OPTIONS[] = {5, 10, 15, 3};
const uint8_t DAYS_IN_MONTH[] = {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

// 在選項清單中往前 / 往後切換 (目前值不在清單裡就跳到第一個)
uint8_t cycle(const uint8_t *opts, uint8_t n, uint8_t cur, bool backward) {
  uint8_t i = 0;
  while (i < n && opts[i] != cur) i++;
  if (i >= n) return opts[0];
  return opts[backward ? (i + n - 1) % n : (i + 1) % n];
}
uint8_t step(uint8_t v, uint8_t lo, uint8_t hi, bool backward) {  // lo~hi 循環
  if (backward) return v <= lo ? hi : v - 1;
  return v >= hi ? lo : v + 1;
}

bool inSleepWindow(uint8_t hour, uint8_t start, uint8_t end) {
  if (start == end) return false;
  if (start < end) return hour >= start && hour < end;
  return hour >= start || hour < end;  // 跨午夜，例如 23 → 7
}

void fmtMMSS(char *buf, size_t n, uint32_t sec) {
  snprintf(buf, n, "%02lu:%02lu", (unsigned long)(sec / 60 % 100), (unsigned long)(sec % 60));
}
}  // namespace

void App::begin() {
  // 電源鎖定：電池供電時必須先拉 HIGH，否則放開 PWR 鍵就會斷電
  pinMode(Pins::POWER_LATCH, OUTPUT);
  digitalWrite(Pins::POWER_LATCH, HIGH);
  // 音效預設關閉：開機先關功放；只有在設定裡開啟音效時才初始化音訊晶片 (不使用麥克風)
  pinMode(Pins::AMP_CTRL, OUTPUT);
  digitalWrite(Pins::AMP_CTRL, LOW);

  Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT
  Serial.setTxTimeoutMs(0);  // 沒接電腦時印訊息不阻塞
#endif

  const uint32_t now = millis();
  settings_.load();
  const Settings &s = settings_.get();
  costumes_.setCurrent(s.costume);
  timer_.configure(s.focusMin, s.breakMin);
  diary_.begin();

  if (!display_.begin()) Serial.println("[DeskPet] LCD init failed");
  // SD 卡：韌體更新 (有的話會直接重開機)、SD 角色、音效 / 歌曲、日記封存
  if (SdStore::begin()) {
    SdUpdate::checkAndApply(display_);
    Characters::loadFromSd();
    sound_.loadSd(Characters::count(), Characters::folder);
    diary_.setArchive(SdArchive::saveDiaryDay);
  }
  if (settings_.get().character >= Characters::count()) settings_.get().character = 0;  // SD 角色不見了
  if (!canvas_.begin() || !scene_.begin() || !walk_.begin()) Serial.println("[DeskPet] out of memory for buffers");
  buttons_.begin(now);
  motion_.begin();
  motion_.setLevel(s.shakeLevel);
  motion_.setPatLevel(s.patLevel);
  sound_.setVolume(s.soundVol);
  sound_.setEnabled(s.soundOn);  // 關閉時不碰音訊硬體
  battery_.begin();
  pet_.begin(costumes_, costumes_.current(), now);
  pet_.setCharacter(s.character, now);
  pet_.setScene(scene_.pixels(), Scene::W, Scene::H, PET_X / PixelCanvas::SCALE, 0);
  appliedCostume_ = costumes_.current();
  wifi_.begin(s.ssid, s.pass, now);
  weather_.configure(s.cwaKey, s.county);
  weather_.begin();
  console_.begin(settings_, wifi_, clock_, costumes_, motion_, battery_, weather_);
  applyPowerMode();
  updateScene(now, true);
  if (s.notifyOn) ancs_.begin("DeskPet");
  static App *self = this;
  console_.setBleClear([]() { self->ancs_.clearBonds(); });
  console_.setExtra([](const char *cmd) { return self->consoleCommand(cmd); });

  enter(Screen::Main, now);
  applyBrightness();
}

void App::loop() {
  const uint32_t now = millis();
  sound_.setVoice(Characters::synthVoice(pet_.character()), pet_.character());  // 叫聲跟著目前畫面上的角色

  buttons_.update(now);
  ButtonEvent e;
  while (buttons_.poll(e)) handleButton(e, now);
  if (buttons_.busy(now, TAP_BLOCK_MS)) motion_.blockTaps(now + TAP_BLOCK_MS);
  if (motion_.update(now)) onShake(now);
  if (motion_.popPat()) onPatTap(now);
  if (screen_ != Screen::Rhythm) {  // 節奏遊戲以外不需要「按下的瞬間」與單次輕拍
    Btn pb;
    uint32_t pt;
    while (buttons_.popPress(pb, pt)) {}
    while (motion_.popTap(pt)) {}
  }
  updateMotion(now);
  updateAlarm(now);
  updateCry(now);

  wifi_.update(now);
  if (wifi_.apJustStarted()) {  // 連不上 Wi-Fi：開設定熱點 + 自動跳出設定頁
    web_.begin(*this);
    web_.setCaptive(true);
    enter(Screen::WifiSetup, now);
  }
  if (!wifi_.apActive()) web_.setCaptive(false);
  if (wifi_.justConnected()) {
    clock_.startSync();
    web_.begin(*this);
    if (screen_ == Screen::WifiSetup) {
      enter(Screen::Main, now);
      showMessage(Str::WIFI_CONNECTED, now, 6000);
    }
  }
  web_.update();
  console_.update(now);
  if (pendingWifiAt_ != 0 && (int32_t)(now - pendingWifiAt_) >= 0) {
    pendingWifiAt_ = 0;
    wifi_.setCredentials(settings_.get().ssid, settings_.get().pass, now);
  }

  // 選單閒置太久自動回主畫面
  if ((screen_ == Screen::Costume || screen_ == Screen::Settings || screen_ == Screen::Info ||
       screen_ == Screen::Diary || screen_ == Screen::Menu || screen_ == Screen::Character) &&
      (int32_t)(now - lastInput_) > (int32_t)MENU_TIMEOUT_MS) {
    if (screen_ == Screen::Settings) settings_.save();
    if (screen_ == Screen::Character) pet_.changeCharacter(settings_.get().character, now);  // 取消預覽
    appliedCostume_ = 0xFF;  // 取消預覽，重新套用該穿的裝扮
    enter(Screen::Main, now);
  }

  if (now - lastDiaryTick_ >= 1000) {  // 日記：換日、天氣、裝扮、定期存檔
    lastDiaryTick_ = now;
    tm t;
    const bool synced = clock_.now(t);
    const WeatherInfo w = weather_.info();
    if (w.valid) diary_.setWeather((uint8_t)w.kind, w.minT, w.maxT);
    diary_.setCostume(pet_.costume());
    diary_.update(now, synced ? &t : nullptr);
    if (synced) logTick(t);
  }

  updateBattery(now);
  updateNotify(now);
  if (weather_.changed()) {
    applyWeatherToPet();
    updateScene(now, true);
    if (screen_ == Screen::Main && !popupActive()) drawWeather();
  }
  updateScene(now, false);
  updateTimer(now);
  updateSleep(now);
  updateCostume(now);

  switch (screen_) {
    case Screen::Main: updateMain(now); break;
    case Screen::Timer: drawTimerScreen(now, false); break;
    case Screen::Walk: updateWalk(now); break;
    case Screen::Rhythm: updateRhythm(now); break;
    case Screen::Music: updateMusic(now); break;
    case Screen::Photo: updatePhoto(now); break;
    case Screen::PowerOff:
      // 用 USB 供電時拉低電源鎖定不會斷電，2 秒後回主畫面
      if (now - screenSince_ > 2000) {
        digitalWrite(Pins::POWER_LATCH, HIGH);
        enter(Screen::Main, now);
      }
      break;
    default: break;
  }

  // 計時結束的螢幕閃爍 (無聲提醒)
  if (flashUntil_ != 0) {
    const bool on = (int32_t)(flashUntil_ - now) > 0 && ((flashUntil_ - now) / FLASH_STEP_MS) % 2 == 0;
    if (on != inverted_) {
      inverted_ = on;
      display_.invert(on);
    }
    if ((int32_t)(now - flashUntil_) >= 0) flashUntil_ = 0;
  }

  // 寵物畫面：只在影格或特效改變時重畫
  if (petVisible() && pet_.update(now)) {
    pet_.render(canvas_, Theme::BG, now);
    display_.pushCanvas(canvas_, PET_X, petY());
  }
}

bool App::petVisible() const {
  return screen_ == Screen::Main || screen_ == Screen::Costume || screen_ == Screen::Timer ||
         screen_ == Screen::Walk || screen_ == Screen::Rhythm || screen_ == Screen::Music ||
         screen_ == Screen::Character;
}

// ------------------------------------------------------------
void App::enter(Screen s, uint32_t now) {
  if (screen_ == Screen::Walk && s != Screen::Walk) finishWalk(now);  // 不管從哪裡離開散步都要結算
  if (screen_ == Screen::Rhythm && s != Screen::Rhythm) {  // 離開節奏遊戲：停止連續輸出
    sound_.setStreaming(false);
    sound_.stop();
  }
  if (screen_ == Screen::Music && s != Screen::Music && musicPlaying_) {
    musicPlaying_ = false;
    sound_.stop();
  }
  screen_ = s;
  screenSince_ = now;
  popup_ = Popup::None;
  lastInput_ = now;
  display_.clear(Theme::BG);
  shownLine_[0] = '\0';
  shownMinute_ = -1;
  shownDate_[0] = '\0';
  shownColon_ = shownWifi_ = -1;
  shownTimerSec_ = -1;
  pet_.invalidate();
  if (s == Screen::Main || s == Screen::Costume || s == Screen::Timer || s == Screen::Rhythm ||
      s == Screen::Music || s == Screen::Character) {  // 散步畫面另外畫
    // 草地背景橫跨整個螢幕寬，只在進入畫面時畫一次；寵物區之後只更新中間那塊
    display_.drawRgbScaled(scene_.pixels(), Scene::W, Scene::H, 0, petY(), PixelCanvas::SCALE);
  }

  switch (s) {
    case Screen::Main:
      lastClockCheck_ = 0;
      drawTopBar(true);
      drawWeather();
      updateMain(now);
      break;
    case Screen::Costume:
      preview_ = costumes_.current();
      pet_.changeCostume(preview_, now);
      drawCostumeMenu();
      break;
    case Screen::Settings:
      settingItem_ = 0;
      drawSettings();
      break;
    case Screen::Timer:
      drawTimerScreen(now, true);
      break;
    case Screen::Info:
      drawInfo();
      break;
    case Screen::WifiSetup:
      drawWifiSetup();
      break;
    case Screen::Walk:
      startWalk(now);
      break;
    case Screen::Diary:
      diaryPage_ = 0;
      drawDiary();
      break;
    case Screen::Menu:
      drawMenu();
      break;
    case Screen::Rhythm:
      startRhythm(now);
      break;
    case Screen::Music:
      drawMusic();
      break;
    case Screen::Character:
      previewChar_ = settings_.get().character < Characters::count() ? settings_.get().character : 0;
      drawCharacterMenu();
      break;
    case Screen::Photo:
      startPhoto(now);
      break;
    case Screen::PowerOff:
      diary_.flush();
      display_.utf8Centered(100, Str::BYE, 2, Theme::TIME, Theme::BG);
      digitalWrite(Pins::POWER_LATCH, LOW);  // 電池供電時在此斷電
      break;
  }
}

bool App::wakeIfSleeping(uint32_t now) {
  lastInput_ = now;  // 重新計算閒置時間
  if (sleep_ == SleepKind::Awake) return false;
  if (sleep_ == SleepKind::Night) manualWakeUntil_ = now + MANUAL_WAKE_MS;
  return true;
}

void App::onShake(uint32_t now) {
  if (wakeIfSleeping(now)) {
    Serial.println("[DeskPet] shake -> wake up");
    return;
  }
  if (petVisible()) {  // 醒著時搖一搖 = 開心
    pet_.play(AnimId::Happy, now);
    if (random(100) < 50) sfx(Sfx::CryHappy, (int8_t)random(-6, 10));
    else sfx(Sfx::Happy);
    diary_.add(DiaryStat::Shake);
  }
}

// 拍機器上方兩下 = 摸頭 (睡覺時 = 叫醒)；只在看得到寵物的畫面有效
void App::sfx(Sfx s, int8_t pitch) {
  if (settings_.get().soundOn && sleep_ == SleepKind::Awake) sound_.play(s, false, pitch);
}

void App::onPatTap(uint32_t now) {
  if (alarmRinging_) {  // 鬧鐘響的時候拍兩下 = 再睡 5 分鐘
    stopAlarm(true, now);
    return;
  }
  if (screen_ == Screen::Rhythm) return;  // 遊戲中的輕拍另外判定
  if (wakeIfSleeping(now)) {
    Serial.println("[DeskPet] pat -> wake up");
    return;
  }
  if (petVisible()) patPet(now);
}

void App::handleButton(const ButtonEvent &e, uint32_t now) {
  if (alarmRinging_) {  // 鬧鐘：中鍵 = 關閉，左 / 右鍵 = 再睡 5 分鐘
    stopAlarm(e.btn != Btn::Pwr, now);
    return;
  }
  // 睡眠中：第一下按鍵只負責叫醒 (不做其他動作)
  if (wakeIfSleeping(now)) return;

  switch (screen_) {
    case Screen::Main:
      if (e.btn == Btn::Plus) {
        enter(e.type == Press::Short ? Screen::Costume : Screen::Menu, now);
      } else if (e.btn == Btn::Boot) {
        if (e.type == Press::Long) enter(Screen::Settings, now);
        else patPet(now);  // 摸頭
      } else {  // PWR
        if (e.type == Press::Long) enter(Screen::PowerOff, now);
        else enter(Screen::Timer, now);
      }
      break;

    case Screen::Costume: {
      const uint8_t n = costumes_.count();
      if (e.type == Press::Short && (e.btn == Btn::Plus || e.btn == Btn::Boot)) {
        preview_ = e.btn == Btn::Plus ? (preview_ + 1) % n : (preview_ + n - 1) % n;
        pet_.changeCostume(preview_, now);
        drawCostumeMenu();
      } else if (e.btn == Btn::Pwr && e.type == Press::Short) {  // 確認
        costumes_.setCurrent(preview_);
        settings_.get().costume = preview_;
        settings_.save();
        appliedCostume_ = preview_;  // 已經穿在身上
        diary_.add(DiaryStat::Dress);
        sfx(Sfx::Dress);
        enter(Screen::Main, now);
        pet_.play(AnimId::Happy, now);
      } else if (e.type == Press::Long) {  // 取消
        appliedCostume_ = 0xFF;
        enter(Screen::Main, now);
      }
      break;
    }

    case Screen::Settings:
      // PLUS 短 = 下一項，PWR 短 = 上一項，BOOT 短 / 長 = 數值往後 / 往前，PLUS 或 PWR 長按 = 儲存返回
      if (e.btn == Btn::Boot) {
        changeSetting(settingItem_, e.type == Press::Long);
        drawSettings();
      } else if (e.type == Press::Long) {
        settings_.save();
        enter(Screen::Main, now);
      } else {
        settingItem_ = e.btn == Btn::Plus ? (settingItem_ + 1) % SETTING_ITEMS
                                          : (settingItem_ + SETTING_ITEMS - 1) % SETTING_ITEMS;
        drawSettings();
      }
      break;

    case Screen::Timer:
      if (e.type == Press::Long) {  // 長按：停止並返回
        timer_.stop();
        enter(Screen::Main, now);
      } else if (e.btn == Btn::Plus) {
        timer_.toggle(now);
        drawTimerScreen(now, true);
      } else if (e.btn == Btn::Boot) {
        if (!timer_.active()) {  // 切換專注時間 (計時中不能改)
          Settings &s = settings_.get();
          s.focusMin = cycle(FOCUS_OPTIONS, sizeof(FOCUS_OPTIONS), s.focusMin, false);
          settings_.save();
          timer_.configure(s.focusMin, s.breakMin);
        } else if (timer_.paused()) {
          timer_.stop();  // 暫停中按 BOOT = 重設
        }
        drawTimerScreen(now, true);
      } else {  // PWR：返回 (計時會在背景繼續)
        enter(Screen::Main, now);
      }
      break;

    case Screen::Info:  // 任一鍵返回
      enter(Screen::Main, now);
      break;

    case Screen::Menu: menuButton(e, now); break;
    case Screen::Character: characterButton(e, now); break;
    case Screen::Photo: photoButton(e, now); break;
    case Screen::Rhythm: rhythmButton(e, now); break;
    case Screen::Music: musicButton(e, now); break;

    case Screen::WifiSetup:
      enter(Screen::Main, now);
      break;

    case Screen::Walk:  // BOOT = 摸摸，PWR 或長按 = 回家
      if (e.type == Press::Short && e.btn != Btn::Pwr) {
        patPet(now);
      } else {
        enter(Screen::Main, now);
      }
      break;

    case Screen::Diary: {  // 左 BOOT = 前一天，右 PLUS = 後一天，PWR 或長按 = 返回
      const uint8_t n = diary_.count();
      if (e.type == Press::Short && e.btn == Btn::Boot) {
        if (diaryPage_ + 1 < n) {
          diaryPage_++;
          drawDiary();
        }
      } else if (e.type == Press::Short && e.btn == Btn::Plus) {
        if (diaryPage_ > 0) {
          diaryPage_--;
          drawDiary();
        }
      } else {
        enter(Screen::Main, now);
      }
      break;
    }

    case Screen::PowerOff:
      break;
  }
}

// ------------------------------------------------------------
// 感測：傾斜看左右
void App::updateMotion(uint32_t now) {
  if (!motion_.available() || now - lastMotionCheck_ < MOTION_CHECK_MS) return;
  lastMotionCheck_ = now;

  if (!settings_.get().tiltOn) {
    if (look_ != 0) {
      look_ = 0;
      pet_.setLook(0, now);
    }
    return;
  }
  const float h = TILT_SIGN * (TILT_AXIS == 0 ? motion_.gx() : motion_.gy());
  int8_t dir = look_;
  if (look_ == 0) {
    if (h > TILT_ON_G) dir = 1;
    else if (h < -TILT_ON_G) dir = -1;
  } else if (h * look_ < TILT_OFF_G) {
    dir = 0;
  }
  if (dir != look_) {
    look_ = dir;
    if (dir != 0) lastInput_ = now;  // 歪一下也算互動，延後小睡
    pet_.setLook(dir, now);
  }
}

void App::updateSleep(uint32_t now) {
  const Settings &s = settings_.get();
  SleepKind want = SleepKind::Awake;
  tm t;
  const bool synced = clock_.now(t);
  if (screen_ == Screen::Main && !timer_.running()) {
    if (s.sleepEnabled && synced && inSleepWindow((uint8_t)t.tm_hour, s.sleepStart, s.sleepEnd) &&
        (int32_t)(now - manualWakeUntil_) >= 0) {
      want = SleepKind::Night;
    } else if (s.napMinutes > 0 &&
               (int32_t)(now - lastInput_) >= (int32_t)(s.napMinutes * 60000UL)) {  // 有號比較：網頁/序列埠操作的時間可能比 now 晚
      want = SleepKind::Nap;  // 閒置太久：小睡
    }
  }
  if (want == sleep_) return;
  if (want == SleepKind::Nap) diary_.add(DiaryStat::Nap);
  const bool woke = want == SleepKind::Awake;
  sleep_ = want;
  if (woke) sfx(Sfx::CrySleepy, (int8_t)random(-5, 6));  // 睡醒：打哈欠的叫聲
  pet_.setAsleep(want != SleepKind::Awake, now);
  applyBrightness();
}

void App::applyBrightness() {
  const uint8_t b = settings_.get().brightness;
  switch (sleep_) {
    case SleepKind::Night: display_.setBrightness(SLEEP_BRIGHTNESS); break;
    case SleepKind::Nap: display_.setBrightness(max<uint8_t>(NAP_BRIGHTNESS_MIN, b / 3)); break;
    default: display_.setBrightness(b); break;
  }
}

// 自動裝扮：生日 → 皇冠，12/1~12/25 → 聖誕帽，夜間睡眠 → 睡眠帽，其他 → 使用者選的
void App::updateCostume(uint32_t now) {
  if (screen_ == Screen::Costume) return;  // 選單中由預覽控制
  const Settings &s = settings_.get();
  uint8_t want = costumes_.current();
  tm t;
  if (s.autoCostume && clock_.now(t)) {
    const int mon = t.tm_mon + 1, day = t.tm_mday;
    if (s.birthMonth != 0 && mon == s.birthMonth && day == s.birthDay) {
      want = (uint8_t)CostumeId::Crown;
    } else if (mon == 12 && day <= 25) {
      want = (uint8_t)CostumeId::SantaHat;
    } else if (sleep_ == SleepKind::Night) {
      want = (uint8_t)CostumeId::Nightcap;
    }
  }
  if (want != appliedCostume_) {
    appliedCostume_ = want;
    pet_.changeCostume(want, now);
  }
}

void App::updateTimer(uint32_t now) {
  const FocusTimer::Event ev = timer_.update(now);
  if (ev == FocusTimer::Event::None) return;
  if (ev == FocusTimer::Event::FocusDone) diary_.add(DiaryStat::Focus);
  showMessage(ev == FocusTimer::Event::FocusDone ? Str::MSG_FOCUS_DONE : Str::MSG_BREAK_DONE, now, 10000);
  flashUntil_ = now + FLASH_MS;
  wakeIfSleeping(now);
  sfx(Sfx::TimerDone);
  pet_.play(AnimId::Happy, now);
}

void App::updateBattery(uint32_t now) {
  battery_.update(now);
  if (!battery_.changed()) return;
  pet_.setSweat(battery_.low());
  if (battery_.low() && !wasLow_) showMessage(Str::MSG_LOW_BATTERY, now);
  if (battery_.charging() && !wasCharging_) showMessage(Str::MSG_CHARGING, now, 4000);
  wasLow_ = battery_.low();
  wasCharging_ = battery_.charging();
  applyPowerMode();  // 自動省電：插拔充電時切換
  if (screen_ == Screen::Main && !popupActive()) drawTopBar(true);
}

void App::showMessage(const char *msg, uint32_t now, uint32_t ms) {
  message_ = msg;
  messageUntil_ = now + ms;
}

// 時段小動作 (整點觸發一次；小睡 / 夜間睡眠中不打擾)
void App::hourlyEvents(const tm &t, uint32_t now) {
  const Settings &s = settings_.get();
  if (s.birthMonth != 0 && t.tm_mon + 1 == s.birthMonth && t.tm_mday == s.birthDay &&
      lastBirthdayDay_ != t.tm_yday && sleep_ == SleepKind::Awake) {
    lastBirthdayDay_ = t.tm_yday;
    showMessage(Str::MSG_BIRTHDAY, now, 15000);
    if (s.soundOn) sound_.playSong(0);  // 生日：播生日快樂歌 (音效開啟時)
    pet_.play(AnimId::Happy, now);
  }
  if (t.tm_min != 0 || lastEventHour_ == t.tm_hour) return;
  lastEventHour_ = t.tm_hour;
  if (!s.hourlyFx) return;
  if (sleep_ != SleepKind::Awake && t.tm_hour != s.sleepEnd) return;

  switch (t.tm_hour) {
    case 7:
      showMessage(Str::MSG_MORNING, now);
      pet_.play(AnimId::Wake, now);
      sfx(Sfx::Cry);
      break;
    case 12:
    case 15:
      showMessage(t.tm_hour == 12 ? Str::MSG_LUNCH : Str::MSG_SNACK, now);
      pet_.showProp(&UI_snack, 15000, now);
      pet_.play(AnimId::Happy, now);
      break;
    case 18:
      showMessage(Str::MSG_EVENING, now);
      pet_.play(AnimId::Happy, now);
      break;
    case 22:
      showMessage(Str::MSG_NIGHT, now);
      pet_.play(AnimId::Wake, now);  // 打個哈欠
      break;
    default:
      showMessage(Str::MSG_HOUR, now, 4000);
      pet_.play(AnimId::Happy, now);
      sfx(Sfx::Hour);
      break;
  }
}

// ------------------------------------------------------------
// 主畫面
void App::updateMain(uint32_t now) {
  if (lastClockCheck_ != 0 && now - lastClockCheck_ < CLOCK_CHECK_MS) return;
  lastClockCheck_ = now ? now : 1;

  tm t;
  const bool synced = clock_.now(t);
  if (popupActive()) {  // 彈出卡片蓋住時間區：時間晚點再補畫
    shownMinute_ = -1;
    shownDate_[0] = '\0';
    shownColon_ = -1;
  } else if (synced) {
    const int16_t minuteKey = t.tm_hour * 60 + t.tm_min;
    if (minuteKey != shownMinute_) {
      shownMinute_ = minuteKey;
      drawTime(&t);
      shownColon_ = 1;
      hourlyEvents(t, now);
    }
    const int8_t colon = (t.tm_sec % 2 == 0) ? 1 : 0;  // 冒號每秒閃爍
    if (colon != shownColon_) {
      shownColon_ = colon;
      drawColon(colon);
    }
  } else if (shownMinute_ != -2) {
    shownMinute_ = -2;
    drawTime(nullptr);
  }
  if (popupActive()) return;

  // 下方文字列：訊息 > 番茄鐘 > 日期 > 同步狀態
  char line[48];
  uint16_t color = Theme::TEXT;
  if (message_ != nullptr && (int32_t)(messageUntil_ - now) > 0) {
    snprintf(line, sizeof(line), "%s", message_);
    color = Theme::ACCENT;
  } else if (timer_.active()) {
    message_ = nullptr;
    char mmss[8];
    fmtMMSS(mmss, sizeof(mmss), timer_.remainingSec(now));
    const char *label = timer_.paused() ? Str::TIMER_PAUSED
                        : timer_.phase() == FocusTimer::Phase::Focus ? Str::TIMER_FOCUS
                                                                     : Str::TIMER_BREAK;
    snprintf(line, sizeof(line), "%s %s", label, mmss);
    color = Theme::TIME;
  } else if (synced) {
    message_ = nullptr;
    line[0] = '\0';  // 平常不顯示 (日期在最上排)
  } else {
    message_ = nullptr;
    snprintf(line, sizeof(line), "%s",
             wifi_.apActive() ? Str::AP_MODE
             : wifi_.state() == WifiState::NoCredentials ? Str::NEED_WIFI : Str::SYNCING);
    color = Theme::DIM;
  }
  drawLine(line, color, MAIN_LINE_Y);
  if (synced) {
    char date[24];
    snprintf(date, sizeof(date), "%02d/%02d %s", t.tm_mon + 1, t.tm_mday, Str::WEEKDAY[t.tm_wday % 7]);
    drawDate(date);
  }

  const int8_t w = wifi_.state() == WifiState::Connected ? 1 : 0;
  if (w != shownWifi_) {
    shownWifi_ = w;
    drawTopBar(false);
  }
}

void App::drawTime(const tm *t) {
  char buf[6];
  if (t) {
    snprintf(buf, sizeof(buf), "%02d:%02d", t->tm_hour, t->tm_min);
  } else {
    strcpy(buf, "--:--");
  }
  display_.text(TIME_X, TIME_Y, buf, TIME_SIZE, t ? Theme::TIME : Theme::DIM, Theme::BG);
}

void App::drawColon(bool on) {
  const char s[2] = {on ? ':' : ' ', '\0'};
  display_.text(TIME_X + 2 * TIME_CHAR_W, TIME_Y, s, TIME_SIZE, Theme::TIME, Theme::BG);
}

void App::drawLine(const char *s, uint16_t color, int16_t y) {
  if (color == shownLineColor_ && strcmp(s, shownLine_) == 0) return;
  strncpy(shownLine_, s, sizeof(shownLine_) - 1);
  shownLine_[sizeof(shownLine_) - 1] = '\0';
  shownLineColor_ = color;
  display_.utf8Centered(y, s, 2, color, Theme::BG);
}

// 右上角：電池 + Wi-Fi
void App::drawTopBar(bool force) {
  (void)force;
  Arduino_GFX &g = display_.gfx();
  // 電池外框 20x10 + 正極
  const uint8_t p = battery_.percent();
  const uint16_t fill = battery_.charging() ? Theme::ACCENT
                        : p <= 20           ? rgb565(236, 96, 110)
                        : p <= 50           ? rgb565(255, 214, 120)
                                            : rgb565(140, 230, 170);
  g.fillRect(BAT_X - 1, BAT_Y - 1, 26, 12, Theme::BG);
  g.drawRect(BAT_X, BAT_Y, 20, 10, Theme::TEXT);
  g.fillRect(BAT_X + 20, BAT_Y + 3, 2, 4, Theme::TEXT);
  const int16_t fw = (int16_t)((uint16_t)16 * p / 100);
  if (fw > 0) g.fillRect(BAT_X + 2, BAT_Y + 2, fw, 6, fill);
  if (battery_.charging()) display_.drawSprite(UI_bolt, BAT_X + 6, BAT_Y, 1, Theme::BG);

  const Sprite &w = wifi_.state() == WifiState::Connected ? UI_wifi_on : UI_wifi_off;
  display_.drawSprite(w, WIFI_X, WIFI_Y, 1, Theme::BG);
}

// 左上角：天氣圖示 + 溫度
void App::drawDate(const char *s) {
  if (strcmp(s, shownDate_) == 0) return;
  strncpy(shownDate_, s, sizeof(shownDate_) - 1);
  shownDate_[sizeof(shownDate_) - 1] = '\0';
  constexpr int16_t L = 88, R = 190;  // 天氣 (左) 與電池 (右) 之間
  display_.gfx().fillRect(L, 0, R - L, 16, Theme::BG);
  const int16_t w = DisplayManager::utf8Width(s, 1);
  display_.utf8((L + R) / 2 - w / 2, 2, s, 1, Theme::DIM_TEXT, Theme::BG);
}

void App::drawWeather() {
  display_.gfx().fillRect(0, 0, 86, 16, Theme::BG);
  const WeatherInfo w = weather_.info();
  if (!w.valid) return;
  const Sprite *icons[] = {&UI_weather_cloud, &UI_weather_sun,     &UI_weather_partly,
                           &UI_weather_cloud, &UI_weather_rain,    &UI_weather_thunder,
                           &UI_weather_snow,  &UI_weather_fog};
  display_.drawSprite(*icons[(uint8_t)w.kind < 8 ? (uint8_t)w.kind : 0], 4, 2, 1, Theme::BG);
  char buf[24];
  snprintf(buf, sizeof(buf), "%d~%d%s", w.minT, w.maxT, Str::DEG_C);
  display_.utf8(22, 2, buf, 1, Theme::TEXT, Theme::BG);
}

// ------------------------------------------------------------
// 共用介面元件 (選單畫面一致的外觀)
void App::drawHeader(const char *title, const char *right) {
  Arduino_GFX &g = display_.gfx();
  g.fillRect(0, 0, 240, HEADER_H, Theme::HEADER);
  g.fillRect(0, HEADER_H, 240, 2, Theme::ACCENT_DK);  // 標題列下緣的細線
  display_.utf8(12, 3, title, 2, Theme::ACCENT, Theme::HEADER);
  if (right) display_.utf8Right(230, 10, right, 1, Theme::DIM, Theme::HEADER);
}

void App::drawKeyHints(const char *left, const char *mid, const char *right, int16_t y) {
  Arduino_GFX &g = display_.gfx();
  g.fillRect(0, y, 240, 240 - y, Theme::HEADER);
  g.fillRect(80, y + 3, 1, 240 - y - 6, Theme::CARD);  // 三格之間的分隔線
  g.fillRect(160, y + 3, 1, 240 - y - 6, Theme::CARD);
  const char *labels[] = {left, mid, right};
  for (uint8_t i = 0; i < 3; i++) {
    if (labels[i] == nullptr || labels[i][0] == '\0') continue;
    const int16_t cx = 40 + i * 80;
    const int16_t w = DisplayManager::utf8Width(labels[i], 1);
    display_.utf8(cx - w / 2, y + (240 - y - 13) / 2, labels[i], 1, Theme::TEXT, Theme::HEADER);
  }
}

void App::drawHintBar(const char *hint) {
  display_.gfx().fillRect(0, HINTBAR_Y, 240, 240 - HINTBAR_Y, Theme::HEADER);
  display_.utf8Centered(HINTBAR_Y + 2, hint, 1, Theme::DIM, Theme::HEADER);
}

// iOS 風格的開關：on = 薄荷綠 + 圓鈕在右
void App::drawToggle(int16_t xRight, int16_t yMid, bool on) {
  Arduino_GFX &g = display_.gfx();
  constexpr int16_t w = 32, h = 18, r = h / 2;
  const int16_t x = xRight - w, y = yMid - r;
  g.fillRoundRect(x, y, w, h, r, on ? Theme::ACCENT : Theme::TOGGLE_OFF);
  g.fillCircle(on ? x + w - r - 1 : x + r, yMid, r - 3, Theme::TEXT);
}

// 一張卡片 (圓角底色)，回傳卡片底色方便在上面寫字
uint16_t App::drawCard(int16_t x, int16_t y, int16_t w, int16_t h, bool highlight) {
  const uint16_t c = highlight ? Theme::CARD_SEL : Theme::CARD;
  display_.gfx().fillRoundRect(x, y, w, h, 8, c);
  return c;
}

// ------------------------------------------------------------
// 裝扮選單
void App::drawCostumeMenu() {
  char buf[48];
  snprintf(buf, sizeof(buf), "%d / %d", preview_ + 1, costumes_.count());
  drawHeader(Str::COSTUME_TITLE, buf);
  // 名稱膠囊 (寵物區正下方)
  const uint16_t c = drawCard(24, NAME_Y, 192, 26, true);
  snprintf(buf, sizeof(buf), "‹ %s ›", costumes_.get(preview_).name);
  display_.utf8Centered(NAME_Y, buf, 2, Theme::TIME, c, 184);
  drawKeyHints(Str::KEY_PREV, Str::KEY_OK, Str::KEY_NEXT, HINTBAR_Y);
}

// ------------------------------------------------------------
// 角色選單：左 / 右 = 上一個 / 下一個 (即時預覽)，中 = 確定，長按 = 取消
void App::drawCharacterMenu() {
  char buf[48];
  snprintf(buf, sizeof(buf), "%d / %d", previewChar_ + 1, Characters::count());
  drawHeader(Str::CHARACTER_TITLE, buf);
  const uint16_t c = drawCard(24, NAME_Y, 192, 26, true);
  snprintf(buf, sizeof(buf), "‹ %s ›", Characters::name(previewChar_));
  display_.utf8Centered(NAME_Y, buf, 2, Theme::TIME, c, 184);
  drawKeyHints(Str::KEY_PREV, Str::KEY_OK, Str::KEY_NEXT, HINTBAR_Y);
}

void App::characterButton(const ButtonEvent &e, uint32_t now) {
  const uint8_t n = Characters::count();
  if (e.type == Press::Short && (e.btn == Btn::Plus || e.btn == Btn::Boot)) {
    previewChar_ = e.btn == Btn::Plus ? (previewChar_ + 1) % n : (previewChar_ + n - 1) % n;
    pet_.changeCharacter(previewChar_, now);
    drawCharacterMenu();
  } else if (e.btn == Btn::Pwr && e.type == Press::Short) {  // 確定
    applyCharacter(previewChar_, now);
    sfx(Sfx::Dress);
    enter(Screen::Main, now);
    pet_.play(AnimId::Happy, now);
  } else if (e.type == Press::Long) {  // 取消：換回原本的角色
    pet_.changeCharacter(settings_.get().character, now);
    enter(Screen::Main, now);
  }
}

void App::applyCharacter(uint8_t c, uint32_t now) {
  if (c >= Characters::count()) return;
  pet_.changeCharacter(c, now);
  settings_.get().character = c;
  settings_.save();
}

// ------------------------------------------------------------
// 設定選單 (可捲動，共 SETTING_ITEMS 項)
void App::changeSetting(uint8_t item, bool back) {
  Settings &s = settings_.get();
  switch (item) {
    case 0: s.sleepEnabled = !s.sleepEnabled; break;
    case 1: s.sleepStart = step(s.sleepStart, 0, 23, back); break;
    case 2: s.sleepEnd = step(s.sleepEnd, 0, 23, back); break;
    case 3:
      s.brightness = (uint8_t)(step(s.brightness / 10, 1, 10, back) * 10);
      applyBrightness();
      break;
    case 4: s.napMinutes = cycle(NAP_OPTIONS, sizeof(NAP_OPTIONS), s.napMinutes, back); break;
    case 5:
      s.powerSave = step(s.powerSave, 0, 2, back);
      applyPowerMode();
      break;
    case 6: s.autoCostume = !s.autoCostume; break;
    case 7:  // 生日月份：未設定 → 1~12；設了月份就給一個合法日期
      s.birthMonth = step(s.birthMonth, 0, 12, back);
      if (s.birthMonth == 0) s.birthDay = 0;
      else if (s.birthDay == 0 || s.birthDay > DAYS_IN_MONTH[s.birthMonth - 1]) s.birthDay = 1;
      break;
    case 8:
      if (s.birthMonth != 0) s.birthDay = step(s.birthDay, 1, DAYS_IN_MONTH[s.birthMonth - 1], back);
      break;
    case 9: s.bgMode = step(s.bgMode, 0, 4, back); break;
    case 10:
      s.weatherFx = !s.weatherFx;
      applyWeatherToPet();
      break;
    case 11:
      s.tempMood = !s.tempMood;
      applyWeatherToPet();
      break;
    case 12:
      s.county = step(s.county, 0, WeatherService::COUNTY_COUNT - 1, back);
      weather_.configure(s.cwaKey, s.county);
      break;
    case 13: s.hourlyFx = !s.hourlyFx; break;
    case 14: s.tiltOn = !s.tiltOn; break;
    case 15:
      s.shakeLevel = step(s.shakeLevel, 0, 2, back);
      motion_.setLevel(s.shakeLevel);
      break;
    case 16:
      s.focusMin = cycle(FOCUS_OPTIONS, sizeof(FOCUS_OPTIONS), s.focusMin, back);
      if (!timer_.active()) timer_.configure(s.focusMin, s.breakMin);
      break;
    case 17:
      s.breakMin = cycle(BREAK_OPTIONS, sizeof(BREAK_OPTIONS), s.breakMin, back);
      if (!timer_.active()) timer_.configure(s.focusMin, s.breakMin);
      break;
    case 18:
      s.notifyOn = !s.notifyOn;
      ancs_.setEnabled(s.notifyOn);
      break;
    case 19: s.notifyDetail = !s.notifyDetail; break;
    case 20:
      s.patLevel = step(s.patLevel, 0, 3, back);
      motion_.setPatLevel(s.patLevel);
      break;
    case 21:
      s.soundOn = !s.soundOn;
      sound_.setEnabled(s.soundOn);
      sfx(Sfx::Test);  // 開啟時試聽
      break;
    case 22:
      s.soundVol = step(s.soundVol, 0, 2, back);
      sound_.setVolume(s.soundVol);
      sfx(Sfx::Test);
      break;
    case 23: s.alarmOn = !s.alarmOn; break;
    case 24: s.alarmHour = step(s.alarmHour, 0, 23, back); break;
    case 25: s.alarmMin = (uint8_t)((s.alarmMin / 5 * 5 + (back ? 55 : 5)) % 60); break;  // 每次 5 分
    case 26: s.alarmDays = step(s.alarmDays, 0, 2, back); break;
    default: break;
  }
}

void App::drawSettings() {
  const Settings &s = settings_.get();
  char buf[32];
  snprintf(buf, sizeof(buf), "%s  %d/%d", Str::SET_GROUPS[settingGroup(settingItem_)], settingItem_ + 1,
           SETTING_ITEMS);
  drawHeader(Str::SETTINGS_TITLE, buf);

  // 捲動：讓選取的項目保持在畫面內
  if (settingItem_ < settingTop_) settingTop_ = settingItem_;
  if (settingItem_ >= settingTop_ + SETTING_ROWS) settingTop_ = settingItem_ - SETTING_ROWS + 1;

  Arduino_GFX &g = display_.gfx();
  for (uint8_t r = 0; r < SETTING_ROWS; r++) {
    const uint8_t i = settingTop_ + r;
    const char *label = "";
    char value[24] = "";
    const char *text = nullptr;
    int8_t toggle = -1;  // -1 = 不是開關；0 / 1 = 開關狀態
    switch (i) {
      case 0: label = Str::SET_SLEEP; toggle = s.sleepEnabled; break;
      case 1: label = Str::SET_FROM; snprintf(value, sizeof(value), "%02d:00", s.sleepStart); break;
      case 2: label = Str::SET_UNTIL; snprintf(value, sizeof(value), "%02d:00", s.sleepEnd); break;
      case 3: label = Str::SET_LIGHT; snprintf(value, sizeof(value), "%d%%", s.brightness); break;
      case 4:
        label = Str::SET_NAP;
        if (s.napMinutes == 0) text = Str::OFF;
        else snprintf(value, sizeof(value), "%d%s", s.napMinutes, Str::MINUTES);
        break;
      case 5:
        label = Str::SET_POWER;
        text = s.powerSave == 0 ? Str::OFF : s.powerSave == 1 ? Str::AUTO : Str::ON;
        break;
      case 6: label = Str::SET_AUTO; toggle = s.autoCostume; break;
      case 7:
        label = Str::SET_BMONTH;
        if (s.birthMonth == 0) text = Str::UNSET;
        else snprintf(value, sizeof(value), "%d%s", s.birthMonth, Str::MONTH);
        break;
      case 8:
        label = Str::SET_BDAY;
        if (s.birthMonth == 0) text = "--";
        else snprintf(value, sizeof(value), "%d%s", s.birthDay, Str::DAY);
        break;
      case 9: label = Str::SET_BG; text = Str::BG_MODES[s.bgMode <= 4 ? s.bgMode : 0]; break;
      case 10: label = Str::SET_WXFX; toggle = s.weatherFx; break;
      case 11: label = Str::SET_TEMP; toggle = s.tempMood; break;
      case 12:
        label = Str::SET_COUNTY;
        text = Str::COUNTIES[s.county < WeatherService::COUNTY_COUNT ? s.county : 0];
        break;
      case 13: label = Str::SET_HOURLY; toggle = s.hourlyFx; break;
      case 14: label = Str::SET_TILT; toggle = s.tiltOn; break;
      case 15: label = Str::SET_SHAKE; text = Str::LEVELS[s.shakeLevel <= 2 ? s.shakeLevel : 1]; break;
      case 16: label = Str::SET_FOCUS; snprintf(value, sizeof(value), "%d%s", s.focusMin, Str::MINUTES); break;
      case 17: label = Str::SET_BREAK; snprintf(value, sizeof(value), "%d%s", s.breakMin, Str::MINUTES); break;
      case 18: label = Str::SET_NOTIFY; toggle = s.notifyOn; break;
      case 19: label = Str::SET_NOTIFY_DETAIL; text = s.notifyDetail ? Str::DETAIL_FULL : Str::DETAIL_HIDDEN; break;
      case 20: label = Str::SET_PAT; text = Str::PAT_LEVELS[s.patLevel <= 3 ? s.patLevel : 2]; break;
      case 21: label = Str::SET_SOUND; toggle = s.soundOn; break;
      case 22: label = Str::SET_VOLUME; text = Str::LEVELS[s.soundVol <= 2 ? s.soundVol : 1]; break;
      case 23: label = Str::SET_ALARM; toggle = s.alarmOn; break;
      case 24: label = Str::SET_ALARM_H; snprintf(value, sizeof(value), "%02u", s.alarmHour); break;
      case 25: label = Str::SET_ALARM_M; snprintf(value, sizeof(value), "%02u", s.alarmMin); break;
      case 26: label = Str::SET_ALARM_DAYS; text = Str::ALARM_DAYS[s.alarmDays <= 2 ? s.alarmDays : 0]; break;
      default: break;
    }
    if (text) snprintf(value, sizeof(value), "%s", text);

    const int16_t y = ROWS_Y + r * ROW_PITCH;
    const bool sel = i == settingItem_;
    g.fillRect(4, y, 228, ROW_PITCH, Theme::BG);  // 清掉上一次的列 (含圓角外側)
    if (i >= SETTING_ITEMS) continue;
    const uint16_t bg = drawCard(6, y, 224, ROW_H, sel);
    if (sel) g.fillRoundRect(6, y + 4, 4, ROW_H - 8, 2, Theme::ACCENT);  // 選取標記
    const int16_t ty = y + (ROW_H - 26) / 2;
    display_.utf8(16, ty, label, 2, sel ? Theme::TEXT : Theme::DIM_TEXT, bg);
    if (toggle >= 0) {
      drawToggle(222, y + ROW_H / 2, toggle == 1);
    } else if (sel) {  // 選取中：值的兩邊加箭頭，提示可以切換
      char v2[32];
      snprintf(v2, sizeof(v2), "‹%s›", value);
      display_.utf8Right(224, ty, v2, 2, Theme::TIME, bg);
    } else {
      display_.utf8Right(222, ty, value, 2, Theme::TEXT, bg);
    }
  }
  // 捲軸
  const int16_t trackY = ROWS_Y + 2, trackH = SETTING_ROWS * ROW_PITCH - 6;
  g.fillRect(233, ROWS_Y, 7, SETTING_ROWS * ROW_PITCH, Theme::BG);
  g.fillRoundRect(235, trackY, 3, trackH, 1, Theme::CARD);
  const int16_t barH = trackH * SETTING_ROWS / SETTING_ITEMS;
  const int16_t barY = trackY + (trackH - barH) * settingTop_ / (SETTING_ITEMS - SETTING_ROWS);
  g.fillRoundRect(235, barY, 3, barH, 1, Theme::ACCENT);
  drawKeyHints(Str::KEY_EDIT, Str::KEY_UP, Str::KEY_DOWN, HINTBAR_Y);
}

uint8_t App::settingGroup(uint8_t item) {
  // 0 睡眠、1 螢幕與電源、2 裝扮、3 背景與天氣、4 互動、5 番茄鐘、6 通知
  static const uint8_t GROUP[SETTING_ITEMS] = {0, 0, 0, 1, 0, 1, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 5, 5, 6, 6, 4, 7, 7, 8, 8, 8, 8};
  return item < SETTING_ITEMS ? GROUP[item] : 0;
}

// 背景：依時段 (或設定固定) + 天氣合成；有變才重畫
void App::updateScene(uint32_t now, bool force) {
  if (!force && now - lastSceneCheck_ < 1000) return;
  lastSceneCheck_ = now;
  const Settings &s = settings_.get();
  DayPeriod period = DayPeriod::Night;
  if (s.bgMode == 0) {
    tm t;
    if (clock_.now(t)) period = Scene::periodForHour(t.tm_hour);
  } else {
    period = (DayPeriod)(s.bgMode - 1);
  }
  const WeatherInfo w = weather_.info();
  if (!scene_.build(period, w.valid ? w.kind : WeatherKind::Unknown, s.weatherFx)) return;
  pet_.invalidate();
  if (screen_ == Screen::Walk) {  // 散步中：用新背景重新合成
    walk_.compose(scene_.pixels());
    display_.drawRgbScaled(walk_.pixels(), WalkMode::W, WalkMode::H, 0, PET_Y, PixelCanvas::SCALE);
    return;
  }
  if (petVisible())
    display_.drawRgbScaled(scene_.pixels(), Scene::W, Scene::H, 0, petY(), PixelCanvas::SCALE);
}

// 天氣 → 寵物：下雨 / 下雪特效、溫度表情
void App::applyWeatherToPet() {
  const Settings &s = settings_.get();
  const WeatherInfo w = weather_.info();
  Pet::Precip p = Pet::Precip::None;
  if (s.weatherFx && w.valid) {
    if (w.kind == WeatherKind::Rain || w.kind == WeatherKind::Thunder) p = Pet::Precip::Rain;
    else if (w.kind == WeatherKind::Snow) p = Pet::Precip::Snow;
  }
  pet_.setPrecip(p);
  int8_t mood = 0;
  if (s.tempMood && w.valid) {
    if (w.maxT >= 32) mood = 1;        // 很熱
    else if (w.minT <= 15) mood = -1;  // 很冷
  }
  pet_.setTempMood(mood);
}

// 省電：CPU 240 → 80MHz、Wi-Fi 加深休眠、主迴圈休息久一點
void App::applyPowerMode() {
  static bool initialized = false;
  const uint8_t m = settings_.get().powerSave;
  // 自動模式：電池還沒取樣完 (開機前 2 秒) 先不省電，避免開機時忽開忽關
  const bool want = m == 2 || (m == 1 && battery_.voltage() > 0 && !battery_.charging());
  if (initialized && want == powerSaving_) return;
  initialized = true;
  powerSaving_ = want;
  setCpuFrequencyMhz(want ? 80 : 240);
  WiFi.setSleep(want ? WIFI_PS_MAX_MODEM : WIFI_PS_MIN_MODEM);
  Serial.printf("[DeskPet] power save %s (cpu %lu MHz)\n", want ? "on" : "off",
                (unsigned long)getCpuFrequencyMhz());
}

// ------------------------------------------------------------
// 番茄鐘
void App::drawTimerScreen(uint32_t now, bool force) {
  const uint32_t sec = timer_.remainingSec(now);
  if (!force && (int32_t)sec == shownTimerSec_) return;
  shownTimerSec_ = (int32_t)sec;

  char mmss[8];
  fmtMMSS(mmss, sizeof(mmss), sec);
  const uint16_t c = timer_.active() ? Theme::TIME : Theme::DIM;
  display_.text(TIME_X, TIME_Y, mmss, TIME_SIZE, c, Theme::BG);

  if (force) {
    display_.utf8Centered(2, Str::TIMER_TITLE, 1, Theme::ACCENT, Theme::BG);
    drawKeyHints(Str::KEY_TIME, Str::KEY_BACK_STOP, Str::KEY_START, KEYBAR_LOW_Y);
  }
  const char *label = !timer_.active() ? Str::TIMER_READY
                      : timer_.paused() ? Str::TIMER_PAUSED
                      : timer_.phase() == FocusTimer::Phase::Focus ? Str::TIMER_FOCUS
                                                                   : Str::TIMER_BREAK;
  display_.utf8Centered(LINE_Y - 4, label, 2, Theme::TEXT, Theme::BG);
}

// ------------------------------------------------------------
// 資訊頁 (PLUS 長按)：網頁位址、電池
void App::drawInfo() {
  drawHeader(Str::INFO_TITLE, nullptr);
  char buf[48];

  // 卡片 1：網路
  uint16_t c = drawCard(8, 38, 224, 70, false);
  if (wifi_.apActive() && wifi_.state() != WifiState::Connected) {
    display_.utf8(18, 44, Str::AP_TITLE, 1, Theme::DIM, c);
    display_.utf8(18, 62, wifi_.apSsid(), 1, Theme::TIME, c);
    snprintf(buf, sizeof(buf), "%s %s", Str::AP_PASS, wifi_.apPass());
    display_.utf8(18, 78, buf, 1, Theme::TIME, c);
    display_.utf8(18, 92, "http://192.168.4.1", 1, Theme::TEXT, c);
  } else if (wifi_.state() == WifiState::Connected) {
    display_.utf8(18, 44, Str::INFO_WEB, 1, Theme::DIM, c);
    snprintf(buf, sizeof(buf), "http://%s", WiFi.localIP().toString().c_str());
    display_.utf8(18, 64, buf, 1, Theme::TIME, c);
    display_.utf8(18, 84, "http://deskpet.local", 1, Theme::TIME, c);
  } else {
    display_.utf8(18, 44, Str::INFO_WEB, 1, Theme::DIM, c);
    display_.utf8(18, 66, Str::INFO_NO_WIFI, 2, Theme::DIM_TEXT, c);
  }

  // 卡片 2：電池 + 天氣
  c = drawCard(8, 114, 224, 52, false);
  Arduino_GFX &g = display_.gfx();
  const uint8_t p = battery_.percent();
  g.fillRoundRect(18, 122, 60, 12, 4, Theme::BG);
  const int16_t fw = (int16_t)(56 * (uint16_t)p / 100);
  if (fw > 0) g.fillRoundRect(20, 124, fw, 8, 3, p <= 20 ? Theme::WARN : Theme::ACCENT);
  snprintf(buf, sizeof(buf), "%u%%  %.2fV%s", p, battery_.voltage(), battery_.charging() ? "  ⚡" : "");
  display_.utf8(86, 121, buf, 1, Theme::TEXT, c);
  const WeatherInfo w = weather_.info();
  if (w.valid) {
    snprintf(buf, sizeof(buf), "%s %d~%d%s  %s %u%%",
             Str::WEATHER_NAMES[(uint8_t)w.kind < 8 ? (uint8_t)w.kind : 0], w.minT, w.maxT, Str::DEG_C,
             Str::RAIN_CHANCE, w.pop);
    display_.utf8(18, 144, buf, 1, Theme::TEXT, c);
  } else {
    display_.utf8(18, 144, settings_.get().cwaKey[0] ? Str::SYNCING : Str::INFO_NO_WEATHER, 1, Theme::DIM, c);
  }

  // 卡片 3：SD 卡
  c = drawCard(8, 172, 224, 46, false);
  if (SdStore::ok()) {
    snprintf(buf, sizeof(buf), "%s %lu / %lu MB", Str::INFO_SD, (unsigned long)SdStore::usedMB(),
             (unsigned long)SdStore::totalMB());
    display_.utf8(18, 178, buf, 1, Theme::TEXT, c);
    snprintf(buf, sizeof(buf), "%s %u  %s %u", Str::CHARACTER_TITLE, (unsigned)Characters::count(), Str::MUSIC_TITLE,
             (unsigned)SoundManager::songCount());
    display_.utf8(18, 198, buf, 1, Theme::DIM, c);
  } else {
    display_.utf8(18, 188, Str::INFO_NO_SD, 1, Theme::DIM, c);
  }

  drawHintBar(Str::INFO_HINT);
}

// ------------------------------------------------------------
// WebPanel
bool App::timeString(char *buf, size_t n) const {
  tm t;
  if (!clock_.now(t)) return false;
  snprintf(buf, n, "%04d/%02d/%02d %02d:%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
           t.tm_hour, t.tm_min);
  return true;
}

void App::webSdChanged(const char *dir) {
  const uint32_t now = millis();
  if (strcmp(dir, "chars") == 0) {
    Characters::reloadFromSd();
    Settings &s = settings_.get();
    if (s.character >= Characters::count()) {
      s.character = 0;
      settings_.save();
    }
    pet_.setCharacter(s.character, now);
    if (screen_ == Screen::Character) enter(Screen::Main, now);
  } else if (strcmp(dir, "music") == 0) {
    musicPlaying_ = false;
    sound_.reloadMusic();
    if (musicSong_ >= SoundManager::songCount()) musicSong_ = 0;
    if (screen_ == Screen::Music) drawMusic();
  }
}

bool App::webBackup() { return SdArchive::backupSettings(settings_.get()); }

bool App::webRestore() {
  Settings s = settings_.get();  // Wi-Fi 帳密與金鑰保留目前的
  if (!SdArchive::restoreSettings(s)) return false;
  SettingsManager::sanitize(s);
  if (s.character >= Characters::count()) s.character = 0;
  webApply(s, false);
  return true;
}

void App::logTick(const tm &t) {
  if (!SdStore::ok() || t.tm_min % 10 != 0) return;
  const int32_t key = t.tm_yday * 1440 + t.tm_hour * 60 + t.tm_min;
  if (key == lastLogKey_) return;
  lastLogKey_ = key;
  const DiaryDay &d = diary_.day(0);
  const WeatherInfo w = weather_.info();
  SdArchive::LogRow r;
  r.batteryPct = battery_.percent();
  r.voltage = battery_.voltage();
  r.charging = battery_.charging();
  r.rssi = wifi_.state() == WifiState::Connected ? (int8_t)WiFi.RSSI() : 0;
  r.pets = d.stat[(size_t)DiaryStat::Pet];
  r.shakes = d.stat[(size_t)DiaryStat::Shake];
  r.character = pet_.character();
  r.wx = w.valid ? (uint8_t)w.kind : 0;
  r.minT = w.valid ? w.minT : 0;
  r.maxT = w.valid ? w.maxT : 0;
  SdArchive::appendLog(t, r);
}

void App::webApply(const Settings &s, bool wifiChanged) {
  const uint32_t now = millis();
  const bool weatherChanged =
      strcmp(s.cwaKey, settings_.get().cwaKey) != 0 || s.county != settings_.get().county;
  const bool charChanged = s.character != settings_.get().character;
  settings_.get() = s;
  if (weatherChanged) weather_.configure(s.cwaKey, s.county);
  if (charChanged) pet_.changeCharacter(s.character, now);
  settings_.save();
  costumes_.setCurrent(settings_.get().costume);
  appliedCostume_ = 0xFF;
  if (!timer_.active()) timer_.configure(s.focusMin, s.breakMin);
  motion_.setLevel(s.shakeLevel);
  motion_.setPatLevel(s.patLevel);
  sound_.setVolume(s.soundVol);
  sound_.setEnabled(s.soundOn);
  lastInput_ = now;
  applyBrightness();
  applyPowerMode();
  applyWeatherToPet();
  updateScene(now, true);
  ancs_.setEnabled(s.notifyOn);
  if (wifiChanged) pendingWifiAt_ = now + 1500;
  if (screen_ == Screen::Settings) drawSettings();
}

void App::webSetWifi(const char *ssid, const char *pass) {
  settings_.setWifi(ssid, pass);
  pendingWifiAt_ = millis() + 1500;  // 先讓網頁回應送出，再切換連線
}

// ------------------------------------------------------------
// iPhone 通知 / 藍牙配對碼：在上方時間區彈出卡片
void App::updateNotify(uint32_t now) {
  ancs_.update(now);

  // 配對碼：不管在哪個畫面都要立刻顯示 (iPhone 正在等你輸入)
  uint32_t pk;
  if (ancs_.passkey(pk)) {
    if (popup_ != Popup::Passkey || pk != shownPasskey_) {
      if (screen_ != Screen::Main) enter(Screen::Main, now);
      wakeIfSleeping(now);
      shownPasskey_ = pk;
      popup_ = Popup::Passkey;
      popupUntil_ = now + 60000;
      Arduino_GFX &g = display_.gfx();
      g.fillRect(0, 0, 240, PET_Y - 1, Theme::PANEL);
      display_.utf8Centered(3, Str::PASSKEY_TITLE, 1, Theme::TEXT, Theme::PANEL);
      char buf[8];
      snprintf(buf, sizeof(buf), "%06lu", (unsigned long)pk);
      display_.text((240 - 6 * 24) / 2, 22, buf, 4, Theme::TIME, Theme::PANEL);
    }
  } else if (popup_ == Popup::Passkey) {
    closePopup();
  }

  // 配對完成
  const AncsState st = ancs_.state();
  if (st != lastAncs_) {
    if (st == AncsState::Ready) showMessage(Str::PHONE_READY, now, 5000);
    lastAncs_ = st;
  }

  // 新通知
  PhoneNotification n;
  while (ancs_.pop(n)) {
    Serial.printf("[ANCS] %s: %s\n", n.app, settings_.get().notifyDetail ? n.title : "(hidden)");
    diary_.add(DiaryStat::Notify);  // 日記只記則數，不記內容
    if (sleep_ == SleepKind::Night) continue;  // 夜間睡眠不打擾
    pendingNote_ = n;
    hasPendingNote_ = true;
    pendingNoteAt_ = now;
  }
  // 只在主畫面彈出；在其他畫面時先記著，回主畫面 60 秒內再顯示
  if (hasPendingNote_ && popup_ != Popup::Passkey && popup_ != Popup::Alarm) {
    if (now - pendingNoteAt_ > 60000) {
      hasPendingNote_ = false;
    } else if (screen_ == Screen::Main) {
      hasPendingNote_ = false;
      wakeIfSleeping(now);
      showNotification(pendingNote_, now);
    }
  }

  if (popup_ != Popup::None && (int32_t)(now - popupUntil_) >= 0) closePopup();
}

void App::showNotification(const PhoneNotification &n, uint32_t now) {
  popup_ = Popup::Notify;
  popupUntil_ = now + NOTIFY_SHOW_MS;
  Arduino_GFX &g = display_.gfx();
  g.fillRect(0, 0, 240, PET_Y - 1, Theme::PANEL);
  char head[140];
  const bool detail = settings_.get().notifyDetail;
  if (detail && n.title[0]) snprintf(head, sizeof(head), "[%s] %s", n.app, n.title);
  else snprintf(head, sizeof(head), "[%s] %s", n.app, detail ? "" : Str::NOTIFY_HIDDEN);
  display_.utf8Wrap(6, 3, 228, head, 1, 1, Theme::TIME, Theme::PANEL);
  sfx(Sfx::Notify);
  if (detail && n.message[0]) display_.utf8Wrap(6, 19, 228, n.message, 1, 3, Theme::TEXT, Theme::PANEL);
  pet_.play(AnimId::Happy, now);
}

void App::closePopup() {
  popup_ = Popup::None;
  if (screen_ != Screen::Main) return;
  display_.gfx().fillRect(0, 0, 240, PET_Y - 1, Theme::BG);  // 還原時間區
  shownMinute_ = -1;
  shownDate_[0] = '\0';
  shownColon_ = -1;
  lastClockCheck_ = 0;
  drawTopBar(true);
  drawWeather();
}

// 設定熱點畫面：教使用者用手機連熱點
void App::drawWifiSetup() {
  drawHeader(Str::AP_TITLE, nullptr);
  display_.utf8(14, 42, Str::AP_STEP1, 1, Theme::TEXT, Theme::BG);
  const uint16_t c = drawCard(8, 60, 224, 72, true);
  display_.utf8Centered(66, wifi_.apSsid(), 2, Theme::TIME, c, 216);
  char buf[32];
  snprintf(buf, sizeof(buf), "%s %s", Str::AP_PASS, wifi_.apPass());
  display_.utf8Centered(100, buf, 2, Theme::TIME, c, 216);
  display_.utf8(14, 148, Str::AP_STEP2, 1, Theme::TEXT, Theme::BG);
  display_.utf8(14, 168, Str::AP_STEP2B, 1, Theme::DIM, Theme::BG);
  drawHintBar(Str::INFO_HINT);
}

void App::webPokePet() {
  const uint32_t now = millis();
  if (!wakeIfSleeping(now)) patPet(now);
}

// 摸頭：手在頭上拍幾下、瞇眼、冒愛心，下方顯示一句話 (輪流)
void App::patPet(uint32_t now) {
  static uint8_t n = 0;
  constexpr uint8_t count = sizeof(Str::MSG_PAT) / sizeof(Str::MSG_PAT[0]);
  pet_.pat(now);
  if (random(100) < 40) sfx(Sfx::CryHappy, (int8_t)random(-8, 12));  // 有時候會開心地叫
  else sfx(Sfx::Pat);
  diary_.add(DiaryStat::Pet);
  if (screen_ == Screen::Walk) {
    walkMsg_ = Str::MSG_PAT[n++ % count];
    walkMsgUntil_ = now + WALK_MSG_MS;
  } else {
    showMessage(Str::MSG_PAT[n++ % count], now, 2500);
  }
}

// ------------------------------------------------------------
// 散步模式：寵物原地走，草地往左捲；兩側只重畫草地那一條
void App::startWalk(uint32_t now) {
  walk_.start(now);
  walk_.compose(scene_.pixels());
  walk_.clearChanged();
  pet_.setScene(walk_.pixels(), WalkMode::W, WalkMode::H, PET_X / PixelCanvas::SCALE, 0);
  pet_.setWalking(true, now);
  walkMsg_ = nullptr;
  display_.drawRgbScaled(walk_.pixels(), WalkMode::W, WalkMode::H, 0, PET_Y, PixelCanvas::SCALE);
  display_.utf8Centered(8, Str::WALK_TITLE, 2, Theme::ACCENT, Theme::BG);
  drawKeyHints(Str::KEY_PAT, Str::KEY_HOME, Str::KEY_PAT, KEYBAR_LOW_Y);
  Serial.println("[DeskPet] walk start");
}

void App::pushWalkStrips() {
  constexpr int16_t rows = WalkMode::H - WalkMode::STRIP_Y;
  constexpr int16_t left = PET_X / PixelCanvas::SCALE;  // 寵物畫布左邊的邏輯寬度
  constexpr int16_t rightX = left + PixelCanvas::LW;
  const uint16_t *base = walk_.pixels() + WalkMode::STRIP_Y * WalkMode::W;
  const int16_t y = PET_Y + WalkMode::STRIP_Y * PixelCanvas::SCALE;
  display_.drawRgbScaled(base, left, rows, 0, y, PixelCanvas::SCALE, WalkMode::W);
  display_.drawRgbScaled(base + rightX, WalkMode::W - rightX, rows, rightX * PixelCanvas::SCALE, y,
                         PixelCanvas::SCALE, WalkMode::W);
}

void App::updateWalk(uint32_t now) {
  switch (walk_.update(now)) {
    case WalkMode::Event::Flower:
      pet_.setWalking(false, now);
      pet_.play(AnimId::Happy, now);
      diary_.add(DiaryStat::Flower);
      sfx(Sfx::Flower);
      walkMsg_ = Str::WALK_FLOWER;
      walkMsgUntil_ = now + WALK_MSG_MS;
      break;
    case WalkMode::Event::Butterfly:
      pet_.play(AnimId::Happy, now);  // 一邊走一邊開心 (結束後回到走路)
      diary_.add(DiaryStat::Butterfly);
      sfx(Sfx::Happy);
      walkMsg_ = Str::WALK_BUTTERFLY;
      walkMsgUntil_ = now + WALK_MSG_MS;
      break;
    case WalkMode::Event::Rest:
      pet_.setWalking(false, now);
      pet_.play(AnimId::Blink, now);
      walkMsg_ = Str::WALK_REST;
      walkMsgUntil_ = now + WALK_MSG_MS;
      break;
    case WalkMode::Event::Resume:
      pet_.setWalking(true, now);
      break;
    default: break;
  }
  if (walk_.moving() && now - lastStepSound_ >= 400) {  // 腳步聲 (每兩步一下，下雨天變踩水聲)
    lastStepSound_ = now;
    const WeatherInfo w = weather_.info();
    const bool wet = settings_.get().weatherFx && w.valid &&
                     (w.kind == WeatherKind::Rain || w.kind == WeatherKind::Thunder);
    sfx(wet ? Sfx::StepRain : Sfx::Step, (int8_t)random(-10, 10));
  }
  if (walk_.changed()) {
    walk_.clearChanged();
    walk_.compose(scene_.pixels());
    pushWalkStrips();
    pet_.invalidate();
  }

  const uint32_t sec = walk_.elapsedSec(now);
  if (sec >= WALK_MAX_SEC) {  // 走太久了：自己回家
    enter(Screen::Main, now);
    return;
  }
  char line[48];
  uint16_t color = Theme::TEXT;
  if (walkMsg_ != nullptr && (int32_t)(walkMsgUntil_ - now) > 0) {
    snprintf(line, sizeof(line), "%s", walkMsg_);
    color = Theme::ACCENT;
  } else {
    char mmss[8];
    fmtMMSS(mmss, sizeof(mmss), sec);
    snprintf(line, sizeof(line), "%s  %lu %s", mmss, (unsigned long)walk_.steps(), Str::WALK_STEPS);
  }
  drawLine(line, color, LINE_Y - 4);  // 散步：往上一點，讓出下方的按鍵提示列
}

// 離開散步畫面時呼叫 (由 enter() 統一處理)：寫進日記、換回一般背景
void App::finishWalk(uint32_t now) {
  const uint32_t sec = walk_.elapsedSec(now);
  if (sec >= 5) diary_.addWalk(sec, walk_.steps());  // 不到 5 秒不算一次散步
  pet_.setWalking(false, now);
  pet_.setScene(scene_.pixels(), Scene::W, Scene::H, PET_X / PixelCanvas::SCALE, 0);
  if (sec >= 5) showMessage(Str::WALK_BACK, now, 5000);
  Serial.printf("[DeskPet] walk end: %lus, %lu steps\n", (unsigned long)sec, (unsigned long)walk_.steps());
}

void App::webStartWalk() {
  const uint32_t now = millis();
  wakeIfSleeping(now);
  if (screen_ != Screen::Walk) enter(Screen::Walk, now);
}

// ------------------------------------------------------------
// 每日小日記 (PLUS 長按 → 資訊頁 → PLUS)
void App::drawDiary() {
  static char lines[Diary::MAX_LINES][Diary::LINE_LEN];  // 靜態：不佔主迴圈堆疊
  display_.clear(Theme::BG);
  const DiaryDay &d = diary_.day(diaryPage_);
  char buf[48];
  if (d.ymd != 0) {
    snprintf(buf, sizeof(buf), "%lu/%lu %s  %u/%u", (unsigned long)(d.ymd / 100 % 100),
             (unsigned long)(d.ymd % 100), Str::WEEKDAY[d.wday % 7], (unsigned)(diaryPage_ + 1),
             (unsigned)diary_.count());
  } else {
    snprintf(buf, sizeof(buf), "%u/%u", (unsigned)(diaryPage_ + 1), (unsigned)diary_.count());
  }
  drawHeader(Str::DIARY_TITLE, buf);

  // 日記本：一張卡片，左邊一排「裝訂孔」
  const uint16_t c = drawCard(6, 38, 228, HINTBAR_Y - 44, false);
  Arduino_GFX &g = display_.gfx();
  for (int16_t y = 50; y < HINTBAR_Y - 12; y += 22) g.fillCircle(15, y, 2, Theme::BG);

  const uint8_t n = Diary::compose(d, diaryPage_ == 0, lines, Diary::MAX_LINES);
  int16_t y = 44;
  for (uint8_t i = 0; i < n && y <= HINTBAR_Y - 20; i++) {
    const uint8_t maxLines = y + 2 * 14 <= HINTBAR_Y - 8 ? 2 : 1;
    const bool last = i == n - 1;  // 最後一句是心情總結，用金色
    display_.utf8(24, y, "♥", 1, last ? Theme::TIME : Theme::ACCENT, c);
    const uint8_t used =
        display_.utf8Wrap(38, y, 190, lines[i], 1, maxLines, last ? Theme::TIME : Theme::TEXT, c);
    y += (int16_t)(used ? used : 1) * 14 + 5;
  }
  drawKeyHints(Str::KEY_DAY_PREV, Str::KEY_BACK, Str::KEY_DAY_NEXT, HINTBAR_Y);
}

// 序列埠指令：walk (開始 / 結束散步)、diary (印出日記)
bool App::consoleCommand(const char *cmd) {
  const uint32_t now = millis();
  if (strncmp(cmd, "char", 4) == 0 && (cmd[4] == '\0' || cmd[4] == ' ')) {  // char [編號]：列出 / 切換角色
    const char *arg = cmd + 4;
    while (*arg == ' ') arg++;
    if (*arg == '\0') {
      for (uint8_t i = 0; i < Characters::count(); i++)
        Serial.printf("%u %s%s%s\n", i, Characters::name(i), i >= Characters::builtinCount() ? " (SD)" : "",
                      i == settings_.get().character ? "  <- current" : "");
    } else {
      char *end = nullptr;
      const long v = strtol(arg, &end, 10);
      if (end == arg || *end != '\0' || v < 0 || v >= Characters::count()) {
        Serial.printf("Usage: char [0-%u]\n", Characters::count() - 1);
      } else {
        wakeIfSleeping(now);
        applyCharacter((uint8_t)v, now);
        Serial.printf("character: %s\n", Characters::name(settings_.get().character));
      }
    }
    return true;
  }
  if (strcmp(cmd, "backup") == 0) {
    Serial.println(webBackup() ? "settings saved to /deskpet/backup/settings.txt" : "backup failed (no SD card?)");
    return true;
  }
  if (strcmp(cmd, "restore") == 0) {
    Serial.println(webRestore() ? "settings restored from SD card" : "restore failed (no SD card or no backup)");
    return true;
  }
  if (strcmp(cmd, "walk") == 0) {
    wakeIfSleeping(now);
    enter(screen_ == Screen::Walk ? Screen::Main : Screen::Walk, now);
    return true;
  }
  if (strcmp(cmd, "sound on") == 0 || strcmp(cmd, "sound off") == 0) {  // 開 / 關音效 (會存檔)
    Settings &s = settings_.get();
    s.soundOn = cmd[7] == 'n';
    settings_.save();
    sound_.setEnabled(s.soundOn);
    Serial.printf("sound %s (ready=%d)\n", s.soundOn ? "on" : "off", sound_.ready());
    return true;
  }
  if (strcmp(cmd, "alarm test") == 0) {  // 立刻響鬧鐘 (測試)
    ringAlarm(now);
    return true;
  }
  if (strcmp(cmd, "rhythm") == 0) {
    wakeIfSleeping(now);
    enter(Screen::Rhythm, now);
    return true;
  }
  if (strcmp(cmd, "cry") == 0) {  // 叫一聲 (不受音效開關影響)
    sound_.play(Sfx::Cry, true, (int8_t)random(-8, 10));
    return true;
  }
  if (strncmp(cmd, "song ", 5) == 0) {  // song 0~3：播音樂盒曲目
    const int n = atoi(cmd + 5);
    if (n < 0 || n >= SoundManager::songCount()) {
      Serial.println("song 0~3");
    } else {
      wakeIfSleeping(now);
      musicSong_ = (uint8_t)n;
      enter(Screen::Music, now);
      sound_.playSong(musicSong_);
      musicPlaying_ = true;
      musicStartedAt_ = musicLastDance_ = now;
      drawMusic();
    }
    return true;
  }
  if (strcmp(cmd, "sound") == 0) {  // 試聽 (音效需在設定裡開啟)
    if (!settings_.get().soundOn) Serial.println("sound is off (settings item 22)");
    else sound_.play(Sfx::Test);
    return true;
  }
  if (strcmp(cmd, "tap") == 0) {  // 印出每次晃動的峰值，用來調拍頭靈敏度
    motion_.setTapDebug(!motion_.tapDebug());
    Serial.printf("tap debug %s (level %u)\n", motion_.tapDebug() ? "on" : "off", settings_.get().patLevel);
    return true;
  }
  if (strcmp(cmd, "diary") == 0) {
    static char lines[Diary::MAX_LINES][Diary::LINE_LEN];
    for (uint8_t i = 0; i < diary_.count(); i++) {
      const DiaryDay &d = diary_.day(i);
      Serial.printf("== %lu ==\n", (unsigned long)d.ymd);
      const uint8_t n = Diary::compose(d, i == 0, lines, Diary::MAX_LINES);
      for (uint8_t k = 0; k < n; k++) Serial.printf("  %s\n", lines[k]);
    }
    return true;
  }
  return false;
}
