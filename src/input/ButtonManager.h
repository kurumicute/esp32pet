#pragma once
#include <stdint.h>

enum class Btn : uint8_t { Boot, Plus, Pwr, Count };
enum class Press : uint8_t { Short, Long };

struct ButtonEvent {
  Btn btn;
  Press type;
};

// 三顆實體按鍵：防抖 + 短按 (放開時觸發) / 長按 (按住達門檻時觸發一次，放開不再送短按)
class ButtonManager {
 public:
  void begin(uint32_t now);
  void update(uint32_t now);
  bool poll(ButtonEvent &out);
  // 有按鍵按著，或 ms 毫秒內剛按下 / 放開 (按按鍵會讓板子震一下，拍頭偵測要避開)
  bool busy(uint32_t now, uint32_t ms) const;
  // 按下的瞬間 (節奏遊戲用，比放開時才觸發的短按準)：t = 實際按下的時間
  bool popPress(Btn &b, uint32_t &t);
  void clearPresses() { pressHead_ = pressTail_ = 0; }

 private:
  struct State {
    int8_t pin;
    bool stable;      // true = 按下
    bool lastRaw;
    uint32_t changedAt;
    uint32_t pressedAt;
    bool longSent;
    bool ignore;      // 開機時已按住 (例如按 PWR 開機)，放開前不產生事件
  };
  void push(Btn b, Press p);

  State s_[(uint8_t)Btn::Count];
  ButtonEvent queue_[8];
  uint8_t head_ = 0;
  uint8_t tail_ = 0;
  uint32_t lastEdge_ = 0;
  struct PressEdge {
    Btn btn;
    uint32_t at;
  };
  PressEdge presses_[4];
  uint8_t pressHead_ = 0;
  uint8_t pressTail_ = 0;
};
