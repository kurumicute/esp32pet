#include "SdArchive.h"

#include <Arduino.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "SdStore.h"

namespace {
// 可以備份的設定欄位 (key 與 NVS 相同)。Wi-Fi 帳密與 API 金鑰刻意不備份。
enum : uint8_t { T_U8, T_BOOL };
struct Field {
  const char *key;
  uint8_t type;
  size_t off;
};
#define F_U8(k, f) {k, T_U8, offsetof(Settings, f)}
#define F_B(k, f) {k, T_BOOL, offsetof(Settings, f)}
const Field FIELDS[] = {
    F_U8("costume", costume),  F_U8("petChar", character), F_B("sleepOn", sleepEnabled), F_U8("sleepS", sleepStart),
    F_U8("sleepE", sleepEnd),  F_U8("bright", brightness), F_U8("nap", napMinutes),      F_B("autoCos", autoCostume),
    F_U8("bMonth", birthMonth), F_U8("bDay", birthDay),    F_U8("focus", focusMin),      F_U8("break", breakMin),
    F_U8("county", county),    F_U8("bgMode", bgMode),     F_B("wxFx", weatherFx),       F_B("tempMood", tempMood),
    F_B("hourly", hourlyFx),   F_B("tilt", tiltOn),        F_U8("shake", shakeLevel),    F_U8("pat", patLevel),
    F_B("sound", soundOn),     F_U8("vol", soundVol),      F_B("alOn", alarmOn),         F_U8("alH", alarmHour),
    F_U8("alM", alarmMin),     F_U8("alD", alarmDays),     F_U8("pwrSave", powerSave),   F_B("notify", notifyOn),
    F_B("notifyDtl", notifyDetail),
};
#undef F_U8
#undef F_B

constexpr const char *BACKUP_PATH = "/deskpet/backup/settings.txt";
}  // namespace

namespace SdArchive {

void saveDiaryDay(const DiaryDay &d) {
  if (!SdStore::ok() || d.ymd == 0) return;
  char path[64];
  snprintf(path, sizeof(path), "%s/diary/%06lu", SdStore::ROOT, (unsigned long)(d.ymd / 100));
  SdStore::ensureDir(path);
  snprintf(path, sizeof(path), "%s/diary/%06lu/%02lu.txt", SdStore::ROOT, (unsigned long)(d.ymd / 100),
           (unsigned long)(d.ymd % 100));
  File f = SdStore::fs().open(path, FILE_WRITE);  // 每次整個覆寫 (檔案很小)
  if (!f) return;
  // 統計 CSV：ymd,wday,pet,shake,nap,focus,notify,dress,walk,flower,butterfly,walkSec,steps,minT,maxT,wx,costume
  f.printf("%lu,%u", (unsigned long)d.ymd, d.wday);
  for (size_t i = 0; i < (size_t)DiaryStat::Count; i++) f.printf(",%u", d.stat[i]);
  f.printf(",%u,%u,%d,%d,%u,%u\n", d.walkSec, d.steps, d.minT, d.maxT, d.wx, d.costume);
  static char lines[Diary::MAX_LINES][Diary::LINE_LEN];
  const uint8_t n = Diary::compose(d, false, lines, Diary::MAX_LINES);
  for (uint8_t i = 0; i < n; i++) f.printf("%s\n", lines[i]);
  f.close();
}

void appendLog(const tm &t, const LogRow &r) {
  if (!SdStore::ok()) return;
  char path[48];
  snprintf(path, sizeof(path), "%s/log/%04d%02d%02d.csv", SdStore::ROOT, t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
  const bool fresh = !SdStore::fs().exists(path);
  File f = SdStore::fs().open(path, FILE_APPEND);
  if (!f) return;
  if (fresh) f.print("time,battery,voltage,charging,rssi,pets,shakes,character,wx,minT,maxT\n");
  f.printf("%02d:%02d,%u,%.2f,%u,%d,%u,%u,%u,%u,%d,%d\n", t.tm_hour, t.tm_min, r.batteryPct, r.voltage,
           r.charging ? 1 : 0, r.rssi, r.pets, r.shakes, r.character, r.wx, r.minT, r.maxT);
  f.close();
}

bool backupSettings(const Settings &s) {
  if (!SdStore::ok()) return false;
  File f = SdStore::fs().open(BACKUP_PATH, FILE_WRITE);
  if (!f) return false;
  f.print("# DeskPet settings backup (no Wi-Fi password / API key)\n");
  const uint8_t *base = (const uint8_t *)&s;
  for (const Field &fd : FIELDS) {
    const unsigned v = fd.type == T_BOOL ? (*(const bool *)(base + fd.off) ? 1u : 0u) : *(base + fd.off);
    f.printf("%s=%u\n", fd.key, v);
  }
  f.close();
  return true;
}

bool restoreSettings(Settings &s) {
  if (!SdStore::ok()) return false;
  File f = SdStore::fs().open(BACKUP_PATH, FILE_READ);
  if (!f) return false;
  uint8_t *base = (uint8_t *)&s;
  uint8_t applied = 0;
  char line[48];
  while (f.available()) {
    const size_t n = f.readBytesUntil('\n', line, sizeof(line) - 1);
    line[n] = '\0';
    if (n > 0 && line[n - 1] == '\r') line[n - 1] = '\0';
    char *eq = strchr(line, '=');
    if (line[0] == '#' || eq == nullptr) continue;
    *eq = '\0';
    char *end = nullptr;
    const long v = strtol(eq + 1, &end, 10);
    if (end == eq + 1 || *end != '\0' || v < 0 || v > 255) continue;  // 格式不對就略過那一行
    for (const Field &fd : FIELDS) {
      if (strcmp(fd.key, line) != 0) continue;
      if (fd.type == T_BOOL) *(bool *)(base + fd.off) = v != 0;
      else *(base + fd.off) = (uint8_t)v;
      applied++;
    }
  }
  f.close();
  return applied > 0;
}

}  // namespace SdArchive
