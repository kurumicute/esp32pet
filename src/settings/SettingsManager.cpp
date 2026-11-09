#include "SettingsManager.h"

#include <Preferences.h>
#include <string.h>

#include "../pet/Characters.h"

namespace {
constexpr const char *NS = "deskpet";
Preferences prefs;

void copyStr(char *dst, size_t cap, const char *src) {
  strncpy(dst, src ? src : "", cap - 1);
  dst[cap - 1] = '\0';
}
}  // namespace

void SettingsManager::sanitize(Settings &s) {
  if (s.character >= Characters::MAX) s.character = 0;
  s.sleepStart %= 24;
  s.sleepEnd %= 24;
  if (s.brightness < 10 || s.brightness > 100) s.brightness = 80;
  if (s.napMinutes > 60) s.napMinutes = 3;
  if (s.birthMonth > 12 || s.birthDay > 31 || (s.birthMonth == 0) != (s.birthDay == 0)) {
    s.birthMonth = s.birthDay = 0;
  }
  if (s.focusMin < 1 || s.focusMin > 90) s.focusMin = 25;
  if (s.breakMin < 1 || s.breakMin > 30) s.breakMin = 5;
  if (s.county >= 22) s.county = 0;
  if (s.bgMode > 4) s.bgMode = 0;
  if (s.shakeLevel > 2) s.shakeLevel = 1;
  if (s.patLevel > 3) s.patLevel = 2;
  if (s.soundVol > 2) s.soundVol = 1;
  if (s.alarmHour > 23) s.alarmHour = 7;
  if (s.alarmMin > 59) s.alarmMin = 0;
  if (s.alarmDays > 2) s.alarmDays = 0;
  if (s.powerSave > 2) s.powerSave = 1;
}

void SettingsManager::load() {
  if (!prefs.begin(NS, true)) {  // 首次開機尚無資料，使用預設值
    saved_ = cur_;
    return;
  }
  cur_.costume = prefs.getUChar("costume", cur_.costume);
  cur_.character = prefs.getUChar("petChar", cur_.character);
  cur_.sleepEnabled = prefs.getBool("sleepOn", cur_.sleepEnabled);
  cur_.sleepStart = prefs.getUChar("sleepS", cur_.sleepStart);
  cur_.sleepEnd = prefs.getUChar("sleepE", cur_.sleepEnd);
  cur_.brightness = prefs.getUChar("bright", cur_.brightness);
  cur_.napMinutes = prefs.getUChar("nap", cur_.napMinutes);
  cur_.autoCostume = prefs.getBool("autoCos", cur_.autoCostume);
  cur_.birthMonth = prefs.getUChar("bMonth", cur_.birthMonth);
  cur_.birthDay = prefs.getUChar("bDay", cur_.birthDay);
  cur_.focusMin = prefs.getUChar("focus", cur_.focusMin);
  cur_.breakMin = prefs.getUChar("break", cur_.breakMin);
  prefs.getString("ssid", cur_.ssid, sizeof(cur_.ssid));
  prefs.getString("pass", cur_.pass, sizeof(cur_.pass));
  cur_.county = prefs.getUChar("county", cur_.county);
  cur_.bgMode = prefs.getUChar("bgMode", cur_.bgMode);
  cur_.weatherFx = prefs.getBool("wxFx", cur_.weatherFx);
  cur_.tempMood = prefs.getBool("tempMood", cur_.tempMood);
  cur_.hourlyFx = prefs.getBool("hourly", cur_.hourlyFx);
  cur_.tiltOn = prefs.getBool("tilt", cur_.tiltOn);
  cur_.shakeLevel = prefs.getUChar("shake", cur_.shakeLevel);
  cur_.patLevel = prefs.getUChar("pat", cur_.patLevel);
  cur_.soundOn = prefs.getBool("sound", cur_.soundOn);
  cur_.soundVol = prefs.getUChar("vol", cur_.soundVol);
  cur_.alarmOn = prefs.getBool("alOn", cur_.alarmOn);
  cur_.alarmHour = prefs.getUChar("alH", cur_.alarmHour);
  cur_.alarmMin = prefs.getUChar("alM", cur_.alarmMin);
  cur_.alarmDays = prefs.getUChar("alD", cur_.alarmDays);
  cur_.powerSave = prefs.getUChar("pwrSave", cur_.powerSave);
  prefs.getString("cwaKey", cur_.cwaKey, sizeof(cur_.cwaKey));
  prefs.end();
  sanitize(cur_);
  saved_ = cur_;
}

void SettingsManager::save() {
  sanitize(cur_);
  if (!prefs.begin(NS, false)) return;
#define SAVE_U8(key, field) \
  if (cur_.field != saved_.field) prefs.putUChar(key, cur_.field)
#define SAVE_BOOL(key, field) \
  if (cur_.field != saved_.field) prefs.putBool(key, cur_.field)
  SAVE_U8("costume", costume);
  SAVE_U8("petChar", character);
  SAVE_BOOL("sleepOn", sleepEnabled);
  SAVE_U8("sleepS", sleepStart);
  SAVE_U8("sleepE", sleepEnd);
  SAVE_U8("bright", brightness);
  SAVE_U8("nap", napMinutes);
  SAVE_BOOL("autoCos", autoCostume);
  SAVE_U8("bMonth", birthMonth);
  SAVE_U8("bDay", birthDay);
  SAVE_U8("focus", focusMin);
  SAVE_U8("break", breakMin);
  SAVE_U8("county", county);
  SAVE_U8("bgMode", bgMode);
  SAVE_BOOL("wxFx", weatherFx);
  SAVE_BOOL("tempMood", tempMood);
  SAVE_BOOL("hourly", hourlyFx);
  SAVE_BOOL("tilt", tiltOn);
  SAVE_U8("shake", shakeLevel);
  SAVE_U8("pat", patLevel);
  SAVE_BOOL("sound", soundOn);
  SAVE_U8("vol", soundVol);
  SAVE_BOOL("alOn", alarmOn);
  SAVE_U8("alH", alarmHour);
  SAVE_U8("alM", alarmMin);
  SAVE_U8("alD", alarmDays);
  SAVE_U8("pwrSave", powerSave);
#undef SAVE_U8
#undef SAVE_BOOL
  if (strcmp(cur_.ssid, saved_.ssid) != 0) prefs.putString("ssid", cur_.ssid);
  if (strcmp(cur_.pass, saved_.pass) != 0) prefs.putString("pass", cur_.pass);
  if (strcmp(cur_.cwaKey, saved_.cwaKey) != 0) prefs.putString("cwaKey", cur_.cwaKey);
  prefs.end();
  saved_ = cur_;
}

void SettingsManager::setWifi(const char *ssid, const char *pass) {
  copyStr(cur_.ssid, sizeof(cur_.ssid), ssid);
  copyStr(cur_.pass, sizeof(cur_.pass), pass);
  save();
}

void SettingsManager::setCwaKey(const char *key) {
  copyStr(cur_.cwaKey, sizeof(cur_.cwaKey), key);
  save();
}
