// 活動選單、鬧鐘、節奏遊戲、音樂盒、寵物叫聲 (App 的成員函式，從 App.cpp 分出來)
#include <Arduino.h>

#include "../display/Theme.h"
#include "../ui/Strings.h"
#include "App.h"
#include "Layout.h"
#include "../display/LargeAlloc.h"
#include "../storage/SdStore.h"

namespace {
constexpr uint8_t MENU_COUNT = sizeof(Str::MENU_ITEMS) / sizeof(Str::MENU_ITEMS[0]);
constexpr uint8_t MENU_ROWS = 6;  // 一頁顯示幾項 (超過就捲動)

// 相框
constexpr uint8_t MAX_PHOTOS = 40;
constexpr uint32_t PHOTO_SLIDE_MS = 10000;  // 自動換下一張
constexpr size_t PHOTO_BYTES = 240 * 240 * 2;
char (*photoPaths)[96] = nullptr;  // 第一次進相框時配置在 PSRAM

// 鬧鐘
constexpr uint32_t ALARM_REPEAT_MS = 3500;      // 鈴聲每隔多久響一次
constexpr uint32_t ALARM_MAX_MS = 3UL * 60000;  // 響 3 分鐘沒人理就自己停
constexpr uint32_t SNOOZE_MS = 5UL * 60000;
constexpr uint32_t ALARM_WAKE_MS = 30UL * 60000;  // 鬧鐘叫醒後，30 分鐘內不回去睡
constexpr int32_t SUNRISE_SEC = 15 * 60;          // 鬧鐘前 15 分鐘螢幕慢慢變亮

// 節奏遊戲 (96 BPM)
constexpr uint32_t BEAT_MS = 625;
constexpr uint8_t COUNT_IN = 4;  // 前 4 拍是預備拍
constexpr uint8_t BEATS = 32;
constexpr int32_t GREAT_MS = 90;
constexpr int32_t GOOD_MS = 160;
constexpr int16_t TRACK_Y = 198;   // 拍子軌道 (寵物區下方)
constexpr int16_t TRACK_H = 25;
constexpr int16_t TARGET_X = 40;   // 判定圈的位置
constexpr float DOT_SPEED = 0.16f;  // 每毫秒移動幾像素

// 叫聲
constexpr uint32_t CRY_MIN_MS = 10UL * 60000;
constexpr uint32_t CRY_MAX_MS = 25UL * 60000;

bool alarmDayOk(uint8_t mode, int wday) {
  if (mode == 1) return wday >= 1 && wday <= 5;
  if (mode == 2) return wday == 0 || wday == 6;
  return true;
}
}  // namespace

// ============================================================
// 活動選單：左 = 進入，中 = 上一項，右 = 下一項，長按 = 返回
void App::drawMenu() {
  char buf[16];
  snprintf(buf, sizeof(buf), "%u/%u", (unsigned)(menuItem_ + 1), (unsigned)MENU_COUNT);
  drawHeader(Str::MENU_TITLE, buf);
  Arduino_GFX &g = display_.gfx();
  if (menuItem_ < menuTop_) menuTop_ = menuItem_;
  if (menuItem_ >= menuTop_ + MENU_ROWS) menuTop_ = menuItem_ - MENU_ROWS + 1;
  for (uint8_t r = 0; r < MENU_ROWS && menuTop_ + r < MENU_COUNT; r++) {
    const uint8_t i = menuTop_ + r;
    const int16_t y = ROWS_Y + r * ROW_PITCH;
    const bool sel = i == menuItem_;
    g.fillRect(4, y, 232, ROW_PITCH, Theme::BG);
    const uint16_t bg = drawCard(6, y, 228, ROW_H, sel);
    if (sel) g.fillRoundRect(6, y + 4, 4, ROW_H - 8, 2, Theme::ACCENT);
    display_.utf8(16, y + 1, Str::MENU_ITEMS[i], 2, sel ? Theme::TEXT : Theme::DIM_TEXT, bg);
    display_.utf8Right(224, y + 8, Str::MENU_DESC[i], 1, sel ? Theme::TIME : Theme::DIM, bg);
  }
  drawKeyHints(Str::KEY_ENTER, Str::KEY_UP_ONLY, Str::KEY_DOWN_ONLY, HINTBAR_Y);
}

void App::menuButton(const ButtonEvent &e, uint32_t now) {
  if (e.type == Press::Long) {
    enter(Screen::Main, now);
    return;
  }
  if (e.btn == Btn::Pwr) {
    menuItem_ = (menuItem_ + MENU_COUNT - 1) % MENU_COUNT;
    drawMenu();
  } else if (e.btn == Btn::Plus) {
    menuItem_ = (menuItem_ + 1) % MENU_COUNT;
    drawMenu();
  } else {  // 左鍵：進入
    static const Screen TARGET[] = {Screen::Walk, Screen::Diary,     Screen::Rhythm, Screen::Music,
                                    Screen::Character, Screen::Photo, Screen::Info};
    static_assert(sizeof(TARGET) / sizeof(TARGET[0]) == MENU_COUNT, "TARGET 與 Str::MENU_ITEMS 數量不一致");
    enter(TARGET[menuItem_ < MENU_COUNT ? menuItem_ : 0], now);
  }
}

// ============================================================
// 鬧鐘 (每秒檢查；響鈴時每 3.5 秒響一次，最多 3 分鐘)
void App::updateAlarm(uint32_t now) {
  if (alarmRinging_) {
    if ((int32_t)(now - alarmNextRing_) >= 0) {
      alarmNextRing_ = now + ALARM_REPEAT_MS;
      sound_.play(Sfx::Alarm, true);  // 鬧鐘一定會響 (不受「音效」開關影響)
      pet_.play(AnimId::Happy, now);
    }
    if (now - alarmStart_ > ALARM_MAX_MS) stopAlarm(false, now);
    return;
  }
  if (snoozeUntil_ != 0 && (int32_t)(now - snoozeUntil_) >= 0) {
    snoozeUntil_ = 0;
    ringAlarm(now);
    return;
  }
  if (now - lastAlarmCheck_ < 1000) return;
  lastAlarmCheck_ = now;
  const Settings &s = settings_.get();
  tm t;
  if (!s.alarmOn || !clock_.now(t) || !alarmDayOk(s.alarmDays, t.tm_wday)) return;
  const int32_t alarmSec = s.alarmHour * 3600 + s.alarmMin * 60;
  const int32_t nowSec = t.tm_hour * 3600 + t.tm_min * 60 + t.tm_sec;
  const int32_t key = t.tm_yday * 1440 + s.alarmHour * 60 + s.alarmMin;
  if (nowSec >= alarmSec && nowSec < alarmSec + 60 && key != lastAlarmKey_) {
    lastAlarmKey_ = key;
    ringAlarm(now);
    return;
  }
  // 晨光：鬧鐘前 15 分鐘，夜間睡眠中的螢幕從最暗慢慢亮到設定亮度
  const int32_t until = alarmSec - nowSec;
  if (until > 0 && until <= SUNRISE_SEC && sleep_ == SleepKind::Night) {
    const uint8_t full = s.brightness;
    const uint8_t b = (uint8_t)(SLEEP_BRIGHTNESS + (int32_t)(full - SLEEP_BRIGHTNESS) * (SUNRISE_SEC - until) / SUNRISE_SEC);
    display_.setBrightness(b);
  }
}

void App::ringAlarm(uint32_t now) {
  Serial.println("[DeskPet] alarm!");
  alarmRinging_ = true;
  alarmStart_ = now;
  alarmNextRing_ = now;
  manualWakeUntil_ = now + ALARM_WAKE_MS;  // 讓寵物醒來 (updateSleep 會處理)
  lastInput_ = now;
  if (screen_ != Screen::Main) enter(Screen::Main, now);
  drawAlarmPopup();
}

void App::drawAlarmPopup() {
  popup_ = Popup::Alarm;
  popupUntil_ = millis() + ALARM_MAX_MS + 60000;  // 由 stopAlarm 關閉
  Arduino_GFX &g = display_.gfx();
  g.fillRect(0, 0, 240, PET_Y - 1, Theme::PANEL);
  const Settings &s = settings_.get();
  char buf[40];
  snprintf(buf, sizeof(buf), "⏰ %02u:%02u %s", s.alarmHour, s.alarmMin, Str::ALARM_TITLE);
  display_.utf8Centered(4, buf, 2, Theme::TIME, Theme::PANEL);
  display_.utf8Centered(38, Str::ALARM_HINT, 1, Theme::TEXT, Theme::PANEL);
}

void App::stopAlarm(bool snooze, uint32_t now) {
  alarmRinging_ = false;
  sound_.stop();
  if (popup_ == Popup::Alarm) closePopup();
  if (snooze) {
    snoozeUntil_ = now + SNOOZE_MS;
    showMessage(Str::MSG_SNOOZE, now, 5000);
  } else {
    snoozeUntil_ = 0;
    showMessage(Str::MSG_ALARM_OFF, now, 6000);
    pet_.play(AnimId::Happy, now);
  }
  Serial.printf("[DeskPet] alarm %s\n", snooze ? "snoozed" : "off");
}

// ============================================================
// 節奏遊戲：跟著「噠」的拍子按左 / 右鍵或拍板子
void App::startRhythm(uint32_t now) {
  sound_.setStreaming(true);  // 連續輸出：延遲固定，拍子才準
  rhythmT0_ = now + 1500;
  rhythmNextTick_ = 0;
  rhythmJudged_ = 0;
  rhythmHits_ = 0;
  rhythmShownBeat_ = 0xFF;
  rhythmDone_ = false;
  rhythmFeedback_ = nullptr;
  uint32_t dummy;
  Btn b;
  while (buttons_.popPress(b, dummy)) {}
  while (motion_.popTap(dummy)) {}
  drawHeader(Str::RHYTHM_TITLE, nullptr);
  display_.utf8Centered(38, Str::RHYTHM_READY, 1, Theme::DIM_TEXT, Theme::BG);
  drawKeyHints(Str::KEY_HIT, Str::KEY_BACK, Str::KEY_HIT, HINTBAR_Y);
  drawRhythmTrack(now);
}

void App::drawRhythmTrack(uint32_t now) {
  Arduino_GFX &g = display_.gfx();
  g.fillRect(0, TRACK_Y, 240, TRACK_H, Theme::BG);
  const int16_t cy = TRACK_Y + TRACK_H / 2;
  g.fillRoundRect(4, cy - 1, 232, 3, 1, Theme::CARD);  // 軌道
  g.drawCircle(TARGET_X, cy, 10, Theme::ACCENT);         // 判定圈
  g.drawCircle(TARGET_X, cy, 9, Theme::ACCENT);
  if (rhythmDone_) return;
  for (uint8_t k = COUNT_IN; k < COUNT_IN + BEATS; k++) {
    if (rhythmJudged_ & (1ULL << k)) continue;
    const int32_t dt = (int32_t)(rhythmT0_ + k * BEAT_MS) - (int32_t)now;
    const int16_t x = TARGET_X + (int16_t)(dt * DOT_SPEED);
    if (x < TARGET_X - 14 || x > 246) continue;
    const bool accent = (k % 4) == 0;
    g.fillCircle(x, cy, accent ? 7 : 5, accent ? Theme::TIME : Theme::TEXT);
  }
}

void App::rhythmHit(uint32_t t) {
  if (rhythmDone_) return;
  // 找最接近、還沒判定的那一拍
  int8_t best = -1;
  int32_t bestDt = 0;
  for (uint8_t k = COUNT_IN; k < COUNT_IN + BEATS; k++) {
    if (rhythmJudged_ & (1ULL << k)) continue;
    const int32_t dt = (int32_t)(t - (rhythmT0_ + k * BEAT_MS));
    if (best < 0 || abs(dt) < abs(bestDt)) {
      best = (int8_t)k;
      bestDt = dt;
    }
  }
  if (best < 0) return;
  const int32_t a = abs(bestDt);
  if (a <= GOOD_MS) {
    rhythmJudged_ |= 1ULL << best;
    rhythmHits_++;
    rhythmFeedback_ = a <= GREAT_MS ? Str::RHYTHM_GREAT : Str::RHYTHM_GOOD;
    pet_.play(AnimId::Happy, millis());
  } else if (a < (int32_t)BEAT_MS / 2) {
    rhythmFeedback_ = bestDt < 0 ? Str::RHYTHM_EARLY : Str::RHYTHM_LATE;
  }
  rhythmShownBeat_ = 0xFF;  // 馬上更新上方的回饋文字
}

void App::updateRhythm(uint32_t now) {
  // 拍子聲：提早 STREAM_LATENCY_MS 送出，讓聲音剛好落在拍點上
  while (rhythmNextTick_ < COUNT_IN + BEATS &&
         (int32_t)(now - (rhythmT0_ + rhythmNextTick_ * BEAT_MS - SoundManager::STREAM_LATENCY_MS)) >= 0) {
    sound_.play(rhythmNextTick_ % 4 == 0 ? Sfx::TickAccent : Sfx::Tick, true);
    rhythmNextTick_++;
  }

  // 輸入：按下的瞬間 (左 / 右鍵) 或輕拍
  Btn b;
  uint32_t t;
  while (buttons_.popPress(b, t))
    if (b != Btn::Pwr) rhythmHit(t);
  while (motion_.popTap(t)) rhythmHit(t);

  if (!rhythmDone_) {
    // 錯過的拍子
    for (uint8_t k = COUNT_IN; k < COUNT_IN + BEATS; k++) {
      if ((rhythmJudged_ & (1ULL << k)) == 0 && (int32_t)(now - (rhythmT0_ + k * BEAT_MS)) > GOOD_MS)
        rhythmJudged_ |= 1ULL << k;  // 只標記，不扣什麼
    }
    // 上方文字：預備拍倒數 → 第幾拍 + 回饋
    const int32_t since = (int32_t)(now - rhythmT0_);
    const uint8_t beat = since < 0 ? 0xFE : (uint8_t)min<int32_t>(since / BEAT_MS, 250);
    if (beat != rhythmShownBeat_) {
      rhythmShownBeat_ = beat;
      char buf[32];
      if (beat < COUNT_IN) {
        snprintf(buf, sizeof(buf), "%s", Str::RHYTHM_COUNT[beat]);
      } else if (beat < COUNT_IN + BEATS) {
        snprintf(buf, sizeof(buf), "%u / %u  %s", (unsigned)(beat - COUNT_IN + 1), (unsigned)BEATS,
                 rhythmFeedback_ ? rhythmFeedback_ : "");
        rhythmFeedback_ = nullptr;
      } else {
        buf[0] = '\0';
      }
      if (beat != 0xFE) {
        display_.gfx().fillRect(0, 34, 240, 26, Theme::BG);
        display_.utf8Centered(34, buf, 2, Theme::TIME, Theme::BG);
      }
    }
    // 結束
    if (since > (int32_t)((COUNT_IN + BEATS) * BEAT_MS + 600)) {
      rhythmDone_ = true;
      sound_.setStreaming(false);
      char buf[48];
      snprintf(buf, sizeof(buf), Str::RHYTHM_RESULT, (unsigned)rhythmHits_, (unsigned)BEATS);
      display_.gfx().fillRect(0, 34, 240, 26, Theme::BG);
      display_.utf8Centered(34, buf, 2, Theme::TIME, Theme::BG);
      const char *r = rhythmHits_ >= BEATS * 7 / 8 ? Str::RHYTHM_R_HIGH
                      : rhythmHits_ >= BEATS / 2   ? Str::RHYTHM_R_MID
                                                   : Str::RHYTHM_R_LOW;
      drawRhythmTrack(now);
      display_.utf8Centered(TRACK_Y + 6, r, 1, Theme::ACCENT, Theme::BG);
      drawKeyHints(Str::KEY_AGAIN, Str::KEY_BACK, Str::KEY_AGAIN, HINTBAR_Y);
      if (rhythmHits_ >= BEATS / 2) {
        pet_.play(AnimId::Happy, now);
        sound_.play(Sfx::CryHappy, true);
      }
      Serial.printf("[DeskPet] rhythm: %u/%u\n", rhythmHits_, BEATS);
      return;
    }
    if (now - rhythmLastDraw_ >= 33) {  // 約 30fps
      rhythmLastDraw_ = now;
      drawRhythmTrack(now);
    }
  }
}

void App::rhythmButton(const ButtonEvent &e, uint32_t now) {
  if (e.type == Press::Long || e.btn == Btn::Pwr) {
    enter(Screen::Main, now);
  } else if (rhythmDone_) {  // 結束後左 / 右鍵 = 再玩一次
    display_.clear(Theme::BG);
    display_.drawRgbScaled(scene_.pixels(), Scene::W, Scene::H, 0, PET_Y, PixelCanvas::SCALE);
    pet_.invalidate();
    startRhythm(now);
  }
  // 遊戲中的左 / 右鍵已經在「按下」時判定過了
}

// ============================================================
// 音樂盒：左 / 右 = 換曲，中 = 播放 / 停止，長按 = 返回
void App::drawMusic() {
  char buf[48];
  snprintf(buf, sizeof(buf), "%u / %u", (unsigned)(SoundManager::songCount() ? musicSong_ + 1 : 0),
           (unsigned)SoundManager::songCount());
  drawHeader(Str::MUSIC_TITLE, buf);
  display_.gfx().fillRect(0, 34, 240, 26, Theme::BG);
  display_.utf8Centered(38, musicPlaying_ ? Str::MUSIC_PLAYING : Str::MUSIC_IDLE, 1,
                        musicPlaying_ ? Theme::ACCENT : Theme::DIM, Theme::BG);
  const uint16_t c = drawCard(24, NAME_Y, 192, 26, true);
  if (SoundManager::songCount() == 0) {
    display_.utf8Centered(NAME_Y + 7, Str::MUSIC_EMPTY, 1, Theme::DIM, c, 184);
  } else {
    snprintf(buf, sizeof(buf), "‹ %s ›", SoundManager::songName(musicSong_ < SoundManager::songCount() ? musicSong_ : 0));
    display_.utf8Centered(NAME_Y, buf, 2, Theme::TIME, c, 184);
  }
  drawKeyHints(Str::KEY_PREV_SONG, Str::KEY_PLAY, Str::KEY_NEXT_SONG, HINTBAR_Y);
}

void App::updateMusic(uint32_t now) {
  if (!musicPlaying_) return;
  if (now - musicStartedAt_ > 500 && !sound_.busy()) {  // 播完了
    musicPlaying_ = false;
    drawMusic();
    return;
  }
  if (now - musicLastDance_ > 900) {  // 跟著音樂跳
    musicLastDance_ = now;
    pet_.play(AnimId::Happy, now);
  }
}

void App::musicButton(const ButtonEvent &e, uint32_t now) {
  const uint8_t n = SoundManager::songCount();
  if (e.type == Press::Long) {
    enter(Screen::Main, now);
    return;
  }
  if (n == 0) return;  // 沒有歌曲 (SD 卡 music/ 是空的)
  if (musicSong_ >= n) musicSong_ = 0;
  if (e.btn == Btn::Pwr) {
    musicPlaying_ = !musicPlaying_;
    if (musicPlaying_) {
      sound_.playSong(musicSong_);  // 音樂盒是主動播放，不受「音效」開關影響
      musicStartedAt_ = musicLastDance_ = now;
    } else {
      sound_.stop();
    }
  } else {
    musicSong_ = e.btn == Btn::Plus ? (musicSong_ + 1) % n : (musicSong_ + n - 1) % n;
    if (musicPlaying_) {
      sound_.playSong(musicSong_);
      musicStartedAt_ = now;
    }
  }
  drawMusic();
}

// ============================================================
// 寵物叫聲：醒著、在主畫面時，每 10~25 分鐘「咿～布！」一聲
void App::updateCry(uint32_t now) {
  if (nextCry_ == 0) nextCry_ = now + random(CRY_MIN_MS, CRY_MAX_MS);
  if ((int32_t)(now - nextCry_) < 0) return;
  nextCry_ = now + random(CRY_MIN_MS, CRY_MAX_MS);
  if (!settings_.get().soundOn || screen_ != Screen::Main || sleep_ != SleepKind::Awake || popupActive()) return;
  sfx(Sfx::Cry, (int8_t)random(-8, 10));
  pet_.play(AnimId::Happy, now);
}

// ============================================================
// 相框：SD 卡 /deskpet/photos/*.565 (240x240 RGB565，tools/sdcard.py photo 產生)
// 左 / 右 = 上一張 / 下一張，中鍵或長按 = 返回；每 10 秒自動換下一張
void App::startPhoto(uint32_t now) {
  photoCount_ = 0;
  if (SdStore::ok() && photoPaths == nullptr) photoPaths = allocLarge<char[96]>(MAX_PHOTOS);
  if (SdStore::ok() && photoPaths != nullptr) {
    char dir[32];
    snprintf(dir, sizeof(dir), "%s/photos", SdStore::ROOT);
    photoCount_ = SdStore::list(dir, ".565", photoPaths, MAX_PHOTOS);
  }
  if (photoIdx_ >= photoCount_) photoIdx_ = 0;
  drawPhoto(now);
}

void App::drawPhoto(uint32_t now) {
  photoShownAt_ = now;
  if (photoCount_ > 0 && photoBuf_ == nullptr) photoBuf_ = allocLarge<uint16_t>(240 * 240);
  bool ok = false;
  if (photoCount_ > 0 && photoBuf_ != nullptr) {
    File file = SdStore::fs().open(photoPaths[photoIdx_], FILE_READ);
    if (file && file.size() == PHOTO_BYTES) ok = file.read((uint8_t *)photoBuf_, PHOTO_BYTES) == PHOTO_BYTES;
    if (file) file.close();
  }
  if (ok) {
    display_.gfx().draw16bitRGBBitmap(0, 0, photoBuf_, 240, 240);
    return;
  }
  display_.clear(Theme::BG);
  drawHeader(Str::MENU_ITEMS[5], nullptr);
  display_.utf8Centered(96, SdStore::ok() ? Str::PHOTO_EMPTY : Str::PHOTO_NO_SD, 2, Theme::TEXT, Theme::BG);
  display_.utf8Centered(136, Str::PHOTO_HOWTO, 1, Theme::DIM, Theme::BG);
  drawKeyHints(nullptr, Str::KEY_BACK, nullptr, HINTBAR_Y);
}

void App::updatePhoto(uint32_t now) {
  if (photoCount_ > 1 && (int32_t)(now - photoShownAt_) >= (int32_t)PHOTO_SLIDE_MS) {
    photoIdx_ = (photoIdx_ + 1) % photoCount_;
    drawPhoto(now);
  }
}

void App::photoButton(const ButtonEvent &e, uint32_t now) {
  if (e.type == Press::Long || e.btn == Btn::Pwr || photoCount_ == 0) {
    enter(Screen::Main, now);
    return;
  }
  photoIdx_ = e.btn == Btn::Plus ? (photoIdx_ + 1) % photoCount_ : (photoIdx_ + photoCount_ - 1) % photoCount_;
  drawPhoto(now);
}
