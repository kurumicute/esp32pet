#pragma once
#include <stdint.h>

// 音效：板載 ES8311 DAC + 喇叭功放 (GPIO7)。
// - 「音效」設定關閉時不初始化音訊硬體；需要時 (開啟音效，或使用者主動用鬧鐘 / 音樂盒 / 節奏遊戲) 才初始化
// - 只用 DAC (喇叭輸出)，不初始化麥克風 (ES7210)、不收音
// - 聲音全部用程式即時合成 (正弦、叫聲、雜音、八音盒)，不需要音檔；在背景 task 播放，不阻塞主迴圈
// - 功放只在播放時打開，播完關掉，避免底噪
// ES8311 暫存器設定照 Espressif esp_codec_dev 的 es8311 驅動 (16kHz、MCLK = 256 x fs、I2S Philips 16bit、slave)
enum class Sfx : uint8_t {
  Pat, Happy, Notify, TimerDone, Wake, Flower, Dress, Hour, Test,
  Cry, CryHappy, CrySleepy,  // 寵物叫聲
  Step, StepRain,            // 散步腳步聲
  Tick, TickAccent, Hit,     // 節奏遊戲
  Alarm,                     // 鬧鐘
  Count
};

class SoundManager {
 public:
  static constexpr uint32_t STREAM_LATENCY_MS = 90;  // 連續輸出模式下，送出到真的發出聲音的延遲 (約 DMA 緩衝長度)

  void setEnabled(bool on);       // 「音效」開關 (自動音效)
  void setVolume(uint8_t level);  // 0 低，1 中，2 高
  static constexpr uint8_t MAX_SLOTS = 16;    // = Characters::MAX
  static constexpr uint8_t SYNTH_VOICES = 4;  // 內建合成叫聲組數：0 小動物 1 幽靈 2 暗影 3 女孩 (百鬼綾目)
  // 叫聲 / 腳步聲換成這個角色的：synthVoice = 內建合成聲組，slot = 角色編號 (SD 卡音效覆蓋)
  void setVoice(uint8_t synthVoice, uint8_t slot) {
    voice_ = synthVoice;
    slot_ = slot;
  }
  // 開機讀 SD 卡：音效覆蓋 (sound/<folder(i)>/*.wav、sound/common/*.wav) 與音樂盒歌曲 (music/*.wav)
  void loadSd(uint8_t charCount, const char *(*folder)(uint8_t));
  void reloadMusic();                       // 網頁上傳 / 刪除歌曲後重新讀歌單 (會先停止播放)
  static bool isSfxName(const char *stem);  // 是不是可以覆蓋的音效名稱 (cry、alarm…、pat_1…pat_24)
  static bool isPatVoiceName(const char *stem);  // pat_1 … pat_24 (摸頭語音)
  bool hasPatVoices() const;  // 目前的角色有 SD 卡語音 (sound/<角色>/pat_N.wav)；摸頭、搖一搖、叫聲都用它
  // 播放；force = true 時即使「音效」關閉也播 (使用者主動開的功能)。pitch = 音高偏移 (百分比)
  void play(Sfx s, bool force = false, int8_t pitch = 0);
  void playSong(uint8_t song);  // 音樂盒 (一定會播)
  void stop();                  // 清空佇列並中斷目前的聲音
  bool busy() const;            // 正在播放 (含佇列裡還有)
  // 連續輸出模式 (節奏遊戲)：功放一直開、持續送靜音，讓延遲固定在 STREAM_LATENCY_MS 左右
  void setStreaming(bool on);
  bool enabled() const { return enabled_; }
  bool ready() const { return hwReady_; }

  static uint8_t songCount();         // SD 卡 music/*.wav 的數量 (沒有內建曲子)
  static const char *songName(uint8_t i);

 private:
  bool initHardware();
  void applyVolume();
  void send(uint8_t kind, uint8_t id, int8_t pitch, bool force);

  bool enabled_ = false;
  bool hwReady_ = false;
  bool hwFailed_ = false;
  uint8_t level_ = 1;
  uint8_t voice_ = 0;
  uint8_t slot_ = 0;
};
