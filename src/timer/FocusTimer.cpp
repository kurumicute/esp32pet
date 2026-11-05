#include "FocusTimer.h"

void FocusTimer::configure(uint8_t focusMin, uint8_t breakMin) {
  focusMin_ = focusMin ? focusMin : 25;
  breakMin_ = breakMin ? breakMin : 5;
}

void FocusTimer::startPhase(Phase p, uint32_t now) {
  phase_ = p;
  paused_ = false;
  endAt_ = now + (uint32_t)(p == Phase::Focus ? focusMin_ : breakMin_) * 60000UL;
}

void FocusTimer::toggle(uint32_t now) {
  if (phase_ == Phase::Idle) {
    startPhase(Phase::Focus, now);
  } else if (paused_) {
    paused_ = false;
    endAt_ = now + pausedLeftMs_;
  } else {
    paused_ = true;
    pausedLeftMs_ = (int32_t)(endAt_ - now) > 0 ? endAt_ - now : 0;
  }
}

void FocusTimer::stop() {
  phase_ = Phase::Idle;
  paused_ = false;
}

uint32_t FocusTimer::remainingSec(uint32_t now) const {
  if (phase_ == Phase::Idle) return (uint32_t)focusMin_ * 60;
  const uint32_t ms = paused_ ? pausedLeftMs_ : ((int32_t)(endAt_ - now) > 0 ? endAt_ - now : 0);
  return (ms + 999) / 1000;
}

FocusTimer::Event FocusTimer::update(uint32_t now) {
  if (phase_ == Phase::Idle || paused_ || (int32_t)(endAt_ - now) > 0) return Event::None;
  if (phase_ == Phase::Focus) {
    startPhase(Phase::Break, now);  // 專注結束自動進入休息
    return Event::FocusDone;
  }
  stop();
  return Event::BreakDone;
}
