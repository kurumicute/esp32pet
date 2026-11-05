#pragma once
#include <FS.h>
#include <stddef.h>
#include <stdint.h>

// TF (microSD) 卡：開機掛載一次 (SDMMC 4-bit)，之後一直掛著。沒插卡時所有功能自動略過。
// 卡片上的資料都放在 /deskpet/ 底下：
//   chars/    角色 (.dpc，tools/sdcard.py 產生)
//   photos/   相框照片 (.565，240x240 RGB565)
//   music/    音樂盒歌曲 (.wav)
//   sound/    音效覆蓋 (<角色資料夾>/xxx.wav、common/xxx.wav)
//   diary/    日記封存 (YYYYMM/DD.txt)
//   log/      紀錄 (YYYYMMDD.csv)
//   backup/   設定備份 (settings.txt，不含 Wi-Fi 密碼與 API 金鑰)
//   update/   韌體更新 (firmware.bin)
namespace SdStore {
constexpr const char *ROOT = "/deskpet";

bool begin();  // 掛載並建立資料夾；沒插卡回傳 false
bool ok();
fs::FS &fs();
uint32_t totalMB();
uint32_t usedMB();
uint8_t cardType();  // CARD_NONE / CARD_MMC / CARD_SD / CARD_SDHC

// 整個檔案讀進 PSRAM (呼叫端負責 heap_caps_free)；超過 maxLen 或讀失敗回傳 nullptr
uint8_t *readAll(const char *path, size_t &len, size_t maxLen);
// 列出資料夾裡副檔名為 ext 的檔案 (完整路徑，依檔名排序)；回傳數量
uint8_t list(const char *dir, const char *ext, char paths[][96], uint8_t maxCount);
// 路徑最後的檔名 (不含副檔名) 複製到 out
void stem(const char *path, char *out, size_t cap);
bool ensureDir(const char *path);
}  // namespace SdStore
