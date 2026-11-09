#pragma once
// 畫面上顯示的所有文字 (UTF-8)。
// ⚠️ 修改或新增中文字之後，請執行 python tools/gen_font.py 重新產生字型，
//    沒收錄的字在螢幕上會顯示成空白方框。

namespace Str {
// 星期 (tm_wday: 0 = 週日)
constexpr const char *WEEKDAY[] = {"週日", "週一", "週二", "週三", "週四", "週五", "週六"};

// 裝扮名稱 (順序與 CostumeManager 的 COSTUMES 表一致)
constexpr const char *COSTUME_NAMES[] = {"原始", "蝴蝶結", "皇冠", "圓框眼鏡", "葉子帽",
                                         "聖誕帽", "睡眠帽", "小披風", "星星眼"};

// 角色名稱 (順序與 tools/png2c.py 的 CHARACTERS / PET_FRAMES 一致)
constexpr const char *CHARACTER_NAMES[] = {"百鬼綾目（袴）"};
constexpr const char *CHARACTER_TITLE = "換角色";

// 主畫面
constexpr const char *SYNCING = "時間同步中…";
constexpr const char *NEED_WIFI = "請用 USB 設定 Wi-Fi";

// 時段小動作
constexpr const char *MSG_MORNING = "早安！伸個懶腰～";
constexpr const char *MSG_LUNCH = "午餐時間！";
constexpr const char *MSG_SNACK = "點心時間～";
constexpr const char *MSG_EVENING = "今天辛苦了！";
constexpr const char *MSG_NIGHT = "該準備睡覺囉";
constexpr const char *MSG_HOUR = "整點囉！";
constexpr const char *MSG_BIRTHDAY = "生日快樂！";
constexpr const char *MSG_LOW_BATTERY = "電量不足，請充電";
constexpr const char *MSG_CHARGING = "充電中～";
// 摸頭 (輪流顯示；hololive 直播台風格。一行最多約 9 個全形字)
constexpr const char *MSG_PAT[] = {"えへへ～被摸頭了", "謝謝老闆的摸摸！", "余は満足じゃ～", "摸摸也算スパチャ？",
                                   "好き好き大好き！", "再摸要收會員費喔", "被摸到了ぺこ！", "おつかれさま～♪"};

// 裝扮選單
constexpr const char *COSTUME_TITLE = "換裝";

// 設定
constexpr const char *SETTINGS_TITLE = "設定";
constexpr const char *SET_SLEEP = "夜間睡眠";
constexpr const char *SET_FROM = "睡覺時間";
constexpr const char *SET_UNTIL = "起床時間";
constexpr const char *SET_LIGHT = "亮度";
constexpr const char *SET_NAP = "閒置小睡";
constexpr const char *SET_AUTO = "自動裝扮";
constexpr const char *SET_POWER = "省電模式";
constexpr const char *SET_BMONTH = "生日月份";
constexpr const char *SET_BDAY = "生日日期";
constexpr const char *SET_BG = "背景";
constexpr const char *SET_WXFX = "天氣特效";
constexpr const char *SET_TEMP = "溫度表情";
constexpr const char *SET_COUNTY = "天氣縣市";
constexpr const char *SET_HOURLY = "整點動作";
constexpr const char *SET_TILT = "傾斜互動";
constexpr const char *SET_SHAKE = "搖晃靈敏度";
constexpr const char *SET_PAT = "拍頭偵測";
constexpr const char *PAT_LEVELS[] = {"關", "低", "中", "高"};
constexpr const char *SET_SOUND = "音效";
constexpr const char *SET_VOLUME = "音量";
constexpr const char *SET_ALARM = "鬧鐘";
constexpr const char *SET_ALARM_H = "鬧鐘（時）";
constexpr const char *SET_ALARM_M = "鬧鐘（分）";
constexpr const char *SET_ALARM_DAYS = "鬧鐘日子";
constexpr const char *ALARM_DAYS[] = {"每天", "平日", "週末"};
constexpr const char *SET_FOCUS = "專注時間";
constexpr const char *SET_BREAK = "休息時間";
constexpr const char *ON = "開";
constexpr const char *OFF = "關";
constexpr const char *AUTO = "自動";
constexpr const char *UNSET = "未設定";
constexpr const char *MINUTES = "分";
constexpr const char *MONTH = "月";
constexpr const char *DAY = "日";
constexpr const char *BG_MODES[] = {"自動", "清晨", "白天", "黃昏", "夜晚"};
constexpr const char *LEVELS[] = {"低", "中", "高"};
// 設定分類 (順序與 App::settingGroup 一致)
constexpr const char *SET_GROUPS[] = {"睡眠", "螢幕與電源", "裝扮", "背景與天氣", "互動", "番茄鐘", "聲音", "鬧鐘"};

// 天氣縣市 (中央氣象署名稱，順序 = 設定編號)
constexpr const char *COUNTIES[] = {
    "臺北市", "新北市", "基隆市", "桃園市", "新竹市", "新竹縣", "苗栗縣", "臺中市",
    "彰化縣", "南投縣", "雲林縣", "嘉義市", "嘉義縣", "臺南市", "高雄市", "屏東縣",
    "宜蘭縣", "花蓮縣", "臺東縣", "澎湖縣", "金門縣", "連江縣"};

// 番茄鐘
constexpr const char *TIMER_TITLE = "番茄鐘";
constexpr const char *TIMER_READY = "準備好了嗎？";
constexpr const char *TIMER_FOCUS = "專注";
constexpr const char *TIMER_BREAK = "休息";
constexpr const char *TIMER_PAUSED = "暫停中";
constexpr const char *MSG_FOCUS_DONE = "專注完成！休息一下";
constexpr const char *MSG_BREAK_DONE = "休息結束！";

// 資訊頁
constexpr const char *INFO_TITLE = "裝置資訊";
constexpr const char *INFO_WEB = "手機網頁設定";
constexpr const char *INFO_NO_WIFI = "Wi-Fi 未連線";
constexpr const char *INFO_BATTERY = "電池";
constexpr const char *INFO_HINT = "按任一鍵返回";
constexpr const char *INFO_WALK = "散步";

// 散步模式
constexpr const char *WALK_TITLE = "散步中";
constexpr const char *WALK_FLOWER = "發現一朵花！聞聞～";
constexpr const char *WALK_BUTTERFLY = "有蝴蝶飛過來了！";
constexpr const char *WALK_REST = "休息一下～";
constexpr const char *WALK_STEPS = "步";
constexpr const char *WALK_BACK = "散步回來了！";

// 每日小日記 (%u 等是數字，順序不要改)
constexpr const char *DIARY_TITLE = "日記";
constexpr const char *DIARY_WEATHER = "天氣%s，%d~%d%s";
constexpr const char *DIARY_HOT = "，熱到流汗了。";
constexpr const char *DIARY_COLD = "，有點冷，圍上圍巾。";
constexpr const char *DIARY_WALK = "去散步 %u 次，共 %u 分鐘、%u 步。";
constexpr const char *DIARY_ON_ROAD = "路上";
constexpr const char *DIARY_FLOWER = "聞了 %u 朵花";
constexpr const char *DIARY_BUTTERFLY = "看到 %u 隻蝴蝶";
constexpr const char *DIARY_PET = "被摸摸 %u 次，好開心～";
constexpr const char *DIARY_SHAKE = "被搖來搖去 %u 次。";
constexpr const char *DIARY_FOCUS = "陪你專注了 %u 輪，你好棒！";
constexpr const char *DIARY_NOTIFY = "幫你看了 %u 則通知。";  // 舊日記用 (iPhone 通知功能已移除)
constexpr const char *DIARY_NAP = "偷偷打了 %u 次盹。";
constexpr const char *DIARY_DRESS = "換了 %u 次衣服，穿著%s。";
constexpr const char *DIARY_WEAR = "今天穿著%s。";
constexpr const char *DIARY_GREAT = "今天是超棒的一天！";
constexpr const char *DIARY_GOOD = "今天過得很開心。";
constexpr const char *DIARY_QUIET = "今天有點安靜，想多跟你玩。";
constexpr const char *DIARY_EMPTY_TODAY = "今天還沒發生什麼事，陪我玩吧！";
constexpr const char *DIARY_EMPTY = "這天很安靜。";

// 天氣 (順序與 WeatherKind 一致)
constexpr const char *WEATHER_NAMES[] = {"--", "晴", "晴時多雲", "多雲", "雨", "雷雨", "雪", "霧"};
constexpr const char *DEG_C = "°C";
constexpr const char *RAIN_CHANCE = "降雨";
constexpr const char *INFO_NO_WEATHER = "天氣：未設定金鑰";

// Wi-Fi 設定熱點
constexpr const char *AP_TITLE = "Wi-Fi 設定";
constexpr const char *AP_STEP1 = "1. 手機連這個 Wi-Fi";
constexpr const char *AP_PASS = "密碼";
constexpr const char *AP_STEP2 = "2. 設定頁會自動跳出";
constexpr const char *AP_STEP2B = "沒跳出就開 192.168.4.1";
constexpr const char *AP_MODE = "Wi-Fi 設定模式（長按右鍵→裝置資訊）";
constexpr const char *WIFI_CONNECTED = "Wi-Fi 已連線！";

// 底部按鍵提示：左 / 中 / 右 三格，對應實體按鍵 BOOT (左)、PWR (中)、PLUS (右)
constexpr const char *KEY_EDIT = "修改";
constexpr const char *KEY_UP = "▲ 長按存";
constexpr const char *KEY_DOWN = "▼ 長按存";
constexpr const char *KEY_PREV = "‹ 上一個";
constexpr const char *KEY_OK = "確定";
constexpr const char *KEY_NEXT = "下一個 ›";
constexpr const char *KEY_BACK = "返回";
constexpr const char *KEY_DAY_PREV = "‹ 前一天";
constexpr const char *KEY_DAY_NEXT = "後一天 ›";
constexpr const char *KEY_PAT = "摸頭";
constexpr const char *KEY_HOME = "回家";
constexpr const char *KEY_TIME = "時間";
constexpr const char *KEY_BACK_STOP = "返回/長按停";
constexpr const char *KEY_START = "開始/暫停";
constexpr const char *KEY_NAME_L = "左鍵";
constexpr const char *KEY_NAME_M = "中鍵";
constexpr const char *KEY_NAME_R = "右鍵";

// 活動選單 (主畫面長按右鍵)
constexpr const char *MENU_TITLE = "活動";
constexpr const char *MENU_ITEMS[] = {"散步", "日記", "節奏遊戲", "音樂盒", "換角色", "相框", "裝置資訊"};
constexpr const char *MENU_DESC[] = {"帶牠出去走走", "最近 7 天", "跟著拍子拍拍", "聽歌", "選一隻陪你", "SD 卡照片", "網路與電池"};
constexpr const char *KEY_ENTER = "進入";
constexpr const char *KEY_UP_ONLY = "▲";
constexpr const char *KEY_DOWN_ONLY = "▼";

// 鬧鐘
constexpr const char *ALARM_TITLE = "起床囉！";
constexpr const char *ALARM_HINT = "左 / 右鍵：再睡 5 分鐘　中鍵：關閉";
constexpr const char *MSG_SNOOZE = "再睡 5 分鐘…";
constexpr const char *MSG_ALARM_OFF = "早安！今天也加油～";

// 節奏遊戲
constexpr const char *RHYTHM_TITLE = "節奏遊戲";
constexpr const char *RHYTHM_READY = "聽「噠」的拍子按鍵或拍我";
constexpr const char *RHYTHM_COUNT[] = {"3", "2", "1", "開始！"};
constexpr const char *RHYTHM_GREAT = "好棒！";
constexpr const char *RHYTHM_GOOD = "不錯～";
constexpr const char *RHYTHM_EARLY = "太早了";
constexpr const char *RHYTHM_LATE = "慢了一點";
constexpr const char *RHYTHM_RESULT = "跟上了 %u / %u 拍";
constexpr const char *RHYTHM_R_HIGH = "節奏大師！";
constexpr const char *RHYTHM_R_MID = "很有節奏感～";
constexpr const char *RHYTHM_R_LOW = "再玩一次吧！";
constexpr const char *KEY_HIT = "拍";
constexpr const char *KEY_AGAIN = "再玩";

// 音樂盒 (傳統兒歌，順序與 SoundManager 的曲目一致)
constexpr const char *MUSIC_TITLE = "音樂盒";
constexpr const char *MUSIC_EMPTY = "SD 卡 music/ 沒有歌曲";
constexpr const char *KEY_PREV_SONG = "‹ 上一首";
constexpr const char *KEY_NEXT_SONG = "下一首 ›";
constexpr const char *KEY_PLAY = "播放/停止";
constexpr const char *MUSIC_PLAYING = "♪ 播放中";
constexpr const char *MUSIC_IDLE = "按中鍵播放";

// 相框 (SD 卡照片)
constexpr const char *PHOTO_EMPTY = "SD 卡裡沒有照片";
constexpr const char *PHOTO_NO_SD = "沒有插 SD 卡";
constexpr const char *PHOTO_HOWTO = "照片放在 /deskpet/photos";
constexpr const char *KEY_PREV_PHOTO = "‹ 上一張";
constexpr const char *KEY_NEXT_PHOTO = "下一張 ›";

// SD 卡
constexpr const char *INFO_SD = "SD 卡";
constexpr const char *INFO_NO_SD = "SD 卡：未插入";
constexpr const char *FW_UPDATING = "韌體更新中…";
constexpr const char *FW_DONT_UNPLUG = "請不要拔掉電源";
constexpr const char *FW_DONE = "更新完成！";
constexpr const char *FW_FAILED = "韌體更新失敗";
constexpr const char *FW_BAD_FILE = "檔案不是 ESP32 韌體";

// 關機
constexpr const char *BYE = "掰掰～";
}  // namespace Str
