#pragma once
#include <stdint.h>
#include <time.h>

#include "../diary/Diary.h"
#include "../settings/SettingsManager.h"

// SD 卡上的長期資料：日記封存、紀錄 (CSV)、設定備份。沒插卡時全部直接略過。
namespace SdArchive {
// 日記：/deskpet/diary/YYYYMM/DD.txt (第一行 = 統計 CSV，後面 = 當天的日記句子)
void saveDiaryDay(const DiaryDay &d);
// 紀錄：/deskpet/log/YYYYMMDD.csv，每 10 分鐘一行
struct LogRow {
  uint8_t batteryPct;
  float voltage;
  bool charging;
  int8_t rssi;  // 0 = 沒連 Wi-Fi
  uint8_t pets, shakes;  // 今天累計
  uint8_t character;
  uint8_t wx;
  int8_t minT, maxT;
};
void appendLog(const tm &t, const LogRow &r);
// 設定備份：/deskpet/backup/settings.txt (key=value，不含 Wi-Fi 密碼與 API 金鑰)
bool backupSettings(const Settings &s);
bool restoreSettings(Settings &s);  // 讀檔覆蓋 s 裡有寫到的欄位 (之後會 sanitize)
}  // namespace SdArchive
