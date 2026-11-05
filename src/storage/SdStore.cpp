#include "SdStore.h"

#include <Arduino.h>
#include <SD_MMC.h>
#include <esp_heap_caps.h>
#include <string.h>

#include "../config/BoardConfig.h"

namespace {
bool mounted = false;

bool endsWithNoCase(const char *s, const char *ext) {
  const size_t n = strlen(s), m = strlen(ext);
  if (m > n) return false;
  return strcasecmp(s + n - m, ext) == 0;
}
}  // namespace

namespace SdStore {

bool begin() {
  if (mounted) return true;
  if (!SD_MMC.setPins(Pins::SD_CLK, Pins::SD_CMD, Pins::SD_D0, Pins::SD_D1, Pins::SD_D2, Pins::SD_D3)) return false;
  if (!SD_MMC.begin("/sdcard", false)) {  // 4-bit；沒插卡會失敗
    Serial.println("[SD] no card");
    return false;
  }
  mounted = true;
  static const char *const DIRS[] = {"chars", "photos", "music", "sound", "sound/common", "diary",
                                     "log", "backup", "update"};
  ensureDir(ROOT);
  char p[48];
  for (const char *d : DIRS) {
    snprintf(p, sizeof(p), "%s/%s", ROOT, d);
    ensureDir(p);
  }
  Serial.printf("[SD] mounted, %lu MB used / %lu MB\n", (unsigned long)usedMB(), (unsigned long)totalMB());
  return true;
}

bool ok() { return mounted; }
fs::FS &fs() { return SD_MMC; }
uint32_t totalMB() { return mounted ? (uint32_t)(SD_MMC.totalBytes() / (1024ULL * 1024ULL)) : 0; }
uint32_t usedMB() { return mounted ? (uint32_t)(SD_MMC.usedBytes() / (1024ULL * 1024ULL)) : 0; }
uint8_t cardType() { return mounted ? (uint8_t)SD_MMC.cardType() : (uint8_t)CARD_NONE; }

bool ensureDir(const char *path) {
  if (!mounted) return false;
  if (SD_MMC.exists(path)) return true;
  return SD_MMC.mkdir(path);
}

uint8_t *readAll(const char *path, size_t &len, size_t maxLen) {
  len = 0;
  if (!mounted) return nullptr;
  File f = SD_MMC.open(path, FILE_READ);
  if (!f || f.isDirectory()) return nullptr;
  const size_t n = f.size();
  if (n == 0 || n > maxLen) {
    f.close();
    return nullptr;
  }
  uint8_t *buf = (uint8_t *)heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (buf == nullptr) {
    f.close();
    return nullptr;
  }
  const size_t got = f.read(buf, n);
  f.close();
  if (got != n) {
    heap_caps_free(buf);
    return nullptr;
  }
  len = n;
  return buf;
}

uint8_t list(const char *dir, const char *ext, char paths[][96], uint8_t maxCount) {
  if (!mounted) return 0;
  File d = SD_MMC.open(dir);
  if (!d || !d.isDirectory()) return 0;
  uint8_t n = 0;
  for (File f = d.openNextFile(); f && n < maxCount; f = d.openNextFile()) {
    const char *name = f.name();
    if (!f.isDirectory() && name[0] != '.' && endsWithNoCase(name, ext)) {
      snprintf(paths[n], 96, "%s/%s", dir, name);
      n++;
    }
    f.close();
  }
  d.close();
  // 依檔名排序 (插入排序，數量很少)
  for (uint8_t i = 1; i < n; i++) {
    char tmp[96];
    memcpy(tmp, paths[i], sizeof(tmp));
    int8_t j = (int8_t)i - 1;
    while (j >= 0 && strcmp(paths[j], tmp) > 0) {
      memcpy(paths[j + 1], paths[j], 96);
      j--;
    }
    memcpy(paths[j + 1], tmp, 96);
  }
  return n;
}

void stem(const char *path, char *out, size_t cap) {
  const char *base = strrchr(path, '/');
  base = base ? base + 1 : path;
  const char *dot = strrchr(base, '.');
  size_t n = dot ? (size_t)(dot - base) : strlen(base);
  if (n >= cap) n = cap - 1;
  memcpy(out, base, n);
  out[n] = '\0';
}

}  // namespace SdStore
