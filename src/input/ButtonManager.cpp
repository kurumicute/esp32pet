#include "ButtonManager.h"

#include <Arduino.h>

#include "../config/BoardConfig.h"

namespace {
constexpr uint32_t DEBOUNCE_MS = 25;
constexpr uint32_t LONG_MS = 700;
constexpr int8_t PINS[] = {Pins::BTN_BOOT, Pins::BTN_PLUS, Pins::BTN_PWR};
}  // namespace

void ButtonManager::begin(uint32_t now) {
  for (uint8_t i = 0; i < (uint8_t)Btn::Count; i++) {
    pinMode(PINS[i], INPUT_PULLUP);
    const bool down = digitalRead(PINS[i]) == LOW;
    s_[i] = {PINS[i], down, down, now, now, false, down};
  }
}

void ButtonManager::push(Btn b, Press p) {
  const uint8_t next = (head_ + 1) % (sizeof(queue_) / sizeof(queue_[0]));
  if (next == tail_) return;  // 佇列滿就丟棄，避免卡住
  queue_[head_] = {b, p};
  head_ = next;
}

bool ButtonManager::poll(ButtonEvent &out) {
  if (tail_ == head_) return false;
  out = queue_[tail_];
  tail_ = (tail_ + 1) % (sizeof(queue_) / sizeof(queue_[0]));
  return true;
}

bool ButtonManager::popPress(Btn &b, uint32_t &t) {
  if (pressTail_ == pressHead_) return false;
  b = presses_[pressTail_].btn;
  t = presses_[pressTail_].at;
  pressTail_ = (pressTail_ + 1) % 4;
  return true;
}

bool ButtonManager::busy(uint32_t now, uint32_t ms) const {
  for (const State &s : s_)
    if (s.lastRaw) return true;
  return lastEdge_ != 0 && now - lastEdge_ < ms;
}

void ButtonManager::update(uint32_t now) {
  for (uint8_t i = 0; i < (uint8_t)Btn::Count; i++) {
    State &s = s_[i];
    const bool raw = digitalRead(s.pin) == LOW;
    if (raw != s.lastRaw) {
      s.lastRaw = raw;
      s.changedAt = now;
      lastEdge_ = now;
    }
    if (raw != s.stable && now - s.changedAt >= DEBOUNCE_MS) {
      s.stable = raw;
      if (raw) {  // 按下
        s.pressedAt = now;
        s.longSent = false;
        if (!s.ignore) {
          const uint8_t next = (pressHead_ + 1) % 4;
          if (next != pressTail_) {
            presses_[pressHead_] = {(Btn)i, s.changedAt};  // 去抖動前的那一刻
            pressHead_ = next;
          }
        }
      } else {    // 放開
        if (!s.ignore && !s.longSent) push((Btn)i, Press::Short);
        s.ignore = false;
      }
    }
    if (s.stable && !s.ignore && !s.longSent && now - s.pressedAt >= LONG_MS) {
      s.longSent = true;
      push((Btn)i, Press::Long);
    }
  }
}
