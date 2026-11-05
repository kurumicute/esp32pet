#include "Diary.h"

#include <Arduino.h>
#include <Preferences.h>
#include <stdarg.h>
#include <string.h>

#include "../ui/Strings.h"

namespace {
constexpr const char *NS = "diary";
constexpr const char *KEY = "days";
constexpr uint8_t VERSION = 1;
constexpr uint32_t SAVE_EVERY_MS = 10UL * 60UL * 1000UL;  // 最多 10 分鐘寫一次 NVS (減少磨損)

struct Blob {
  uint8_t version;
  DiaryDay days[Diary::DAYS];
};
Preferences prefs;

uint16_t addSat16(uint32_t a, uint32_t b) { return (uint16_t)(a + b > 0xFFFF ? 0xFFFF : a + b); }
uint8_t addSat8(uint32_t a, uint32_t b) { return (uint8_t)(a + b > 0xFF ? 0xFF : a + b); }

void merge(DiaryDay &into, const DiaryDay &from) {
  for (size_t i = 0; i < (size_t)DiaryStat::Count; i++) into.stat[i] = addSat8(into.stat[i], from.stat[i]);
  into.walkSec = addSat16(into.walkSec, from.walkSec);
  into.steps = addSat16(into.steps, from.steps);
  if (from.wx != 0) {
    into.wx = from.wx;
    into.minT = from.minT;
    into.maxT = from.maxT;
  }
}

// 安全地附加格式化字串 (不會超出緩衝區)
void appendf(char *buf, size_t cap, const char *fmt, ...) {
  const size_t len = strlen(buf);
  if (len + 1 >= cap) return;
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf + len, cap - len, fmt, ap);
  va_end(ap);
}
}  // namespace

void Diary::begin() {
  if (!prefs.begin(NS, true)) return;  // 第一次開機：沒有資料
  Blob b;
  const size_t n = prefs.getBytesLength(KEY);
  if (n == sizeof(Blob) && prefs.getBytes(KEY, &b, sizeof(Blob)) == sizeof(Blob) && b.version == VERSION) {
    memcpy(days_, b.days, sizeof(days_));
    for (DiaryDay &d : days_) {  // 讀回來的資料也檢查一下範圍
      if (d.wday > 6) d.wday = 0;
      if (d.wx > 7) d.wx = 0;
    }
  }
  prefs.end();
}

void Diary::save() {
  if (!prefs.begin(NS, false)) return;
  Blob b;
  b.version = VERSION;
  memcpy(b.days, days_, sizeof(days_));
  prefs.putBytes(KEY, &b, sizeof(Blob));
  prefs.end();
  dirty_ = false;
  if (archive_ != nullptr) {
    archive_(days_[0]);
    if (days_[1].ymd != 0) archive_(days_[1]);  // 換日時把昨天最後的樣子也存一次
  }
}

void Diary::update(uint32_t now, const tm *t) {
  if (t != nullptr) {
    bool urgent = false;
    const uint32_t ymd = (uint32_t)(t->tm_year + 1900) * 10000UL + (uint32_t)(t->tm_mon + 1) * 100UL + t->tm_mday;
    if (days_[0].ymd != ymd) {  // 換日 (或第一次使用)：舊的往後推一格
      if (days_[0].ymd != 0) memmove(&days_[1], &days_[0], sizeof(DiaryDay) * (DAYS - 1));
      memset(&days_[0], 0, sizeof(DiaryDay));
      days_[0].ymd = ymd;
      days_[0].wday = (uint8_t)(t->tm_wday % 7);
      dirty_ = true;
      urgent = true;  // 換日馬上存，避免斷電少一天
    }
    if (!synced_) {  // 時間剛同步：把同步前的紀錄併進今天
      synced_ = true;
      merge(days_[0], pending_);
      if (pending_.costume) days_[0].costume = pending_.costume;
      memset(&pending_, 0, sizeof(pending_));
      dirty_ = true;
    }
    if (dirty_ && (urgent || lastSave_ == 0 || now - lastSave_ >= SAVE_EVERY_MS)) {
      lastSave_ = now ? now : 1;
      save();
    }
  }
}

void Diary::flush() {
  if (synced_ && dirty_) save();
}

void Diary::add(DiaryStat s) {
  if (s >= DiaryStat::Count) return;
  DiaryDay &d = cur();
  d.stat[(size_t)s] = addSat8(d.stat[(size_t)s], 1);
  dirty_ = true;
}

void Diary::addWalk(uint32_t sec, uint32_t steps) {
  DiaryDay &d = cur();
  d.walkSec = addSat16(d.walkSec, sec);
  d.steps = addSat16(d.steps, steps);
  d.stat[(size_t)DiaryStat::Walk] = addSat8(d.stat[(size_t)DiaryStat::Walk], 1);
  dirty_ = true;
}

void Diary::setWeather(uint8_t kind, int8_t minT, int8_t maxT) {
  if (kind == 0 || kind > 7) return;
  DiaryDay &d = cur();
  if (d.wx == kind && d.minT == minT && d.maxT == maxT) return;
  d.wx = kind;
  d.minT = minT;
  d.maxT = maxT;
  dirty_ = true;
}

void Diary::setCostume(uint8_t costume) {
  constexpr uint8_t n = sizeof(Str::COSTUME_NAMES) / sizeof(Str::COSTUME_NAMES[0]);
  if (costume >= n) return;
  DiaryDay &d = cur();
  if (d.costume == costume) return;
  d.costume = costume;
  dirty_ = true;
}

uint8_t Diary::count() const {
  uint8_t n = 1;
  while (n < DAYS && days_[n].ymd != 0) n++;
  return n;
}

const DiaryDay &Diary::day(uint8_t i) const {
  if (i == 0 && !synced_) return pending_;
  return days_[i < DAYS ? i : 0];
}

uint8_t Diary::compose(const DiaryDay &d, bool today, char out[][LINE_LEN], uint8_t maxLines) {
  uint8_t n = 0;
  auto line = [&]() -> char * {
    if (n >= maxLines) return nullptr;
    out[n][0] = '\0';
    return out[n++];
  };
  const uint8_t *st = d.stat;
  auto S = [&](DiaryStat s) -> unsigned { return st[(size_t)s]; };
  char *l;

  if (d.wx != 0 && (l = line())) {
    snprintf(l, LINE_LEN, Str::DIARY_WEATHER, Str::WEATHER_NAMES[d.wx], d.minT, d.maxT, Str::DEG_C);
    appendf(l, LINE_LEN, "%s", d.maxT >= 32 ? Str::DIARY_HOT : d.minT <= 15 ? Str::DIARY_COLD : "。");
  }
  if (S(DiaryStat::Walk) && (l = line()))
    snprintf(l, LINE_LEN, Str::DIARY_WALK, S(DiaryStat::Walk), (unsigned)((d.walkSec + 59) / 60), (unsigned)d.steps);
  if ((S(DiaryStat::Flower) || S(DiaryStat::Butterfly)) && (l = line())) {
    snprintf(l, LINE_LEN, "%s", Str::DIARY_ON_ROAD);
    if (S(DiaryStat::Flower)) appendf(l, LINE_LEN, Str::DIARY_FLOWER, S(DiaryStat::Flower));
    if (S(DiaryStat::Flower) && S(DiaryStat::Butterfly)) appendf(l, LINE_LEN, "，");
    if (S(DiaryStat::Butterfly)) appendf(l, LINE_LEN, Str::DIARY_BUTTERFLY, S(DiaryStat::Butterfly));
    appendf(l, LINE_LEN, "。");
  }
  if (S(DiaryStat::Pet) && (l = line())) snprintf(l, LINE_LEN, Str::DIARY_PET, S(DiaryStat::Pet));
  if (S(DiaryStat::Shake) && (l = line())) snprintf(l, LINE_LEN, Str::DIARY_SHAKE, S(DiaryStat::Shake));
  if (S(DiaryStat::Focus) && (l = line())) snprintf(l, LINE_LEN, Str::DIARY_FOCUS, S(DiaryStat::Focus));
  if (S(DiaryStat::Notify) && (l = line())) snprintf(l, LINE_LEN, Str::DIARY_NOTIFY, S(DiaryStat::Notify));
  if (S(DiaryStat::Nap) && (l = line())) snprintf(l, LINE_LEN, Str::DIARY_NAP, S(DiaryStat::Nap));
  constexpr uint8_t costumes = sizeof(Str::COSTUME_NAMES) / sizeof(Str::COSTUME_NAMES[0]);
  const char *cos = Str::COSTUME_NAMES[d.costume < costumes ? d.costume : 0];
  if (S(DiaryStat::Dress) && (l = line())) snprintf(l, LINE_LEN, Str::DIARY_DRESS, S(DiaryStat::Dress), cos);
  else if (d.costume != 0 && (l = line())) snprintf(l, LINE_LEN, Str::DIARY_WEAR, cos);

  const unsigned score = S(DiaryStat::Pet) + S(DiaryStat::Shake) + S(DiaryStat::Walk) * 3 +
                         S(DiaryStat::Focus) * 2 + S(DiaryStat::Flower) + S(DiaryStat::Butterfly);
  if (n == 0 || (n == 1 && d.wx != 0 && score == 0)) {
    if ((l = line())) snprintf(l, LINE_LEN, "%s", today ? Str::DIARY_EMPTY_TODAY : Str::DIARY_EMPTY);
  } else if ((l = line())) {
    snprintf(l, LINE_LEN, "%s", score >= 12 ? Str::DIARY_GREAT : score >= 4 ? Str::DIARY_GOOD : Str::DIARY_QUIET);
  }
  return n;
}
