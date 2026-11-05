#include "Characters.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <string.h>

#include "../assets/Sprites.h"
#include "../audio/SoundManager.h"
#include "../display/LargeAlloc.h"
#include "../storage/SdStore.h"
#include "../ui/Strings.h"

static_assert(sizeof(Str::CHARACTER_NAMES) / sizeof(Str::CHARACTER_NAMES[0]) == PET_CHAR_COUNT,
              "Str::CHARACTER_NAMES 與 png2c.py 的 CHARACTERS 數量不一致");
static_assert(PET_CHAR_COUNT <= Characters::MAX, "內建角色太多");

namespace {
struct SdChar {
  char name[48];
  char folder[32];
  uint8_t voice;
  bool noCostume;
  Sprite spr[PF_COUNT];
  PetFrameAsset fr[PF_COUNT];
  uint8_t *blob;  // 檔案內容 (sprite 的調色盤 / 像素直接指到這裡)
};
SdChar *sdChars[Characters::MAX] = {};
uint8_t sdCount = 0;

constexpr size_t MAX_FILE = 256 * 1024;

// 解析 .dpc；成功時 c 的 sprite 指標指進 blob
bool parse(SdChar &c, uint8_t *b, size_t len) {
  size_t p = 0;
  auto need = [&](size_t n) { return p + n <= len; };
  if (!need(7) || memcmp(b, "DPC1", 4) != 0 || b[4] != 1) return false;
  c.noCostume = (b[5] & 0x80) != 0;
  c.voice = (b[5] & 0x7F) < SoundManager::SYNTH_VOICES ? (b[5] & 0x7F) : 0;
  const uint8_t nameLen = b[6];
  p = 7;
  if (!need(nameLen + 1u)) return false;
  if (nameLen > 0) {
    const size_t n = nameLen < sizeof(c.name) - 1 ? nameLen : sizeof(c.name) - 1;
    memcpy(c.name, b + p, n);
    c.name[n] = '\0';
  }
  p += nameLen;
  if (b[p++] != PF_COUNT) return false;
  for (uint8_t i = 0; i < PF_COUNT; i++) {
    if ((p + 3) % 2 != 0) p++;  // 補一個 0 讓調色盤 (u16) 對齊
    if (!need(3)) return false;
    const uint8_t w = b[p], h = b[p + 1], pal = b[p + 2];
    p += 3;
    if (w == 0 || h == 0 || w > 64 || h > 64 || pal == 0) return false;
    if (!need((size_t)pal * 2 + (size_t)w * h + 7)) return false;
    if (((uintptr_t)(b + p)) % 2 != 0) return false;  // 調色盤要 2 byte 對齊 (工具會補齊)
    c.spr[i] = {w, h, (const uint16_t *)(b + p), b + p + pal * 2};
    p += (size_t)pal * 2;
    for (size_t k = 0; k < (size_t)w * h; k++)
      if (b[p + k] >= pal) return false;  // 索引超出調色盤
    p += (size_t)w * h;
    c.fr[i].sprite = &c.spr[i];
    for (uint8_t a = 0; a < ANCHOR_COUNT; a++) {
      c.fr[i].anchors[a].x = (int8_t)b[p + a * 2];
      c.fr[i].anchors[a].y = (int8_t)b[p + a * 2 + 1];
    }
    c.fr[i].eyesOpen = b[p + 6] != 0;
    p += 7;
  }
  return true;
}
}  // namespace

namespace Characters {

void loadFromSd() {
  if (!SdStore::ok()) return;
  char (*paths)[96] = allocLarge<char[96]>(MAX);
  if (paths == nullptr) return;
  char dir[32];
  snprintf(dir, sizeof(dir), "%s/chars", SdStore::ROOT);
  const uint8_t n = SdStore::list(dir, ".dpc", paths, MAX - PET_CHAR_COUNT);
  for (uint8_t i = 0; i < n; i++) {
    size_t len = 0;
    uint8_t *blob = SdStore::readAll(paths[i], len, MAX_FILE);
    if (blob == nullptr) continue;
    SdChar *c = allocLarge<SdChar>(1);
    if (c == nullptr) {
      heap_caps_free(blob);
      break;
    }
    SdStore::stem(paths[i], c->folder, sizeof(c->folder));
    strncpy(c->name, c->folder, sizeof(c->name) - 1);
    if (!parse(*c, blob, len)) {
      Serial.printf("[SD] bad character file: %s\n", paths[i]);
      heap_caps_free(blob);
      heap_caps_free(c);
      continue;
    }
    c->blob = blob;
    sdChars[sdCount++] = c;
    Serial.printf("[SD] character %u: %s\n", (unsigned)(PET_CHAR_COUNT + sdCount - 1), c->name);
  }
  heap_caps_free(paths);
}

void reloadFromSd() {
  for (uint8_t i = 0; i < sdCount; i++) {
    heap_caps_free(sdChars[i]->blob);
    heap_caps_free(sdChars[i]);
    sdChars[i] = nullptr;
  }
  sdCount = 0;
  loadFromSd();
}

uint8_t count() { return PET_CHAR_COUNT + sdCount; }
uint8_t builtinCount() { return PET_CHAR_COUNT; }

const char *name(uint8_t c) {
  if (c < PET_CHAR_COUNT) return Str::CHARACTER_NAMES[c];
  c -= PET_CHAR_COUNT;
  return c < sdCount ? sdChars[c]->name : Str::CHARACTER_NAMES[0];
}

const char *folder(uint8_t c) {
  if (c < PET_CHAR_COUNT) return PET_CHAR_IDS[c];
  c -= PET_CHAR_COUNT;
  return c < sdCount ? sdChars[c]->folder : PET_CHAR_IDS[0];
}

uint8_t synthVoice(uint8_t c) {
  if (c < PET_CHAR_COUNT) return c;
  c -= PET_CHAR_COUNT;
  return c < sdCount ? sdChars[c]->voice : 0;
}

bool wearsCostumes(uint8_t c) {
  if (c < PET_CHAR_COUNT) return true;
  c -= PET_CHAR_COUNT;
  return c < sdCount ? !sdChars[c]->noCostume : true;
}

const PetFrameAsset &frame(uint8_t c, uint8_t pf) {
  if (pf >= PF_COUNT) pf = 0;
  if (c >= PET_CHAR_COUNT && (uint8_t)(c - PET_CHAR_COUNT) < sdCount) return sdChars[c - PET_CHAR_COUNT]->fr[pf];
  return *PET_FRAMES[c < PET_CHAR_COUNT ? c : 0][pf];
}

}  // namespace Characters
