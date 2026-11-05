#include "DisplayManager.h"

#include "../assets/Font12.h"
#include "../config/BoardConfig.h"
#include "LargeAlloc.h"

namespace {
// UTF-8 文字緩衝區：最寬 240px、最高 2 倍字 (26px)
constexpr int16_t TEXT_MAX_SCALE = 2;
constexpr size_t TEXT_BUF_PX = DisplayManager::WIDTH * FONT12_HEIGHT * TEXT_MAX_SCALE;
uint16_t *textBuf = nullptr;

// 解出下一個 UTF-8 字元；遇到不合法序列回傳 '?'
uint32_t nextCodepoint(const char *&p) {
  const uint8_t c = (uint8_t)*p++;
  if (c < 0x80) return c;
  int extra = 0;
  uint32_t cp = 0;
  if ((c & 0xE0) == 0xC0) { extra = 1; cp = c & 0x1F; }
  else if ((c & 0xF0) == 0xE0) { extra = 2; cp = c & 0x0F; }
  else if ((c & 0xF8) == 0xF0) { extra = 3; cp = c & 0x07; }
  else return '?';
  while (extra-- > 0) {
    const uint8_t n = (uint8_t)*p;
    if ((n & 0xC0) != 0x80) return '?';
    cp = (cp << 6) | (n & 0x3F);
    p++;
  }
  return cp;
}

constexpr int GLYPH_MISSING = -1;     // 沒收錄的字 → 畫方框 (例如 emoji)
constexpr int GLYPH_ZERO_WIDTH = -2;  // 不佔位置的控制字元 → 略過

int findGlyph(uint32_t cp) {
  // emoji 變體選擇符、零寬連接字、BOM 等：不顯示
  if ((cp >= 0xFE00 && cp <= 0xFE0F) || cp == 0x200B || cp == 0x200C || cp == 0x200D ||
      cp == 0xFEFF || (cp >= 0x1F3FB && cp <= 0x1F3FF) || cp < 0x20)
    return GLYPH_ZERO_WIDTH;
  if (cp > 0xFFFF) return GLYPH_MISSING;
  int lo = 0, hi = FONT12_COUNT - 1;
  while (lo <= hi) {
    const int mid = (lo + hi) / 2;
    if (FONT12_CODES[mid] == cp) return mid;
    if (FONT12_CODES[mid] < cp) lo = mid + 1;
    else hi = mid - 1;
  }
  return GLYPH_MISSING;
}

constexpr uint8_t MISSING_W = 8;  // 沒收錄的字畫成方框
uint8_t glyphWidth(int g) {
  if (g == GLYPH_ZERO_WIDTH) return 0;
  return g >= 0 ? FONT12_WIDTHS[g] : MISSING_W;
}
// 靜態物件，不使用 new
Arduino_ESP32SPI bus(Pins::LCD_DC, Pins::LCD_CS, Pins::LCD_SCK, Pins::LCD_MOSI, GFX_NOT_DEFINED);
Arduino_ST7789 panel(&bus, Pins::LCD_RST, 0 /*rotation*/, true /*IPS*/, 240, 240);

constexpr uint32_t BL_FREQ = 5000;
constexpr uint8_t BL_RES_BITS = 10;
}  // namespace

// ---------------- PixelCanvas ----------------
bool PixelCanvas::begin() {
  if (buf_ == nullptr) buf_ = allocLarge<uint16_t>((size_t)W * H);
  return buf_ != nullptr;
}

void PixelCanvas::fill(uint16_t color) {
  if (buf_ == nullptr) return;
  for (int32_t i = 0; i < W * H; i++) buf_[i] = color;
}

void PixelCanvas::blit(const Sprite &s, int16_t lx, int16_t ly) {
  if (buf_ == nullptr) return;
  for (int16_t sy = 0; sy < s.h; sy++) {
    const int16_t y = ly + sy;
    if (y < 0 || y >= LH) continue;
    const uint8_t *row = s.px + sy * s.w;
    for (int16_t sx = 0; sx < s.w; sx++) {
      const uint8_t idx = row[sx];
      if (idx == 0) continue;
      const int16_t x = lx + sx;
      if (x < 0 || x >= LW) continue;
      const uint16_t c = s.pal[idx];
      uint16_t *p = buf_ + (y * SCALE) * W + x * SCALE;
      for (int16_t dy = 0; dy < SCALE; dy++, p += W)
        for (int16_t dx = 0; dx < SCALE; dx++) p[dx] = c;
    }
  }
}

void PixelCanvas::fillRect(int16_t lx, int16_t ly, int16_t lw, int16_t lh, uint16_t color) {
  if (buf_ == nullptr) return;
  for (int16_t y = ly; y < ly + lh; y++) {
    if (y < 0 || y >= LH) continue;
    for (int16_t x = lx; x < lx + lw; x++) {
      if (x < 0 || x >= LW) continue;
      uint16_t *p = buf_ + (y * SCALE) * W + x * SCALE;
      for (int16_t dy = 0; dy < SCALE; dy++, p += W)
        for (int16_t dx = 0; dx < SCALE; dx++) p[dx] = color;
    }
  }
}

void PixelCanvas::copyFrom(const uint16_t *src, int16_t srcW, int16_t srcH, int16_t offX,
                           int16_t offY) {
  if (buf_ == nullptr || src == nullptr) return;
  for (int16_t y = 0; y < LH; y++) {
    const int16_t sy = y + offY;
    for (int16_t x = 0; x < LW; x++) {
      const int16_t sx = x + offX;
      const uint16_t c = (sx >= 0 && sx < srcW && sy >= 0 && sy < srcH) ? src[sy * srcW + sx] : 0;
      uint16_t *p = buf_ + (y * SCALE) * W + x * SCALE;
      for (int16_t dy = 0; dy < SCALE; dy++, p += W)
        for (int16_t dx = 0; dx < SCALE; dx++) p[dx] = c;
    }
  }
}

// ---------------- DisplayManager ----------------
bool DisplayManager::begin() {
  if (textBuf == nullptr) textBuf = allocLarge<uint16_t>(TEXT_BUF_PX);
  if (textBuf == nullptr) return false;
  ledcAttach(Pins::LCD_BL, BL_FREQ, BL_RES_BITS);
  setBrightness(0);  // 初始化期間先關背光，避免看到雜訊畫面
  if (!panel.begin(LCD_SPI_HZ)) return false;
  panel.setTextWrap(false);
  return true;
}

void DisplayManager::setBrightness(uint8_t percent) {
  if (percent > 100) percent = 100;
  if (percent == brightness_) return;
  brightness_ = percent;
  ledcWrite(Pins::LCD_BL, (uint32_t)percent * ((1u << BL_RES_BITS) - 1) / 100);
}

Arduino_GFX &DisplayManager::gfx() { return panel; }

void DisplayManager::clear(uint16_t color) { panel.fillScreen(color); }

void DisplayManager::text(int16_t x, int16_t y, const char *s, uint8_t size, uint16_t fg,
                          uint16_t bg) {
  panel.setTextSize(size);
  panel.setTextColor(fg, bg);
  panel.setCursor(x, y);
  panel.print(s);
}

void DisplayManager::textCentered(int16_t y, const char *s, uint8_t size, uint16_t fg,
                                  uint16_t bg, int16_t clearW) {
  const int16_t w = textWidth(s, size);
  const int16_t x = (WIDTH - w) / 2;
  const int16_t clearX = (WIDTH - clearW) / 2;
  const int16_t h = 8 * size;
  if (x > clearX) {
    panel.fillRect(clearX, y, x - clearX, h, bg);
    panel.fillRect(x + w, y, clearX + clearW - (x + w), h, bg);
  }
  text(x, y, s, size, fg, bg);
}

void DisplayManager::drawSprite(const Sprite &s, int16_t x, int16_t y, uint8_t scale,
                                uint16_t bg) {
  for (int16_t sy = 0; sy < s.h; sy++) {
    for (int16_t sx = 0; sx < s.w; sx++) {
      const uint8_t idx = s.px[sy * s.w + sx];
      panel.fillRect(x + sx * scale, y + sy * scale, scale, scale, idx ? s.pal[idx] : bg);
    }
  }
}

int16_t DisplayManager::utf8Width(const char *s, uint8_t scale) {
  int16_t w = 0;
  while (*s) w += glyphWidth(findGlyph(nextCodepoint(s)));
  return w * scale;
}

void DisplayManager::utf8(int16_t x, int16_t y, const char *s, uint8_t scale, uint16_t fg,
                          uint16_t bg) {
  if (scale < 1) scale = 1;
  if (scale > TEXT_MAX_SCALE) scale = TEXT_MAX_SCALE;
  if (x < 0) x = 0;
  int16_t w = utf8Width(s, scale);
  if (x + w > WIDTH) w = WIDTH - x;
  if (w <= 0) return;
  const int16_t h = FONT12_HEIGHT * scale;
  for (int32_t i = 0; i < w * h; i++) textBuf[i] = bg;

  int16_t penX = 0;
  while (*s) {
    const int g = findGlyph(nextCodepoint(s));
    if (g == GLYPH_ZERO_WIDTH) continue;
    const uint8_t gw = glyphWidth(g);
    for (uint8_t gy = 0; gy < FONT12_HEIGHT; gy++) {
      uint16_t bits;
      if (g >= 0) {
        bits = FONT12_ROWS[(uint32_t)g * FONT12_HEIGHT + gy];
      } else {  // 方框
        bits = (gy == 1 || gy == FONT12_HEIGHT - 2) ? 0x7E00 : (gy > 1 && gy < FONT12_HEIGHT - 2 ? 0x4200 : 0);
      }
      for (uint8_t gx = 0; gx < gw && bits; gx++, bits <<= 1) {
        if (!(bits & 0x8000)) continue;
        const int16_t px = penX + gx * scale;
        for (uint8_t sy = 0; sy < scale; sy++)
          for (uint8_t sx = 0; sx < scale; sx++) {
            const int16_t xx = px + sx;
            if (xx < w) textBuf[(gy * scale + sy) * w + xx] = fg;
          }
      }
    }
    penX += gw * scale;
    if (penX >= w) break;
  }
  panel.draw16bitRGBBitmap(x, y, textBuf, w, h);
}

uint8_t DisplayManager::utf8Wrap(int16_t x, int16_t y, int16_t maxW, const char *s,
                                 uint8_t scale, uint8_t maxLines, uint16_t fg, uint16_t bg) {
  const int16_t lineH = FONT12_HEIGHT * scale + 1;
  const int16_t ellipsisW = utf8Width("…", scale);
  char line[200];
  uint8_t used = 0;
  while (*s && used < maxLines) {
    const bool last = used == maxLines - 1;
    int16_t w = 0;
    const char *p = s;
    const char *cut = s;
    while (*p && *p != '\n') {
      const char *q = p;
      const int16_t gw = glyphWidth(findGlyph(nextCodepoint(q))) * scale;
      if (w + gw > maxW) break;
      w += gw;
      p = q;
      cut = p;
    }
    if (cut == s && *p && *p != '\n') {  // 一個字都放不下：至少放一個，避免無窮迴圈
      nextCodepoint(p);
      cut = p;
    }
    size_t n = (size_t)(cut - s);
    const bool more = *cut != '\0' && !(*cut == '\n' && cut[1] == '\0');
    if (last && more) {  // 最後一行還有剩：截到「放得下 …」的位置
      int16_t ww = 0;
      const char *r = s;
      const char *fit = s;
      while (r < cut) {
        const char *q = r;
        ww += glyphWidth(findGlyph(nextCodepoint(q))) * scale;
        if (ww + ellipsisW > maxW) break;
        r = q;
        fit = r;
      }
      n = (size_t)(fit - s);
    }
    if (n > sizeof(line) - 4) n = sizeof(line) - 4;
    memcpy(line, s, n);
    line[n] = '\0';
    if (last && more) strcat(line, "…");
    const int16_t lw = utf8Width(line, scale);
    if (lw > 0) utf8(x, y + used * lineH, line, scale, fg, bg);
    if (lw < maxW) panel.fillRect(x + lw, y + used * lineH, maxW - lw, lineH, bg);
    used++;
    s = cut;
    if (*s == '\n') s++;
  }
  return used;
}

void DisplayManager::utf8Centered(int16_t y, const char *s, uint8_t scale, uint16_t fg,
                                  uint16_t bg, int16_t clearW) {
  int16_t w = utf8Width(s, scale);
  if (w > WIDTH) w = WIDTH;
  const int16_t x = (WIDTH - w) / 2;
  const int16_t clearX = (WIDTH - clearW) / 2;
  const int16_t h = FONT12_HEIGHT * scale;
  if (x > clearX) {
    panel.fillRect(clearX, y, x - clearX, h, bg);
    panel.fillRect(x + w, y, clearX + clearW - (x + w), h, bg);
  }
  utf8(x, y, s, scale, fg, bg);
}

void DisplayManager::utf8Right(int16_t xRight, int16_t y, const char *s, uint8_t scale,
                               uint16_t fg, uint16_t bg) {
  utf8(xRight - utf8Width(s, scale), y, s, scale, fg, bg);
}

void DisplayManager::drawSpriteFast(const Sprite &s, int16_t x, int16_t y, uint8_t scale,
                                    uint16_t bg) {
  // 借用文字緩衝區：一次處理一列 (寬 <= 240，高 = scale)
  const int16_t w = s.w * scale;
  if (w > WIDTH || scale > FONT12_HEIGHT * TEXT_MAX_SCALE) return;
  for (int16_t sy = 0; sy < s.h; sy++) {
    const uint8_t *row = s.px + sy * s.w;
    for (int16_t sx = 0; sx < s.w; sx++) {
      const uint16_t c = row[sx] ? s.pal[row[sx]] : bg;
      for (uint8_t k = 0; k < scale; k++) textBuf[sx * scale + k] = c;
    }
    for (uint8_t r = 1; r < scale; r++) memcpy(textBuf + r * w, textBuf, w * sizeof(uint16_t));
    panel.draw16bitRGBBitmap(x, y + sy * scale, textBuf, w, scale);
  }
}

void DisplayManager::drawRgbScaled(const uint16_t *src, int16_t w, int16_t h, int16_t x,
                                   int16_t y, uint8_t scale, int16_t stride) {
  if (src == nullptr || w <= 0 || h <= 0) return;
  if (stride <= 0) stride = w;
  const int16_t sw = w * scale;
  if (sw > WIDTH || scale > FONT12_HEIGHT * TEXT_MAX_SCALE) return;
  for (int16_t sy = 0; sy < h; sy++) {
    const uint16_t *row = src + sy * stride;
    for (int16_t sx = 0; sx < w; sx++)
      for (uint8_t k = 0; k < scale; k++) textBuf[sx * scale + k] = row[sx];
    for (uint8_t r = 1; r < scale; r++) memcpy(textBuf + r * sw, textBuf, sw * sizeof(uint16_t));
    panel.draw16bitRGBBitmap(x, y + sy * scale, textBuf, sw, scale);
  }
}

void DisplayManager::invert(bool on) { panel.invertDisplay(on); }

void DisplayManager::pushCanvas(PixelCanvas &c, int16_t x, int16_t y) {
  if (c.data() == nullptr) return;
  panel.draw16bitRGBBitmap(x, y, c.data(), PixelCanvas::W, PixelCanvas::H);
}
