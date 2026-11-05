#pragma once
#include <stdint.h>

// 調色盤索引圖：px[] 每像素 1 byte，索引 0 = 透明，pal[] 為 RGB565
struct Sprite {
  uint8_t w;
  uint8_t h;
  const uint16_t *pal;
  const uint8_t *px;
};

struct Point8 {
  int8_t x;
  int8_t y;
};

// 寵物錨點編號 (與 tools/png2c.py 的 ANCHOR_IDS 一致)
enum AnchorId : uint8_t { ANCHOR_HEAD_TOP = 0, ANCHOR_EYES = 1, ANCHOR_NECK = 2, ANCHOR_COUNT = 3 };

// 一張寵物影格 + 裝扮對齊用的錨點 (座標皆為 64x64 影格內的像素)
struct PetFrameAsset {
  const Sprite *sprite;
  Point8 anchors[ANCHOR_COUNT];
  bool eyesOpen;  // 閉眼影格不畫「特殊眼睛」類裝扮
};

// 一個裝扮圖層：畫在 anchor 錨點 + (offX, offY) 的位置
struct AccessoryLayer {
  const Sprite *sprite;
  uint8_t anchor;
  int8_t offX;
  int8_t offY;
  bool eyesOpenOnly;
};
