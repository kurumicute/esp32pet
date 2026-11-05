#pragma once
#include <stdint.h>

// 無聲番茄鐘：專注 → 自動休息 → 結束。只靠 millis() 計時，不阻塞。
class FocusTimer {
 public:
  enum class Phase : uint8_t { Idle, Focus, Break };
  enum class Event : uint8_t { None, FocusDone, BreakDone };

  void configure(uint8_t focusMin, uint8_t breakMin);
  void toggle(uint32_t now);  // 開始 / 暫停 / 繼續
  void stop();
  Event update(uint32_t now);

  Phase phase() const { return phase_; }
  bool running() const { return phase_ != Phase::Idle && !paused_; }
  bool active() const { return phase_ != Phase::Idle; }
  bool paused() const { return paused_; }
  uint32_t remainingSec(uint32_t now) const;
  uint8_t focusMin() const { return focusMin_; }

 private:
  void startPhase(Phase p, uint32_t now);

  uint8_t focusMin_ = 25;
  uint8_t breakMin_ = 5;
  Phase phase_ = Phase::Idle;
  bool paused_ = false;
  uint32_t endAt_ = 0;
  uint32_t pausedLeftMs_ = 0;
};
