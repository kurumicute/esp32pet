#pragma once
#include <stdint.h>

#include "../assets/SpriteTypes.h"

// 寵物散步模式：寵物在畫面中央原地走路，草地往左捲動 (天空、遠山不動，有遠近感)。
// 路上隨機遇到：路邊的花 (停下來聞)、蝴蝶 (在頭上飛一陣子)、休息一下。
// 只是散步時的小事件，不收集、不解鎖、沒有獎勵；次數會寫進當天的日記。
// 合成結果放在 120x68 的 RGB565 緩衝區 (PSRAM)，寵物畫布用它當背景。
class WalkMode {
 public:
  static constexpr int16_t W = 120;
  static constexpr int16_t H = 68;
  static constexpr int16_t GROUND_Y = 52;  // 這一列以下的草地會捲動
  static constexpr int16_t STRIP_Y = 50;   // 兩側需要重畫的區塊 (含路邊的花)

  enum class Event : uint8_t { None, Flower, Butterfly, Rest, Resume };

  bool begin();  // 配置緩衝區 (開機呼叫一次)
  void start(uint32_t now);
  // 每次呼叫推進狀態；回傳發生的事件。畫面有變時 changed() 為 true
  Event update(uint32_t now);
  // 用時段背景重新合成 (背景或散步畫面有變時呼叫)
  void compose(const uint16_t *scene);
  bool changed() const { return changed_; }
  void clearChanged() { changed_ = false; }
  bool moving() const { return state_ == State::Walking; }
  const uint16_t *pixels() const { return buf_; }
  uint32_t steps() const { return steps_; }
  // now 可能比 start() 時稍早 (主迴圈開頭取的時間)，用有號數比較避免變成超大值
  uint32_t elapsedSec(uint32_t now) const {
    const int32_t d = (int32_t)(now - startedAt_);
    return d > 0 ? (uint32_t)d / 1000 : 0;
  }

 private:
  enum class State : uint8_t { Walking, Sniff, Rest };
  void blit(const Sprite &s, int16_t x, int16_t y);

  uint16_t *buf_ = nullptr;
  State state_ = State::Walking;
  uint32_t startedAt_ = 0;
  uint32_t lastStep_ = 0;
  uint32_t stateUntil_ = 0;
  uint32_t nextEvent_ = 0;
  uint32_t butterflyUntil_ = 0;
  uint32_t offset_ = 0;  // 已經往前走了幾個邏輯像素
  uint32_t steps_ = 0;
  int32_t flowerX_ = -1;  // 路邊的花在世界座標的位置 (-1 = 沒有)
  bool flowerSniffed_ = false;
  uint8_t wingTick_ = 0xFF;
  bool changed_ = true;
};
