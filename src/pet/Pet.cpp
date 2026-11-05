#include "Pet.h"

#include <Arduino.h>

#include "../assets/Sprites.h"
#include "Characters.h"

namespace {
constexpr int16_t PET_Y = 2;           // 影格在畫布中的基準 y (上方留浮動空間)
constexpr uint32_t SPARKLE_MS = 800;
constexpr uint32_t EFFECT_STEP_MS = 110;
constexpr uint32_t ZZZ_STEP_MS = 500;
constexpr uint32_t RAIN_STEP_MS = 90;
constexpr uint16_t RAIN_COLOR = 0x6D9F;  // 淡藍
constexpr uint16_t SNOW_COLOR = 0xFFFF;
constexpr uint32_t SHIVER_MS = 900;
constexpr uint32_t SHIVER_STEP_MS = 70;
constexpr uint8_t PAT_HAND_FRAMES = 6;   // PAT 動畫前幾格有手
constexpr uint32_t HEART_DELAY_MS = 300;
constexpr uint32_t HEART_LIFE_MS = 1100;
constexpr uint8_t HEARTS = 3;
const int8_t HEART_X[HEARTS] = {9, 48, 16};

const Sprite *const SPARKLES[] = {&UI_sparkle_0, &UI_sparkle_1, &UI_sparkle_2};
const Point8 SPARKLE_POS[] = {{4, 10}, {51, 6}, {2, 40}, {54, 36}, {27, 0}};

void drawLayer(PixelCanvas &c, const AccessoryLayer *layer, const PetFrameAsset &f, int16_t ox,
               int16_t oy) {
  if (layer == nullptr) return;
  if (layer->eyesOpenOnly && !f.eyesOpen) return;
  const Point8 a = f.anchors[layer->anchor < ANCHOR_COUNT ? layer->anchor : 0];
  c.blit(*layer->sprite, ox + a.x + layer->offX, oy + a.y + layer->offY);
}
}  // namespace

void Pet::begin(const CostumeManager &costumes, uint8_t costume, uint32_t now) {
  costumes_ = &costumes;
  costume_ = costume < costumes.count() ? costume : 0;
  randomSeed(esp_random());
  start(AnimId::Idle, now);
  scheduleRandom(now);
}

void Pet::start(AnimId id, uint32_t now) {
  anim_ = id;
  frame_ = 0;
  frameStart_ = now;
  dirty_ = true;
}

void Pet::scheduleRandom(uint32_t now) { nextRandom_ = now + random(2500, 6500); }

void Pet::doRandomEvent(uint32_t now) {
  const long r = random(100);
  if (r < 40) {
    start(AnimId::Blink, now);
  } else if (r < 55) {
    start(AnimId::Blink, now);
    queued_ = AnimId::Blink;  // 連眨兩下
  } else if (r < 70) {
    start(AnimId::LookLeft, now);
    queued_ = AnimId::Blink;
  } else if (r < 85) {
    start(AnimId::LookRight, now);
    queued_ = AnimId::Blink;
  } else {
    start(AnimId::Happy, now);
  }
  if (tempMood_ < 0 && random(100) < 35) shiverUntil_ = now + SHIVER_MS;  // 冷：偶爾發抖
}

void Pet::setAsleep(bool asleep, uint32_t now) {
  if (asleep == asleep_) return;
  asleep_ = asleep;
  queued_ = AnimId::Count;
  if (asleep) {
    start(AnimId::Sleep, now);
  } else {
    start(AnimId::Wake, now);
    scheduleRandom(now);
  }
}

void Pet::play(AnimId id, uint32_t now) {
  if (asleep_) return;
  queued_ = AnimId::Count;
  start(id, now);
  scheduleRandom(now);
}

void Pet::pat(uint32_t now) {
  if (asleep_) return;
  queued_ = AnimId::Count;
  start(AnimId::Pat, now);
  heartStart_ = now + 500;  // 摸了一下之後才開始冒愛心
  hearts_ = true;
  scheduleRandom(now);
}

void Pet::changeCostume(uint8_t costume, uint32_t now) {
  if (costumes_ == nullptr || costume >= costumes_->count()) return;
  pendingCostume_ = costume;
  sparkleUntil_ = now + SPARKLE_MS;
  queued_ = AnimId::Count;
  if (asleep_) {  // 睡覺時不打斷睡眠動畫，直接換
    costume_ = costume;
    pendingCostume_ = -1;
    dirty_ = true;
  } else {
    start(AnimId::DressChange, now);
  }
}

void Pet::setCharacter(uint8_t character, uint32_t now) {
  character_ = character < Characters::count() ? character : 0;
  pendingCharacter_ = -1;
  dirty_ = true;
  (void)now;
}

void Pet::changeCharacter(uint8_t character, uint32_t now) {
  if (character >= Characters::count()) return;
  if (character == character_ && pendingCharacter_ < 0) return;
  pendingCharacter_ = character;
  sparkleUntil_ = now + SPARKLE_MS;
  queued_ = AnimId::Count;
  if (asleep_) {  // 睡覺時不打斷睡眠動畫，直接換
    character_ = character;
    pendingCharacter_ = -1;
    dirty_ = true;
  } else {
    start(AnimId::DressChange, now);
  }
}

void Pet::setLook(int8_t dir, uint32_t now) {
  if (dir == look_) return;
  look_ = dir;
  if (asleep_) return;
  if (dir != 0) {
    queued_ = AnimId::Count;
    start(dir < 0 ? AnimId::LookLeft : AnimId::LookRight, now);
  } else if (anim_ == AnimId::LookLeft || anim_ == AnimId::LookRight) {
    start(AnimId::Idle, now);
    scheduleRandom(now);
  }
}

void Pet::setWalking(bool on, uint32_t now) {
  if (on == walking_) return;
  walking_ = on;
  if (asleep_) return;
  if (anim_ == AnimId::Idle || anim_ == AnimId::Walk) {
    queued_ = AnimId::Count;
    start(baseAnim(), now);
  }
  scheduleRandom(now);
}

void Pet::showProp(const Sprite *prop, uint32_t durationMs, uint32_t now) {
  prop_ = prop;
  propUntil_ = now + durationMs;
  dirty_ = true;
}

void Pet::setScene(const uint16_t *pixels, int16_t w, int16_t h, int16_t offX, int16_t offY) {
  scene_ = pixels;
  sceneW_ = w;
  sceneH_ = h;
  sceneX_ = offX;
  sceneY_ = offY;
  dirty_ = true;
}

void Pet::setPrecip(Precip p) {
  if (p == precip_) return;
  precip_ = p;
  dirty_ = true;
}

void Pet::setTempMood(int8_t mood) {
  if (mood == tempMood_) return;
  tempMood_ = mood;
  dirty_ = true;
}

void Pet::setSweat(bool on) {
  if (on == sweat_) return;
  sweat_ = on;
  dirty_ = true;
}

bool Pet::update(uint32_t now) {
  const Animation &a = getAnimation(anim_);
  if (now - frameStart_ >= a.frames[frame_].ms) {
    frameStart_ = now;
    dirty_ = true;
    if (++frame_ >= a.count) {
      const bool holdLook = !asleep_ && look_ != 0 &&
                            anim_ == (look_ < 0 ? AnimId::LookLeft : AnimId::LookRight);
      if (a.loop || holdLook) {  // 傾斜中就一直看著那一邊
        frame_ = 0;
      } else if (queued_ != AnimId::Count && !asleep_) {
        const AnimId next = queued_;
        queued_ = AnimId::Count;
        start(next, now);
      } else {
        start(baseAnim(), now);
      }
    }
  }

  // 換裝 / 換角色動畫進行到一半時換上新的樣子
  if (pendingCostume_ >= 0 && (anim_ != AnimId::DressChange || frame_ >= 2)) {
    costume_ = (uint8_t)pendingCostume_;
    pendingCostume_ = -1;
    dirty_ = true;
  }
  if (pendingCharacter_ >= 0 && (anim_ != AnimId::DressChange || frame_ >= 2)) {
    character_ = (uint8_t)pendingCharacter_;
    pendingCharacter_ = -1;
    dirty_ = true;
  }

  if (!asleep_ && anim_ == AnimId::Idle && look_ != 0) {
    start(look_ < 0 ? AnimId::LookLeft : AnimId::LookRight, now);
  } else if (!asleep_ && anim_ == AnimId::Idle && (int32_t)(now - nextRandom_) >= 0) {
    doRandomEvent(now);
    scheduleRandom(now);
  }

  if (prop_ != nullptr && (int32_t)(now - propUntil_) >= 0) {  // 道具時間到
    prop_ = nullptr;
    dirty_ = true;
  }

  // 閃光 / Zzz / 愛心特效的節拍
  if (hearts_ && (int32_t)(now - heartStart_) > (int32_t)(HEART_DELAY_MS * (HEARTS - 1) + HEART_LIFE_MS)) {
    hearts_ = false;
    dirty_ = true;
  }
  uint8_t tick = 0xFF;
  if (hearts_) {
    tick = (uint8_t)(now / EFFECT_STEP_MS);
  } else if ((int32_t)(sparkleUntil_ - now) > 0) {
    tick = (uint8_t)(now / EFFECT_STEP_MS);
  } else if (!asleep_ && (int32_t)(shiverUntil_ - now) > 0) {
    tick = (uint8_t)(now / SHIVER_STEP_MS);
  } else if (asleep_) {
    tick = (uint8_t)(now / ZZZ_STEP_MS);
  } else if (precip_ != Precip::None) {
    tick = (uint8_t)(now / RAIN_STEP_MS);
  }
  if (tick != effectTick_) {
    effectTick_ = tick;
    dirty_ = true;
  }

  const bool d = dirty_;
  dirty_ = false;
  return d;
}

void Pet::render(PixelCanvas &c, uint16_t bg, uint32_t now) const {
  const Animation &a = getAnimation(anim_);
  const AnimFrame &af = a.frames[frame_ < a.count ? frame_ : 0];
  const PetFrameAsset &f = Characters::frame(character_, af.frame);
  const Costume &cos = costumes_->get(costume_);
  const bool shiver = !asleep_ && (int32_t)(shiverUntil_ - now) > 0;
  const int16_t ox = shiver ? ((now / SHIVER_STEP_MS) % 2 ? 1 : -1) : 0;  // 發抖：左右抖動
  const int16_t oy = PET_Y + af.dy;

  if (scene_ != nullptr) {
    c.copyFrom(scene_, sceneW_, sceneH_, sceneX_, sceneY_);  // 背景中對應寵物區的那一塊
  } else {
    c.fill(bg);
  }
  if (precip_ != Precip::None && !asleep_) {  // 背景雨滴 / 雪花 (畫在寵物後面)
    const bool snow = precip_ == Precip::Snow;
    const uint32_t step = now / RAIN_STEP_MS;
    for (uint8_t i = 0; i < 9; i++) {
      int16_t x = (i * 23 + 5) % PixelCanvas::LW;
      const int16_t y = (int16_t)(((snow ? step : step * 2) + i * 17) % (PixelCanvas::LH + 4)) - 4;
      if (snow) x += ((step + i) / 4) % 2;  // 雪花左右飄
      c.fillRect(x, y, 1, snow ? 1 : 3, snow ? SNOW_COLOR : RAIN_COLOR);
    }
  }
  c.blit(UI_shadow, (PixelCanvas::LW - UI_shadow.w) / 2, 61);  // 影子固定不動
  const bool deco = Characters::wearsCostumes(character_);  // 貼圖角色不疊裝扮 / 圍巾
  if (deco) drawLayer(c, cos.back, f, ox, oy);
  c.blit(*f.sprite, ox, oy);
  if (deco && tempMood_ < 0) {  // 冷：圍巾圍在頸環上
    const Point8 n = f.anchors[ANCHOR_NECK];
    c.blit(UI_scarf, ox + n.x - UI_scarf.w / 2, oy + n.y - 2);
  }
  if (deco) drawLayer(c, cos.front, f, ox, oy);

  if (tempMood_ > 0 && !asleep_) c.blit(UI_fan, 3, 38);  // 熱：旁邊擺把扇子
  if (prop_ != nullptr) c.blit(*prop_, 2, 50);  // 道具放在左下角地上
  if ((sweat_ || tempMood_ > 0) && !asleep_) {  // 汗滴 (低電量或很熱)
    const Point8 h = f.anchors[ANCHOR_HEAD_TOP];
    c.blit(UI_sweat, ox + h.x + 10, oy + h.y + 2);
  }

  if (anim_ == AnimId::Pat && frame_ < PAT_HAND_FRAMES) {  // 摸頭的手：往下壓時貼著頭頂
    const Point8 h = f.anchors[ANCHOR_HEAD_TOP];
    const int16_t lift = af.dy > 0 ? 0 : 3;
    c.blit(UI_hand, ox + h.x - 5, oy + h.y - UI_hand.h + 2 - lift);
  }
  if (hearts_) {
    for (uint8_t i = 0; i < HEARTS; i++) {
      const int32_t age = (int32_t)(now - heartStart_) - (int32_t)(i * HEART_DELAY_MS);
      if (age < 0 || age >= (int32_t)HEART_LIFE_MS) continue;
      const int16_t x = HEART_X[i] + ((age / 180) % 2);  // 一邊往上飄一邊左右晃
      const int16_t y = 30 - (int16_t)(age / 45);
      c.blit(UI_heart, x, y);
    }
  }

  if ((int32_t)(sparkleUntil_ - now) > 0) {
    const uint32_t step = now / EFFECT_STEP_MS;
    for (uint8_t i = 0; i < sizeof(SPARKLE_POS) / sizeof(SPARKLE_POS[0]); i++) {
      const uint8_t phase = (step + i * 2) % 4;  // 0,1,2 = 由小到大，3 = 隱藏
      if (phase < 3) c.blit(*SPARKLES[phase], SPARKLE_POS[i].x, SPARKLE_POS[i].y);
    }
  } else if (asleep_) {
    const uint8_t n = (now / ZZZ_STEP_MS) % 4;  // 0~3 個 Z 依序冒出
    for (uint8_t k = 0; k < n; k++) c.blit(UI_zzz, 46 + k * 5, 22 - k * 7);
  }
}
