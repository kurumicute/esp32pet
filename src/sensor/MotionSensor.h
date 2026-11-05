#pragma once
#include <stdint.h>

// 板載 QMI8658 六軸感測器 (只開加速度計)：偵測搖晃、輕拍 (拍頭)，並提供低通濾波後的重力方向。
// 找不到感測器時 available() 為 false，其餘功能照常運作。
//
// 輕拍判斷 (軟體)：扣掉重力後的晃動量，在安靜之後突然變大、又在 TAP_MAX_MS 內安靜下來 = 一下輕拍；
// 1 秒內拍 2 下 = 摸頭。持續晃動 (拿起來、搖晃) 太久就不算拍，照舊交給搖晃偵測。
class MotionSensor {
 public:
  bool begin();
  bool available() const { return ok_; }
  // 定期呼叫；偵測到搖晃時回傳 true (每次搖晃只回報一次)
  bool update(uint32_t now);
  void setLevel(uint8_t level);  // 搖晃靈敏度：0 低，1 中，2 高
  void setPatLevel(uint8_t level);  // 拍頭靈敏度：0 關，1 低，2 中，3 高
  bool popPat();                    // 偵測到拍頭 (拍兩下) 時回傳 true 一次
  bool popTap(uint32_t &at);        // 每一下輕拍 (節奏遊戲用)；at = 開始晃動的時間
  // 按鍵按下 / 放開時呼叫：按按鍵也會震一下，這段時間內不算拍
  void blockTaps(uint32_t until) { tapBlockUntil_ = until; tapCount_ = 0; }
  void setTapDebug(bool on) { tapDebug_ = on; }  // 序列埠印出每次晃動的峰值 (調靈敏度用)
  bool tapDebug() const { return tapDebug_; }
  // 濾波後的重力向量 (單位 g，感測器座標)
  float gx() const { return gx_; }
  float gy() const { return gy_; }
  float gz() const { return gz_; }

 private:
  bool readAccel(float &x, float &y, float &z);
  bool addJolt(uint32_t now);  // 記一次劇烈晃動；達到搖晃條件回傳 true
  void onTap(uint32_t now);

  bool ok_ = false;
  bool hasPrev_ = false;
  float px_ = 0, py_ = 0, pz_ = 0;
  float gx_ = 0, gy_ = 0, gz_ = 0;
  uint32_t lastPoll_ = 0;
  uint32_t lastShake_ = 0;
  bool shakeFlag_ = false;
  // 輕拍
  uint8_t patLevel_ = 2;
  float tapG_ = 0.18f;
  bool episode_ = false;        // 正在晃動中
  bool episodeLong_ = false;    // 這次晃動已經太久 (不是拍)
  uint32_t episodeStart_ = 0;
  uint32_t lastActive_ = 0;
  uint32_t quietSince_ = 0;
  bool episodeQuietBefore_ = false;
  float peak_ = 0;
  uint32_t pendingJolts_[5] = {0};  // 晃動還短的時候先暫存，確定不是拍才算進搖晃
  uint8_t pendingCount_ = 0;
  uint32_t tapBlockUntil_ = 0;
  uint32_t firstTap_ = 0;
  uint32_t lastTap_ = 0;
  uint8_t tapCount_ = 0;
  uint32_t lastPat_ = 0;
  bool patFlag_ = false;
  bool tapFlag_ = false;
  uint32_t tapAt_ = 0;
  bool tapDebug_ = false;
  uint32_t jolts_[5] = {0};  // 最近幾次劇烈晃動的時間
  float joltG_ = 0.45f;
  uint8_t joltsNeeded_ = 4;
  uint8_t joltIdx_ = 0;
  uint32_t cooldownUntil_ = 0;
};
