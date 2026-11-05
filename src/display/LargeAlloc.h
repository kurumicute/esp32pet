#pragma once
#include <esp_heap_caps.h>
#include <stddef.h>

// 大型緩衝區放在 PSRAM (開機配置一次，之後不釋放)，把晶片內部記憶體留給 Wi-Fi / 藍牙 / HTTPS
template <typename T>
T *allocLarge(size_t count) {
  void *p = heap_caps_calloc(count, sizeof(T), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (p == nullptr) p = heap_caps_calloc(count, sizeof(T), MALLOC_CAP_8BIT);  // 沒有 PSRAM 時退回內部
  return static_cast<T *>(p);
}
