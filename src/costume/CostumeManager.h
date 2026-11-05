#pragma once
#include <stdint.h>

#include "../assets/SpriteTypes.h"

// 一套裝扮 = 畫在寵物後方的圖層 (可無) + 畫在寵物前方的圖層 (可無)
// 新增裝扮：在 assets/accessories/ 加圖、執行 tools/png2c.py，再到 CostumeManager.cpp 的表格加一行。
struct Costume {
  const char *name;
  const AccessoryLayer *back;
  const AccessoryLayer *front;
};

// 自動換裝會用到的裝扮編號
enum class CostumeId : uint8_t {
  Original, Bow, Crown, Glasses, LeafHat, SantaHat, Nightcap, Cape, StarEyes, Count
};

class CostumeManager {
 public:
  uint8_t count() const;
  const Costume &get(uint8_t index) const;
  uint8_t current() const { return current_; }
  void setCurrent(uint8_t index);

 private:
  uint8_t current_ = 0;
};
