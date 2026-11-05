#pragma once
#include <stdint.h>

constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

// 柔和復古配色
namespace Theme {
constexpr uint16_t BG = rgb565(36, 48, 74);
constexpr uint16_t PANEL = rgb565(58, 74, 110);
constexpr uint16_t TEXT = rgb565(247, 241, 227);
constexpr uint16_t TIME = rgb565(255, 214, 120);
constexpr uint16_t DIM = rgb565(140, 152, 186);
constexpr uint16_t ACCENT = rgb565(140, 230, 210);
// 選單介面
constexpr uint16_t HEADER = rgb565(26, 35, 58);       // 標題列 / 提示列
constexpr uint16_t ACCENT_DK = rgb565(70, 130, 130);  // 標題列下緣細線
constexpr uint16_t CARD = rgb565(46, 60, 94);         // 卡片
constexpr uint16_t CARD_SEL = rgb565(68, 88, 136);    // 選取中的卡片
constexpr uint16_t TOGGLE_OFF = rgb565(92, 102, 134); // 開關關閉
constexpr uint16_t DIM_TEXT = rgb565(196, 202, 222);  // 未選取的標籤
constexpr uint16_t WARN = rgb565(236, 96, 110);
}  // namespace Theme
