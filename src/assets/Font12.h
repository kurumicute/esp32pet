// 由 tools/gen_font.py 自動產生，請勿手動修改
#pragma once
#include <stdint.h>

constexpr uint8_t FONT12_HEIGHT = 13;
constexpr uint16_t FONT12_COUNT = 31741;
extern const uint16_t FONT12_CODES[];   // 由小到大排序的 Unicode 碼點 (BMP)
extern const uint8_t FONT12_WIDTHS[];   // 每個字的寬度 (像素)
extern const uint16_t FONT12_ROWS[];    // 第 i 個字在 [i*FONT12_HEIGHT]，每列 16 bit，最高位 = 最左邊
