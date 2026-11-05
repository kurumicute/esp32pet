#include "WalkMode.h"

#include <Arduino.h>

#include "../assets/Sprites.h"
#include "../display/LargeAlloc.h"

namespace {
constexpr uint32_t STEP_MS = 100;        // 每 100ms 往前 1 個邏輯像素
constexpr uint32_t SNIFF_MS = 2600;      // 停下來聞花
constexpr uint32_t REST_MS = 4000;       // 坐下休息
constexpr uint32_t BUTTERFLY_MS = 7000;  // 蝴蝶陪著飛
constexpr uint32_t WING_MS = 160;
constexpr int16_t FLOWER_STOP_X = 80;  // 花走到寵物臉前面 (畫面邏輯 x) 就停下來
constexpr int16_t FLOWER_Y = 51;
// 蝴蝶只在寵物畫布範圍 (畫面邏輯 x 28~91) 內飛，兩側不用重畫天空
constexpr int16_t BFLY_CX = 58, BFLY_CY = 14;
}  // namespace

bool WalkMode::begin() {
  if (buf_ == nullptr) buf_ = allocLarge<uint16_t>((size_t)W * H);
  return buf_ != nullptr;
}

void WalkMode::start(uint32_t now) {
  state_ = State::Walking;
  startedAt_ = lastStep_ = now;
  nextEvent_ = now + random(5000, 10000);
  butterflyUntil_ = now;
  steps_ = 0;
  flowerX_ = -1;
  flowerSniffed_ = false;
  wingTick_ = 0xFF;
  changed_ = true;
}

WalkMode::Event WalkMode::update(uint32_t now) {
  Event ev = Event::None;

  if (state_ != State::Walking) {
    if ((int32_t)(now - stateUntil_) >= 0) {
      state_ = State::Walking;
      lastStep_ = now;
      ev = Event::Resume;
    }
  } else if ((int32_t)(now - lastStep_) >= (int32_t)STEP_MS) {
    lastStep_ += STEP_MS;
    if (now - lastStep_ > 1000) lastStep_ = now;  // 落後太多 (例如卡住) 就不要一次衝很遠
    offset_++;
    if (offset_ % 2 == 0) steps_++;
    changed_ = true;

    if (flowerX_ >= 0) {
      const int32_t sx = flowerX_ - (int32_t)offset_;
      if (!flowerSniffed_ && sx <= FLOWER_STOP_X) {
        flowerSniffed_ = true;
        state_ = State::Sniff;
        stateUntil_ = now + SNIFF_MS;
        ev = Event::Flower;
      } else if (sx < -UI_walk_flower.w) {
        flowerX_ = -1;  // 走過去了
      }
    }

    if (ev == Event::None && flowerX_ < 0 && (int32_t)(now - nextEvent_) >= 0) {
      nextEvent_ = now + random(8000, 16000);
      const long r = random(100);
      if (r < 45) {
        flowerX_ = (int32_t)offset_ + W + 2;  // 從右邊出現，慢慢走到面前
        flowerSniffed_ = false;
      } else if (r < 80 && (int32_t)(now - butterflyUntil_) >= 0) {
        butterflyUntil_ = now + BUTTERFLY_MS;
        ev = Event::Butterfly;
      } else {
        state_ = State::Rest;
        stateUntil_ = now + REST_MS;
        ev = Event::Rest;
      }
    }
  }

  // 蝴蝶拍翅膀
  const uint8_t tick = (int32_t)(butterflyUntil_ - now) > 0 ? (uint8_t)(now / WING_MS) : 0xFF;
  if (tick != wingTick_) {
    wingTick_ = tick;
    changed_ = true;
  }
  if (ev != Event::None) changed_ = true;
  return ev;
}

void WalkMode::blit(const Sprite &s, int16_t x, int16_t y) {
  for (int16_t sy = 0; sy < s.h; sy++) {
    const int16_t yy = y + sy;
    if (yy < 0 || yy >= H) continue;
    for (int16_t sx = 0; sx < s.w; sx++) {
      const int16_t xx = x + sx;
      const uint8_t idx = s.px[sy * s.w + sx];
      if (idx == 0 || xx < 0 || xx >= W) continue;
      buf_[yy * W + xx] = s.pal[idx];
    }
  }
}

void WalkMode::compose(const uint16_t *scene) {
  if (buf_ == nullptr || scene == nullptr) return;
  // 天空與遠山不動
  memcpy(buf_, scene, (size_t)W * GROUND_Y * sizeof(uint16_t));
  // 草地往左捲 (背景圖每 120 像素無縫重複)
  const uint32_t shift = offset_ % W;
  for (int16_t y = GROUND_Y; y < H; y++) {
    const uint16_t *src = scene + y * W;
    uint16_t *dst = buf_ + y * W;
    const int16_t first = (int16_t)(W - shift);
    memcpy(dst, src + shift, first * sizeof(uint16_t));
    if (shift) memcpy(dst + first, src, shift * sizeof(uint16_t));
  }
  if (flowerX_ >= 0) blit(UI_walk_flower, (int16_t)(flowerX_ - (int32_t)offset_), FLOWER_Y);
  if (wingTick_ != 0xFF) {
    const float t = (float)wingTick_ * 0.35f;
    const int16_t x = BFLY_CX + (int16_t)(sinf(t * 0.7f) * 12.0f);
    const int16_t y = BFLY_CY + (int16_t)(cosf(t) * 4.0f);
    blit(wingTick_ % 2 ? UI_butterfly_1 : UI_butterfly_0, x, y);
  }
}
