#pragma once
#include <stdint.h>

#include "../assets/SpriteTypes.h"

enum class AnimId : uint8_t {
  Idle,
  Blink,
  Happy,
  Sleep,
  Wake,
  LookLeft,
  LookRight,
  DressChange,
  Walk,
  Pat,
  Count
};

struct AnimFrame {
  uint8_t frame;  // PetFrameId (PF_xxx)；實際圖片依目前角色從 PET_FRAMES 取
  uint16_t ms;  // 這一格停留時間
  int8_t dy;    // 上下位移 (邏輯像素，負值往上)
};

struct Animation {
  const AnimFrame *frames;
  uint8_t count;
  bool loop;
};

// 新增動畫：在 PetAnimation.cpp 加一組影格表，並在 AnimId 加上名稱
const Animation &getAnimation(AnimId id);
