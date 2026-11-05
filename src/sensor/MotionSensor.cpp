#include "MotionSensor.h"

#include <Arduino.h>
#include <Wire.h>
#include <math.h>

#include "../config/BoardConfig.h"

// 暫存器與設定值參考 Waveshare 官方 bsp_qmi8658.c
namespace {
constexpr uint8_t ADDR = 0x6B;
constexpr uint8_t REG_WHO_AM_I = 0x00;  // 應為 0x05
constexpr uint8_t REG_CTRL1 = 0x02;
constexpr uint8_t REG_CTRL2 = 0x03;
constexpr uint8_t REG_CTRL7 = 0x08;
constexpr uint8_t REG_AX_L = 0x35;
constexpr uint8_t REG_RESET = 0x60;

constexpr float LSB_TO_G = 4.0f / 32768.0f;  // ±4g
constexpr uint32_t POLL_MS = 5;    // 輕拍很短，要取樣快一點 (主迴圈約每 5ms 一次)
constexpr uint32_t SHAKE_MS = 20;  // 搖晃與重力濾波維持 50Hz
// 靈敏度 (低 / 中 / 高)：相鄰兩次取樣的變化量超過 JOLT_G = 一次晃動；1 秒內達 JOLTS 次 = 搖晃
constexpr float JOLT_G[] = {0.65f, 0.45f, 0.30f};
constexpr uint8_t JOLTS[] = {5, 4, 3};
constexpr uint32_t WINDOW_MS = 1000;
constexpr uint32_t COOLDOWN_MS = 1500;
// 輕拍 (拍頭)：靈敏度 低 / 中 / 高 的峰值門檻 (g)。尚未依實機校正，可用序列埠 tap 指令觀察
constexpr float TAP_G[] = {0.30f, 0.18f, 0.10f};
constexpr float QUIET_G = 0.05f;          // 低於這個算安靜
constexpr uint32_t TAP_MAX_MS = 120;      // 一下輕拍的最長晃動時間
constexpr uint32_t END_GAP_MS = 40;       // 安靜這麼久算晃動結束
constexpr uint32_t PRE_QUIET_MS = 100;    // 拍之前要先安靜一下
constexpr uint32_t TAP_GAP_MS = 1000;     // 兩下輕拍最多間隔
constexpr uint32_t PAT_REPEAT_MS = 1700;  // 一直拍：每隔多久再摸一次 (約一次摸頭動畫)

bool writeReg(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(ADDR);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

bool readRegs(uint8_t reg, uint8_t *buf, uint8_t len) {
  Wire.beginTransmission(ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(ADDR, len) != len) return false;
  for (uint8_t i = 0; i < len; i++) buf[i] = Wire.read();
  return true;
}
}  // namespace

bool MotionSensor::begin() {
  Wire.begin(Pins::I2C_SDA, Pins::I2C_SCL, 400000);
  Wire.setTimeOut(10);
  uint8_t id = 0;
  if (!readRegs(REG_WHO_AM_I, &id, 1) || id != 0x05) {
    Serial.printf("[DeskPet] QMI8658 not found (id=0x%02X), shake disabled\n", id);
    ok_ = false;
    return false;
  }
  writeReg(REG_RESET, 0xB0);
  delay(20);                    // 只在開機時執行一次
  writeReg(REG_CTRL1, 0x40);    // 位址自動遞增
  writeReg(REG_CTRL2, 0x15);    // 加速度 ±4g
  writeReg(REG_CTRL7, 0x01);    // 只開加速度計 (陀螺儀關閉省電)
  ok_ = true;
  Serial.println("[DeskPet] QMI8658 ready");
  return true;
}

void MotionSensor::setLevel(uint8_t level) {
  if (level > 2) level = 1;
  joltG_ = JOLT_G[level];
  joltsNeeded_ = JOLTS[level];
  for (uint32_t &t : jolts_) t = 0;
  joltIdx_ = 0;
}

bool MotionSensor::readAccel(float &x, float &y, float &z) {
  uint8_t b[6];
  if (!readRegs(REG_AX_L, b, 6)) return false;
  x = (int16_t)(b[0] | (b[1] << 8)) * LSB_TO_G;
  y = (int16_t)(b[2] | (b[3] << 8)) * LSB_TO_G;
  z = (int16_t)(b[4] | (b[5] << 8)) * LSB_TO_G;
  return true;
}

bool MotionSensor::addJolt(uint32_t now) {
  jolts_[joltIdx_] = now;
  joltIdx_ = (joltIdx_ + 1) % joltsNeeded_;
  // 環狀緩衝區裡最舊的一筆也在時間窗內 → 搖晃成立
  const uint32_t oldest = jolts_[joltIdx_];
  if (oldest != 0 && now - oldest <= WINDOW_MS) {
    for (uint8_t i = 0; i < joltsNeeded_; i++) jolts_[i] = 0;
    cooldownUntil_ = now + COOLDOWN_MS;
    return true;
  }
  return false;
}

void MotionSensor::setPatLevel(uint8_t level) {
  patLevel_ = level <= 3 ? level : 2;
  if (patLevel_ > 0) tapG_ = TAP_G[patLevel_ - 1];
  tapCount_ = 0;
}

bool MotionSensor::popTap(uint32_t &at) {
  if (!tapFlag_) return false;
  tapFlag_ = false;
  at = tapAt_;
  return true;
}

bool MotionSensor::popPat() {
  const bool p = patFlag_;
  patFlag_ = false;
  return p;
}

void MotionSensor::onTap(uint32_t now) {
  if (tapCount_ > 0 && now - lastTap_ > TAP_GAP_MS) tapCount_ = 0;  // 隔太久：重新數
  if (tapCount_ == 0) firstTap_ = now;
  lastTap_ = now;
  if (tapCount_ < 255) tapCount_++;
  // 第 2 下 = 摸頭；繼續拍的話，每隔 PAT_REPEAT_MS 再摸一次
  if (tapCount_ >= 2 && (lastPat_ == 0 || now - lastPat_ >= PAT_REPEAT_MS)) {
    lastPat_ = now;
    patFlag_ = true;
    cooldownUntil_ = now + 600;  // 拍完馬上的餘震不算搖晃
  }
}

bool MotionSensor::update(uint32_t now) {
  if (!ok_ || now - lastPoll_ < POLL_MS) return false;
  lastPoll_ = now;

  float x, y, z;
  if (!readAccel(x, y, z)) return false;
  if (!hasPrev_) {
    hasPrev_ = true;
    px_ = gx_ = x;
    py_ = gy_ = y;
    pz_ = gz_ = z;
    lastShake_ = quietSince_ = now;
    return false;
  }

  // ---- 輕拍 (每次取樣)：扣掉重力後的晃動量 ----
  const bool tapsOn = patLevel_ > 0;
  if (tapsOn) {
    const float hx = x - gx_, hy = y - gy_, hz = z - gz_;
    const float m = sqrtf(hx * hx + hy * hy + hz * hz);
    if (m > QUIET_G) {
      if (!episode_) {
        episode_ = true;
        episodeLong_ = false;
        episodeStart_ = now;
        episodeQuietBefore_ = now - quietSince_ >= PRE_QUIET_MS;
        peak_ = 0;
        pendingCount_ = 0;
      }
      lastActive_ = now;
      if (m > peak_) peak_ = m;
      if (!episodeLong_ && now - episodeStart_ > TAP_MAX_MS) {  // 太久：不是拍，暫存的晃動算進搖晃
        episodeLong_ = true;
        for (uint8_t i = 0; i < pendingCount_; i++)
          if (addJolt(pendingJolts_[i])) shakeFlag_ = true;
        pendingCount_ = 0;
      }
    } else if (episode_ && now - lastActive_ >= END_GAP_MS) {  // 晃動結束
      episode_ = false;
      quietSince_ = now;
      const uint32_t dur = lastActive_ - episodeStart_;
      const bool blocked = (int32_t)(tapBlockUntil_ - now) > 0;
      const bool tap = !episodeLong_ && episodeQuietBefore_ && peak_ >= tapG_ && !blocked;
      if (tapDebug_) {
        Serial.printf("[tap] peak %.2fg %lums %s\n", peak_, (unsigned long)dur,
                      tap ? "TAP" : episodeLong_ ? "long" : blocked ? "button" : !episodeQuietBefore_ ? "not-quiet" : "weak");
      }
      if (tap) {
        pendingCount_ = 0;  // 是拍：不算搖晃
        tapFlag_ = true;
        tapAt_ = episodeStart_;
        onTap(now);
      } else {
        for (uint8_t i = 0; i < pendingCount_; i++)
          if (addJolt(pendingJolts_[i])) shakeFlag_ = true;
        pendingCount_ = 0;
      }
    }
  }

  // ---- 重力低通 + 搖晃 (維持原本的 50Hz 節奏) ----
  if (now - lastShake_ >= SHAKE_MS) {
    lastShake_ = now;
    const float dx = x - px_, dy = y - py_, dz = z - pz_;
    px_ = x;
    py_ = y;
    pz_ = z;
    constexpr float A = 0.08f;  // 低通濾波：約 0.25 秒時間常數
    // 短暫晃動中不更新重力 (避免把拍的衝擊算進重力)；長時間移動 (例如拿起來轉方向) 照常追蹤
    if (!episode_ || episodeLong_) {
      gx_ += (x - gx_) * A;
      gy_ += (y - gy_) * A;
      gz_ += (z - gz_) * A;
    }
    if ((int32_t)(cooldownUntil_ - now) <= 0 && sqrtf(dx * dx + dy * dy + dz * dz) >= joltG_) {
      if (tapsOn && episode_ && !episodeLong_) {
        if (pendingCount_ < sizeof(pendingJolts_) / sizeof(pendingJolts_[0])) pendingJolts_[pendingCount_++] = now;
      } else if (addJolt(now)) {
        shakeFlag_ = true;
      }
    }
  }

  const bool shook = shakeFlag_;
  shakeFlag_ = false;
  return shook;
}
