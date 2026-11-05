#include "SdUpdate.h"

#include <Arduino.h>
#include <Update.h>
#include <esp_heap_caps.h>

#include "../display/DisplayManager.h"
#include "../display/Theme.h"
#include "../ui/Strings.h"
#include "SdStore.h"

namespace {
constexpr const char *FW_PATH = "/deskpet/update/firmware.bin";
constexpr const char *FW_DONE = "/deskpet/update/firmware.done";
constexpr const char *FW_FAILED = "/deskpet/update/firmware.failed";

void show(DisplayManager &d, const char *line1, const char *line2, uint16_t color) {
  d.clear(Theme::BG);
  d.utf8Centered(90, line1, 2, color, Theme::BG);
  if (line2) d.utf8Centered(130, line2, 1, Theme::DIM, Theme::BG);
}

void rename(const char *to) {
  SdStore::fs().remove(to);
  SdStore::fs().rename(FW_PATH, to);
}
}  // namespace

namespace SdUpdate {

void checkAndApply(DisplayManager &display) {
  if (!SdStore::ok() || !SdStore::fs().exists(FW_PATH)) return;
  File f = SdStore::fs().open(FW_PATH, FILE_READ);
  if (!f) return;
  const size_t size = f.size();
  const int magic = f.peek();
  Serial.printf("[SD] firmware update found (%u bytes)\n", (unsigned)size);
  if (size < 1024 || magic != 0xE9) {  // ESP32 韌體映像檔開頭一定是 0xE9
    f.close();
    rename(FW_FAILED);
    show(display, Str::FW_FAILED, Str::FW_BAD_FILE, Theme::WARN);
    delay(3000);
    return;
  }
  show(display, Str::FW_UPDATING, Str::FW_DONT_UNPLUG, Theme::TIME);
  constexpr size_t BUF = 4096;
  uint8_t *buf = (uint8_t *)heap_caps_malloc(BUF, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  bool ok = buf != nullptr && Update.begin(size);
  size_t written = 0;
  if (ok) {
    uint32_t lastDraw = 0;
    while (ok && f.available()) {
      const size_t n = f.read(buf, BUF);
      if (n == 0) break;
      ok = Update.write(buf, n) == n;
      written += n;
      if (millis() - lastDraw > 300) {  // 進度條
        lastDraw = millis();
        const int16_t w = (int16_t)(200UL * written / size);
        display.gfx().fillRoundRect(20, 170, 200, 10, 4, Theme::CARD);
        display.gfx().fillRoundRect(20, 170, w, 10, 4, Theme::ACCENT);
      }
    }
    ok = ok && written == size && Update.end(true);
  }
  if (buf) heap_caps_free(buf);
  f.close();
  if (!ok) {
    Serial.printf("[SD] firmware update failed: %s\n", Update.errorString());
    Update.abort();
    rename(FW_FAILED);
    show(display, Str::FW_FAILED, Update.errorString(), Theme::WARN);
    delay(3000);
    return;
  }
  rename(FW_DONE);
  show(display, Str::FW_DONE, nullptr, Theme::ACCENT);
  Serial.println("[SD] firmware updated, restarting");
  delay(1500);
  ESP.restart();
}

}  // namespace SdUpdate
