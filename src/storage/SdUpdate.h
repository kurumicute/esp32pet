#pragma once

class DisplayManager;

// 從 SD 卡更新韌體：開機時若有 /deskpet/update/firmware.bin，寫進另一個 OTA 分割區後重開機。
// 成功後檔案改名成 firmware.done (不會一直重刷)，失敗改名成 firmware.failed。
namespace SdUpdate {
void checkAndApply(DisplayManager &display);  // 有更新時不會回來 (直接重開機)
}
