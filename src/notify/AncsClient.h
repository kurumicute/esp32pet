#pragma once
#include <stdint.h>

// iPhone 通知同步 (Apple Notification Center Service, ANCS)
// - 板子用藍牙廣播「我想讀通知」，iPhone 在「設定 > 藍牙」連線配對 (要輸入板子螢幕上的 6 位數配對碼)
// - 配對後 iPhone 有新通知時，板子會收到 App、標題、內容
// - 配對資料存在 NVS，重開機不用重新配對；iPhone 不用安裝任何 App
// 藍牙事件在 NimBLE 工作中處理，收到的通知放進佇列，由主迴圈 pop() 取出顯示。

struct PhoneNotification {
  char app[24];       // 顯示用 App 名稱 (例如 IG、FB、LINE)
  char title[96];
  char message[200];
};

enum class AncsState : uint8_t { Off, Waiting, Pairing, Connected, Ready };

class AncsClient {
 public:
  void begin(const char *deviceName);
  void update(uint32_t now);
  AncsState state() const;
  bool pop(PhoneNotification &out);
  // 配對碼：iPhone 要求配對時產生，顯示在螢幕上；配對完成或斷線後清除
  bool passkey(uint32_t &out) const;
  void clearBonds();  // 刪除所有配對 (iPhone 端也要「忘記此裝置」)
  void setEnabled(bool on);  // 關閉 = 停止廣播並斷線 (藍牙堆疊保留，再開啟很快)
  bool started() const { return started_; }

 private:
  bool started_ = false;
};
