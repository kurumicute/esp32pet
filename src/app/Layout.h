#pragma once
#include <stdint.h>

#include "../display/DisplayManager.h"

// 畫面版面常數 (240x240)，App.cpp 與 AppFun.cpp 共用
namespace Layout {
constexpr int16_t TIME_SIZE = 5;  // 內建 5x7 字型放大 5 倍 → 每字 30x40
constexpr int16_t TIME_CHAR_W = 6 * TIME_SIZE;
constexpr int16_t TIME_X = (240 - 5 * TIME_CHAR_W) / 2;
constexpr int16_t TIME_Y = 18;
constexpr int16_t PET_X = (240 - PixelCanvas::W) / 2;
constexpr int16_t PET_Y = 60;
constexpr int16_t LINE_Y = 204;  // 下方中文字列 (2 倍字，26px 高)
// 主畫面：寵物區貼齊螢幕底部 (背景天空最上面就是 Theme::BG，跟時間區自然接起來)，訊息放在時間下面的天空
constexpr int16_t MAIN_PET_Y = 240 - PixelCanvas::H;  // = 104
constexpr int16_t MAIN_LINE_Y = 68;
constexpr int16_t HINT_Y = 227;  // 最下方提示 (1 倍字，13px 高)
constexpr int16_t HEADER_H = 32;    // 選單畫面的標題列高度
constexpr int16_t HINTBAR_Y = 224;  // 選單畫面最下方的提示列
constexpr int16_t ROWS_Y = 38;      // 設定清單第一列
constexpr int16_t ROW_PITCH = 31;   // 每列間距
constexpr int16_t ROW_H = 28;       // 每列卡片高度
constexpr int16_t NAME_Y = 197;     // 換裝畫面的名稱膠囊
constexpr int16_t KEYBAR_LOW_Y = 226;  // 番茄鐘 / 散步畫面的按鍵提示列 (上方有 2 倍字)
constexpr uint8_t SLEEP_BRIGHTNESS = 4;  // 夜間睡眠亮度
}  // namespace Layout
using namespace Layout;
