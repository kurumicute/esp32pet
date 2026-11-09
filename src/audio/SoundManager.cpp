#include "SoundManager.h"

#include <Arduino.h>
#include <ESP_I2S.h>
#include <Wire.h>
#include <math.h>

#include "../config/BoardConfig.h"
#include "../display/LargeAlloc.h"
#include "../storage/SdStore.h"
#include "../ui/Strings.h"

namespace {
constexpr uint8_t ES8311_ADDR = 0x18;  // 7-bit (I2C 掃描結果)
constexpr uint32_t RATE = 16000;
// DAC 音量暫存器 0x32：0x00 = -95.5dB … 0xBF = 0dB，每格 0.5dB
constexpr uint8_t VOLUME_REG[] = {0xA8, 0xB4, 0xBF};  // 低 -11.5dB / 中 -5.5dB / 高 0dB
constexpr float AMPLITUDE = 0.35f;                     // 合成波形的振幅 (滿刻度的比例)，避免太吵或破音

I2SClass i2s;
QueueHandle_t queue = nullptr;
TaskHandle_t task = nullptr;
volatile bool playEnabled = false;

bool wr(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(ES8311_ADDR);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

bool rd(uint8_t reg, uint8_t &val) {
  Wire.beginTransmission(ES8311_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(ES8311_ADDR, (uint8_t)1) != 1) return false;
  val = Wire.read();
  return true;
}

// 讀-改-寫：(reg & keep) | set
bool rmw(uint8_t reg, uint8_t keep, uint8_t set) {
  uint8_t v = 0;
  if (!rd(reg, v)) return false;
  return wr(reg, (uint8_t)((v & keep) | set));
}

// ---- ES8311 初始化 (順序與數值照 esp_codec_dev es8311.c：open → set_fs → start) ----
bool es8311Init() {
  bool ok = true;
  uint8_t v = 0;
  // es8311_open
  ok &= rd(0x0D, v);
  if (v != 0xFA) ok &= wr(0x0D, 0xFA);
  wr(0x44, 0x08);  // 提高 I2C 抗干擾 (官方驅動寫兩次，第一次偶爾會失敗)
  ok &= wr(0x44, 0x08);
  ok &= wr(0x01, 0x30);
  ok &= wr(0x02, 0x00);
  ok &= wr(0x03, 0x10);
  ok &= wr(0x16, 0x24);
  ok &= wr(0x04, 0x10);
  ok &= wr(0x05, 0x00);
  ok &= wr(0x0B, 0x00);
  ok &= wr(0x0C, 0x00);
  ok &= wr(0x10, 0x1F);
  ok &= wr(0x11, 0x7F);
  ok &= wr(0x00, 0x80);
  ok &= rmw(0x00, 0xBF, 0x00);  // slave mode
  ok &= wr(0x01, 0x3F);         // 用 MCLK 腳當時脈、不反相
  ok &= rmw(0x06, (uint8_t)~0x20, 0x00);  // SCLK 不反相
  ok &= wr(0x13, 0x10);
  ok &= wr(0x1B, 0x0A);
  ok &= wr(0x1C, 0x6A);
  ok &= wr(0x44, 0x58);
  // es8311_set_fs：16bit、I2S normal、16kHz (係數表 {4096000, 16000, 1,1,1,1,0,0x00,0xff,4,0x10,0x20})
  ok &= rmw(0x09, 0xFF, 0x0C);
  ok &= rmw(0x0A, 0xFF, 0x0C);
  ok &= rmw(0x09, 0xFC, 0x00);
  ok &= rmw(0x0A, 0xFC, 0x00);
  ok &= rmw(0x02, 0x07, (0 << 5) | (0 << 3));  // pre_div 1、pre_multi x1
  ok &= wr(0x05, 0x00);                         // adc_div 1、dac_div 1
  ok &= rmw(0x03, 0x80, (0 << 6) | 0x10);       // single speed、adc_osr
  ok &= rmw(0x04, 0x80, 0x20);                  // dac_osr
  ok &= rmw(0x07, 0xC0, 0x00);                  // lrck_h
  ok &= wr(0x08, 0xFF);                         // lrck_l
  ok &= rmw(0x06, 0xE0, 4 - 1);                 // bclk_div 4
  // es8311_start (只開 DAC)
  ok &= wr(0x00, 0x80);
  ok &= wr(0x01, 0x3F);
  ok &= rmw(0x09, 0xBF, 0x00);
  ok &= rmw(0x0A, 0xBF, 0x00);
  ok &= wr(0x17, 0xBF);
  ok &= wr(0x0E, 0x02);
  ok &= wr(0x12, 0x00);
  ok &= wr(0x14, 0x1A);
  ok &= rmw(0x14, (uint8_t)~0x40, 0x00);  // 不用數位麥克風
  ok &= wr(0x0D, 0x01);
  ok &= wr(0x15, 0x40);
  ok &= wr(0x37, 0x08);
  ok &= wr(0x45, 0x00);
  ok &= rmw(0x31, 0x9F, 0x00);  // 取消靜音
  return ok;
}

// ---- 音效合成 ----
enum Voice : uint8_t { V_SINE, V_VOICE, V_NOISE, V_BELL, V_CLICK, V_GHOST, V_DARK };
struct Note {
  uint16_t hz;     // 0 = 休止；雜音時 = 濾波頻率
  uint16_t ms;
  uint16_t hzEnd;  // 0 = 不滑音；否則從 hz 滑到 hzEnd
  uint8_t voice;
  uint8_t amp;     // 0~100
};
// 一般音效 (柔和正弦)
#define S(hz, ms) {hz, ms, 0, V_SINE, 100}
#define R(ms) {0, ms, 0, V_SINE, 0}
// 叫聲：「咿～布！」(有泛音 + 顫音 + 滑音)
#define VC(hz, ms, to) {hz, ms, to, V_VOICE, 100}
const Note SFX_PAT[] = {S(784, 70), R(25), S(988, 110)};
const Note SFX_HAPPY[] = {S(880, 60), S(1175, 60), S(1568, 100)};
const Note SFX_NOTIFY[] = {S(1319, 110), S(1760, 220)};
const Note SFX_TIMER[] = {S(1047, 140), S(1319, 140), S(1568, 140), R(70), S(2093, 320)};
const Note SFX_WAKE[] = {{420, 360, 900, V_SINE, 100}, R(40), S(1175, 90)};
const Note SFX_FLOWER[] = {S(2093, 50), S(2637, 50), S(3136, 90)};
const Note SFX_DRESS[] = {S(1568, 40), S(2093, 40), S(2637, 40), S(3136, 80)};
const Note SFX_HOUR[] = {{1047, 400, 0, V_BELL, 100}, {784, 600, 0, V_BELL, 100}};
const Note SFX_CRY[] = {VC(1050, 150, 1450), VC(1450, 110, 1250), R(35), VC(950, 170, 700)};
const Note SFX_CRY_HAPPY[] = {VC(1200, 90, 1550), R(25), VC(1000, 80, 850), R(25), VC(1300, 140, 1700)};
const Note SFX_CRY_SLEEPY[] = {VC(650, 260, 900), VC(900, 320, 620)};
const Note SFX_STEP[] = {{900, 35, 0, V_NOISE, 45}};        // 草地上「噠」
const Note SFX_STEP_RAIN[] = {{3500, 50, 0, V_NOISE, 40}};  // 踩水「啪」
const Note SFX_TICK[] = {{1800, 30, 0, V_CLICK, 70}};
const Note SFX_TICK_ACCENT[] = {{2400, 40, 0, V_CLICK, 100}};
const Note SFX_HIT[] = {S(1568, 45), S(2093, 70)};
const Note SFX_ALARM[] = {{1047, 130, 0, V_BELL, 100}, {1319, 130, 0, V_BELL, 100}, {1568, 130, 0, V_BELL, 100},
                          {2093, 300, 0, V_BELL, 100}, R(120), {1568, 130, 0, V_BELL, 100},
                          {2093, 400, 0, V_BELL, 100}};

// ---- 合成叫聲組 (voice) 的叫聲 / 腳步聲；voice 0 = 上面 SFX_TABLE 原本的小動物叫聲 ----
// (原本是水伊布 / 燈火靈 / 達克萊伊三個內建角色的叫聲；角色已刪除，叫聲組留給 SD 卡角色用 --voice 選)
// 水晶燈火靈：低沉、空心、慢慢搖晃的幽靈聲「嗚～嗡」，開心時加吊燈的叮叮聲；飄著走，腳步改成輕輕的「咻」
#define VG(hz, ms, to) {hz, ms, to, V_GHOST, 100}
const Note CHAND_CRY[] = {VG(440, 360, 620), VG(620, 300, 400)};
const Note CHAND_CRY_HAPPY[] = {VG(520, 220, 700), {1568, 120, 0, V_BELL, 70}, {2093, 160, 0, V_BELL, 70},
                                {2637, 260, 0, V_BELL, 60}};
const Note CHAND_CRY_SLEEPY[] = {VG(330, 420, 420), VG(420, 480, 300)};
const Note CHAND_STEP[] = {{500, 60, 0, V_NOISE, 22}};
const Note CHAND_STEP_RAIN[] = {{2000, 60, 0, V_NOISE, 22}};
// 達克萊伊：低沉、沙啞的暗影低語 (次諧波 + 大量氣音)，也是飄著走
#define VD(hz, ms, to) {hz, ms, to, V_DARK, 100}
const Note DARK_CRY[] = {VD(300, 280, 420), R(30), VD(420, 340, 230)};
const Note DARK_CRY_HAPPY[] = {VD(320, 150, 500), R(30), VD(460, 220, 640)};
const Note DARK_CRY_SLEEPY[] = {VD(230, 520, 260), VD(260, 420, 190)};
const Note DARK_STEP[] = {{350, 70, 0, V_NOISE, 20}};
const Note DARK_STEP_RAIN[] = {{1600, 70, 0, V_NOISE, 20}};
// 百鬼綾目 (SD 卡貼圖角色用 voice 3)：高音、短促、有精神的「余～！」，赤腳走路是輕輕的啪噠聲
const Note AYAME_CRY[] = {VC(1500, 90, 1900), R(20), VC(1900, 150, 1550)};
const Note AYAME_CRY_HAPPY[] = {VC(1600, 70, 2100), R(20), VC(1800, 70, 2250), R(20), VC(2000, 170, 2450)};
const Note AYAME_CRY_SLEEPY[] = {VC(1100, 240, 1400), VC(1400, 300, 980)};
const Note AYAME_STEP[] = {{1400, 25, 0, V_NOISE, 32}};
const Note AYAME_STEP_RAIN[] = {{3800, 40, 0, V_NOISE, 32}};

constexpr uint8_t CHAR_SFX_FIRST = (uint8_t)Sfx::Cry;  // Cry, CryHappy, CrySleepy, Step, StepRain
constexpr uint8_t CHAR_SFX_COUNT = 5;
static_assert((uint8_t)Sfx::StepRain == CHAR_SFX_FIRST + CHAR_SFX_COUNT - 1, "角色音效範圍與 Sfx 順序不一致");
struct SfxDef {
  const Note *notes;
  uint8_t count;
};
#define SFX(a) {a, (uint8_t)(sizeof(a) / sizeof(a[0]))}
// voice 1 以後的叫聲 / 腳步聲 (voice 0 用 SFX_TABLE 原本的)
const SfxDef CHAR_SFX[][CHAR_SFX_COUNT] = {
    {SFX(CHAND_CRY), SFX(CHAND_CRY_HAPPY), SFX(CHAND_CRY_SLEEPY), SFX(CHAND_STEP), SFX(CHAND_STEP_RAIN)},
    {SFX(DARK_CRY), SFX(DARK_CRY_HAPPY), SFX(DARK_CRY_SLEEPY), SFX(DARK_STEP), SFX(DARK_STEP_RAIN)},
    {SFX(AYAME_CRY), SFX(AYAME_CRY_HAPPY), SFX(AYAME_CRY_SLEEPY), SFX(AYAME_STEP), SFX(AYAME_STEP_RAIN)},
};
constexpr uint8_t VOICE_COUNT = 1 + sizeof(CHAR_SFX) / sizeof(CHAR_SFX[0]);
static_assert(VOICE_COUNT == SoundManager::SYNTH_VOICES, "SYNTH_VOICES 與 CHAR_SFX 數量不一致");

const SfxDef SFX_TABLE[] = {SFX(SFX_PAT),       SFX(SFX_HAPPY),      SFX(SFX_NOTIFY),         SFX(SFX_TIMER),
                            SFX(SFX_WAKE),      SFX(SFX_FLOWER),     SFX(SFX_DRESS),          SFX(SFX_HOUR),
                            SFX(SFX_HAPPY),     SFX(SFX_CRY),        SFX(SFX_CRY_HAPPY),      SFX(SFX_CRY_SLEEPY),
                            SFX(SFX_STEP),      SFX(SFX_STEP_RAIN),  SFX(SFX_TICK),           SFX(SFX_TICK_ACCENT),
                            SFX(SFX_HIT),       SFX(SFX_ALARM)};
static_assert(sizeof(SFX_TABLE) / sizeof(SFX_TABLE[0]) == (size_t)Sfx::Count, "SFX_TABLE 與 Sfx 數量不一致");


constexpr int FRAMES = 128;
int16_t frameBuf[FRAMES * 2];  // 立體聲 (左右聲道相同)
volatile bool abortPlay = false;
volatile bool streaming = false;
volatile bool playing = false;
uint32_t noiseState = 0x12345678;

struct Req {
  uint8_t kind;  // 0 = 音效，1 = 曲子
  uint8_t id;
  int8_t pitch;
  bool force;
  uint8_t voice;  // 用哪一組內建合成叫聲
  uint8_t slot;   // 角色編號 (SD 卡音效覆蓋用)
};

// ---- SD 卡音效 / 音樂 ----
// 音效覆蓋：/deskpet/sound/<角色資料夾>/<名稱>.wav 或 /deskpet/sound/common/<名稱>.wav
// 開機時讀進 PSRAM 並轉成 16kHz 單聲道；每個最長 3 秒
const char *const SFX_FILE[] = {"pat",  "happy",      "notify", "timer", "wake",  "flower",
                                "dress", "hour",      "test",   "cry",   "cry_happy", "cry_sleepy",
                                "step", "step_rain",  "tick",   "tick_accent", "hit", "alarm"};
static_assert(sizeof(SFX_FILE) / sizeof(SFX_FILE[0]) == (size_t)Sfx::Count, "SFX_FILE 與 Sfx 數量不一致");
constexpr uint8_t SLOTS = SoundManager::MAX_SLOTS + 1;  // 最後一格 = common
constexpr uint32_t MAX_SFX_SAMPLES = RATE * 3;
constexpr float WAV_GAIN = 0.7f;
struct Pcm {
  int16_t *s;
  uint32_t n;
};
Pcm wavSfx[SLOTS][(size_t)Sfx::Count] = {};

// 摸頭語音：/deskpet/sound/<角色資料夾>/pat_1.wav … pat_24.wav，摸頭時隨機挑一句 (不連續重複)。
// 有這些檔案的角色，摸頭、搖一搖和各種叫聲都改播這些語音 (不再用合成的叫聲)。每句最長 6 秒，超過的淡出截掉
constexpr uint8_t MAX_PAT_VOICES = 24;
constexpr uint32_t MAX_VOICE_SAMPLES = RATE * 6;
struct PatPool {
  Pcm clip[MAX_PAT_VOICES];
  uint8_t n;
  uint8_t last;
};
PatPool *patPool[SLOTS] = {};  // PSRAM；同一個資料夾的角色共用同一份

constexpr uint8_t MAX_SD_SONGS = 30;
char (*sdSongPath)[96] = nullptr;  // PSRAM (loadSd 時配置)
char (*sdSongName)[48] = nullptr;
uint8_t sdSongCount = 0;

struct WavInfo {
  uint16_t channels;
  uint32_t rate;
  uint32_t dataBytes;
};

// 讀 WAV 檔頭並把檔案位置移到 data 開頭；只支援 16-bit PCM
bool readWavHeader(File &f, WavInfo &w) {
  uint8_t h[12];
  if (f.read(h, 12) != 12 || memcmp(h, "RIFF", 4) != 0 || memcmp(h + 8, "WAVE", 4) != 0) return false;
  bool fmtOk = false;
  for (int guard = 0; guard < 32; guard++) {
    uint8_t ch[8];
    if (f.read(ch, 8) != 8) return false;
    const uint32_t len = ch[4] | (ch[5] << 8) | (ch[6] << 16) | ((uint32_t)ch[7] << 24);
    if (memcmp(ch, "fmt ", 4) == 0) {
      uint8_t fmt[16];
      if (len < 16 || f.read(fmt, 16) != 16) return false;
      const uint16_t format = fmt[0] | (fmt[1] << 8);
      w.channels = fmt[2] | (fmt[3] << 8);
      w.rate = fmt[4] | (fmt[5] << 8) | (fmt[6] << 16) | ((uint32_t)fmt[7] << 24);
      const uint16_t bits = fmt[14] | (fmt[15] << 8);
      if (format != 1 || bits != 16 || w.channels < 1 || w.channels > 2 || w.rate < 4000 || w.rate > 48000) return false;
      fmtOk = true;
      if (len > 16 && !f.seek(f.position() + len - 16 + (len & 1))) return false;
    } else if (memcmp(ch, "data", 4) == 0) {
      w.dataBytes = len;
      return fmtOk;
    } else if (!f.seek(f.position() + len + (len & 1))) {
      return false;
    }
  }
  return false;
}

// 讀一段來源樣本 (轉成單聲道)；回傳讀到幾個
uint32_t readMono(File &f, const WavInfo &w, int16_t *out, uint32_t maxSamples, uint32_t &bytesLeft) {
  static int16_t raw[512];
  const uint32_t frameBytes = 2u * w.channels;
  uint32_t want = maxSamples;
  if (want * frameBytes > sizeof(raw)) want = sizeof(raw) / frameBytes;
  if (want * frameBytes > bytesLeft) want = bytesLeft / frameBytes;
  if (want == 0) return 0;
  const int got = f.read((uint8_t *)raw, want * frameBytes);
  if (got <= 0) return 0;
  const uint32_t frames = (uint32_t)got / frameBytes;
  bytesLeft -= frames * frameBytes;
  for (uint32_t i = 0; i < frames; i++)
    out[i] = w.channels == 2 ? (int16_t)(((int32_t)raw[i * 2] + raw[i * 2 + 1]) / 2) : raw[i];
  return frames;
}

// 整個短音效讀進 PSRAM (轉成 16kHz 單聲道)
bool loadPcm(const char *path, Pcm &out, uint32_t maxSamples = MAX_SFX_SAMPLES) {
  File f = SdStore::fs().open(path, FILE_READ);
  if (!f) return false;
  WavInfo w;
  if (!readWavHeader(f, w)) {
    f.close();
    Serial.printf("[SD] unsupported wav (need 16-bit PCM): %s\n", path);
    return false;
  }
  const uint32_t srcN = w.dataBytes / (2u * w.channels);
  uint32_t outN = (uint32_t)((uint64_t)srcN * RATE / w.rate);
  const bool cut = outN > maxSamples;
  if (cut) outN = maxSamples;
  if (outN == 0) {
    f.close();
    return false;
  }
  int16_t *src = (int16_t *)heap_caps_malloc(srcN * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  int16_t *dst = (int16_t *)heap_caps_malloc(outN * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (src == nullptr || dst == nullptr) {
    if (src) heap_caps_free(src);
    if (dst) heap_caps_free(dst);
    f.close();
    return false;
  }
  uint32_t left = w.dataBytes, got = 0;
  while (got < srcN) {
    const uint32_t n = readMono(f, w, src + got, srcN - got, left);
    if (n == 0) break;
    got += n;
  }
  f.close();
  for (uint32_t i = 0; i < outN; i++) {  // 線性內插重新取樣
    const float pos = (float)i * w.rate / RATE;
    const uint32_t k = (uint32_t)pos;
    const float fr = pos - k;
    const int16_t a = k < got ? src[k] : 0, b = k + 1 < got ? src[k + 1] : a;
    dst[i] = (int16_t)(a + (b - a) * fr);
  }
  if (cut) {  // 被截掉的聲音：最後 0.15 秒淡出，避免「啪」一聲
    const uint32_t fade = outN < RATE * 15 / 100 ? outN : RATE * 15 / 100;
    for (uint32_t i = 0; i < fade; i++) dst[outN - 1 - i] = (int16_t)((int32_t)dst[outN - 1 - i] * (int32_t)i / (int32_t)fade);
  }
  heap_caps_free(src);
  out.s = dst;
  out.n = outN;
  return true;
}

void writeSilence(uint32_t ms) {
  memset(frameBuf, 0, sizeof(frameBuf));
  uint32_t frames = RATE * ms / 1000;
  while (frames > 0) {
    const uint32_t n = frames > FRAMES ? FRAMES : frames;
    i2s.write((const uint8_t *)frameBuf, n * 4);
    frames -= n;
  }
}

void playNote(const Note &n, float pitchMul) {
  const uint32_t total = RATE * n.ms / 1000;
  if (n.hz == 0 || n.amp == 0) {
    writeSilence(n.ms);
    return;
  }
  const uint32_t attack = n.voice == V_CLICK || n.voice == V_NOISE ? RATE / 2000 : RATE * 4 / 1000;
  const uint32_t release = total > RATE / 25 ? RATE / 25 : total / 2;  // 40ms 收尾 (避免爆音)
  const float amp = AMPLITUDE * n.amp / 100.0f;
  const float hz0 = n.hz * pitchMul, hz1 = (n.hzEnd ? n.hzEnd : n.hz) * pitchMul;
  float phase = 0, lp = 0;
  uint32_t i = 0;
  while (i < total && !abortPlay) {
    int k = 0;
    for (; k < FRAMES && i < total; k++, i++) {
      const float t = (float)i / (float)total;
      float env = 1.0f;
      if (i < attack) env = (float)i / attack;
      float s;
      switch (n.voice) {
        case V_VOICE: {  // 叫聲：滑音 + 6Hz 顫音 + 二三次泛音
          const float vib = 1.0f + 0.03f * sinf(2.0f * (float)M_PI * 6.0f * i / RATE);
          phase += 2.0f * (float)M_PI * (hz0 + (hz1 - hz0) * t) * vib / RATE;
          s = sinf(phase) * 0.6f + sinf(phase * 2.0f) * 0.28f + sinf(phase * 3.0f) * 0.12f;
          if (i > total - release) env = (float)(total - i) / release;
          break;
        }
        case V_NOISE: {  // 腳步：低通雜音，快速衰減
          noiseState = noiseState * 1664525u + 1013904223u;
          const float x = (float)(int32_t)noiseState / 2147483648.0f;
          const float a = hz0 / (float)RATE * 6.0f;
          lp += (x - lp) * (a > 1.0f ? 1.0f : a);
          s = lp * 2.0f;
          env *= expf(-t * 5.0f);
          break;
        }
        case V_BELL:  // 八音盒：泛音 + 指數衰減
          phase += 2.0f * (float)M_PI * hz0 / RATE;
          s = sinf(phase) * 0.7f + sinf(phase * 2.0f) * 0.2f + sinf(phase * 4.0f) * 0.1f;
          env *= expf(-t * 4.0f);
          if (i > total - RATE / 200) env *= (float)(total - i) / (RATE / 200);
          break;
        case V_GHOST: {  // 幽靈聲：慢速大幅顫音 + 五度泛音 + 氣音，音量輕輕起伏
          const float sec = (float)i / RATE;
          const float vib = 1.0f + 0.05f * sinf(2.0f * (float)M_PI * 4.5f * sec);
          phase += 2.0f * (float)M_PI * (hz0 + (hz1 - hz0) * t) * vib / RATE;
          noiseState = noiseState * 1664525u + 1013904223u;
          const float x = (float)(int32_t)noiseState / 2147483648.0f;
          lp += (x - lp) * 0.08f;
          s = sinf(phase) * 0.68f + sinf(phase * 1.5f) * 0.16f + sinf(phase * 2.0f) * 0.08f + lp * 0.5f;
          env *= 0.82f + 0.18f * sinf(2.0f * (float)M_PI * 7.0f * sec);
          if (i < RATE * 30 / 1000) env *= (float)i / (RATE * 30 / 1000);  // 慢慢浮現
          if (i > total - release) env *= (float)(total - i) / release;
          break;
        }
        case V_DARK: {  // 暗影聲：基音 + 低八度次諧波 + 粗糙泛音 + 很多氣音，快速顫抖
          const float sec = (float)i / RATE;
          const float vib = 1.0f + 0.02f * sinf(2.0f * (float)M_PI * 9.0f * sec);
          phase += 2.0f * (float)M_PI * (hz0 + (hz1 - hz0) * t) * vib / RATE;
          noiseState = noiseState * 1664525u + 1013904223u;
          const float x = (float)(int32_t)noiseState / 2147483648.0f;
          lp += (x - lp) * 0.18f;
          s = sinf(phase) * 0.45f + sinf(phase * 0.5f) * 0.3f + sinf(phase * 3.0f) * 0.1f + lp * 0.9f;
          env *= 0.75f + 0.25f * sinf(2.0f * (float)M_PI * 11.0f * sec);
          if (i < RATE * 25 / 1000) env *= (float)i / (RATE * 25 / 1000);
          if (i > total - release) env *= (float)(total - i) / release;
          break;
        }
        case V_CLICK:  // 節拍器：很短的「噠」
          phase += 2.0f * (float)M_PI * hz0 / RATE;
          s = sinf(phase);
          env *= expf(-t * 7.0f);
          break;
        default:  // 柔和正弦 (+ 一點二次泛音)
          phase += 2.0f * (float)M_PI * (hz0 + (hz1 - hz0) * t) / RATE;
          s = sinf(phase) * 0.85f + sinf(phase * 2.0f) * 0.15f;
          if (i > total - release) env = (float)(total - i) / release;
          break;
      }
      if (phase > 2.0f * (float)M_PI * 64.0f) phase -= 2.0f * (float)M_PI * 64.0f;  // 避免浮點誤差累積
      const int16_t v = (int16_t)(s * env * amp * 32767.0f);
      frameBuf[k * 2] = v;
      frameBuf[k * 2 + 1] = v;
    }
    i2s.write((const uint8_t *)frameBuf, k * 4);
  }
}

// 播放 PCM (pitchMul 改變播放速度 = 音高)
void playPcm(const Pcm &p, float pitchMul) {
  float pos = 0;
  const float amp = AMPLITUDE / 0.35f * WAV_GAIN;
  while (pos < p.n && !abortPlay) {
    int k = 0;
    for (; k < FRAMES && pos < p.n; k++) {
      const uint32_t i = (uint32_t)pos;
      float v = p.s[i] * amp;
      if (v > 32767.0f) v = 32767.0f;
      if (v < -32768.0f) v = -32768.0f;
      frameBuf[k * 2] = frameBuf[k * 2 + 1] = (int16_t)v;
      pos += pitchMul;
    }
    i2s.write((const uint8_t *)frameBuf, k * 4);
  }
}

// 從 SD 卡串流播放一首 WAV (音樂盒)
void streamWav(const char *path) {
  File f = SdStore::fs().open(path, FILE_READ);
  if (!f) return;
  WavInfo w;
  if (!readWavHeader(f, w)) {
    f.close();
    return;
  }
  static int16_t src[257];
  uint32_t left = w.dataBytes;
  const float step = (float)w.rate / RATE;
  float pos = 0;     // 在 src[] 裡的位置 (src[0] = 上一段最後一個樣本)
  uint32_t have = 1;
  src[0] = 0;
  while (!abortPlay) {
    if (pos + 1 >= have) {  // 讀下一段
      src[0] = src[have - 1];
      pos -= (float)(have - 1);
      const uint32_t n = readMono(f, w, src + 1, 256, left);
      if (n == 0) break;
      have = n + 1;
    }
    int k = 0;
    for (; k < FRAMES && pos + 1 < have; k++) {
      const uint32_t i = (uint32_t)pos;
      const float fr = pos - i;
      const float v = (src[i] + (src[i + 1] - src[i]) * fr) * WAV_GAIN;
      frameBuf[k * 2] = frameBuf[k * 2 + 1] = (int16_t)v;
      pos += step;
    }
    if (k > 0) i2s.write((const uint8_t *)frameBuf, k * 4);
  }
  f.close();
}

PatPool *patPoolFor(uint8_t slot) {
  if (slot < SoundManager::MAX_SLOTS && patPool[slot] != nullptr) return patPool[slot];
  return patPool[SLOTS - 1];  // 共用 (common)
}

void playSfx(uint8_t id, int8_t pitch, uint8_t voice, uint8_t slot) {
  if (id >= (uint8_t)Sfx::Count) return;
  const float pmul = 1.0f + pitch / 100.0f;
  // 摸頭語音；有語音的角色，叫聲 (cry / cry_happy / cry_sleepy) 也改成隨機一句語音，
  // 除非 SD 卡另外放了那個叫聲的專屬檔 (例如 sound/ayame/cry_sleepy.wav)。語音一律原音高播放
  const bool isCry = id == (uint8_t)Sfx::Cry || id == (uint8_t)Sfx::CryHappy || id == (uint8_t)Sfx::CrySleepy;
  const bool hasOwnFile = slot < SoundManager::MAX_SLOTS && wavSfx[slot][id].s != nullptr;
  if (id == (uint8_t)Sfx::Pat || (isCry && !hasOwnFile)) {
    PatPool *p = patPoolFor(slot);
    if (p != nullptr && p->n > 0) {
      uint8_t k = (uint8_t)random(p->n);
      if (p->n > 1 && k == p->last) k = (uint8_t)((k + 1 + random(p->n - 1)) % p->n);
      p->last = k;
      playPcm(p->clip[k], 1.0f);
      return;
    }
  }
  if (slot < SoundManager::MAX_SLOTS && wavSfx[slot][id].s != nullptr) {  // SD 卡：這個角色專屬
    playPcm(wavSfx[slot][id], pmul);
    return;
  }
  if (wavSfx[SLOTS - 1][id].s != nullptr) {  // SD 卡：共用
    playPcm(wavSfx[SLOTS - 1][id], pmul);
    return;
  }
  const bool charSfx = voice > 0 && voice < VOICE_COUNT && id >= CHAR_SFX_FIRST && id < CHAR_SFX_FIRST + CHAR_SFX_COUNT;
  const SfxDef &d = charSfx ? CHAR_SFX[voice - 1][id - CHAR_SFX_FIRST] : SFX_TABLE[id];
  const float mul = 1.0f + pitch / 100.0f;
  for (uint8_t i = 0; i < d.count && !abortPlay; i++) playNote(d.notes[i], mul);
}

// 音樂盒：只播 SD 卡 music/*.wav (串流)
void playSongNotes(uint8_t id) {
  if (id < sdSongCount) streamWav(sdSongPath[id]);
}

void soundTask(void *) {
  Req r;
  bool wasStreaming = false;
  for (;;) {
    if (wasStreaming && !streaming) {  // 連續輸出剛結束：關功放
      writeSilence(40);
      digitalWrite(Pins::AMP_CTRL, LOW);
      playing = false;
    }
    wasStreaming = streaming;
    // 連續輸出模式：沒有聲音時也一直送靜音 (延遲固定)
    if (xQueueReceive(queue, &r, streaming ? 0 : portMAX_DELAY) != pdTRUE) {
      if (streaming) {
        digitalWrite(Pins::AMP_CTRL, HIGH);
        playing = true;
        writeSilence(4);
      } else {
        playing = false;
      }
      continue;
    }
    playing = true;
    abortPlay = false;
    digitalWrite(Pins::AMP_CTRL, HIGH);  // 開功放
    if (!streaming) writeSilence(15);    // 等功放穩定
    for (;;) {
      if (playEnabled || r.force) {
        if (r.kind == 1) playSongNotes(r.id);
        else playSfx(r.id, r.pitch, r.voice, r.slot);
        abortPlay = false;
      }
      // 連續播放 (例如腳步聲) 時，300ms 內有下一個就不關功放，避免一直「啪」
      if (xQueueReceive(queue, &r, pdMS_TO_TICKS(streaming ? 0 : 300)) != pdTRUE) break;
    }
    if (!streaming) {
      writeSilence(40);  // 把 DMA 裡的聲音放完再關功放
      digitalWrite(Pins::AMP_CTRL, LOW);
      playing = false;
    }
  }
}
}  // namespace

bool SoundManager::initHardware() {
  if (hwReady_) return true;
  if (hwFailed_) return false;
  pinMode(Pins::AMP_CTRL, OUTPUT);
  digitalWrite(Pins::AMP_CTRL, LOW);
  i2s.setPins(Pins::I2S_BCLK, Pins::I2S_LRCK, Pins::I2S_DOUT, -1, Pins::I2S_MCLK);
  if (!i2s.begin(I2S_MODE_STD, RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO)) {
    Serial.println("[Sound] I2S init failed");
    hwFailed_ = true;
    return false;
  }
  delay(5);  // 只在第一次需要聲音時執行：讓 MCLK 先跑起來
  if (!es8311Init()) {
    Serial.println("[Sound] ES8311 init failed");
    i2s.end();
    hwFailed_ = true;
    return false;
  }
  queue = xQueueCreate(6, sizeof(Req));
  if (queue == nullptr || xTaskCreate(soundTask, "sound", 4096, nullptr, 3, &task) != pdPASS) {
    Serial.println("[Sound] task create failed");
    hwFailed_ = true;
    return false;
  }
  hwReady_ = true;
  applyVolume();
  Serial.println("[Sound] ES8311 ready");
  return true;
}

void SoundManager::applyVolume() {
  if (hwReady_) wr(0x32, VOLUME_REG[level_ <= 2 ? level_ : 1]);
}

void SoundManager::setEnabled(bool on) {
  enabled_ = on;
  playEnabled = on;
  if (on) initHardware();
  else if (!hwReady_) digitalWrite(Pins::AMP_CTRL, LOW);
}

void SoundManager::setVolume(uint8_t level) {
  level_ = level <= 2 ? level : 1;
  applyVolume();
}

void SoundManager::send(uint8_t kind, uint8_t id, int8_t pitch, bool force) {
  if (!force && !enabled_) return;
  if (!initHardware() || queue == nullptr) return;  // 主動功能第一次用到時才初始化
  const Req r = {kind, id, pitch, force, voice_, slot_};
  xQueueSend(queue, &r, 0);  // 佇列滿就丟掉，不等待
}

void SoundManager::play(Sfx s, bool force, int8_t pitch) { send(0, (uint8_t)s, pitch, force); }

void SoundManager::playSong(uint8_t song) {
  if (song >= songCount()) return;
  stop();
  send(1, song, 0, true);
}

void SoundManager::stop() {
  if (!hwReady_ || queue == nullptr) return;
  xQueueReset(queue);
  abortPlay = true;
}

bool SoundManager::busy() const {
  if (!hwReady_ || queue == nullptr) return false;
  return uxQueueMessagesWaiting(queue) > 0 || (playing && !streaming);
}

void SoundManager::setStreaming(bool on) {
  if (on && !initHardware()) return;
  streaming = on;
  if (on && queue != nullptr) {  // 叫醒 task 進入連續輸出
    const Req r = {0, (uint8_t)Sfx::Count, 0, true, 0, 0};  // 無效 id = 只是喚醒
    xQueueSend(queue, &r, 0);
  }
}

uint8_t SoundManager::songCount() { return sdSongCount; }
void SoundManager::reloadMusic() {
  if (!SdStore::ok()) return;
  stop();
  if (sdSongPath == nullptr) sdSongPath = allocLarge<char[96]>(MAX_SD_SONGS);
  if (sdSongName == nullptr) sdSongName = allocLarge<char[48]>(MAX_SD_SONGS);
  if (sdSongPath == nullptr || sdSongName == nullptr) return;
  char path[48];
  snprintf(path, sizeof(path), "%s/music", SdStore::ROOT);
  sdSongCount = 0;  // 先清空，避免讀歌單途中被拿去播放
  const uint8_t n = SdStore::list(path, ".wav", sdSongPath, MAX_SD_SONGS);
  for (uint8_t i = 0; i < n; i++) SdStore::stem(sdSongPath[i], sdSongName[i], 48);
  sdSongCount = n;
}

bool SoundManager::isSfxName(const char *stem) {
  for (const char *n : SFX_FILE)
    if (strcmp(n, stem) == 0) return true;
  return isPatVoiceName(stem);
}

// pat_1 … pat_24 (摸頭語音)
bool SoundManager::isPatVoiceName(const char *stem) {
  if (strncmp(stem, "pat_", 4) != 0) return false;
  const char *d = stem + 4;
  if (d[0] < '1' || d[0] > '9') return false;  // 不收 pat_0、pat_01
  if (d[1] == '\0') return true;
  if (d[1] < '0' || d[1] > '9' || d[2] != '\0') return false;
  return (d[0] - '0') * 10 + (d[1] - '0') <= MAX_PAT_VOICES;
}

bool SoundManager::hasPatVoices() const { return patPoolFor(slot_) != nullptr; }


const char *SoundManager::songName(uint8_t i) { return i < sdSongCount ? sdSongName[i] : ""; }

// 讀一個角色資料夾的摸頭語音 (pat_N.wav)；資料夾跟前面的角色一樣時共用同一份 (省 PSRAM)
uint16_t loadPatVoices(uint8_t slot, const char *dir, uint8_t charCount, const char *(*folder)(uint8_t)) {
  if (slot < SoundManager::MAX_SLOTS) {
    for (uint8_t o = 0; o < slot && o < charCount; o++) {
      if (strcmp(folder(o), dir) == 0) {
        patPool[slot] = patPool[o];
        return 0;
      }
    }
  }
  char d[64];
  snprintf(d, sizeof(d), "%s/sound/%s", SdStore::ROOT, dir);
  char (*paths)[96] = allocLarge<char[96]>(MAX_PAT_VOICES + 24);
  if (paths == nullptr) return 0;
  const uint8_t n = SdStore::list(d, ".wav", paths, MAX_PAT_VOICES + 24);
  PatPool *pool = nullptr;
  for (uint8_t i = 0; i < n; i++) {
    char stem[32];
    SdStore::stem(paths[i], stem, sizeof(stem));
    for (char *c = stem; *c; c++) *c = (char)tolower((unsigned char)*c);
    if (!SoundManager::isPatVoiceName(stem)) continue;
    if (pool == nullptr) {
      pool = allocLarge<PatPool>(1);
      if (pool == nullptr) break;
      memset(pool, 0, sizeof(PatPool));
    }
    if (pool->n < MAX_PAT_VOICES && loadPcm(paths[i], pool->clip[pool->n], MAX_VOICE_SAMPLES)) pool->n++;
  }
  heap_caps_free(paths);
  if (pool != nullptr && pool->n == 0) {
    heap_caps_free(pool);
    pool = nullptr;
  }
  patPool[slot] = pool;
  if (pool != nullptr) Serial.printf("[SD] %s: %u pat voices\n", dir, (unsigned)pool->n);
  return pool != nullptr ? pool->n : 0;
}

void SoundManager::loadSd(uint8_t charCount, const char *(*folder)(uint8_t)) {
  if (!SdStore::ok()) return;
  char path[96];
  uint16_t loaded = 0;
  for (uint8_t slot = 0; slot < SLOTS; slot++) {
    if (slot < MAX_SLOTS && slot >= charCount) continue;
    const char *dir = slot < MAX_SLOTS ? folder(slot) : "common";
    for (uint8_t id = 0; id < (uint8_t)Sfx::Count; id++) {
      snprintf(path, sizeof(path), "%s/sound/%s/%s.wav", SdStore::ROOT, dir, SFX_FILE[id]);
      if (SdStore::fs().exists(path) && loadPcm(path, wavSfx[slot][id])) loaded++;
    }
    loaded += loadPatVoices(slot, dir, charCount, folder);
  }
  reloadMusic();
  Serial.printf("[SD] %u sound overrides, %u songs\n", (unsigned)loaded, (unsigned)sdSongCount);
}
