#pragma once
#include <Arduino_GFX_Library.h>

#include "../assets/SpriteTypes.h"

// 寵物區的離屏畫布：以「邏輯像素」作畫，輸出時每個邏輯像素放大成 SCALE x SCALE。
// 使用靜態緩衝區，不做動態配置；只在寵物畫面有變化時整塊推送，避免閃爍。
class PixelCanvas {
 public:
  static constexpr int16_t LW = 64;   // 邏輯寬
  static constexpr int16_t LH = 68;   // 邏輯高 (含上下浮動空間)
  static constexpr int16_t SCALE = 2;
  static constexpr int16_t W = LW * SCALE;
  static constexpr int16_t H = LH * SCALE;

  bool begin();  // 配置緩衝區 (開機呼叫一次)
  void fill(uint16_t color);
  void blit(const Sprite &s, int16_t lx, int16_t ly);
  void fillRect(int16_t lx, int16_t ly, int16_t lw, int16_t lh, uint16_t color);  // 邏輯座標
  // 用 RGB565 圖 (邏輯解析度 srcW x srcH) 的 (offX, offY) 區塊填滿整個畫布
  void copyFrom(const uint16_t *src, int16_t srcW, int16_t srcH, int16_t offX, int16_t offY);
  uint16_t *data() { return buf_; }

 private:
  uint16_t *buf_ = nullptr;
};

class DisplayManager {
 public:
  static constexpr int16_t WIDTH = 240;
  static constexpr int16_t HEIGHT = 240;

  bool begin();
  void setBrightness(uint8_t percent);
  Arduino_GFX &gfx();

  void clear(uint16_t color);
  // 內建 5x7 像素字型；有背景色，可直接覆寫舊字不閃爍
  void text(int16_t x, int16_t y, const char *s, uint8_t size, uint16_t fg, uint16_t bg);
  // 置中文字，並清掉 clearW 寬度內左右殘留的舊字
  void textCentered(int16_t y, const char *s, uint8_t size, uint16_t fg, uint16_t bg,
                    int16_t clearW = WIDTH);
  static int16_t textWidth(const char *s, uint8_t size) { return (int16_t)strlen(s) * 6 * size; }

  // UTF-8 文字 (中英文，12px 像素字型 Font12)。scale 1 = 13px 高，2 = 26px 高。
  // 會先畫進緩衝區再一次推送，背景色會覆蓋整個文字區塊
  void utf8(int16_t x, int16_t y, const char *s, uint8_t scale, uint16_t fg, uint16_t bg);
  void utf8Centered(int16_t y, const char *s, uint8_t scale, uint16_t fg, uint16_t bg,
                    int16_t clearW = WIDTH);
  void utf8Right(int16_t xRight, int16_t y, const char *s, uint8_t scale, uint16_t fg,
                 uint16_t bg);
  static int16_t utf8Width(const char *s, uint8_t scale);
  // 自動換行，超過 maxLines 時最後一行結尾加「…」；每行右側多餘處填背景色。回傳用了幾行
  uint8_t utf8Wrap(int16_t x, int16_t y, int16_t maxW, const char *s, uint8_t scale,
                   uint8_t maxLines, uint16_t fg, uint16_t bg);

  void drawSprite(const Sprite &s, int16_t x, int16_t y, uint8_t scale, uint16_t bg);
  // 大圖 (例如背景) 用：逐列組好再推送，比逐點畫快很多。透明處填 bg
  void drawSpriteFast(const Sprite &s, int16_t x, int16_t y, uint8_t scale, uint16_t bg);
  // RGB565 圖放大推送 (例如合成好的背景)
  // stride = 來源每列的像素數 (0 = 與 w 相同)，可推送大圖中的一塊
  void drawRgbScaled(const uint16_t *src, int16_t w, int16_t h, int16_t x, int16_t y, uint8_t scale,
                     int16_t stride = 0);
  void invert(bool on);
  void pushCanvas(PixelCanvas &c, int16_t x, int16_t y);

 private:
  uint8_t brightness_ = 0xFF;
};
