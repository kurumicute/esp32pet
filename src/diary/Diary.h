#pragma once
#include <stddef.h>
#include <stdint.h>
#include <time.h>

// 每日小日記：自動記錄今天發生的事，存在 NVS，保留最近 7 天。
// 只記次數 (摸摸、散步、專注…)，不記通知內容等個人資料。
enum class DiaryStat : uint8_t { Pet, Shake, Nap, Focus, Notify, Dress, Walk, Flower, Butterfly, Count };

struct DiaryDay {
  uint32_t ymd;  // 例如 20261105；0 = 空白
  uint8_t wday;  // 0 = 週日
  uint8_t stat[(size_t)DiaryStat::Count];
  uint16_t walkSec;
  uint16_t steps;
  int8_t minT, maxT;
  uint8_t wx;       // WeatherKind；0 = 沒有天氣資料
  uint8_t costume;  // 今天最後穿的裝扮
};

class Diary {
 public:
  static constexpr uint8_t DAYS = 7;
  static constexpr uint8_t MAX_LINES = 10;
  static constexpr size_t LINE_LEN = 120;

  void begin();
  // 每秒呼叫：t = nullptr 代表時間還沒同步。處理換日與定期存檔
  void update(uint32_t now, const tm *t);
  void flush();  // 有變更就馬上存 (關機前)
  // 每次存檔時也呼叫 fn(今天) 與 fn(昨天)，用來封存到 SD 卡 (保留更久)
  void setArchive(void (*fn)(const DiaryDay &)) { archive_ = fn; }

  void add(DiaryStat s);
  void addWalk(uint32_t sec, uint32_t steps);
  void setWeather(uint8_t kind, int8_t minT, int8_t maxT);
  void setCostume(uint8_t costume);

  uint8_t count() const;                         // 有內容的天數 (今天一定算)
  const DiaryDay &day(uint8_t i) const;          // 0 = 今天，1 = 昨天…
  // 把一天組成幾句話；回傳句數
  static uint8_t compose(const DiaryDay &d, bool today, char out[][LINE_LEN], uint8_t maxLines);

 private:
  DiaryDay &cur() { return synced_ ? days_[0] : pending_; }
  void save();

  DiaryDay days_[DAYS] = {};
  DiaryDay pending_ = {};  // 開機後時間還沒同步前的紀錄，同步後併入今天
  bool synced_ = false;
  bool dirty_ = false;
  uint32_t lastSave_ = 0;
  void (*archive_)(const DiaryDay &) = nullptr;
};
