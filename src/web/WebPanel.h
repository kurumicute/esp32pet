#pragma once
#include <stdint.h>

class App;

// 手機網頁設定 (http://<板子IP>/ 或 http://deskpet.local/)
// ⚠️ 依使用者要求「不設密碼」：同一個 Wi-Fi 網路裡的任何人都能修改設定。
//    網頁不會顯示 Wi-Fi 密碼；所有欄位在後端驗證範圍與長度；輸出的字串都做 HTML 跳脫。
class WebPanel {
 public:
  void begin(App &app);  // 連上 Wi-Fi 後呼叫 (可重複呼叫)
  void update();
  void setCaptive(bool on);  // 設定熱點開著時：DNS 全部導向板子，自動跳出設定頁
  bool started() const { return started_; }

 private:
  App *app_ = nullptr;
  bool started_ = false;
};
