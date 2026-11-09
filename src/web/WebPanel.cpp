#include "WebPanel.h"

#include <DNSServer.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <WiFi.h>
#include <stdarg.h>

#include "../app/App.h"
#include "../pet/Characters.h"
#include "../storage/SdStore.h"
#include "../ui/Strings.h"

namespace {
WebServer server(80);
App *gApp = nullptr;

// 分段送出 HTML，用固定緩衝區，不在每次請求動態組大字串
char chunk[768];
void sendf(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(chunk, sizeof(chunk), fmt, ap);
  va_end(ap);
  server.sendContent(chunk);
}

// HTML 跳脫 (防 XSS)
void escapeHtml(const char *in, char *out, size_t cap) {
  size_t o = 0;
  for (; *in && o + 7 < cap; in++) {
    const char *rep = nullptr;
    switch (*in) {
      case '&': rep = "&amp;"; break;
      case '<': rep = "&lt;"; break;
      case '>': rep = "&gt;"; break;
      case '"': rep = "&quot;"; break;
      case '\'': rep = "&#39;"; break;
      default: break;
    }
    if (rep) {
      while (*rep) out[o++] = *rep++;
    } else {
      out[o++] = *in;
    }
  }
  out[o] = '\0';
}

void securityHeaders() {
  server.sendHeader("Cache-Control", "no-store");
  server.sendHeader("X-Content-Type-Options", "nosniff");
  server.sendHeader("X-Frame-Options", "DENY");
  server.sendHeader("Referrer-Policy", "no-referrer");
}

// 只接受純數字並檢查範圍
bool parseNum(const char *name, long lo, long hi, long &out) {
  if (!server.hasArg(name)) return false;
  const String v = server.arg(name);
  if (v.length() == 0 || v.length() > 4) return false;
  for (size_t i = 0; i < v.length(); i++)
    if (v[i] < '0' || v[i] > '9') return false;
  out = v.toInt();
  return out >= lo && out <= hi;
}

void sendPageHead();  // 定義在下方 (PAGE_HEAD 之後)

void badRequest(const char *why) {
  securityHeaders();
  char esc[160];
  escapeHtml(why, esc, sizeof(esc));
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(400, "text/html; charset=utf-8", "");
  sendPageHead();
  sendf("<title>DeskPet</title><div class=card><h2>⚠️ 設定沒有儲存</h2><div class=row>%s</div>"
        "<div class=row><a href='/'>← 返回</a></div></div></html>", esc);
  server.sendContent("");
}

// ---- 共用樣式 (卡片、開關、底部固定的儲存鈕) ----
const char PAGE_HEAD[] =
    "<!doctype html><html lang=zh-Hant><meta charset=utf-8>"
    "<meta name=viewport content='width=device-width,initial-scale=1'><meta name=theme-color content=#1d2740>"
    "<style>"
    ":root{--bg:#1d2740;--card:#27334f;--line:#34416a;--in:#1a2238;--txt:#f7f1e3;--dim:#8c98ba;--acc:#8ce6d2;--gold:#ffd678;--red:#ff9aa8}"
    "*{box-sizing:border-box}"
    "body{font-family:-apple-system,'PingFang TC','Noto Sans TC',sans-serif;background:var(--bg);color:var(--txt);"
    "margin:0 auto;padding:18px 14px 96px;max-width:540px;line-height:1.45}"
    "h1{font-size:22px;margin:2px 0 4px}h1 span{color:var(--acc)}"
    ".sub{color:var(--dim);font-size:13px;margin:0 0 14px}"
    ".card{background:var(--card);border-radius:18px;padding:4px 16px 8px;margin:0 0 14px;box-shadow:0 3px 12px #0003}"
    ".card h2{font-size:15px;color:var(--gold);margin:12px 0 2px;letter-spacing:.5px}"
    ".row{display:flex;align-items:center;justify-content:space-between;gap:12px;padding:11px 0;border-top:1px solid var(--line)}"
    ".card h2+.row,.card h2+form .row{border-top:0}"
    ".row>span{flex:1;min-width:0}.row small{display:block}"
    ".ctl{display:flex;align-items:center;gap:6px;flex:none;color:var(--dim)}"
    ".col{flex-direction:column;align-items:stretch}"
    "input,select{font:inherit;font-size:16px;padding:8px 10px;border-radius:10px;border:1px solid var(--line);"
    "background:var(--in);color:var(--txt)}"
    "select{max-width:190px}input[type=number]{width:72px;text-align:center}"
    "input[type=text],input[type=password]{width:100%}"
    ".sw{appearance:none;-webkit-appearance:none;width:48px;height:28px;border-radius:14px;background:#46527a;"
    "position:relative;flex:none;border:0;padding:0;margin:0;transition:.2s;cursor:pointer}"
    ".sw::after{content:'';position:absolute;top:3px;left:3px;width:22px;height:22px;border-radius:50%;"
    "background:#fff;transition:.2s;box-shadow:0 1px 3px #0005}"
    ".sw:checked{background:var(--acc)}.sw:checked::after{left:23px}"
    ".btn{display:block;text-align:center;text-decoration:none}"
    "button,.btn{font:inherit;font-size:16px;font-weight:bold;border:0;border-radius:12px;padding:12px 16px;"
    "background:var(--acc);color:#1d2740;cursor:pointer;width:100%}"
    ".b2{background:#34416a;color:var(--txt)}.bad{background:none;color:var(--red);border:1px solid #ff9aa866}"
    ".acts{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin:0 0 14px}.acts form{margin:0}"
    ".save{position:fixed;left:0;right:0;bottom:0;padding:12px 14px calc(12px + env(safe-area-inset-bottom));"
    "background:#1d2740e8;backdrop-filter:blur(8px);-webkit-backdrop-filter:blur(8px);border-top:1px solid var(--line)}"
    ".save button{display:block;max-width:512px;margin:0 auto}"
    ".kv{display:grid;grid-template-columns:auto 1fr;gap:6px 14px;padding:8px 0 10px}.kv b{color:var(--dim);font-weight:normal}"
    ".bar{display:inline-block;width:70px;height:10px;border-radius:5px;background:var(--in);vertical-align:middle;overflow:hidden;margin-right:6px}"
    ".bar i{display:block;height:100%;background:var(--acc)}"
    ".day{padding:10px 0;border-top:1px solid var(--line)}.day:first-of-type{border-top:0}.day b{color:var(--acc)}"
    ".day p{margin:4px 0 0}small{color:var(--dim);font-size:13px}a{color:var(--gold)}"
    ".note{color:var(--dim);font-size:12px;text-align:center;margin:18px 0}"
    "</style>";

void sendPageHead() { server.sendContent(PAGE_HEAD); }

void cardStart(const char *title) { sendf("<div class=card><h2>%s</h2>", title); }
void cardEnd() { server.sendContent("</div>"); }

// 開關列：標題 (+ 說明) 在左，開關在右
void toggleRow(const char *name, const char *label, bool on, const char *hint = nullptr) {
  sendf("<label class=row><span>%s%s%s%s</span><input type=checkbox class=sw name=%s value=1%s></label>", label,
        hint ? "<small>" : "", hint ? hint : "", hint ? "</small>" : "", name, on ? " checked" : "");
}

// 數字列：標題在左，輸入框 + 單位在右
void numRow(const char *name, const char *label, int lo, int hi, int step, int v, const char *unit,
            const char *hint = nullptr) {
  sendf("<label class=row><span>%s%s%s%s</span><span class=ctl><input type=number name=%s min=%d max=%d step=%d "
        "value=%d inputmode=numeric>%s</span></label>",
        label, hint ? "<small>" : "", hint ? hint : "", hint ? "</small>" : "", name, lo, hi, step, v, unit);
}

// 下拉選單列
void selectRow(const char *name, const char *label, const char *const *opts, uint8_t n, uint8_t cur) {
  sendf("<label class=row><span>%s</span><select name=%s>", label, name);
  for (uint8_t i = 0; i < n; i++) sendf("<option value=%u%s>%s</option>", i, i == cur ? " selected" : "", opts[i]);
  server.sendContent("</select></label>");
}

void hourRow(const char *name, const char *label, uint8_t cur) {
  sendf("<label class=row><span>%s</span><select name=%s>", label, name);
  for (uint8_t h = 0; h < 24; h++) sendf("<option value=%u%s>%02u:00</option>", h, h == cur ? " selected" : "", h);
  server.sendContent("</select></label>");
}

void actionButton(const char *action, const char *text, const char *cls, const char *confirmMsg = nullptr) {
  if (confirmMsg) {
    sendf("<form method=post action=%s onsubmit=\"return confirm('%s')\"><button class=%s>%s</button></form>",
          action, confirmMsg, cls, text);
  } else {
    sendf("<form method=post action=%s><button class=%s>%s</button></form>", action, cls, text);
  }
}

void sendDiary();

void handleRoot() {
  const Settings s = gApp->webSettings();
  const CostumeManager &cos = gApp->costumes();
  const BatteryMonitor &bat = gApp->battery();

  securityHeaders();
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html; charset=utf-8", "");
  server.sendContent(PAGE_HEAD);
  {
    char escName[96];
    escapeHtml(Characters::name(s.character), escName, sizeof(escName));
    sendf("<title>DeskPet</title><h1>💧 <span>%s桌寵</span></h1>"
          "<p class=sub>修改後按最下方「儲存設定」</p>",
          escName);
  }

  // ---- 狀態 ----
  char timeBuf[32];
  if (!gApp->timeString(timeBuf, sizeof(timeBuf))) strcpy(timeBuf, "尚未同步");
  const WeatherInfo w = gApp->weather();
  cardStart("📋 狀態");
  sendf("<div class=kv><b>時間</b><span>%s</span><b>電池</b><span><span class=bar><i style='width:%u%%'></i></span>"
        "%u%%（%.2fV）%s</span><b>IP</b><span>%s</span>",
        timeBuf, bat.percent(), bat.percent(), bat.voltage(), bat.charging() ? " ⚡充電中" : "",
        WiFi.localIP().toString().c_str());
  if (w.valid) {
    sendf("<b>天氣</b><span>%s %d~%d°C，降雨 %u%%</span>",
          Str::WEATHER_NAMES[(uint8_t)w.kind < 8 ? (uint8_t)w.kind : 0], w.minT, w.maxT, w.pop);
  }
  server.sendContent("</div>");
  cardEnd();

  // ---- 互動按鈕 (各自獨立的 form，不能放進設定表單裡) ----
  server.sendContent("<div class=acts>");
  actionButton("/pet", "🤚 摸摸頭", "b2");
  actionButton("/walk", "🌼 去散步", "b2");
  server.sendContent("</div>");

  server.sendContent("<form method=post action=/save>");

  // ---- 裝扮 ----
  cardStart("🎀 角色與裝扮");
  sendf("<label class=row><span>角色</span><select name=petChar>");
  for (uint8_t i = 0; i < Characters::count(); i++) {
    char esc[96];
    escapeHtml(Characters::name(i), esc, sizeof(esc));  // SD 卡角色名稱來自檔案，要跳脫
    sendf("<option value=%u%s>%s</option>", i, i == s.character ? " selected" : "", esc);
  }
  server.sendContent("</select></label>");
  sendf("<label class=row><span>目前裝扮</span><select name=costume>");
  for (uint8_t i = 0; i < cos.count(); i++)
    sendf("<option value=%u%s>%s</option>", i, i == s.costume ? " selected" : "", cos.get(i).name);
  server.sendContent("</select></label>");
  toggleRow("autoCos", "自動裝扮", s.autoCostume, "聖誕節聖誕帽、生日皇冠、睡覺睡眠帽");
  sendf("<label class=row><span>生日<small>0 = 不設定</small></span><span class=ctl>"
        "<input type=number name=bMonth min=0 max=12 value=%u inputmode=numeric>月"
        "<input type=number name=bDay min=0 max=31 value=%u inputmode=numeric>日</span></label>",
        s.birthMonth, s.birthDay);
  cardEnd();

  // ---- 睡眠與螢幕 ----
  static const char *const PS_OPTS[] = {"關", "自動（沒充電時）", "開"};
  cardStart("🌙 睡眠與螢幕");
  toggleRow("sleepOn", "夜間睡眠", s.sleepEnabled);
  hourRow("sleepS", "睡覺時間", s.sleepStart);
  hourRow("sleepE", "起床時間", s.sleepEnd);
  numRow("nap", "閒置小睡", 0, 60, 1, s.napMinutes, "分", "0 = 關閉");
  numRow("bright", "亮度", 10, 100, 10, s.brightness, "%");
  selectRow("pwrSave", "省電模式", PS_OPTS, 3, s.powerSave);
  cardEnd();

  // ---- 背景與互動 ----
  static const char *const BG_OPTS[] = {"自動（依時間）", "清晨", "白天", "黃昏", "夜晚"};
  static const char *const LV_OPTS[] = {"低", "中", "高"};
  cardStart("🌿 背景與互動");
  selectRow("bgMode", "背景", BG_OPTS, 5, s.bgMode);
  toggleRow("wxFx", "天氣特效", s.weatherFx, "雲、雨、雪、霧");
  toggleRow("tempMood", "溫度表情", s.tempMood, "≥32°C 扇子、≤15°C 圍巾");
  toggleRow("hourly", "整點小動作", s.hourlyFx);
  toggleRow("tilt", "傾斜互動", s.tiltOn, "歪一邊，寵物就看那一邊");
  selectRow("shake", "搖晃靈敏度", LV_OPTS, 3, s.shakeLevel);
  selectRow("patLv", "拍頭偵測（拍上方兩下）", Str::PAT_LEVELS, 4, s.patLevel);
  cardEnd();

  // ---- 聲音 ----
  cardStart("🔔 聲音");
  toggleRow("sound", "音效", s.soundOn, "預設關閉；睡覺時不會發出聲音");
  selectRow("vol", "音量", LV_OPTS, 3, s.soundVol);
  cardEnd();

  // ---- 鬧鐘 ----
  static const char *const DAY_OPTS[] = {"每天", "平日（週一～五）", "週末"};
  cardStart("⏰ 鬧鐘");
  toggleRow("alarmOn", "鬧鐘", s.alarmOn, "響鈴前 15 分鐘螢幕會慢慢變亮；鬧鐘一定有聲音");
  hourRow("alarmH", "幾點", s.alarmHour);
  numRow("alarmM", "幾分", 0, 59, 1, s.alarmMin, "分");
  selectRow("alarmD", "哪幾天", DAY_OPTS, 3, s.alarmDays);
  cardEnd();

  // ---- 番茄鐘 ----
  cardStart("🍅 番茄鐘");
  numRow("focus", "專注", 1, 90, 1, s.focusMin, "分");
  numRow("break", "休息", 1, 30, 1, s.breakMin, "分");
  cardEnd();


  // ---- 天氣 ----
  cardStart("☀️ 天氣（中央氣象署）");
  sendf("<label class=row><span>縣市</span><select name=county>");
  for (uint8_t i = 0; i < WeatherService::COUNTY_COUNT; i++)
    sendf("<option value=%u%s>%s</option>", i, i == s.county ? " selected" : "", WeatherService::countyName(i));
  server.sendContent("</select></label>");
  sendf("<label class='row col'><span>API 金鑰%s</span><input type=password name=cwaKey maxlength=64 "
        "autocomplete=off placeholder=\"%s\"></label>",
        w.valid || !s.cwaKey[0] ? "" : "<small>尚未取得資料</small>",
        s.cwaKey[0] ? "已設定（留空 = 不變更）" : "尚未設定");
  toggleRow("clearKey", "清除金鑰", false);
  if (!w.valid && s.cwaKey[0]) sendf("<div class=row><small>最近一次連線：HTTP %d</small></div>", w.lastHttp);
  cardEnd();

  // ---- Wi-Fi ----
  char ssidEsc[200];
  escapeHtml(s.ssid, ssidEsc, sizeof(ssidEsc));
  cardStart("📶 Wi-Fi");
  sendf("<label class='row col'><span>名稱（SSID）</span><input type=text name=ssid maxlength=32 value=\"%s\" "
        "autocomplete=off></label>"
        "<label class='row col'><span>密碼</span><input type=password name=pass maxlength=64 autocomplete=off "
        "placeholder=\"留空 = 不變更\"></label>"
        "<div class=row><small>只支援 2.4GHz。改了 Wi-Fi 後板子會重新連線，此頁面會斷線。</small></div>",
        ssidEsc);
  cardEnd();

  server.sendContent("<div class=save><button type=submit>💾 儲存設定</button></div></form>");

  sendDiary();

  cardStart("💾 SD 卡");
  if (SdStore::ok()) {
    sendf("<div class=row><span>已用 %lu / %lu MB<small>角色 %u 隻、音樂盒 %u 首</small></span></div>",
          (unsigned long)SdStore::usedMB(), (unsigned long)SdStore::totalMB(), (unsigned)Characters::count(),
          (unsigned)SoundManager::songCount());
    server.sendContent("<div class=acts style='margin:8px 0'><a class='btn b2' href='/history'>📔 日記封存</a>"
                       "<a class='btn b2' href='/stats'>📈 紀錄圖表</a></div>"
                       "<div class=acts style='margin:0 0 8px;grid-template-columns:1fr'><a class='btn b2' href='/upload'>"
                       "⬆️ 上傳角色 / 照片 / 歌曲 / 音效</a></div><div class=acts style='margin:0 0 8px'>");
    actionButton("/backup", "備份設定", "b2");
    actionButton("/restore", "還原設定", "b2", "用 SD 卡上的備份覆蓋目前的設定？（Wi-Fi 與金鑰不變）");
    server.sendContent("</div>");
  } else {
    server.sendContent("<div class=row><small>沒有插 SD 卡（插卡後重新開機）。</small></div>");
  }
  cardEnd();

  server.sendContent("<p class=note>⚠️ 此頁面沒有密碼保護，同一網路的人都能修改設定。</p></html>");
  server.sendContent("");  // 結束 chunked 傳輸
}

// 日記：內容只由程式內建文字 + 數字組成，仍一律跳脫後輸出
void sendDiary() {
  static char lines[Diary::MAX_LINES][Diary::LINE_LEN];
  char esc[Diary::LINE_LEN * 2];
  const Diary &diary = gApp->diary();
  cardStart("📔 每日小日記（最近 7 天）");
  for (uint8_t i = 0; i < diary.count(); i++) {
    const DiaryDay &d = diary.day(i);
    if (d.ymd != 0) {
      sendf("<div class=day><b>%lu/%lu %s%s</b><p>", (unsigned long)(d.ymd / 100 % 100),
            (unsigned long)(d.ymd % 100), Str::WEEKDAY[d.wday % 7], i == 0 ? "（今天）" : "");
    } else {
      server.sendContent("<div class=day><b>今天</b><p>");
    }
    const uint8_t n = Diary::compose(d, i == 0, lines, Diary::MAX_LINES);
    for (uint8_t k = 0; k < n; k++) {
      escapeHtml(lines[k], esc, sizeof(esc));
      sendf("%s", esc);
    }
    server.sendContent("</p></div>");
  }
  cardEnd();
}

void handleSave() {
  Settings n = gApp->webSettings();
  long v;
  if (!parseNum("costume", 0, gApp->costumes().count() - 1, v)) return badRequest("裝扮編號不正確");
  n.costume = (uint8_t)v;
  if (!parseNum("petChar", 0, Characters::count() - 1, v)) return badRequest("角色編號不正確");
  n.character = (uint8_t)v;
  if (!parseNum("sleepS", 0, 23, v)) return badRequest("睡覺時間不正確");
  n.sleepStart = (uint8_t)v;
  if (!parseNum("sleepE", 0, 23, v)) return badRequest("起床時間不正確");
  n.sleepEnd = (uint8_t)v;
  if (!parseNum("bright", 10, 100, v)) return badRequest("亮度需為 10~100");
  n.brightness = (uint8_t)v;
  if (!parseNum("nap", 0, 60, v)) return badRequest("閒置小睡需為 0~60 分鐘");
  n.napMinutes = (uint8_t)v;
  if (!parseNum("focus", 1, 90, v)) return badRequest("專注時間需為 1~90 分鐘");
  n.focusMin = (uint8_t)v;
  if (!parseNum("break", 1, 30, v)) return badRequest("休息時間需為 1~30 分鐘");
  n.breakMin = (uint8_t)v;
  long m, d;
  if (!parseNum("bMonth", 0, 12, m) || !parseNum("bDay", 0, 31, d)) return badRequest("生日格式不正確");
  static const uint8_t DAYS[] = {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if ((m == 0) != (d == 0) || (m > 0 && d > DAYS[m - 1])) return badRequest("生日日期不存在");
  n.birthMonth = (uint8_t)m;
  n.birthDay = (uint8_t)d;
  if (!parseNum("county", 0, WeatherService::COUNTY_COUNT - 1, v)) return badRequest("縣市不正確");
  n.county = (uint8_t)v;
  const String key = server.arg("cwaKey");
  if (server.hasArg("clearKey")) {
    n.cwaKey[0] = '\0';
  } else if (key.length() > 0) {
    if (key.length() > 64 || !WeatherService::validKey(key.c_str())) return badRequest("API 金鑰格式不正確");
    strncpy(n.cwaKey, key.c_str(), sizeof(n.cwaKey) - 1);
    n.cwaKey[sizeof(n.cwaKey) - 1] = '\0';
  }
  if (!parseNum("bgMode", 0, 4, v)) return badRequest("背景選項不正確");
  n.bgMode = (uint8_t)v;
  if (!parseNum("shake", 0, 2, v)) return badRequest("搖晃靈敏度不正確");
  n.shakeLevel = (uint8_t)v;
  if (!parseNum("patLv", 0, 3, v)) return badRequest("拍頭偵測設定不正確");
  n.patLevel = (uint8_t)v;
  if (!parseNum("vol", 0, 2, v)) return badRequest("音量設定不正確");
  n.soundVol = (uint8_t)v;
  n.soundOn = server.hasArg("sound");
  if (!parseNum("alarmH", 0, 23, v)) return badRequest("鬧鐘時間（時）不正確");
  n.alarmHour = (uint8_t)v;
  if (!parseNum("alarmM", 0, 59, v)) return badRequest("鬧鐘時間（分）需為 0~59");
  n.alarmMin = (uint8_t)v;
  if (!parseNum("alarmD", 0, 2, v)) return badRequest("鬧鐘日子不正確");
  n.alarmDays = (uint8_t)v;
  n.alarmOn = server.hasArg("alarmOn");
  if (!parseNum("pwrSave", 0, 2, v)) return badRequest("省電模式不正確");
  n.powerSave = (uint8_t)v;
  n.weatherFx = server.hasArg("wxFx");
  n.tempMood = server.hasArg("tempMood");
  n.hourlyFx = server.hasArg("hourly");
  n.tiltOn = server.hasArg("tilt");
  n.sleepEnabled = server.hasArg("sleepOn");
  n.autoCostume = server.hasArg("autoCos");

  const String ssid = server.arg("ssid");
  const String pass = server.arg("pass");
  if (ssid.length() > 32) return badRequest("SSID 最多 32 字元");
  if (pass.length() > 64 || (pass.length() > 0 && pass.length() < 8)) return badRequest("密碼需為 8~64 字元");
  bool wifiChanged = false;
  if (ssid != n.ssid || pass.length() > 0) {
    // 密碼留空：同一個 SSID 保留原密碼；換了 SSID 則視為開放網路
    if (pass.length() > 0) {
      strncpy(n.pass, pass.c_str(), sizeof(n.pass) - 1);
      n.pass[sizeof(n.pass) - 1] = '\0';
    } else if (ssid != n.ssid) {
      n.pass[0] = '\0';
    }
    strncpy(n.ssid, ssid.c_str(), sizeof(n.ssid) - 1);
    n.ssid[sizeof(n.ssid) - 1] = '\0';
    wifiChanged = true;
  }

  gApp->webApply(n, wifiChanged);
  securityHeaders();
  server.sendHeader("Location", "/");
  server.send(303, "text/plain", "");
}

void handleWalk() {
  gApp->webStartWalk();
  securityHeaders();
  server.sendHeader("Location", "/");
  server.send(303, "text/plain", "");
}

void handlePet() {
  gApp->webPokePet();
  securityHeaders();
  server.sendHeader("Location", "/");
  server.send(303, "text/plain", "");
}

// ---------------- SD 卡：日記封存 / 紀錄圖表 / 下載 / 備份 ----------------
// 參數只接受固定長度的純數字
bool digitsArg(const char *name, size_t len, char *out) {
  if (!server.hasArg(name)) return false;
  const String v = server.arg(name);
  if (v.length() != len) return false;
  for (size_t i = 0; i < len; i++)
    if (v[i] < '0' || v[i] > '9') return false;
  memcpy(out, v.c_str(), len + 1);
  return true;
}

void pageStart(const char *title) {
  securityHeaders();
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html; charset=utf-8", "");
  sendPageHead();
  sendf("<title>DeskPet</title><h1>%s</h1><p class=sub><a href='/'>← 返回</a></p>", title);
}

void pageEnd() {
  server.sendContent("</html>");
  server.sendContent("");
}

void msgPage(const char *title, const char *msg) {
  pageStart(title);
  sendf("<div class=card><div class=row>%s</div></div>", msg);
  pageEnd();
}

// 列出資料夾裡的項目名稱 (只取 len 長度的純數字名稱，例如 YYYYMM)，由新到舊
uint8_t listDigitNames(const char *dir, size_t len, const char *suffix, char names[][16], uint8_t maxN) {
  uint8_t n = 0;
  File d = SdStore::fs().open(dir);
  if (!d || !d.isDirectory()) return 0;
  for (File f = d.openNextFile(); f && n < maxN; f = d.openNextFile()) {
    const char *nm = f.name();
    const size_t sl = strlen(suffix);
    bool ok = strlen(nm) == len + sl && strcmp(nm + len, suffix) == 0;
    for (size_t i = 0; ok && i < len; i++) ok = nm[i] >= '0' && nm[i] <= '9';
    if (ok) {
      memcpy(names[n], nm, len);
      names[n][len] = '\0';
      n++;
    }
    f.close();
  }
  d.close();
  for (uint8_t i = 1; i < n; i++) {  // 由新到舊
    char tmp[16];
    memcpy(tmp, names[i], 16);
    int8_t j = (int8_t)i - 1;
    while (j >= 0 && strcmp(names[j], tmp) < 0) {
      memcpy(names[j + 1], names[j], 16);
      j--;
    }
    memcpy(names[j + 1], tmp, 16);
  }
  return n;
}

void handleHistory() {
  if (!SdStore::ok()) return msgPage("📔 日記封存", "沒有插 SD 卡。");
  static char months[60][16];
  char dir[48];
  snprintf(dir, sizeof(dir), "%s/diary", SdStore::ROOT);
  const uint8_t nm = listDigitNames(dir, 6, "", months, 60);
  char m[8] = "";
  if (!digitsArg("m", 6, m) && nm > 0) memcpy(m, months[0], 7);
  pageStart("📔 日記封存");
  if (nm == 0) {
    server.sendContent("<div class=card><div class=row>還沒有封存的日記（每 10 分鐘與換日時會自動存一次）。</div></div>");
    return pageEnd();
  }
  server.sendContent("<div class=card><h2>月份</h2><div class=row style='flex-wrap:wrap;justify-content:flex-start'>");
  for (uint8_t i = 0; i < nm; i++)
    sendf("<a href='/history?m=%s'>%c%.4s/%.2s%c</a>", months[i], strcmp(months[i], m) == 0 ? '[' : ' ', months[i],
          months[i] + 4, strcmp(months[i], m) == 0 ? ']' : ' ');
  server.sendContent("</div></div>");

  static char days[31][16];
  snprintf(dir, sizeof(dir), "%s/diary/%s", SdStore::ROOT, m);
  const uint8_t nd = listDigitNames(dir, 2, ".txt", days, 31);
  sendf("<div class=card><h2>%.4s 年 %.2s 月</h2>", m, m + 4);
  sendf("<div class=row><a href='/history.csv?m=%s'>⬇️ 下載這個月的統計 (CSV)</a></div>", m);
  static char line[200];
  static char esc[400];
  for (uint8_t i = 0; i < nd; i++) {
    char path[64];
    snprintf(path, sizeof(path), "%s/%s.txt", dir, days[i]);
    File f = SdStore::fs().open(path, FILE_READ);
    if (!f) continue;
    f.readBytesUntil('\n', line, sizeof(line) - 1);  // 第一行是統計，略過
    sendf("<div class=day><b>%.2s/%s</b><p>", m + 4, days[i]);
    while (f.available()) {
      const size_t n = f.readBytesUntil('\n', line, sizeof(line) - 1);
      line[n] = '\0';
      escapeHtml(line, esc, sizeof(esc));
      sendf("%s", esc);
    }
    f.close();
    server.sendContent("</p></div>");
  }
  server.sendContent("</div>");
  pageEnd();
}

void handleHistoryCsv() {
  char m[8];
  if (!SdStore::ok() || !digitsArg("m", 6, m)) return server.send(400, "text/plain", "bad request");
  static char days[31][16];
  char dir[48];
  snprintf(dir, sizeof(dir), "%s/diary/%s", SdStore::ROOT, m);
  const uint8_t nd = listDigitNames(dir, 2, ".txt", days, 31);
  securityHeaders();
  snprintf(chunk, sizeof(chunk), "attachment; filename=diary-%s.csv", m);
  server.sendHeader("Content-Disposition", chunk);
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/csv; charset=utf-8", "");
  server.sendContent("date,weekday,pet,shake,nap,focus,notify,dress,walk,flower,butterfly,walkSec,steps,minT,maxT,weather,costume\n");
  static char line[200];
  for (int i = nd - 1; i >= 0; i--) {  // 舊到新
    char path[64];
    snprintf(path, sizeof(path), "%s/%s.txt", dir, days[i]);
    File f = SdStore::fs().open(path, FILE_READ);
    if (!f) continue;
    const size_t n = f.readBytesUntil('\n', line, sizeof(line) - 2);
    f.close();
    line[n] = '\n';
    line[n + 1] = '\0';
    server.sendContent(line);
  }
  server.sendContent("");
}

void handleStats() {
  if (!SdStore::ok()) return msgPage("📈 紀錄圖表", "沒有插 SD 卡。");
  static char daysList[60][16];
  char dir[48];
  snprintf(dir, sizeof(dir), "%s/log", SdStore::ROOT);
  const uint8_t nl = listDigitNames(dir, 8, ".csv", daysList, 60);
  char d[10] = "";
  if (!digitsArg("d", 8, d) && nl > 0) memcpy(d, daysList[0], 9);
  pageStart("📈 紀錄圖表");
  if (nl == 0) {
    server.sendContent("<div class=card><div class=row>還沒有紀錄（時間同步後每 10 分鐘記一筆）。</div></div>");
    return pageEnd();
  }
  // 讀這一天的紀錄
  static uint16_t minute[160];
  static uint8_t battery[160], pets[160];
  uint8_t rows = 0;
  char path[64];
  snprintf(path, sizeof(path), "%s/%s.csv", dir, d);
  File f = SdStore::fs().open(path, FILE_READ);
  static char line[120];
  if (f) {
    f.readBytesUntil('\n', line, sizeof(line) - 1);  // 標題列
    while (f.available() && rows < 160) {
      const size_t n = f.readBytesUntil('\n', line, sizeof(line) - 1);
      line[n] = '\0';
      unsigned hh, mm, bat, charging, pt, sk;
      float volt;
      int rssi;
      if (sscanf(line, "%u:%u,%u,%f,%u,%d,%u,%u", &hh, &mm, &bat, &volt, &charging, &rssi, &pt, &sk) != 8) continue;
      if (hh > 23 || mm > 59 || bat > 100) continue;
      minute[rows] = (uint16_t)(hh * 60 + mm);
      battery[rows] = (uint8_t)bat;
      pets[rows] = (uint8_t)(pt > 255 ? 255 : pt);
      rows++;
    }
    f.close();
  }
  // 每小時被摸幾次 (紀錄是當天累計，相減得到每小時的量)
  uint16_t perHour[24] = {};
  uint8_t prev = 0;
  for (uint8_t i = 0; i < rows; i++) {
    const uint8_t v = pets[i];
    perHour[minute[i] / 60] += v >= prev ? v - prev : v;
    prev = v;
  }
  uint16_t maxH = 1;
  for (uint16_t v : perHour)
    if (v > maxH) maxH = v;

  sendf("<div class=card><h2>%.4s/%.2s/%.2s</h2>", d, d + 4, d + 6);
  // 電量折線
  server.sendContent("<div class=row><span>電量 %<svg viewBox='0 0 300 110' style='width:100%;background:#1a2238;border-radius:10px'>"
                     "<g stroke='#34416a'><line x1=10 y1=10 x2=290 y2=10 /><line x1=10 y1=55 x2=290 y2=55 />"
                     "<line x1=10 y1=100 x2=290 y2=100 /></g><polyline fill=none stroke='#8ce6d2' stroke-width=2 points='");
  for (uint8_t i = 0; i < rows; i++)
    sendf("%u,%u ", (unsigned)(10 + minute[i] * 280UL / 1440), (unsigned)(100 - battery[i] * 90U / 100));
  server.sendContent("'/><g fill='#8c98ba' font-size=9><text x=12 y=20>100</text><text x=12 y=97>0</text>"
                     "<text x=10 y=109>0時</text><text x=143 y=109>12時</text><text x=272 y=109>24時</text></g></svg></span></div>");
  // 每小時摸頭長條圖
  server.sendContent("<div class=row><span>每小時被摸幾次<svg viewBox='0 0 300 110' style='width:100%;background:#1a2238;border-radius:10px'>");
  for (uint8_t h = 0; h < 24; h++) {
    const unsigned bh = perHour[h] * 85U / maxH;
    sendf("<rect x=%u y=%u width=9 height=%u fill='#ffd678'><title>%u時：%u 次</title></rect>", 12 + h * 11 + 1,
          97 - bh, bh, h, perHour[h]);
  }
  server.sendContent("<g fill='#8c98ba' font-size=9><text x=10 y=109>0時</text><text x=140 y=109>12時</text>"
                     "<text x=270 y=109>23時</text></g></svg></span></div>");
  sendf("<div class=row><a href='/dl?f=log/%s.csv'>⬇️ 下載這天的紀錄 (CSV)</a></div></div>", d);
  // 其他天
  server.sendContent("<div class=card><h2>其他天</h2><div class=row style='flex-wrap:wrap;justify-content:flex-start'>");
  for (uint8_t i = 0; i < nl; i++)
    sendf("<a href='/stats?d=%s'>%.2s/%.2s</a>", daysList[i], daysList[i] + 4, daysList[i] + 6);
  server.sendContent("</div></div>");
  pageEnd();
}

// 下載 SD 卡上的紀錄 / 日記 / 備份 (只允許這三個資料夾，不允許 ..)
void handleDownload() {
  const String rel = server.arg("f");
  bool ok = SdStore::ok() && rel.length() > 0 && rel.length() < 60 && rel.indexOf("..") < 0 &&
            (rel.startsWith("log/") || rel.startsWith("diary/") || rel.startsWith("backup/"));
  for (size_t i = 0; ok && i < rel.length(); i++) {
    const char c = rel[i];
    ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '/' || c == '.' ||
         c == '_' || c == '-';
  }
  if (!ok) return server.send(400, "text/plain", "bad request");
  char path[96];
  snprintf(path, sizeof(path), "%s/%s", SdStore::ROOT, rel.c_str());
  File f = SdStore::fs().open(path, FILE_READ);
  if (!f || f.isDirectory()) {
    if (f) f.close();
    return server.send(404, "text/plain", "not found");
  }
  const char *base = strrchr(path, '/') + 1;
  securityHeaders();
  snprintf(chunk, sizeof(chunk), "attachment; filename=%s", base);
  server.sendHeader("Content-Disposition", chunk);
  server.streamFile(f, rel.endsWith(".csv") ? "text/csv" : "text/plain");
  f.close();
}

void handleBackup() {
  if (gApp->webBackup()) msgPage("💾 備份設定", "✅ 已備份到 SD 卡 /deskpet/backup/settings.txt（不含 Wi-Fi 密碼與 API 金鑰）。");
  else msgPage("💾 備份設定", "⚠️ 備份失敗：沒有插 SD 卡？");
}

void handleRestore() {
  if (gApp->webRestore()) msgPage("💾 還原設定", "✅ 已從 SD 卡還原設定。");
  else msgPage("💾 還原設定", "⚠️ 還原失敗：沒有插 SD 卡，或卡上沒有備份檔。");
}

// ---------------- SD 卡：網頁上傳 / 刪除 (只限角色、照片、歌曲、音效；韌體不開放網頁上傳) ----------------
struct UpKind {
  const char *dir;
  const char *ext;
  size_t maxBytes;
  const char *label;
};
const UpKind UP_KINDS[] = {
    {"chars", ".dpc", 256 * 1024, "角色 (.dpc)"},
    {"photos", ".565", 240 * 240 * 2, "照片 (.565)"},
    {"music", ".wav", 60UL * 1024 * 1024, "歌曲 (.wav)"},
    {"sound", ".wav", 1024 * 1024, "音效 (.wav)"},
};

const UpKind *findKind(const String &dir) {
  for (const UpKind &k : UP_KINDS)
    if (dir == k.dir) return &k;
  return nullptr;
}

// 檔名：可以有中文 (UTF-8)，不能有路徑、控制字元、開頭的點，長度有限
bool safeFileName(const String &n, const char *ext) {
  if (n.length() < 5 || n.length() > 60 || n[0] == '.' || n.indexOf("..") >= 0) return false;
  for (size_t i = 0; i < n.length(); i++) {
    const uint8_t c = (uint8_t)n[i];
    if (c < 0x20 || c == 0x7F || c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' ||
        c == '<' || c == '>' || c == '|' || c == '\'' || c == '`' || c == '&')
      return false;
  }
  String lower = n;
  lower.toLowerCase();
  return lower.endsWith(ext);
}

// 音效子資料夾：common 或某個角色的資料夾名稱
bool validSoundSub(const String &sub) {
  if (sub == "common") return true;
  for (uint8_t i = 0; i < Characters::count(); i++)
    if (sub == Characters::folder(i)) return true;
  return false;
}

File upFile;
char upTmp[128];
char upFinal[128];
const UpKind *upKind = nullptr;
const char *upError = nullptr;
size_t upWritten = 0;

void upFail(const char *why) {
  if (upError == nullptr) upError = why;
  if (upFile) upFile.close();
  if (upTmp[0]) SdStore::fs().remove(upTmp);
  upTmp[0] = '\0';
}

void handleUploadData() {
  HTTPUpload &up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    upError = nullptr;
    upTmp[0] = upFinal[0] = '\0';
    upWritten = 0;
    upKind = findKind(server.arg("dir"));
    if (!SdStore::ok()) return upFail("沒有插 SD 卡");
    if (upKind == nullptr) return upFail("不支援的類別");
    String name = up.filename;
    const int slash = name.lastIndexOf('/') > name.lastIndexOf('\\') ? name.lastIndexOf('/') : name.lastIndexOf('\\');
    if (slash >= 0) name = name.substring(slash + 1);
    String lower = name;
    lower.toLowerCase();
    if (!safeFileName(name, upKind->ext) || !lower.endsWith(upKind->ext)) return upFail("檔名或副檔名不正確");
    char sub[40] = "";
    if (strcmp(upKind->dir, "sound") == 0) {
      const String s = server.arg("sub");
      if (!validSoundSub(s)) return upFail("音效資料夾不正確");
      String stem = lower.substring(0, lower.length() - 4);
      if (!SoundManager::isSfxName(stem.c_str())) return upFail("音效檔名要是 cry、cry_happy、alarm、pat_1… 這類名稱");
      snprintf(sub, sizeof(sub), "/%s", s.c_str());
      name = lower;
    }
    char dir[96];
    snprintf(dir, sizeof(dir), "%s/%s%s", SdStore::ROOT, upKind->dir, sub);
    SdStore::ensureDir(dir);
    snprintf(upFinal, sizeof(upFinal), "%s/%s", dir, name.c_str());
    snprintf(upTmp, sizeof(upTmp), "%s/upload.part", dir);
    SdStore::fs().remove(upTmp);
    upFile = SdStore::fs().open(upTmp, FILE_WRITE);
    if (!upFile) return upFail("SD 卡寫入失敗");
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (upError || !upFile) return;
    if (upWritten + up.currentSize > upKind->maxBytes) return upFail("檔案太大");
    if (upFile.write(up.buf, up.currentSize) != up.currentSize) return upFail("SD 卡寫入失敗 (空間不足？)");
    upWritten += up.currentSize;
  } else if (up.status == UPLOAD_FILE_END) {
    if (upError || !upFile) return;
    upFile.close();
    if (strcmp(upKind->dir, "photos") == 0 && upWritten != upKind->maxBytes) return upFail("照片大小不對 (要用 tools/sdcard.py photo 轉換)");
    if (upWritten == 0) return upFail("空檔案");
    SdStore::fs().remove(upFinal);  // 同名就覆蓋
    if (!SdStore::fs().rename(upTmp, upFinal)) return upFail("SD 卡寫入失敗");
    upTmp[0] = '\0';
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    upFail("上傳中斷");
  }
}

void handleUploadDone() {
  const bool ok = upError == nullptr && upFinal[0] != '\0' && upTmp[0] == '\0';
  if (ok && upKind) gApp->webSdChanged(upKind->dir);
  if (server.hasArg("api")) {  // tools/sdcard.py push 用：純文字回應
    securityHeaders();
    server.send(ok ? 200 : 400, "text/plain; charset=utf-8", ok ? "ok" : (upError ? upError : "失敗"));
    return;
  }
  char esc[200];
  escapeHtml(ok ? upFinal : (upError ? upError : "失敗"), esc, sizeof(esc));
  pageStart(ok ? "✅ 上傳完成" : "⚠️ 上傳失敗");
  sendf("<div class=card><div class=row>%s</div>%s<div class=row><a href='/upload'>← 繼續上傳</a></div></div>", esc,
        ok && strcmp(upKind->dir, "sound") == 0 ? "<div class=row><small>音效要重新開機才會生效。</small></div>" : "");
  pageEnd();
}

void handleUploadPage() {
  if (!SdStore::ok()) return msgPage("⬆️ 上傳到 SD 卡", "沒有插 SD 卡。");
  pageStart("⬆️ 上傳到 SD 卡");
  server.sendContent("<p class=sub>檔案先用電腦上的 <b>tools/sdcard.py</b> 轉換格式。角色、歌曲上傳完馬上可以用；音效要重新開機。</p>");
  static char paths[40][96];
  for (const UpKind &k : UP_KINDS) {
    sendf("<div class=card><h2>%s</h2>", k.label);
    sendf("<form method=post enctype=multipart/form-data action='/upload?dir=%s' "
          "onsubmit=\"var s=this.querySelector('select');if(s)this.action='/upload?dir=%s&sub='+s.value\">",
          k.dir, k.dir);
    if (strcmp(k.dir, "sound") == 0) {
      server.sendContent("<label class=row><span>給誰用<small>檔名要是 cry.wav、alarm.wav、pat_1.wav… 這類名稱</small></span>"
                         "<select><option value=common>全部角色共用</option>");
      for (uint8_t i = 0; i < Characters::count(); i++) {
        char esc[96];
        escapeHtml(Characters::name(i), esc, sizeof(esc));
        sendf("<option value='%s'>%s</option>", Characters::folder(i), esc);
      }
      server.sendContent("</select></label>");
    }
    sendf("<div class=row><input type=file name=f accept='%s' required style='max-width:60%%'>"
          "<button style='width:auto'>上傳</button></div></form>", k.ext);
    // 已經在卡上的檔案 (音效資料夾裡是子資料夾，不列)
    if (strcmp(k.dir, "sound") != 0) {
      char dir[48];
      snprintf(dir, sizeof(dir), "%s/%s", SdStore::ROOT, k.dir);
      const uint8_t n = SdStore::list(dir, k.ext, paths, 40);
      for (uint8_t i = 0; i < n; i++) {
        const char *base = strrchr(paths[i], '/') + 1;
        char esc[160];
        escapeHtml(base, esc, sizeof(esc));
        sendf("<div class=row><span>%s</span><form method=post action=/sddel onsubmit=\"return confirm('刪除這個檔案？')\">",
              esc);
        sendf("<input type=hidden name=dir value=%s><input type=hidden name=f value=\"%s\">"
              "<button class=bad style='width:auto;padding:6px 12px'>刪除</button></form></div>",
              k.dir, esc);
      }
    }
    server.sendContent("</div>");
  }
  server.sendContent("<p class=note>⚠️ 沒有密碼保護：同一個網路的人也能上傳或刪除這些檔案。韌體更新檔不能從網頁上傳。</p>");
  pageEnd();
}

void handleSdDelete() {
  const UpKind *k = findKind(server.arg("dir"));
  const String name = server.arg("f");
  if (!SdStore::ok() || k == nullptr || strcmp(k->dir, "sound") == 0 || !safeFileName(name, k->ext))
    return msgPage("🗑️ 刪除", "⚠️ 參數不正確。");
  char path[128];
  snprintf(path, sizeof(path), "%s/%s/%s", SdStore::ROOT, k->dir, name.c_str());
  const bool ok = SdStore::fs().remove(path);
  if (ok) gApp->webSdChanged(k->dir);
  securityHeaders();
  server.sendHeader("Location", "/upload");
  server.send(303, "text/plain", "");
}

// ---------------- 設定熱點：強制登入頁 (captive portal) ----------------
DNSServer dns;
bool captive = false;

void redirectToSetup() {
  server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/wifi");
  server.send(302, "text/plain", "");
}

// 手機連上熱點時會先打這些網址檢查能不能上網；導到設定頁就會自動跳出登入視窗
const char *const PROBE_PATHS[] = {"/hotspot-detect.html", "/library/test/success.html",
                                   "/generate_204", "/gen_204", "/connecttest.txt",
                                   "/ncsi.txt", "/redirect", "/fwlink", "/canonical.html"};

void handleWifiPage() {
  if (server.hasArg("rescan")) WiFi.scanNetworks(true);  // 非同步掃描
  securityHeaders();
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html; charset=utf-8", "");
  sendPageHead();
  server.sendContent("<title>DeskPet Wi-Fi</title><h1>📶 <span>Wi-Fi 設定</span></h1>"
                     "<p class=sub>讓桌寵連上你家的網路</p><form method=post action=/wifi><div class=card><h2>選擇網路</h2>");

  const int n = WiFi.scanComplete();
  if (n > 0) {
    server.sendContent("<label class='row col'><span>附近的 Wi-Fi</span><select name=pick style='max-width:none'><option value=''>（自己輸入 ↓）</option>");
    char esc[200];
    for (int i = 0; i < n && i < 20; i++) {
      const String ssid = WiFi.SSID(i);
      if (ssid.length() == 0 || ssid.length() > 32) continue;
      escapeHtml(ssid.c_str(), esc, sizeof(esc));
      sendf("<option value=\"%s\">%s（%d dBm）</option>", esc, esc, (int)WiFi.RSSI(i));
    }
    server.sendContent("</select></label>");
  } else {
    if (n == WIFI_SCAN_FAILED) WiFi.scanNetworks(true);
    server.sendContent("<div class=row><small>正在掃描附近的 Wi-Fi…</small><a href='/wifi'>重新整理</a></div>");
  }
  server.sendContent(
      "<label class='row col'><span>或自己輸入名稱</span><input type=text name=ssid maxlength=32 autocomplete=off></label>"
      "<label class='row col'><span>密碼<small>開放網路請留空</small></span>"
      "<input type=password name=pass maxlength=64 autocomplete=off></label>"
      "<div class=row><button type=submit>連線</button></div></div></form>"
      "<div class=acts><a class='btn b2' href='/wifi?rescan=1'>🔄 重新掃描</a>"
      "<a class='btn b2' href='/'>⚙️ 其他設定</a></div>"
      "<p class=note>只支援 2.4GHz。連線成功後，這個設定熱點會自動關閉。</p></html>");
  server.sendContent("");
}

void handleWifiSave() {
  String ssid = server.arg("pick");
  if (ssid.length() == 0) ssid = server.arg("ssid");
  const String pass = server.arg("pass");
  if (ssid.length() == 0 || ssid.length() > 32) return badRequest("Wi-Fi 名稱需為 1~32 字元");
  if (pass.length() > 64 || (pass.length() > 0 && pass.length() < 8)) return badRequest("密碼需為 8~64 字元（開放網路請留空）");
  gApp->webSetWifi(ssid.c_str(), pass.c_str());

  char esc[200];
  escapeHtml(ssid.c_str(), esc, sizeof(esc));
  securityHeaders();
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html; charset=utf-8", "");
  sendPageHead();
  sendf("<title>DeskPet Wi-Fi</title><div class=card><h2>⏳ 正在連線到「%s」…</h2>"
        "<div class=row>請看板子螢幕：連上後會顯示「Wi-Fi 已連線！」，這個熱點約 20 秒後關閉。</div>"
        "<div class=row><small>如果連不上，熱點會繼續開著，可以重新連上熱點再試一次。</small></div></div></html>", esc);
  server.sendContent("");
}
}  // namespace

void WebPanel::begin(App &app) {
  if (started_) return;
  app_ = &app;
  gApp = &app;
  server.on("/", HTTP_GET, []() {
    if (captive && !WiFi.isConnected()) return redirectToSetup();  // 熱點模式先設定 Wi-Fi
    handleRoot();
  });
  server.on("/save", HTTP_POST, handleSave);
  server.on("/pet", HTTP_POST, handlePet);
  server.on("/walk", HTTP_POST, handleWalk);
  server.on("/history", HTTP_GET, handleHistory);
  server.on("/history.csv", HTTP_GET, handleHistoryCsv);
  server.on("/stats", HTTP_GET, handleStats);
  server.on("/dl", HTTP_GET, handleDownload);
  server.on("/backup", HTTP_POST, handleBackup);
  server.on("/upload", HTTP_GET, handleUploadPage);
  server.on("/upload", HTTP_POST, handleUploadDone, handleUploadData);
  server.on("/sddel", HTTP_POST, handleSdDelete);
  server.on("/restore", HTTP_POST, handleRestore);
  server.on("/wifi", HTTP_GET, handleWifiPage);
  server.on("/wifi", HTTP_POST, handleWifiSave);
  for (const char *p : PROBE_PATHS) {
    server.on(p, []() {
      if (captive) redirectToSetup();
      else server.send(404, "text/plain", "Not found");
    });
  }
  server.onNotFound([]() {
    if (captive) redirectToSetup();
    else server.send(404, "text/plain", "Not found");
  });
  server.begin();
  if (MDNS.begin("deskpet")) MDNS.addService("http", "tcp", 80);
  started_ = true;
  Serial.println("[DeskPet] web panel started (http://deskpet.local/)");
}

void WebPanel::setCaptive(bool on) {
  if (on == captive) return;
  captive = on;
  if (on) {
    dns.setErrorReplyCode(DNSReplyCode::NoError);
    dns.start(53, "*", WiFi.softAPIP());  // 所有網域都指向板子
    WiFi.scanNetworks(true);
  } else {
    dns.stop();
  }
}

void WebPanel::update() {
  if (!started_) return;
  if (captive) dns.processNextRequest();
  server.handleClient();
}
