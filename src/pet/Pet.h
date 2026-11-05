#pragma once
#include <stdint.h>

#include "../costume/CostumeManager.h"
#include "../display/DisplayManager.h"
#include "PetAnimation.h"

// 寵物狀態機：非阻塞，update() 只依 millis() 推進影格，回傳畫面是否需要重畫。
class Pet {
 public:
  void begin(const CostumeManager &costumes, uint8_t costume, uint32_t now);
  bool update(uint32_t now);
  void render(PixelCanvas &canvas, uint16_t bg, uint32_t now) const;

  void setAsleep(bool asleep, uint32_t now);
  bool isAsleep() const { return asleep_; }
  void play(AnimId id, uint32_t now);                // 醒著時播放一次性動畫
  void pat(uint32_t now);  // 摸頭：手在頭上拍幾下 + 冒愛心
  void changeCostume(uint8_t costume, uint32_t now);  // 換裝動畫 + 閃光，動畫中途換上新裝扮
  uint8_t costume() const { return costume_; }
  // 角色 (0 ~ Characters::count()-1)：setCharacter 直接換 (開機用)，changeCharacter 播換裝動畫 + 閃光
  void setCharacter(uint8_t character, uint32_t now);
  void changeCharacter(uint8_t character, uint32_t now);
  uint8_t character() const { return character_; }
  // 傾斜：-1 = 一直看左邊，1 = 一直看右邊，0 = 正常
  void setLook(int8_t dir, uint32_t now);
  // 在寵物旁邊顯示道具 (例如點心) 一段時間
  void showProp(const Sprite *prop, uint32_t durationMs, uint32_t now);
  void setSweat(bool on);  // 低電量流汗
  enum class Precip : uint8_t { None, Rain, Snow };
  void setPrecip(Precip p);  // 下雨 / 下雪：背景特效
  // 溫度表情：-1 = 冷 (圍巾、偶爾發抖)，1 = 熱 (扇子、流汗)，0 = 正常
  void setTempMood(int8_t mood);
  // 背景：RGB565 圖 (邏輯 w x h)，畫布左上角對應到 (offX, offY)
  void setScene(const uint16_t *pixels, int16_t w, int16_t h, int16_t offX, int16_t offY);
  void invalidate() { dirty_ = true; }
  // 散步模式：待機動畫改成走路 (不做隨機小動作)
  void setWalking(bool on, uint32_t now);

 private:
  void start(AnimId id, uint32_t now);
  AnimId baseAnim() const {
    return asleep_ ? AnimId::Sleep : walking_ ? AnimId::Walk : AnimId::Idle;
  }
  void scheduleRandom(uint32_t now);
  void doRandomEvent(uint32_t now);

  const CostumeManager *costumes_ = nullptr;
  uint8_t costume_ = 0;
  int16_t pendingCostume_ = -1;
  uint8_t character_ = 0;
  int16_t pendingCharacter_ = -1;

  AnimId anim_ = AnimId::Idle;
  AnimId queued_ = AnimId::Count;  // Count = 無
  uint8_t frame_ = 0;
  uint32_t frameStart_ = 0;
  uint32_t nextRandom_ = 0;

  bool asleep_ = false;
  bool walking_ = false;
  int8_t look_ = 0;
  const Sprite *prop_ = nullptr;
  uint32_t propUntil_ = 0;
  bool sweat_ = false;
  Precip precip_ = Precip::None;
  int8_t tempMood_ = 0;
  uint32_t shiverUntil_ = 0;
  const uint16_t *scene_ = nullptr;
  int16_t sceneW_ = 0, sceneH_ = 0, sceneX_ = 0, sceneY_ = 0;
  uint32_t sparkleUntil_ = 0;
  uint32_t heartStart_ = 0;
  bool hearts_ = false;
  uint8_t effectTick_ = 0xFF;
  bool dirty_ = true;
};
