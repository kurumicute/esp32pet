#pragma once
#include <stdint.h>

#include "../assets/SpriteTypes.h"

// 角色清單 = 韌體內建的角色 (PET_FRAMES) + 開機時從 SD 卡 /deskpet/chars/*.dpc 載入的角色。
// 角色編號：內建在前 (0 ~ 內建數-1)，SD 卡角色依檔名排序接在後面。
//
// .dpc 格式 (tools/sdcard.py char 產生，全部 little-endian)：
//   "DPC1" | version u8 (=1) | voice u8 (低 7 bit = 內建叫聲 0~3；最高位 0x80 = 不穿裝扮)
//   | nameLen u8 | name (UTF-8) | frameCount u8 (= PF_COUNT，順序同 PetFrameId)
//   每格：[補 0 讓 pal 從偶數位址開始] w u8 | h u8 | palCount u8 (含索引 0 透明) | pal u16 x palCount | px u8 x (w*h)
//         | head_top x,y i8 | eyes x,y i8 | neck x,y i8 | eyesOpen u8
namespace Characters {
constexpr uint8_t MAX = 16;

void loadFromSd();    // SdStore::begin() 之後呼叫一次
void reloadFromSd();  // 網頁上傳 / 刪除角色後重新載入 (在主迴圈呼叫，不能在畫面繪製途中)
uint8_t count();
uint8_t builtinCount();
const char *name(uint8_t c);     // 顯示名稱
const char *folder(uint8_t c);   // 資料夾 / 檔名 (SD 音效資料夾 /deskpet/sound/<folder>/)
uint8_t synthVoice(uint8_t c);   // 用哪一組內建合成叫聲
bool wearsCostumes(uint8_t c);   // false = 不疊裝扮、圍巾 (.dpc 檔頭標記)
const PetFrameAsset &frame(uint8_t c, uint8_t pf);
}  // namespace Characters
